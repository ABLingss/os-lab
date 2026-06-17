/*
 * 实验4：Linux下生产者/消费者问题的多进程实现
 *
 * 使用 XSI 信号量集 + 共享内存实现
 * - 3个生产者进程 + 4个消费者进程（fork创建）
 * - 共享内存环形缓冲区
 * - XSI信号量集实现互斥和同步
 *
 * 编译：gcc exp4_producer_consumer.c -o producer_consumer
 * 运行：./producer_consumer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

/* ========== 配置参数 ========== */
#define BUFFER_SIZE   8         /* 缓冲区大小 */
#define NUM_PRODUCERS 3         /* 生产者数量 */
#define NUM_CONSUMERS 4         /* 消费者数量 */
#define ITEMS_PER_PRODUCER 5    /* 每个生产者生产的数据量 */
#define TOTAL_ITEMS (NUM_PRODUCERS * ITEMS_PER_PRODUCER)

/* ========== 共享内存数据结构 ========== */
struct shared_buffer {
	int buffer[BUFFER_SIZE];   /* 环形缓冲区 */
	int in;                    /* 生产者写入位置 */
	int out;                   /* 消费者读取位置 */
	int count;                 /* 当前缓冲区数据量 */
	int produced;              /* 已生产总数 */
	int consumed;              /* 已消费总数 */
};

/* ========== 信号量集定义 ========== */
/*
 * 信号量集包含3个信号量:
 *   [0] MUTEX   — 互斥访问共享内存
 *   [1] EMPTY   — 空闲缓冲区槽位数
 *   [2] FULL    — 已占用缓冲区槽位数
 */
#define SEM_MUTEX 0
#define SEM_EMPTY 1
#define SEM_FULL  2

#define SHM_KEY  0x20250604  /* 共享内存key */
#define SEM_KEY  0x20250605  /* 信号量集key */

/* ========== 信号量操作辅助函数 ========== */

/* 对单个信号量做 P 操作（减1，若无资源则阻塞） */
static void sem_p(int semid, int sem_num)
{
	struct sembuf sb;
	sb.sem_num = sem_num;
	sb.sem_op  = -1;   /* P操作：减1 */
	sb.sem_flg = 0;    /* 阻塞等待 */
	if (semop(semid, &sb, 1) == -1) {
		perror("semop P");
		exit(EXIT_FAILURE);
	}
}

/* 对单个信号量做 V 操作（加1，唤醒等待者） */
static void sem_v(int semid, int sem_num)
{
	struct sembuf sb;
	sb.sem_num = sem_num;
	sb.sem_op  = 1;    /* V操作：加1 */
	sb.sem_flg = 0;
	if (semop(semid, &sb, 1) == -1) {
		perror("semop V");
		exit(EXIT_FAILURE);
	}
}

/* ========== 生产者进程 ========== */
static void producer(int id, int semid, struct shared_buffer *sb)
{
	/* 随机种子：每个进程用不同种子 */
	srand(time(NULL) ^ (getpid() << 8) ^ (id << 4));

	for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
		/* 生产一个数据项 */
		int item = (id * 1000) + i + 1;
		usleep(rand() % 300000);  /* 模拟生产耗时 */

		/* P(EMPTY) — 等待空闲槽位 */
		sem_p(semid, SEM_EMPTY);

		/* P(MUTEX) — 互斥进入临界区 */
		sem_p(semid, SEM_MUTEX);

		/* === 临界区：放入缓冲区 === */
		sb->buffer[sb->in] = item;
		sb->in = (sb->in + 1) % BUFFER_SIZE;
		sb->produced++;
		printf("[生产者%d] 生产数据: %d → 缓冲区位置[%d] "
		       "(缓冲区: %d/%d, 总生产: %d)\n",
		       id, item, (sb->in - 1 + BUFFER_SIZE) % BUFFER_SIZE,
		       sb->produced - sb->consumed, BUFFER_SIZE,
		       sb->produced);
		/* ======================== */

		/* V(MUTEX) — 退出临界区 */
		sem_v(semid, SEM_MUTEX);

		/* V(FULL) — 增加一个已占用槽位 */
		sem_v(semid, SEM_FULL);
	}

	printf("[生产者%d] 已完成全部生产 (%d项)\n", id, ITEMS_PER_PRODUCER);
}

/* ========== 消费者进程 ========== */
static void consumer(int id, int semid, struct shared_buffer *sb)
{
	srand(time(NULL) ^ (getpid() << 12) ^ (id << 8));

	/* 消费者需要消费直到所有产品被消费完 */
	int consumed_count = 0;
	while (1) {
		/* P(FULL) — 等待有数据可消费 */
		sem_p(semid, SEM_FULL);

		/* P(MUTEX) — 互斥进入临界区 */
		sem_p(semid, SEM_MUTEX);

		/* 检查是否所有数据都已消费 */
		if (sb->consumed >= TOTAL_ITEMS) {
			/* 所有数据处理完毕，释放信号量并退出 */
			sem_v(semid, SEM_MUTEX);
			sem_v(semid, SEM_FULL);  /* 归还多拿的FULL */
			break;
		}

		/* === 临界区：从缓冲区取数据 === */
		int item = sb->buffer[sb->out];
		sb->out = (sb->out + 1) % BUFFER_SIZE;
		sb->consumed++;
		consumed_count++;
		printf("[消费者%d] 消费数据: %d ← 缓冲区位置[%d] "
		       "(缓冲区: %d/%d, 已消费: %d/%d)\n",
		       id, item,
		       (sb->out - 1 + BUFFER_SIZE) % BUFFER_SIZE,
		       sb->produced - sb->consumed, BUFFER_SIZE,
		       sb->consumed, TOTAL_ITEMS);
		/* ======================== */

		/* V(MUTEX) — 退出临界区 */
		sem_v(semid, SEM_MUTEX);

		/* V(EMPTY) — 增加一个空闲槽位 */
		sem_v(semid, SEM_EMPTY);

		usleep(rand() % 400000);  /* 模拟消费耗时 */

		if (sb->consumed >= TOTAL_ITEMS) {
			/* 所有数据已消费完，唤醒下一个等待的消费者 */
			sem_v(semid, SEM_FULL);
			break;
		}
	}

	printf("[消费者%d] 已消费 %d 项数据，退出\n", id, consumed_count);
}

/* ========== 主函数 ========== */
int main(void)
{
	int shmid, semid;
	struct shared_buffer *sb;
	pid_t pid;
	int producer_count = 0, consumer_count = 0;

	/* 禁用输出缓冲，避免fork后子进程继承父进程缓冲区 */
	setvbuf(stdout, NULL, _IONBF, 0);

	printf("============================================\n");
	printf("实验4：生产者/消费者问题的多进程实现\n");
	printf("生产者: %d个 | 消费者: %d个 | 缓冲区: %d\n",
	       NUM_PRODUCERS, NUM_CONSUMERS, BUFFER_SIZE);
	printf("总生产量: %d\n", TOTAL_ITEMS);
	printf("============================================\n\n");

	/* ===== 1. 创建共享内存 ===== */
	shmid = shmget(SHM_KEY, sizeof(struct shared_buffer),
		       IPC_CREAT | 0666);
	if (shmid == -1) {
		perror("shmget");
		exit(EXIT_FAILURE);
	}

	sb = (struct shared_buffer *)shmat(shmid, NULL, 0);
	if (sb == (void *)-1) {
		perror("shmat");
		exit(EXIT_FAILURE);
	}

	/* 初始化共享内存 */
	memset(sb, 0, sizeof(*sb));

	/* ===== 2. 创建信号量集 ===== */
	semid = semget(SEM_KEY, 3, IPC_CREAT | 0666);
	if (semid == -1) {
		perror("semget");
		goto cleanup_shm;
	}

	/*
	 * 初始化3个信号量:
	 *   SEM_MUTEX(0) = 1   — 互斥信号量，初值为1（可用）
	 *   SEM_EMPTY(1) = N   — 空闲槽位，初值为缓冲区大小
	 *   SEM_FULL (2) = 0   — 已占用槽位，初值为0
	 */
	if (semctl(semid, SEM_MUTEX, SETVAL, 1) == -1) {
		perror("semctl MUTEX");
		goto cleanup_all;
	}
	if (semctl(semid, SEM_EMPTY, SETVAL, BUFFER_SIZE) == -1) {
		perror("semctl EMPTY");
		goto cleanup_all;
	}
	if (semctl(semid, SEM_FULL, SETVAL, 0) == -1) {
		perror("semctl FULL");
		goto cleanup_all;
	}

	printf("共享内存和信号量集初始化完成。\n");
	printf("  共享内存ID: %d\n", shmid);
	printf("  信号量集ID: %d\n\n", semid);
	printf("========== 开始生产/消费 ==========\n\n");

	/* ===== 3. 创建生产者进程 ===== */
	for (int i = 1; i <= NUM_PRODUCERS; i++) {
		pid = fork();
		if (pid == -1) {
			perror("fork producer");
			exit(EXIT_FAILURE);
		}
		if (pid == 0) {
			/* 子进程：生产者 */
			producer(i, semid, sb);
			exit(EXIT_SUCCESS);
		}
		producer_count++;
	}

	/* ===== 4. 创建消费者进程 ===== */
	for (int i = 1; i <= NUM_CONSUMERS; i++) {
		pid = fork();
		if (pid == -1) {
			perror("fork consumer");
			exit(EXIT_FAILURE);
		}
		if (pid == 0) {
			/* 子进程：消费者 */
			consumer(i, semid, sb);
			exit(EXIT_SUCCESS);
		}
		consumer_count++;
	}

	/* ===== 5. 父进程等待所有子进程结束 ===== */
	printf("[父进程] 已创建 %d个生产者 + %d个消费者，等待完成...\n\n",
	       producer_count, consumer_count);

	for (int i = 0; i < producer_count + consumer_count; i++) {
		int status;
		pid_t child = wait(&status);
		if (WIFEXITED(status)) {
			printf("[父进程] 子进程 %d 正常退出 (exit=%d)\n",
			       child, WEXITSTATUS(status));
		} else {
			printf("[父进程] 子进程 %d 异常退出\n", child);
		}
	}

	printf("\n========== 生产/消费结束 ==========\n\n");
	printf("最终统计:\n");
	printf("  总生产: %d 项\n", sb->produced);
	printf("  总消费: %d 项\n", sb->consumed);
	printf("  缓冲区残留: %d 项\n", sb->produced - sb->consumed);

	/* ===== 6. 清理资源 ===== */
cleanup_all:
	/* 删除信号量集 */
	if (semctl(semid, 0, IPC_RMID) == -1)
		perror("semctl IPC_RMID");

cleanup_shm:
	/* 断开共享内存 */
	if (shmdt(sb) == -1)
		perror("shmdt");

	/* 删除共享内存 */
	if (shmctl(shmid, IPC_RMID, NULL) == -1)
		perror("shmctl IPC_RMID");

	printf("资源清理完成。\n");
	return EXIT_SUCCESS;
}
