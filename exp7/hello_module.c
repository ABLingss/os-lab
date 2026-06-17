/*
 * 实验7：Linux设备文件与驱动程序 — 基础内核模块
 *
 * 编译：见同目录 Makefile（make hello）
 * 加载：sudo insmod hello_module.ko
 * 查看：lsmod | grep hello && dmesg | tail
 * 卸载：sudo rmmod hello_module
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ma Yingzhe");
MODULE_DESCRIPTION("Experiment 7 — Hello Kernel Module");

static int __init hello_init(void)
{
	printk(KERN_INFO "[hello_module] Hello, Kernel! Module loaded.\n");
	return 0;
}

static void __exit hello_exit(void)
{
	printk(KERN_INFO "[hello_module] Goodbye, Kernel! Module unloaded.\n");
}

module_init(hello_init);
module_exit(hello_exit);
