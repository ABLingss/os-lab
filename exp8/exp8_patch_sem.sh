#!/bin/bash
# 实验8：修改 ipc/sem.c — 添加死锁检测和死锁解除功能
# 手动执行：bash ~/os/exp8_patch_sem.sh
set -e

KERNEL_SRC=/usr/src/linux-6.18.15
SEMC=$KERNEL_SRC/ipc/sem.c

echo "=== Step 1: 添加头文件 ==="
sudo sed -i '/^#include <linux\/rhashtable.h>/a\#include <linux\/sched\/signal.h>\n#include <linux\/mm.h>' $SEMC

echo "=== Step 2: 添加死锁检测和死锁解除函数 ==="
# 找到 ksys_semctl 的行号
LINE=$(grep -n "^static long ksys_semctl" $SEMC | cut -d: -f1)
PREV=$((LINE - 1))

cat > /tmp/deadlock_code.c << 'EOF'
/*
 * 实验8：死锁检测 — 检查信号量集等待队列中是否存在循环等待
 */
static int detect_sem_deadlock(struct sem_array *sma)
{
	struct sem_queue *q;
	int waiters = 0;
	int all_zero = 1;
	int i;

	spin_lock(&sma->sem_perm.lock);
	list_for_each_entry(q, &sma->pending_alter, list) {
		waiters++;
	}
	spin_unlock(&sma->sem_perm.lock);

	for (i = 0; i < sma->sem_nsems; i++) {
		if (sma->sems[i].semval != 0) {
			all_zero = 0;
			break;
		}
	}

	if (all_zero && waiters >= 2 && waiters >= sma->sem_nsems) {
		pr_warn("[sem_deadlock] DEADLOCK DETECTED! semid=%d, waiters=%d, nsems=%d\n",
		       sma->sem_perm.id, waiters, sma->sem_nsems);

		spin_lock(&sma->sem_perm.lock);
		list_for_each_entry(q, &sma->pending_alter, list) {
			pr_warn("[sem_deadlock] Waiter: %s PID=%d\n",
			       q->sleeper->comm, q->sleeper->pid);
		}
		spin_unlock(&sma->sem_perm.lock);
		return 1;
	}
	return 0;
}

/*
 * 实验8：死锁解除 — 杀死等待队列中 RSS 最小的进程
 */
static int break_sem_deadlock(struct sem_array *sma)
{
	struct sem_queue *q, *victim = NULL;
	struct task_struct *victim_task = NULL;
	unsigned long min_rss = ~0UL;

	pr_warn("[sem_deadlock] DEADBREAK: breaking deadlock on semid=%d\n",
	       sma->sem_perm.id);

	spin_lock(&sma->sem_perm.lock);
	list_for_each_entry(q, &sma->pending_alter, list) {
		struct task_struct *tsk = q->sleeper;
		unsigned long rss;

		if (!tsk)
			continue;
		if (tsk->mm)
			rss = get_mm_rss(tsk->mm) << PAGE_SHIFT;
		else
			rss = 0;
		pr_info("[sem_deadlock] Candidate: %s PID=%d RSS=%lu\n",
		       tsk->comm, tsk->pid, rss);
		if (rss < min_rss && tsk->mm) {
			min_rss = rss;
			victim = q;
			victim_task = tsk;
		}
	}

	if (victim && victim_task) {
		pr_warn("[sem_deadlock] DEADBREAK: Killing %s PID=%d RSS=%lu\n",
		       victim_task->comm, victim_task->pid, min_rss);
		kill_pid(task_pid(victim_task), SIGKILL, 1);
		wake_up_process(victim->sleeper);
	}
	spin_unlock(&sma->sem_perm.lock);

	return victim ? 0 : -ESRCH;
}
EOF

sudo sed -i "${PREV}r /tmp/deadlock_code.c" $SEMC

echo "=== Step 3: 在 ksys_semctl 中添加 DEADCHECK/DEADBREAK 分发 ==="
# 在第一个 "case IPC_RMID:" (ksys_semctl中) 之前添加
sudo sed -i '0,/case IPC_RMID:/s/case IPC_RMID:/case 0xDEAD:\n\t\tcase 0xBEAK:\n\t\t\treturn semctl_down(ns, semid, cmd, \&semid64);\n\t\tcase IPC_RMID:/' $SEMC

echo "=== Step 4: 在 compat_ksys_semctl 中添加 DEADCHECK/DEADBREAK 分发 ==="
# compat_ksys_semctl 是第二个 "case IPC_RMID:"
sudo sed -i '/^static long compat_ksys_semctl/,/^}/s/case IPC_RMID:/case 0xDEAD:\n\t\tcase 0xBEAK:\n\t\t\treturn semctl_down(ns, semid, cmd, \&semid64);\n\t\tcase IPC_RMID:/' $SEMC

echo "=== Step 5: 在 semctl_down 中添加 DEADCHECK/DEADBREAK 处理 ==="
sudo sed -i '/^static int semctl_down/,/case IPC_RMID:/s/case IPC_RMID:/case 0xDEAD: {\n\t\t\tint dl = detect_sem_deadlock(sma);\n\t\t\terr = dl;\n\t\t\tif (dl)\n\t\t\t\tpr_warn("[sem_deadlock] DEADCHECK: deadlock on semid=%d\\n", sma->sem_perm.id);\n\t\t\telse\n\t\t\t\tpr_info("[sem_deadlock] DEADCHECK: no deadlock on semid=%d\\n", sma->sem_perm.id);\n\t\t\tgoto out_unlock1;\n\t\t}\n\t\tcase 0xBEAK: {\n\t\t\terr = break_sem_deadlock(sma);\n\t\t\tif (err == 0)\n\t\t\t\tpr_warn("[sem_deadlock] DEADBREAK: broken on semid=%d\\n", sma->sem_perm.id);\n\t\t\tgoto out_unlock1;\n\t\t}\n\t\tcase IPC_RMID:/' $SEMC

echo ""
echo "=== 修改完成！验证 ==="
grep -c "detect_sem_deadlock\|break_sem_deadlock\|0xDEAD\|0xBEAK" $SEMC
echo "应至少有 8 处匹配"
echo ""
echo "下一步：cd $KERNEL_SRC && sudo make -j\$(nproc) bzImage"
