// SPDX-License-Identifier: GPL-2.0
/*
 * 커널 안에서 task_struct를 직접 따라가 본다.
 *   sudo insmod ptree.ko pid=$$     -> 내 셸부터 init(pid 1)까지 조상 출력
 *   sudo dmesg | tail
 */
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>

static int pid = 1;
module_param(pid, int, 0444);
MODULE_PARM_DESC(pid, "조상을 따라 올라갈 프로세스의 PID");

static int __init ptree_init(void)
{
	struct task_struct *task, *p;
	int nr_procs = 0, nr_threads = 0;

	/* 프로세스 목록은 RCU로 보호된다. 읽는 동안 task가 사라지지 않게 잠근다. */
	rcu_read_lock();

	task = pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!task) {
		rcu_read_unlock();
		pr_err("ptree: pid %d not found\n", pid);
		return -ESRCH;
	}

	pr_info("ptree: ancestors of pid %d\n", pid);
	for (p = task; ; p = rcu_dereference(p->real_parent)) {
		pr_info("ptree:   %-16s pid=%-6d tgid=%-6d state=0x%x prio=%d\n",
			p->comm, p->pid, p->tgid, READ_ONCE(p->__state), p->prio);
		if (p->pid <= 1)
			break;
	}

	for_each_process(p) {
		struct task_struct *t;

		nr_procs++;
		for_each_thread(p, t)
			nr_threads++;
	}

	rcu_read_unlock();

	pr_info("ptree: %d processes, %d threads in total\n", nr_procs, nr_threads);
	return 0;
}

static void __exit ptree_exit(void)
{
	pr_info("ptree: bye\n");
}

module_init(ptree_init);
module_exit(ptree_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LINUX-DEEP-DIVE");
MODULE_DESCRIPTION("Walk task_struct parents and count processes/threads");
