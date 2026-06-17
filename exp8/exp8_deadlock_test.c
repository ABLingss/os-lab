/*
 * 实验8：Linux进程通信分析及信号量机制改进 — 用户态死锁测试程序
 *
 * 功能：
 *   1. 创建3个子进程，使用信号量集构造死锁状态
 *   2. 第4个子进程调用 DEADCHECK 检测死锁
 *   3. 检测到死锁后调用 DEADBREAK 解除死锁
 *
 * 编译：gcc exp8_deadlock_test.c -o deadlock_test
 * 运行：./deadlock_test
 *
 * 注意：需要运行在打过 DEADCHECK/DEADBREAK 补丁的新内核上
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

/* 自定义命令值 — 需与内核补丁保持一致 */
#define DEADCHECK  0xDEAD
#define DEADBREAK  0xBEAC

#define SEM_KEY 0x20250608
#define NUM_SEMS 2
#define NUM_PROCESSES 3

/*
 * 信号量集包含2个信号量:
 *   [0] — 资源A
 *   [1] — 资源B
 *
 * 构造死锁：
 *   进程1: 持有[0] 等待[1]
 *   进程2: 持有[1] 等待[0]
 *   进程3: 等待[0] (在进程1释放后才能获得)
 */

/* 信号量操作辅助 */
static void sem_op(int semid, int sem_num, int op)
{
	struct sembuf sb;
	sb.sem_num = sem_num;
	sb.sem_op  = op;
	sb.sem_flg = 0;
	if (semop(semid, &sb, 1) == -1) {
		perror("semop");
	}
}

/* 进程1：持有资源0，等待资源1（形成死锁的一环） */
static void process1(int semid)
{
	printf("[进程1 PID=%d] 尝试获取资源[0]...\n", getpid());
	sem_op(semid, 0, -1);  /* P操作：持有资源0 */
	printf("[进程1 PID=%d] 获取资源[0]成功，sleep 2s...\n", getpid());
	sleep(2);

	printf("[进程1 PID=%d] 尝试获取资源[1]...（将阻塞，形成死锁）\n", getpid());
	sem_op(semid, 1, -1);  /* P操作：等待资源1 — 会阻塞 */

	/* 正常情况不会走到这里 */
	sem_op(semid, 1, 1);   /* V操作：释放资源1 */
	sem_op(semid, 0, 1);   /* V操作：释放资源0 */
	printf("[进程1 PID=%d] 完成\n", getpid());
}

/* 进程2：持有资源1，等待资源0（形成死锁的另一环） */
static void process2(int semid)
{
	printf("[进程2 PID=%d] 尝试获取资源[1]...\n", getpid());
	sem_op(semid, 1, -1);  /* P操作：持有资源1 */
	printf("[进程2 PID=%d] 获取资源[1]成功，sleep 2s...\n", getpid());
	sleep(2);

	printf("[进程2 PID=%d] 尝试获取资源[0]...（将阻塞，形成死锁）\n", getpid());
	sem_op(semid, 0, -1);  /* P操作：等待资源0 — 会阻塞 */

	sem_op(semid, 0, 1);
	sem_op(semid, 1, 1);
	printf("[进程2 PID=%d] 完成\n", getpid());
}

/* 进程3：等待资源0（附加等待者） */
static void process3(int semid)
{
	sleep(3);
	printf("[进程3 PID=%d] 尝试获取资源[0]...（将阻塞）\n", getpid());
	sem_op(semid, 0, -1);  /* 会阻塞 */

	sem_op(semid, 0, 1);
	printf("[进程3 PID=%d] 完成\n", getpid());
}

int main(void)
{
	int semid;
	pid_t pids[4];

	printf("========================================\n");
	printf("实验8：信号量死锁检测与解除测试\n");
	printf("========================================\n\n");

	/* 1. 创建信号量集 */
	semid = semget(SEM_KEY, NUM_SEMS, IPC_CREAT | 0666);
	if (semid == -1) {
		perror("semget");
		exit(EXIT_FAILURE);
	}

	/* 2. 初始化信号量：资源[0]=1, 资源[1]=1（初始各1个资源可用） */
	if (semctl(semid, 0, SETVAL, 1) == -1) {
		perror("semctl SETVAL 0");
		goto cleanup;
	}
	if (semctl(semid, 1, SETVAL, 1) == -1) {
		perror("semctl SETVAL 1");
		goto cleanup;
	}

	printf("信号量集创建完成 (semid=%d)\n", semid);
	printf("  资源[0] = 1  资源[1] = 1\n\n");

	/* 3. 创建死锁进程 */
	printf("=== 阶段1：构造死锁 ===\n");

	pid_t pid1 = fork();
	if (pid1 == 0) {
		process1(semid);
		_exit(0);
	}
	pids[0] = pid1;

	pid_t pid2 = fork();
	if (pid2 == 0) {
		process2(semid);
		_exit(0);
	}
	pids[1] = pid2;

	pid_t pid3 = fork();
	if (pid3 == 0) {
		process3(semid);
		_exit(0);
	}
	pids[2] = pid3;

	/* 4. 等待死锁形成 */
	printf("[父进程] 等待死锁形成 (sleep 5s)...\n");
	sleep(5);

	/* 5. 检测死锁 */
	printf("\n=== 阶段2：检测死锁 ===\n");
	int deadlock_result = syscall(__NR_semctl, semid, 0, DEADCHECK, 0);
	printf("[父进程] DEADCHECK 返回: %d\n", deadlock_result);
	if (deadlock_result > 0)
		printf("[父进程] ⚠ 检测到死锁！\n");
	else if (deadlock_result >= 0)
		printf("[父进程] 未检测到死锁。\n");
	else
		printf("[父进程] DEADCHECK 失败 (可能内核未打补丁): %s\n",
		       strerror(errno));

	/* 6. 解除死锁 */
	if (deadlock_result > 0) {
		printf("\n=== 阶段3：解除死锁 ===\n");
		sleep(1);
		int break_result = syscall(__NR_semctl, semid, 0, DEADBREAK, 0);
		printf("[父进程] DEADBREAK 返回: %d\n", break_result);
		if (break_result == 0)
			printf("[父进程] ✓ 死锁已解除！\n");
		else
			printf("[父进程] DEADBREAK 失败: %s\n", strerror(errno));
	}

	/* 7. 回收所有子进程 */
	printf("\n=== 回收子进程 ===\n");
	for (int i = 0; i < 3; i++) {
		int status;
		pid_t w = waitpid(pids[i], &status, WNOHANG);
		if (w == 0) {
			printf("[父进程] 进程%d(PID=%d) 仍在运行\n", i + 1, pids[i]);
		} else if (w > 0) {
			printf("[父进程] 进程%d(PID=%d) 已退出 (信号:%d)\n",
			       i + 1, w,
			       WIFSIGNALED(status) ? WTERMSIG(status) : 0);
		}
	}

	/* 等待剩余进程（可能已被KILL） */
	for (int i = 0; i < 3; i++) {
		int status;
		waitpid(pids[i], &status, 0);
	}

cleanup:
	/* 8. 清理信号量集 */
	if (semctl(semid, 0, IPC_RMID) == -1)
		perror("semctl IPC_RMID");
	printf("\n信号量集已清理。\n");

	return EXIT_SUCCESS;
}
