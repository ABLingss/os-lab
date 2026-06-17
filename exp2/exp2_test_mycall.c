/*
 * 实验2：Linux系统调用分析和增加系统调用
 * 用户态测试程序 — test_mycall.c
 *
 * 编译：gcc exp2_test_mycall.c -o test_mycall
 * 运行：./test_mycall
 * 验证：dmesg | tail -5
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>

/* 系统调用号 — 必须与内核中分配的一致 */
#define __NR_my_add 470

int main(void)
{
	int a = 10, b = 20;
	long result;

	printf("=== 实验2：自定义系统调用测试 ===\n");
	printf("调用 sys_my_add(%d, %d)\n", a, b);

	result = syscall(__NR_my_add, a, b);

	if (result < 0) {
		fprintf(stderr, "系统调用失败: %s (errno=%d)\n",
			strerror(errno), errno);
		return EXIT_FAILURE;
	}

	printf("返回值: %d + %d = %ld\n", a, b, result);
	printf("测试通过！请执行 dmesg | tail -5 查看内核日志输出。\n");

	return EXIT_SUCCESS;
}
