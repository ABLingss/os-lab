/*
 * exp11/mfq_test.c — MFQ调度器验证程序
 *
 * 创建多个子进程，每个切换到自己到 policy=4 (SCHED_MFQ)，
 * 展示8级多级反馈队列的调度行为。
 *
 * 编译: gcc -O2 -o mfq_test mfq_test.c
 * 使用: sudo ./mfq_test
 * 验证: dmesg | grep '\[MFQ\]'
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sched.h>
#include <errno.h>

#define SCHED_MFQ  4
#define N_CHILDREN 4

/* CPU密集型: 持续计算，演示降级(L0→L1→...→L7) */
static void cpu_bound_work(const char *name)
{
	volatile unsigned long x = 0;
	unsigned long i;
	printf("[%s] pid=%d CPU-bound, 观察降级过程\n", name, getpid());
	for (i = 0; i < 500000000UL; i++) {
		x = x * 1103515245 + 12345;
	}
	/* 防止编译器优化掉 */
	(void)x;
	printf("[%s] pid=%d done\n", name, getpid());
}

/* I/O密集型: 间歇sleep, 演示保持高优先级 */
static void io_bound_work(const char *name)
{
	int round;
	printf("[%s] pid=%d I/O-bound, 观察保持高优先级\n", name, getpid());
	for (round = 0; round < 50; round++) {
		/* 短暂计算 */
		volatile unsigned long x = 0;
		for (int j = 0; j < 500000; j++) x++;
		/* I/O等待 (模拟交互) */
		usleep(10000);  /* 10ms */
	}
	printf("[%s] pid=%d done\n", name, getpid());
}

int main(void)
{
	pid_t pids[N_CHILDREN];
	int i;

	printf("=== MFQ调度器验证 (SCHED_MFQ=policy 4) ===\n\n");

	/* 切换到MFQ (glibc包装函数拒绝policy=4，用raw syscall绕过) */
	struct sched_param param = { .sched_priority = 0 };
	if (syscall(__NR_sched_setscheduler, 0, SCHED_MFQ, &param) < 0) {
		perror("sched_setscheduler(SCHED_MFQ)");
		fprintf(stderr, "请确认已启动MFQ内核 (6.18.15-mfq)\n");
		return 1;
	}
	printf("[父进程] pid=%d 已切换到MFQ\n\n", getpid());

	/* 创建子进程: 前2个CPU密集型，后2个I/O密集型 */
	for (i = 0; i < N_CHILDREN; i++) {
		pids[i] = fork();
		if (pids[i] < 0) {
			perror("fork");
			return 1;
		}
		if (pids[i] == 0) {
			/* 子进程: 也切换到MFQ (glibc拒绝policy=4，用raw syscall) */
			syscall(__NR_sched_setscheduler, 0, SCHED_MFQ, &param);

			char name[32];
			snprintf(name, sizeof(name), "child-%d", i);

			if (i < 2)
				cpu_bound_work(name);  /* CPU密集型 */
			else
				io_bound_work(name);   /* I/O密集型 */

			_exit(0);
		}
	}

	/* 父进程等待所有子进程 */
	for (i = 0; i < N_CHILDREN; i++)
		waitpid(pids[i], NULL, 0);

	printf("\n=== 验证完成 ===\n");
	printf("运行: dmesg | grep '\\[MFQ\\]' 查看调度日志\n");
	printf("预期看到:\n");
	printf("  - FORK: 新进程进入L0\n");
	printf("  - RUN:  进程在各级队列运行\n");
	printf("  - DEMOTE: CPU密集型进程逐级降级 L0→L1→...→L7\n");
	printf("  - I/O密集型进程保持在较高优先级\n");
	return 0;
}
