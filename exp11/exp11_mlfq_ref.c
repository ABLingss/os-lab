/*
 * 实验11：Linux进程管理分析及多级反馈队列调度算法实现 — 内核参考代码
 *
 * 说明：本文件是修改 kernel/sched/core.c（或创建 kernel/sched/mlfq.c）
 *       的参考代码，不可单独编译。
 *
 * 参考位置：/usr/src/linux-6.18.15/kernel/sched/
 *
 * 多级反馈队列调度 (MLFQ) 设计：
 *   - 8级队列 Q0~Q7，优先级从高到低
 *   - Q0时间片 = 10 tick，每级时间片翻倍
 *   - 进程用完时间片后降级到下一级队列
 *   - 高优先级队列为空时才调度低优先级队列
 *   - 每100 tick提升所有进程到最高优先级（防止饥饿）
 */

// ================================================================
// 第一部分：数据结构定义
// ================================================================

/*
 * 在 include/linux/sched.h 或 sched.h 的 task_struct 中添加:
 *
 * struct task_struct {
 *     ...
 * +#ifdef CONFIG_MLFQ_SCHED
 * +    int mlfq_level;        // 当前所在队列级别 (0~7)
 * +    unsigned int mlfq_slice;  // 剩余时间片
 * +#endif
 *     ...
 * };
 */


// ================================================================
// 第二部分：MLFQ 调度器核心逻辑
// ================================================================

/*
 * 简化方案：在现有 CFS 调度器的基础上，
 * 通过修改 fair.c 或编写一个简单的调度类来实现 MLFQ。
 *
 * 更简化的演示方案：
 * 在 schedule() 或 pick_next_task() 路径中添加 printk，
 * 观察现有调度行为，同时模拟 MLFQ 队列切换的打印。
 *
 * 完整实现建议创建独立文件 kernel/sched/mlfq.c
 */

#define MLFQ_LEVELS     8
#define MLFQ_BASE_SLICE 10       /* Q0 基础时间片 (tick) */
#define MLFQ_BOOST_TICK 100      /* 优先级提升间隔 (tick) */

/* 每级时间片 = BASE_SLICE * 2^level */
#define MLFQ_SLICE(level) (MLFQ_BASE_SLICE << (level))

struct mlfq_queue {
	struct list_head tasks;    /* 该队列的任务链表 */
	int nr_running;            /* 该队列运行任务数 */
};

static struct mlfq_queue mlfq_queues[MLFQ_LEVELS];

/*
 * MLFQ 调度器初始化
 */
static void mlfq_init(void)
{
	int i;
	for (i = 0; i < MLFQ_LEVELS; i++) {
		INIT_LIST_HEAD(&mlfq_queues[i].tasks);
		mlfq_queues[i].nr_running = 0;
	}
	printk(KERN_INFO "[MLFQ] Multi-Level Feedback Queue scheduler initialized\n");
	printk(KERN_INFO "[MLFQ] %d levels, base slice = %d ticks\n",
	       MLFQ_LEVELS, MLFQ_BASE_SLICE);
}

/*
 * 将任务加入指定级别的队列
 */
static void mlfq_enqueue(struct task_struct *p, int level)
{
	if (level < 0) level = 0;
	if (level >= MLFQ_LEVELS) level = MLFQ_LEVELS - 1;

	p->mlfq_level = level;
	p->mlfq_slice = MLFQ_SLICE(level);
	list_add_tail(&p->mlfq_node, &mlfq_queues[level].tasks);
	mlfq_queues[level].nr_running++;

	printk(KERN_DEBUG "[MLFQ] %s(PID=%d) enqueued to Q%d (slice=%u)\n",
	       p->comm, p->pid, level, p->mlfq_slice);
}

/*
 * 选择下一个要运行的任务（MLFQ调度核心）
 * 从 Q0 到 Q7 扫描，返回第一个非空队列的队首任务
 */
static struct task_struct *mlfq_pick_next(void)
{
	int level;

	for (level = 0; level < MLFQ_LEVELS; level++) {
		if (!list_empty(&mlfq_queues[level].tasks)) {
			struct task_struct *p = list_first_entry(
				&mlfq_queues[level].tasks,
				struct task_struct, mlfq_node);

			list_del_init(&p->mlfq_node);
			mlfq_queues[level].nr_running--;

			printk(KERN_DEBUG "[MLFQ] Picked %s(PID=%d) from Q%d "
			       "(slice=%u, prio=%d)\n",
			       p->comm, p->pid, level,
			       p->mlfq_slice, p->prio);

			return p;
		}
	}

	return NULL;  /* 无可运行任务 */
}

/*
 * 时间片用完处理：将任务降级到下一级队列
 */
static void mlfq_timeslice_expired(struct task_struct *p)
{
	int new_level;

	if (p->mlfq_level < MLFQ_LEVELS - 1)
		new_level = p->mlfq_level + 1;
	else
		new_level = p->mlfq_level;  /* 已在最低级，保持不变 */

	printk(KERN_INFO "[MLFQ] %s(PID=%d) time slice expired, "
	       "demoted: Q%d → Q%d\n",
	       p->comm, p->pid, p->mlfq_level, new_level);

	mlfq_enqueue(p, new_level);
}

/*
 * 优先级提升：将所有任务提升到最高优先级
 * 周期性调用，防止低优先级任务饥饿
 */
static void mlfq_priority_boost(void)
{
	int level;
	struct task_struct *p, *tmp;

	printk(KERN_INFO "[MLFQ] Priority boost triggered!\n");

	/* 从低优先级队列反向扫描 */
	for (level = MLFQ_LEVELS - 1; level > 0; level--) {
		list_for_each_entry_safe(p, tmp,
					 &mlfq_queues[level].tasks,
					 mlfq_node) {
			list_del_init(&p->mlfq_node);
			mlfq_queues[level].nr_running--;

			p->mlfq_level = 0;
			p->mlfq_slice = MLFQ_SLICE(0);
			list_add_tail(&p->mlfq_node, &mlfq_queues[0].tasks);
			mlfq_queues[0].nr_running++;

			printk(KERN_DEBUG "[MLFQ] Boosted %s(PID=%d) → Q0\n",
			       p->comm, p->pid);
		}
	}
}


// ================================================================
// 第三部分：在调度路径中添加 printk（观察现有 CFS 调度行为）
// ================================================================

/*
 * 简易观察方案（不替换调度器）：
 * 在 kernel/sched/core.c 的 __schedule() 函数中，
 * pick_next_task() 之后添加 printk 观察被调度的进程：
 *
 * 在 __schedule() 中找到 pick_next_task() 调用，
 * 在其返回值后添加：
 *
 *     if (next && next->pid != 0) {
 *         printk(KERN_INFO "[MLFQ] Switched to %s (PID=%d, prio=%d)\n",
 *                next->comm, next->pid, next->prio);
 *     }
 *
 * 这样可以通过 dmesg 观察调度行为，而不需要完整替换调度器。
 */


// ================================================================
// 第四部分：dmesg 验证调度行为
// ================================================================

/*
 * 编译新内核后，运行测试程序，用 dmesg 观察：
 *
 * [MLFQ] test_proc(PID=1234) enqueued to Q0 (slice=10)
 * [MLFQ] Picked test_proc(PID=1234) from Q0 (slice=10, prio=120)
 * [MLFQ] test_proc(PID=1234) time slice expired, demoted: Q0 → Q1
 * [MLFQ] test_proc(PID=1234) enqueued to Q1 (slice=20)
 * ...
 * [MLFQ] Priority boost triggered!
 * [MLFQ] Boosted test_proc(PID=1234) → Q0
 *
 * 验证要点：
 *   1. 进程时间片用完是否降级
 *   2. 是否从高优先级队列优先选择
 *   3. 优先级提升是否正常工作
 *   4. 低优先级任务是否最终得到执行（无饥饿）
 */
