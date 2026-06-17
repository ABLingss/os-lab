/*
 * 实验10：Linux内存管理分析与验证 — 用户态观察程序3
 *         Copy-On-Write (COW) 观察
 *
 * 编译：gcc exp10_cow.c -o cow
 * 运行：./cow
 *
 * 观察：
 *   1. 父进程分配 1GB 并写入
 *   2. fork() 子进程
 *   3. 观察父子进程 RES（子进程应很小 — COW 共享父进程页面）
 *   4. 子进程写入 → 触发 COW → 子进程 RES 增大
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>

#define SIZE (1024L * 1024 * 1024)  /* 1 GB */

static void print_mem(const char *who)
{
	char cmd[256];
	printf("=== %s (PID=%d) ===\n", who, getpid());
	snprintf(cmd, sizeof(cmd),
		 "grep -E 'VmPeak|VmSize|VmRSS|VmData' /proc/%d/status",
		 getpid());
	fflush(stdout);
	system(cmd);
	printf("\n");
}

int main(void)
{
	char *ptr;
	pid_t pid;

	printf("实验10-3: Copy-On-Write (COW) 观察\n");
	printf("父进程 PID = %d\n\n", getpid());

	/* 1. 父进程分配并写入 1GB */
	printf("[父进程] 分配 1GB 并写入...\n");
	ptr = mmap(NULL, SIZE, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED) {
		perror("mmap");
		return EXIT_FAILURE;
	}

	/* 写入数据（全部页面变为脏页） */
	long step = 100L * 1024 * 1024;
	for (long off = 0; off < SIZE; off += step) {
		ptr[off] = 0xAA;
	}
	printf("[父进程] 1GB 已写入\n");
	print_mem("父进程 (fork前)");

	/* 2. fork 子进程 */
	printf("[父进程] 调用 fork()...\n");
	pid = fork();

	if (pid == -1) {
		perror("fork");
		return EXIT_FAILURE;
	}

	if (pid == 0) {
		/* 子进程 */
		printf("[子进程] fork 成功，PID = %d\n", getpid());
		sleep(1);
		print_mem("子进程 (fork后，未写入)");
		printf("!!! 观察: 子进程 RES 应很小 — 页面被 COW 共享 !!!\n\n");

		/* 3. 子进程写入数据（触发 COW） */
		printf("[子进程] 开始修改页面（触发COW）...\n");
		for (long off = 0; off < SIZE; off += step) {
			ptr[off] = 0xBB;
			if (off % (200L * 1024 * 1024) == 0) {
				printf("  已修改 %ld MB (观察 RES 增长)\n",
				       off / (1024 * 1024));
				sleep(2);
			}
		}
		print_mem("子进程 (COW后)");
		printf("!!! 观察: 子进程 RES 增大了 — COW 复制了页面 !!!\n");
		exit(EXIT_SUCCESS);
	}

	/* 父进程等待 */
	printf("[父进程] 子进程 PID = %d, 等待中...\n", pid);
	printf("在另一个终端: top -p %d,%d 观察父子 RES 变化\n", getpid(), pid);
	wait(NULL);
	print_mem("父进程 (子进程结束后)");

	munmap(ptr, SIZE);
	return EXIT_SUCCESS;
}
