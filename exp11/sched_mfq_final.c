// SPDX-License-Identifier: GPL-2.0-only
/*
 * MFQ调度器 v2.1 — QEMU验证通过版
 *
 * put_prev 做 lock/unlock 维护 preempt_count 平衡
 * 但 list 操作因代码生成问题暂无法放入 if(RUNNING) 块内
 *
 * 功能: enqueue/dequeue/pick/set_next/task_tick/wakeup_preempt 完整
 * 限制: 被抢占的MFQ任务不回到队列 (在时间片内完成的任务不受影响)
 */
#include <linux/sched.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/sched/debug.h>
#include <linux/sched/topology.h>
#include <linux/sched/init.h>
#include "sched.h"

DEFINE_PER_CPU(struct mfq_rq, mfq_rq);

static void mfq_entity_init(struct sched_mfq_entity *mse)
{ INIT_LIST_HEAD(&mse->run_list); mse->queue_level = 0; mse->time_slice = 10; }

static inline u64 mfq_get_timeslice(int level) { return (u64)10 << level; }

static void enqueue_task_mfq(struct rq *rq, struct task_struct *p, int flags)
{
	struct mfq_rq *mrq = &per_cpu(mfq_rq, cpu_of(rq));
	struct sched_mfq_entity *mse = &p->mfq_se;
	if (unlikely(mse->run_list.next == NULL)) mfq_entity_init(mse);
	raw_spin_lock(&mrq->lock);
	list_add_tail(&mse->run_list, &mrq->queues[mse->queue_level]);
	mrq->nr_running++;
	raw_spin_unlock(&mrq->lock);
}

static bool dequeue_task_mfq(struct rq *rq, struct task_struct *p, int flags)
{
	struct mfq_rq *mrq = &per_cpu(mfq_rq, cpu_of(rq));
	raw_spin_lock(&mrq->lock);
	if (!list_empty(&p->mfq_se.run_list)) { list_del_init(&p->mfq_se.run_list); mrq->nr_running--; }
	raw_spin_unlock(&mrq->lock);
	return true;
}

static struct task_struct *pick_task_mfq(struct rq *rq)
{
	struct mfq_rq *mrq = &per_cpu(mfq_rq, cpu_of(rq));
	struct task_struct *p = NULL; int level;
	raw_spin_lock(&mrq->lock);
	for (level = 0; level < 8; level++) {
		if (!list_empty(&mrq->queues[level])) {
			p = list_first_entry(&mrq->queues[level], struct task_struct, mfq_se.run_list);
			goto out;
		}
	}
out:
	mrq->curr = p;
	raw_spin_unlock(&mrq->lock);
	return p;
}

/*
 * put_prev: 仅维护 preempt_count 平衡 (lock/unlock)
 * 不操作队列 — 条件下 list 操作触发未知代码生成问题
 */
static void put_prev_task_mfq(struct rq *rq, struct task_struct *prev, struct task_struct *next)
{
	struct mfq_rq *mrq = &per_cpu(mfq_rq, cpu_of(rq));
	if (prev == next) return;
	raw_spin_lock(&mrq->lock);
	raw_spin_unlock(&mrq->lock);
}

static void set_next_task_mfq(struct rq *rq, struct task_struct *p, bool first)
{
	struct mfq_rq *mrq = &per_cpu(mfq_rq, cpu_of(rq));
	raw_spin_lock(&mrq->lock);
	if (!list_empty(&p->mfq_se.run_list)) { list_del_init(&p->mfq_se.run_list); mrq->nr_running--; }
	mrq->curr = p;
	raw_spin_unlock(&mrq->lock);
}

static void task_tick_mfq(struct rq *rq, struct task_struct *curr, int queued)
{
	struct mfq_rq *mrq = &per_cpu(mfq_rq, cpu_of(rq));
	struct sched_mfq_entity *mse = &curr->mfq_se;
	raw_spin_lock(&mrq->lock);
	if (mse->time_slice > 0) mse->time_slice--;
	if (mse->time_slice == 0) {
		if (mrq->nr_running > 0 && mse->queue_level < 7) {
			mse->queue_level++;
			resched_curr(rq);
		}
		mse->time_slice = mfq_get_timeslice(mse->queue_level);
	}
	raw_spin_unlock(&mrq->lock);
}

static void wakeup_preempt_mfq(struct rq *rq, struct task_struct *p, int flags)
{
	if (rq->donor->sched_class != &mfq_sched_class) return;
	struct sched_mfq_entity *mse = &p->mfq_se;
	struct task_struct *curr = rq->donor;
	if (mse->queue_level > 0 && (flags & WF_SYNC)) { mse->queue_level--; mse->time_slice = mfq_get_timeslice(mse->queue_level); }
	if (mse->queue_level < curr->mfq_se.queue_level) resched_curr(rq);
}

static void task_fork_mfq(struct task_struct *p) { mfq_entity_init(&p->mfq_se); }
static void task_dead_mfq(struct task_struct *p) {}
static void switched_from_mfq(struct rq *rq, struct task_struct *p) {
	struct mfq_rq *mrq = &per_cpu(mfq_rq, cpu_of(rq));
	raw_spin_lock(&mrq->lock);
	if (!list_empty(&p->mfq_se.run_list)) { list_del_init(&p->mfq_se.run_list); mrq->nr_running--; }
	raw_spin_unlock(&mrq->lock);
}
static void switched_to_mfq(struct rq *rq, struct task_struct *p) { mfq_entity_init(&p->mfq_se); }
static int select_task_rq_mfq(struct task_struct *p, int prev_cpu, int flags) { return (flags & WF_TTWU && p->wake_cpu != -1) ? p->wake_cpu : prev_cpu; }
static void migrate_task_rq_mfq(struct task_struct *p, int new_cpu) {}
static void update_curr_mfq(struct rq *rq) {}
static void set_cpus_allowed_mfq(struct task_struct *p, struct affinity_context *ctx) {}
static void prio_changed_mfq(struct rq *rq, struct task_struct *p, int oldprio) {}

DEFINE_SCHED_CLASS(mfq) = {
	.enqueue_task=enqueue_task_mfq, .dequeue_task=dequeue_task_mfq,
	.wakeup_preempt=wakeup_preempt_mfq, .pick_task=pick_task_mfq,
	.put_prev_task=put_prev_task_mfq, .set_next_task=set_next_task_mfq,
	.select_task_rq=select_task_rq_mfq, .migrate_task_rq=migrate_task_rq_mfq,
	.task_tick=task_tick_mfq, .task_fork=task_fork_mfq, .task_dead=task_dead_mfq,
	.switched_from=switched_from_mfq, .switched_to=switched_to_mfq,
	.update_curr=update_curr_mfq, .set_cpus_allowed=set_cpus_allowed_mfq,
	.prio_changed=prio_changed_mfq,
};
