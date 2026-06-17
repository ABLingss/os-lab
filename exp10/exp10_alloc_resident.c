/*
 * 实验10：Linux内存管理分析与验证 — 用户态观察程序2
 *         内存分配 vs 驻留观察
 *
 * 编译：gcc exp10_alloc_resident.c -o alloc_resident
 * 运行：./alloc_resident &
 *       top -p $(pgrep alloc_resident)
 *
 * 观察：
 *   阶段1 分配 1GB 但不访问 → VIRT 增大，RES 很小
 *   阶段2 访问分配的内存 → RES 随之增大（缺页中断触发物理分配）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define ALLOC_SIZE (1024L * 1024 * 1024)  /* 1 GB */

int main(void)
{
	char *ptr;

	printf("实验10-2: 内存分配 vs 驻留 (RES) 观察\n");
	printf("PID = %d\n", getpid());
	printf("1GB = %ld bytes\n\n", ALLOC_SIZE);

	/* 阶段1：分配但不访问 */
	printf("=== 阶段1：malloc(1GB)，暂不访问 ===\n");
	printf("请在另一个终端执行：top -p %d\n", getpid());
	printf("观察 VIRT (虚拟内存) 和 RES (驻留内存) 的区别\n");
	printf("按 Enter 继续...\n");
	getchar();

	ptr = (char *)malloc(ALLOC_SIZE);
	if (!ptr) {
		perror("malloc 1GB");
		return EXIT_FAILURE;
	}
	printf("malloc 成功，虚拟机: %p ~ %p\n", ptr, ptr + ALLOC_SIZE);
	printf("!!! 现在 top 中 VIRT 应约为 1GB，RES 很小 (约几MB) !!!\n");
	printf("按 Enter 启动阶段2 (访问内存)...\n");
	getchar();

	/* 阶段2：逐步访问，触发缺页 */
	printf("\n=== 阶段2：每100MB访问一次（触发缺页中断） ===\n");
	long step = 100L * 1024 * 1024;  /* 100 MB */
	for (long offset = 0; offset < ALLOC_SIZE; offset += step) {
		ptr[offset] = 0x42;  /* 写入一个字节触发缺页 */
		printf("  已访问 %ld MB (观察 top 中 RES 应逐步增长)\n",
		       offset / (1024 * 1024));
		sleep(2);  /* 给时间查看 top */
	}

	printf("\n=== 全部访问完毕 ===\n");
	printf("现在 RES 应接近 1GB\n");
	printf("按 Enter 释放内存并退出...\n");
	getchar();

	free(ptr);
	printf("内存已释放。\n");
	return EXIT_SUCCESS;
}
