/*
 * 实验3：Linux下哲学家进餐问题的多线程实现与解决
 *
 * 编译：gcc exp3_philosophers.c -o philosophers -lpthread
 * 运行：./philosophers [deadlock|prevent] [运行次数]
 *       deadlock — 死锁版本（所有哲学家先拿左筷子再拿右筷子）
 *       prevent  — 死锁预防版本（奇偶号哲学家拿筷子顺序不同）
 *
 * 示例：
 *   ./philosophers deadlock          # 运行死锁版本（大概率死锁）
 *   ./philosophers prevent           # 运行预防版本（不会死锁）
 *   ./philosophers prevent 30        # 运行预防版本30次，验证无死锁
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#define N 5                     /* 哲学家数量 */
#define EAT_COUNT 3             /* 每个哲学家需要进餐的次数 */
#define THINK_TIME 100000       /* 思考时间 (微秒) */
#define EAT_TIME 100000         /* 进餐时间 (微秒) */
#define HUNGRY_TIMEOUT 5000000  /* 饥饿超时判定死锁 (微秒) */

/* 哲学家状态 */
enum state { THINKING, HUNGRY, EATING };
static const char *state_str[] = { "思考", "饥饿", "进餐" };

/* 全局数据 */
static pthread_mutex_t chopsticks[N];  /* N把筷子（互斥锁） */
static enum state states[N];           /* 各哲学家状态 */
static int eat_count[N];               /* 各哲学家已进餐次数 */
static int deadlock_detected;          /* 死锁标志 */
static int mode_prevent;               /* 0=死锁版本, 1=预防版本 */
static pthread_mutex_t print_lock;     /* 打印互斥锁 */

/* 线程参数 */
struct philosopher {
	int id;          /* 哲学家编号 0~N-1 */
	int left;        /* 左筷子编号 */
	int right;       /* 右筷子编号 */
};

/* 带锁打印状态（支持格式化） */
static void print_state(int id, const char *fmt, ...)
{
	va_list args;
	char action[256];

	va_start(args, fmt);
	vsnprintf(action, sizeof(action), fmt, args);
	va_end(args);

	pthread_mutex_lock(&print_lock);
	printf("[哲学家%d] %s (状态: %s, 已进餐: %d次)\n",
	       id, action, state_str[states[id]], eat_count[id]);
	pthread_mutex_unlock(&print_lock);
}

/*
 * 死锁版本：所有哲学家先拿左筷子再拿右筷子
 * 当所有哲学家同时拿起左筷子等待右筷子时，发生死锁
 */
static void *philosopher_deadlock(void *arg)
{
	struct philosopher *p = (struct philosopher *)arg;
	int id = p->id;
	int left = p->left;
	int right = p->right;

	while (eat_count[id] < EAT_COUNT) {
		/* 思考 */
		states[id] = THINKING;
		print_state(id, "正在思考...");
		usleep(rand() % THINK_TIME);

		/* 饥饿 — 尝试拿筷子 */
		states[id] = HUNGRY;
		print_state(id, "饿了，尝试拿筷子");

		pthread_mutex_lock(&chopsticks[left]);
		print_state(id, "拿起左筷子");
		usleep(1000);  /* 短暂延迟，增加死锁概率 */

		pthread_mutex_lock(&chopsticks[right]);
		print_state(id, "拿起右筷子，开始进餐");

		/* 进餐 */
		states[id] = EATING;
		eat_count[id]++;
		print_state(id, "正在进餐");
		usleep(rand() % EAT_TIME);

		/* 放下筷子 */
		pthread_mutex_unlock(&chopsticks[right]);
		pthread_mutex_unlock(&chopsticks[left]);
		print_state(id, "放下筷子，进餐完毕");
	}

	print_state(id, "已完成所有进餐，退出");
	return NULL;
}

/*
 * 死锁预防版本：奇偶号哲学家拿筷子顺序不同
 * - 奇数号哲学家：先拿左筷子，再拿右筷子
 * - 偶数号哲学家：先拿右筷子，再拿左筷子
 * 破坏"循环等待"条件，预防死锁
 */
static void *philosopher_prevent(void *arg)
{
	struct philosopher *p = (struct philosopher *)arg;
	int id = p->id;
	int left = p->left;
	int right = p->right;

	/* 偶数号哲学家：交换拿筷子顺序 */
	int first  = (id % 2 == 0) ? right : left;
	int second = (id % 2 == 0) ? left  : right;
	const char *first_name  = (id % 2 == 0) ? "右筷子" : "左筷子";
	const char *second_name = (id % 2 == 0) ? "左筷子" : "右筷子";

	while (eat_count[id] < EAT_COUNT) {
		/* 思考 */
		states[id] = THINKING;
		print_state(id, "正在思考...");
		usleep(rand() % THINK_TIME);

		/* 饥饿 */
		states[id] = HUNGRY;
		print_state(id, "饿了，尝试拿筷子");

		pthread_mutex_lock(&chopsticks[first]);
		print_state(id, "拿起%s", first_name);

		pthread_mutex_lock(&chopsticks[second]);
		print_state(id, "拿起%s，开始进餐", second_name);

		/* 进餐 */
		states[id] = EATING;
		eat_count[id]++;
		print_state(id, "正在进餐");
		usleep(rand() % EAT_TIME);

		/* 放下筷子 */
		pthread_mutex_unlock(&chopsticks[second]);
		pthread_mutex_unlock(&chopsticks[first]);
		print_state(id, "放下筷子，进餐完毕");
	}

	print_state(id, "已完成所有进餐，退出");
	return NULL;
}

/* 死锁监视线程：检测是否有哲学家饥饿过久 */
static void *deadlock_watcher(void *arg)
{
	(void)arg;
	usleep(HUNGRY_TIMEOUT);

	for (int i = 0; i < N; i++) {
		if (states[i] == HUNGRY && eat_count[i] < EAT_COUNT) {
			/* 有哲学家饿了超过5秒还没吃到 → 可能死锁 */
			deadlock_detected = 1;
			fprintf(stderr, "\n!!! 检测到疑似死锁！哲学家%d饿了超过5秒 !!!\n\n", i);
			return NULL;
		}
	}
	return NULL;
}

/* 打印运行统计 */
static void print_stats(const char *mode_name, int run_count)
{
	if (run_count > 1) {
		printf("\n========================================\n");
		printf("模式: %s | 运行次数: %d\n", mode_name, run_count);
		printf("========================================\n");
	}
}

static void reset_state(void)
{
	deadlock_detected = 0;
	for (int i = 0; i < N; i++) {
		states[i] = THINKING;
		eat_count[i] = 0;
	}
}

static int run_simulation(const char *mode_name, int mode, int run)
{
	pthread_t philosophers[N];
	pthread_t watcher;
	struct philosopher params[N];
	void *(*routine)(void *);

	routine = (mode == 0) ? philosopher_deadlock : philosopher_prevent;

	reset_state();

	/* 初始化筷子 */
	for (int i = 0; i < N; i++)
		pthread_mutex_init(&chopsticks[i], NULL);
	pthread_mutex_init(&print_lock, NULL);

	/* 创建哲学家线程 */
	for (int i = 0; i < N; i++) {
		params[i].id    = i;
		params[i].left  = i;
		params[i].right = (i + 1) % N;
		pthread_create(&philosophers[i], NULL, routine, &params[i]);
	}

	/* 启动死锁监视 */
	pthread_create(&watcher, NULL, deadlock_watcher, NULL);

	/* 等待所有哲学家线程完成 */
	for (int i = 0; i < N; i++)
		pthread_join(philosophers[i], NULL);

	pthread_cancel(watcher);
	pthread_join(watcher, NULL);

	/* 清理 */
	for (int i = 0; i < N; i++)
		pthread_mutex_destroy(&chopsticks[i]);
	pthread_mutex_destroy(&print_lock);

	if (deadlock_detected) {
		printf("[第%d次] 结果: 死锁！\n", run);
		return 0;
	}

	/* 检查是否所有哲学家都完成了进餐 */
	int all_done = 1;
	for (int i = 0; i < N; i++) {
		if (eat_count[i] < EAT_COUNT) {
			all_done = 0;
			break;
		}
	}

	printf("[第%d次] 结果: %s\n", run,
	       all_done ? "全部完成，无死锁 ✓" : "未全部完成 ✗");
	return all_done ? 1 : 0;
}

int main(int argc, char *argv[])
{
	const char *mode_name = "死锁版本";
	int mode = 0;  /* 0=死锁, 1=预防 */
	int runs = 1;  /* 运行次数 */

	if (argc >= 2) {
		if (strcmp(argv[1], "prevent") == 0) {
			mode = 1;
			mode_name = "死锁预防版本(奇偶交替)";
		} else if (strcmp(argv[1], "deadlock") == 0) {
			mode = 0;
			mode_name = "死锁版本";
		} else {
			fprintf(stderr, "用法: %s [deadlock|prevent] [运行次数]\n", argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (argc >= 3) {
		runs = atoi(argv[2]);
		if (runs < 1) runs = 1;
		if (runs > 1000) runs = 1000;
	}

	srand(time(NULL));

	printf("============================================\n");
	printf("实验3：哲学家进餐问题 — %s\n", mode_name);
	printf("哲学家数量: %d | 每人需进餐: %d次\n", N, EAT_COUNT);
	if (runs > 1)
		printf("运行次数: %d\n", runs);
	printf("============================================\n\n");

	if (runs == 1) {
		run_simulation(mode_name, mode, 1);
	} else {
		int success = 0;
		int deadlocks = 0;

		for (int i = 1; i <= runs; i++) {
			int ok = run_simulation(mode_name, mode, i);
			if (ok)
				success++;
			else
				deadlocks++;
		}

		printf("\n========================================\n");
		printf("统计结果 (%d次运行)\n", runs);
		printf("  成功完成: %d 次\n", success);
		printf("  死锁/未完成: %d 次\n", deadlocks);
		printf("  成功率: %.1f%%\n", 100.0 * success / runs);
		printf("========================================\n");

		if (mode == 1 && deadlocks > 0) {
			printf("\n⚠ 预防版本不应出现死锁，请检查实现！\n");
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
