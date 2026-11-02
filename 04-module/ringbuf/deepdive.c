// SPDX-License-Identifier: GPL-2.0
/*
 * /dev/deepdive : 커널 안의 링 버퍼를 파일처럼 쓰는 캐릭터 디바이스
 *
 *   echo hello | sudo tee /dev/deepdive      write
 *   sudo cat /dev/deepdive                   read (비어 있으면 데이터가 올 때까지 잠든다)
 *   sudo ./stats                             ioctl로 통계 읽기 (C), go 폴더는 Go 버전
 *
 * 여기서 배우는 것
 *   - file_operations: 유저의 read/write/ioctl 시스템 콜이 도착하는 곳
 *   - copy_to_user / copy_from_user: 커널은 유저 포인터를 직접 믿고 만지면 안 된다
 *   - mutex: 여러 프로세스가 동시에 read/write 할 때 버퍼 보호
 *   - wait queue: 데이터가 없으면 프로세스를 재우고, 들어오면 깨우기
 */
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "deepdive_ioctl.h"

#define BUF_SIZE 4096

static char buf[BUF_SIZE];
static size_t head;  /* 다음에 쓸 위치 */
static size_t tail;  /* 다음에 읽을 위치 */
static size_t used;  /* 쌓여 있는 바이트 수 */
static u64 total_written, total_read;

static DEFINE_MUTEX(buf_lock);
static DECLARE_WAIT_QUEUE_HEAD(readers);

static ssize_t deepdive_read(struct file *file, char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	size_t n, first;
	int ret;

	if (mutex_lock_interruptible(&buf_lock))
		return -ERESTARTSYS;

	while (used == 0) {
		mutex_unlock(&buf_lock);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		/* 잠든다. write가 wake_up 하거나 Ctrl+C(시그널)가 오면 깬다 */
		ret = wait_event_interruptible(readers, READ_ONCE(used) > 0);
		if (ret)
			return ret;
		if (mutex_lock_interruptible(&buf_lock))
			return -ERESTARTSYS;
	}

	n = min(count, used);
	/* 링 버퍼 끝에서 잘리면 두 번에 나눠 복사 */
	first = min(n, (size_t)BUF_SIZE - tail);
	if (copy_to_user(ubuf, buf + tail, first) ||
	    copy_to_user(ubuf + first, buf, n - first)) {
		mutex_unlock(&buf_lock);
		return -EFAULT;
	}
	tail = (tail + n) % BUF_SIZE;
	used -= n;
	total_read += n;
	mutex_unlock(&buf_lock);

	pr_debug("deepdive: %s(%d) read %zu bytes\n", current->comm, current->pid, n);
	return n;
}

static ssize_t deepdive_write(struct file *file, const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	size_t n, first;

	if (mutex_lock_interruptible(&buf_lock))
		return -ERESTARTSYS;

	n = min(count, (size_t)BUF_SIZE - used);
	if (n == 0) {
		mutex_unlock(&buf_lock);
		return -ENOSPC; /* 가득 참. 블로킹 write로 바꾸는 건 실습 과제 */
	}
	first = min(n, (size_t)BUF_SIZE - head);
	if (copy_from_user(buf + head, ubuf, first) ||
	    copy_from_user(buf, ubuf + first, n - first)) {
		mutex_unlock(&buf_lock);
		return -EFAULT;
	}
	head = (head + n) % BUF_SIZE;
	used += n;
	total_written += n;
	mutex_unlock(&buf_lock);

	wake_up_interruptible(&readers);
	return n;
}

static long deepdive_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct deepdive_stats st;

	switch (cmd) {
	case DEEPDIVE_IOC_RESET:
		mutex_lock(&buf_lock);
		head = tail = used = 0;
		mutex_unlock(&buf_lock);
		return 0;

	case DEEPDIVE_IOC_GET_STATS:
		mutex_lock(&buf_lock);
		st.bytes_written = total_written;
		st.bytes_read = total_read;
		st.used = used;
		st.capacity = BUF_SIZE;
		mutex_unlock(&buf_lock);
		if (copy_to_user((void __user *)arg, &st, sizeof(st)))
			return -EFAULT;
		return 0;

	default:
		return -ENOTTY; /* 모르는 ioctl 번호에 대한 관례적인 에러 */
	}
}

static const struct file_operations deepdive_fops = {
	.owner = THIS_MODULE,
	.read = deepdive_read,
	.write = deepdive_write,
	.unlocked_ioctl = deepdive_ioctl,
};

/* misc device: 주 번호(major) 관리를 커널에 맡기고 /dev/deepdive를 자동 생성 */
static struct miscdevice deepdive_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "deepdive",
	.fops = &deepdive_fops,
	.mode = 0666,
};

static int __init deepdive_init(void)
{
	int ret = misc_register(&deepdive_dev);

	if (ret)
		return ret;
	pr_info("deepdive: /dev/deepdive ready (minor %d, %d byte ring)\n",
		deepdive_dev.minor, BUF_SIZE);
	return 0;
}

static void __exit deepdive_exit(void)
{
	misc_deregister(&deepdive_dev);
	pr_info("deepdive: unloaded\n");
}

module_init(deepdive_init);
module_exit(deepdive_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LINUX-DEEP-DIVE");
MODULE_DESCRIPTION("Ring buffer character device with blocking read and ioctl");
