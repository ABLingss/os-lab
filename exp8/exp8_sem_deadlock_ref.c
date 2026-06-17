/*
 * 实验8：Linux进程通信分析及信号量机制改进 — 内核参考代码
 *
 * 本文件是添加到 ipc/sem.c 的参考代码，不可单独编译。
 * 在 Linux 内核的信号量机制上增加死锁检测和死锁解除功能。
 *
 * 参考位置：/usr/src/linux-6.18.15/ipc/sem.c
 */

// ================================================================
// 第一部分：在 ipc/sem.c 开头附近添加新的 cmd 定义
// ================================================================

/*
 * 在 include/uapi/linux/sem.h 中添加（或直接定义在 sem.c 中）:
 *
 * #define DEADCHECK  __TODO_DEADCHECK  // 选择一个未使用的 cmd 值
 * #define DEADBREAK  __TODO_DEADBREAK
 *
 * 或在 sem.c 中使用自定义的 cmd 值，例如：
 *   case 0xDEAD:  // DEADCHECK — 检测死锁
 *   case 0xBEAK:  // DEADBREAK — 解除死锁
 */


// ================================================================
// 第二部分：死锁检测函数
// ================================================================

/*
 * 死锁检测算法（基于资源分配图）：
 * 1. 遍历当前信号量集的所有等待进程
 * 2. 对于每个等待该信号量的进程，检查它正在持有哪个信号量
 * 3. 若形成等待环，则判定死锁存在
 *
 * 简化实现：检测是否有两个以上进程互相等待
 */
static int detect_sem_deadlock(struct sem_array *sma)
{
	struct sem_queue *q1, *q2;
	int deadlock_found = 0;

	/*
	 * 遍历等待队列，检查是否存在循环等待
	 * 简单策略：如果有 >=2 个进程在等待同一信号量集中的不同信号量，
	 * 且彼此持有的资源形成环，则报告死锁
	 */

	/* 检查等待该信号量集的进程数 */
	int waiters = 0;
	spin_lock(&sma->sem_perm.lock);
	list_for_each_entry(q1, &sma->pending_alter, list) {
		waiters++;
	}
	spin_unlock(&sma->sem_perm.lock);

	/*
	 * 简化判定：如果有多个进程等待且信号量全为0，
	 * 可能存在死锁（实际应构建等待图做完整检测）
	 */
	if (waiters >= 2) {
		int all_zero = 1;
		for (int i = 0; i < sma->sem_nsems; i++) {
			if (sma->sems[i].semval != 0) {
				all_zero = 0;
				break;
			}
		}
		if (all_zero && waiters >= sma->sem_nsems) {
			deadlock_found = 1;

			/* 打印死锁检测信息 */
			printk(KERN_WARNING "[sem_deadlock] DEADLOCK DETECTED! "
			       "semid=%d, waiters=%d, nsems=%d\n",
			       sma->sem_perm.id, waiters, sma->sem_nsems);

			spin_lock(&sma->sem_perm.lock);
			list_for_each_entry(q1, &sma->pending_alter, list) {
				printk(KERN_WARNING "[sem_deadlock] Waiting process: "
				       "%s (PID=%d)\n",
				       q1->sleeper->comm, q1->sleeper->pid);
			}
			spin_unlock(&sma->sem_perm.lock);
		}
	}

	return deadlock_found;
}

/*
 * 死锁解除：杀死等待队列中占用内存最小的进程
 * 这破坏了死锁的"不可抢占"条件
 */
static int break_sem_deadlock(struct sem_array *sma)
{
	struct sem_queue *q, *victim = NULL;
	struct task_struct *victim_task = NULL;
	unsigned long min_rss = ~0UL;

	printk(KERN_WARNING "[sem_deadlock] DEADBREAK: Attempting to break deadlock "
	       "on semid=%d\n", sma->sem_perm.id);

	/* 1. 找到内存占用最小的等待进程 */
	spin_lock(&sma->sem_perm.lock);
	list_for_each_entry(q, &sma->pending_alter, list) {
		struct task_struct *tsk = q->sleeper;
		unsigned long rss;

		if (!tsk)
			continue;

		/* 获取进程的 RSS (Resident Set Size) */
		if (tsk->mm) {
			rss = get_mm_rss(tsk->mm) << PAGE_SHIFT;
		} else {
			rss = 0;  /* 内核线程 */
		}

		printk(KERN_INFO "[sem_deadlock] Candidate: %s PID=%d RSS=%lu bytes\n",
		       tsk->comm, tsk->pid, rss);

		if (rss < min_rss && tsk->mm) {
			min_rss = rss;
			victim = q;
			victim_task = tsk;
		}
	}

	if (victim && victim_task) {
		printk(KERN_WARNING "[sem_deadlock] DEADBREAK: Killing process "
		       "%s (PID=%d, RSS=%lu bytes) to break deadlock\n",
		       victim_task->comm, victim_task->pid, min_rss);

		/* 2. 发送 SIGKILL 杀死选中进程 */
		kill_pid(task_pid(victim_task), SIGKILL, 1);

		/* 3. 从等待队列移除 */
		if (victim->sleeper)
			wake_up_process(victim->sleeper);
	}

	spin_unlock(&sma->sem_perm.lock);

	return victim ? 0 : -ESRCH;
}


// ================================================================
// 第三部分：在 semctl() 中添加 DEADCHECK 和 DEADBREAK 分支
// ================================================================

/*
 * 在 semctl() 函数的 switch(version) 之前或 case 分支添加：
 *
 * 定位方法：搜索 "case IPC_STAT:" 或 "case GETALL:" 等现有分支
 * 在这些分支附近添加以下代码：
 */

#if 0  /* 参考代码 — 添加到实际 semctl() 中 */
	case DEADCHECK: {
		int deadlock = detect_sem_deadlock(sma);
		err = deadlock;
		if (deadlock)
			printk(KERN_WARNING "[sem_deadlock] DEADCHECK result: "
			       "deadlock detected on semid=%d\n", semid);
		else
			printk(KERN_INFO "[sem_deadlock] DEADCHECK result: "
			       "no deadlock on semid=%d\n", semid);
		goto out_unlock;
	}

	case DEADBREAK: {
		err = break_sem_deadlock(sma);
		if (err == 0)
			printk(KERN_WARNING "[sem_deadlock] DEADBREAK: "
			       "deadlock broken on semid=%d\n", semid);
		goto out_unlock;
	}
#endif
