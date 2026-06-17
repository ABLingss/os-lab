/*
 * 实验10：Linux内存管理分析与验证 — 用户态观察程序1
 *          brk vs mmap 系统调用观察
 *
 * 编译：gcc exp10_brk_mmap.c -o brk_mmap
 * 运行：strace ./brk_mmap
 *
 * 观察：小内存分配使用 brk()，大内存分配使用 mmap()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

static void print_usage(const char *label)
{
	char cmd[256];
	snprintf(cmd, sizeof(cmd),
		 "grep -E 'VmSize|VmRSS|VmData' /proc/%d/status 2>/dev/null",
		 getpid());
	printf("=== %s ===\n", label);
	fflush(stdout);
	system(cmd);
	printf("\n");
}

int main(void)
{
	void *ptr;

	printf("实验10-1: brk vs mmap 系统调用观察\n");
	printf("PID = %d\n\n", getpid());

	print_usage("初始状态");

	/* 1. 小内存分配 (128KB) — 预期使用 brk */
	printf("[1] malloc(128KB) — 预期使用 brk() 扩展堆\n");
	ptr = malloc(128 * 1024);
	if (!ptr) { perror("malloc"); return 1; }
	memset(ptr, 0xAB, 128 * 1024);
	print_usage("malloc(128KB) 后");
	free(ptr);

	/* 2. 中等内存分配 (256KB) — 可能使用 mmap */
	printf("[2] malloc(256KB) — 可能使用 mmap()（取决于阈值）\n");
	ptr = malloc(256 * 1024);
	if (!ptr) { perror("malloc"); return 1; }
	memset(ptr, 0xCD, 256 * 1024);
	print_usage("malloc(256KB) 后");
	free(ptr);

	/* 3. 大内存分配 (2MB) — 预期使用 mmap */
	printf("[3] malloc(2MB) — 预期使用 mmap()\n");
	ptr = malloc(2 * 1024 * 1024);
	if (!ptr) { perror("malloc"); return 1; }
	memset(ptr, 0xEF, 2 * 1024 * 1024);
	print_usage("malloc(2MB) 后");
	free(ptr);

	/* 4. 直接使用 mmap 分配 */
	printf("[4] mmap(1MB) — 直接匿名映射\n");
	ptr = mmap(NULL, 1024 * 1024, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED) { perror("mmap"); return 1; }
	memset(ptr, 0x11, 1024 * 1024);
	print_usage("mmap(1MB) 后");
	munmap(ptr, 1024 * 1024);

	printf("\n请使用 strace ./brk_mmap 查看实际使用的系统调用。\n");
	return 0;
}
