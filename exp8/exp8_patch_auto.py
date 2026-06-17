#!/usr/bin/env python3
"""实验8：自动修改 ipc/sem.c — 一处修改，无重复"""
import sys

SEMC = '/usr/src/linux-6.18.15/ipc/sem.c'

with open(SEMC, 'r') as f:
    lines = f.readlines()

# === 新增内容 ===

# 1. 头文件（插在 rhashtable.h 之后）
HEADERS = '#include <linux/sched/signal.h>\n#include <linux/mm.h>\n'

# 2. 死锁检测和解除函数
DEADLOCK_FUNCS = '''
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
	list_for_each_entry(q, &sma->pending_alter, list)
		waiters++;
	spin_unlock(&sma->sem_perm.lock);

	for (i = 0; i < sma->sem_nsems; i++) {
		if (sma->sems[i].semval != 0) {
			all_zero = 0;
			break;
		}
	}

	if (all_zero && waiters >= 2 && waiters >= sma->sem_nsems) {
		pr_warn("[sem_deadlock] DEADLOCK DETECTED! semid=%d waiters=%d nsems=%d\\n",
		       sma->sem_perm.id, waiters, sma->sem_nsems);
		spin_lock(&sma->sem_perm.lock);
		list_for_each_entry(q, &sma->pending_alter, list)
			pr_warn("[sem_deadlock] Waiter: %s PID=%d\\n",
			       q->sleeper->comm, q->sleeper->pid);
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

	pr_warn("[sem_deadlock] DEADBREAK: breaking deadlock on semid=%d\\n",
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
		pr_info("[sem_deadlock] Candidate: %s PID=%d RSS=%lu\\n",
		       tsk->comm, tsk->pid, rss);
		if (rss < min_rss && tsk->mm) {
			min_rss = rss;
			victim = q;
			victim_task = tsk;
		}
	}

	if (victim && victim_task) {
		pr_warn("[sem_deadlock] DEADBREAK: Killing %s PID=%d RSS=%lu\\n",
		       victim_task->comm, victim_task->pid, min_rss);
		kill_pid(task_pid(victim_task), SIGKILL, 1);
		wake_up_process(victim->sleeper);
	}
	spin_unlock(&sma->sem_perm.lock);

	return victim ? 0 : -ESRCH;
}
'''

# 3. ksys_semctl 中的分发（在第一个属于 ksys_semctl 的 case IPC_RMID 前插入）
KSYS_DISPATCH = ('\t\tcase 0xDEAD:\n'
                 '\t\tcase 0xBEAC:\n'
                 '\t\t\treturn semctl_down(ns, semid, cmd, &semid64);\n')

# 4. compat_ksys_semctl 中的分发
COMPAT_DISPATCH = ('\t\tcase 0xDEAD:\n'
                   '\t\tcase 0xBEAC:\n'
                   '\t\t\treturn semctl_down(ns, semid, cmd, &semid64);\n')

# 5. semctl_down 中的处理
SEMCTL_DOWN_HANDLER = (
    '\t\tcase 0xDEAD: {\n'
    '\t\t\tint dl = detect_sem_deadlock(sma);\n'
    '\t\t\terr = dl;\n'
    '\t\t\tif (dl)\n'
    '\t\t\t\tpr_warn("[sem_deadlock] DEADCHECK: deadlock on semid=%d\\n", sma->sem_perm.id);\n'
    '\t\t\telse\n'
    '\t\t\t\tpr_info("[sem_deadlock] DEADCHECK: no deadlock on semid=%d\\n", sma->sem_perm.id);\n'
    '\t\t\tgoto out_unlock1;\n'
    '\t\t}\n'
    '\t\tcase 0xBEAC: {\n'
    '\t\t\terr = break_sem_deadlock(sma);\n'
    '\t\t\tif (err == 0)\n'
    '\t\t\t\tpr_warn("[sem_deadlock] DEADBREAK: broken on semid=%d\\n", sma->sem_perm.id);\n'
    '\t\t\tgoto out_unlock1;\n'
    '\t\t}\n'
)

# === 逐行处理 ===
new_lines = []
func_scope = None  # Track which function we're in
i = 0

while i < len(lines):
    line = lines[i]

    # ---- A. 在 rhashtable.h 之后插入头文件 ----
    if line.strip() == '#include <linux/rhashtable.h>':
        new_lines.append(line)
        new_lines.append(HEADERS)
        i += 1
        continue

    # ---- B. 跟踪函数作用域 ----
    if 'static long ksys_semctl' in line:
        func_scope = 'ksys_semctl'
    elif 'static long compat_ksys_semctl' in line:
        func_scope = 'compat_ksys_semctl'
    elif 'static int semctl_down' in line:
        func_scope = 'semctl_down'

    # ---- C. 在 ksys_semctl 之前插入死锁函数 ----
    if 'static long ksys_semctl' in line and 'ksys_semctl' in line:
        new_lines.append(DEADLOCK_FUNCS)
        new_lines.append('\n')

    # ---- D. ksys_semctl 中的分发 ----
    if (func_scope == 'ksys_semctl' and
        line.rstrip() == '\t\tcase IPC_RMID:'):
        new_lines.append(KSYS_DISPATCH)
        new_lines.append(line)
        i += 1
        continue

    # ---- E. compat_ksys_semctl 中的分发 ----
    if (func_scope == 'compat_ksys_semctl' and
        line.rstrip() == '\t\tcase IPC_RMID:'):
        new_lines.append(COMPAT_DISPATCH)
        new_lines.append(line)
        i += 1
        continue

    # ---- F. semctl_down 中的处理 ----
    if (func_scope == 'semctl_down' and
        line.rstrip() == '\t\tcase IPC_RMID:'):
        new_lines.append(SEMCTL_DOWN_HANDLER)
        new_lines.append(line)
        i += 1
        continue

    new_lines.append(line)
    i += 1

# === 写入 ===
with open('/tmp/sem.c.patched', 'w') as f:
    f.writelines(new_lines)

print("Patch generated: /tmp/sem.c.patched")
print(f"Lines: {len(lines)} -> {len(new_lines)} (+{len(new_lines) - len(lines)})")

# Verify
content = ''.join(new_lines)
checks = ['detect_sem_deadlock', 'break_sem_deadlock', '0xDEAD', '0xBEAC',
          'sched/signal.h', 'linux/mm.h', 'semctl_down(ns, semid, cmd, &semid64)']
for c in checks:
    cnt = content.count(c)
    print(f"  '{c}': {cnt}")
