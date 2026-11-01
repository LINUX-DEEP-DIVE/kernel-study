// SPDX-License-Identifier: GPL-2.0
/*
 * 첫 커널 모듈.
 * insmod 할 때 init 함수가, rmmod 할 때 exit 함수가 호출된다.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>

static char *who = "world";
/* /sys/module/hello/parameters/who 로 노출된다 (0444 = 읽기 전용) */
module_param(who, charp, 0444);
MODULE_PARM_DESC(who, "인사할 대상");

static int __init hello_init(void)
{
	pr_info("hello: hello, %s!\n", who);
	return 0;
}

static void __exit hello_exit(void)
{
	pr_info("hello: goodbye, %s\n", who);
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LINUX-DEEP-DIVE");
MODULE_DESCRIPTION("First kernel module for the deep dive study");
