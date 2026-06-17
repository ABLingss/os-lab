/*
 * 实验2：参考代码 — 添加到 kernel/sys.c 末尾的系统调用实现
 * 本文件不是独立编译的，内容需要添加到内核源码 /usr/src/linux-6.18.15/kernel/sys.c
 *
 * 注意：本文件仅供你参考，实际操作请按 EXP2_GUIDE.md 指导进行。
 */

// ============================================================
// 在 kernel/sys.c 文件末尾添加以下代码
// ============================================================

#include <linux/kernel.h>
#include <linux/syscalls.h>

/*
 * 自定义系统调用：返回两个整数的和
 *
 * @a: 第一个加数
 * @b: 第二个加数
 * 返回值: a + b，若参数非法返回 -EINVAL
 *
 * 同时 printk 输出 "This is a new syscall" 到内核日志
 */
SYSCALL_DEFINE2(my_add, int, a, int, b)
{
	printk(KERN_INFO "This is a new syscall: my_add(%d, %d) called\n", a, b);

	/* 参数合法性检查 */
	if (a < 0 || b < 0)
		return -EINVAL;

	return (long)(a + b);
}
