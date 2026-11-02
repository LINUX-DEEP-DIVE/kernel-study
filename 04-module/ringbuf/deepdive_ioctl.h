/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * 유저 공간과 커널 모듈이 같이 쓰는 ioctl 정의.
 * 커널에서는 <linux/ioctl.h>, 유저에서는 <sys/ioctl.h>가 같은 매크로를 준다.
 */
#ifndef DEEPDIVE_IOCTL_H
#define DEEPDIVE_IOCTL_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint32_t __u32;
typedef uint64_t __u64;
#endif

struct deepdive_stats {
	__u64 bytes_written; /* 지금까지 write로 들어온 총 바이트 */
	__u64 bytes_read;    /* 지금까지 read로 나간 총 바이트 */
	__u32 used;          /* 지금 버퍼에 쌓여 있는 바이트 */
	__u32 capacity;      /* 버퍼 크기 */
};

#define DEEPDIVE_IOC_MAGIC 'd'
/* _IO: 데이터 없음, _IOR: 커널 → 유저로 데이터를 읽어 감 */
#define DEEPDIVE_IOC_RESET     _IO(DEEPDIVE_IOC_MAGIC, 1)
#define DEEPDIVE_IOC_GET_STATS _IOR(DEEPDIVE_IOC_MAGIC, 2, struct deepdive_stats)

#endif
