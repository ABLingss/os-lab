#!/usr/bin/env python3
"""实验8 v2：在 ksys_semctl 中直接处理 DEADCHECK/DEADBREAK"""
import sys

SEMC = '/usr/src/linux-6.18.15/ipc/sem.c'

with open(SEMC, 'r') as f:
    lines = f.readlines()

HEADER_LINES = [
    '#include <linux/sched/signal.h>\n',
    '#include <linux/mm.h>\n',
]

# Forward declaration
FORWARD_DECL = 'static int detect_sem_deadlock(struct sem_array *sma);\nstatic int break_sem_deadlock(struct sem_array *sma);\n'

DEADLOCK_LINES = [
    '\n',
    '/* 实验8：死锁检测 */\n',
    'static int detect_sem_deadlock(struct sem_array *sma)\n',
    '{\n',
    '\tstruct sem_queue *q;\n',
    '\tint waiters = 0;\n',
    '\tint all_zero = 1;\n',
    '\tint i;\n',
    '\n',
    '\tspin_lock(&sma->sem_perm.lock);\n',
    '\tlist_for_each_entry(q, &sma->pending_alter, list)\n',
    '\t\twaiters++;\n',
    '\tspin_unlock(&sma->sem_perm.lock);\n',
    '\n',
    '\tfor (i = 0; i < sma->sem_nsems; i++) {\n',
    '\t\tif (sma->sems[i].semval != 0) {\n',
    '\t\t\tall_zero = 0;\n',
    '\t\t\tbreak;\n',
    '\t\t}\n',
    '\t}\n',
    '\n',
    '\tif (all_zero && waiters >= 2 && waiters >= sma->sem_nsems) {\n',
    '\t\tpr_warn("[sem_deadlock] DEADLOCK DETECTED! semid=%d waiters=%d nsems=%d\\n",\n',
    '\t\t       sma->sem_perm.id, waiters, sma->sem_nsems);\n',
    '\t\tspin_lock(&sma->sem_perm.lock);\n',
    '\t\tlist_for_each_entry(q, &sma->pending_alter, list)\n',
    '\t\t\tpr_warn("[sem_deadlock] Waiter: %s PID=%d\\n",\n',
    '\t\t\t       q->sleeper->comm, q->sleeper->pid);\n',
    '\t\tspin_unlock(&sma->sem_perm.lock);\n',
    '\t\treturn 1;\n',
    '\t}\n',
    '\treturn 0;\n',
    '}\n',
    '\n',
    '/* 实验8：死锁解除 */\n',
    'static int break_sem_deadlock(struct sem_array *sma)\n',
    '{\n',
    '\tstruct sem_queue *q, *victim = NULL;\n',
    '\tstruct task_struct *victim_task = NULL;\n',
    '\tunsigned long min_rss = ~0UL;\n',
    '\n',
    '\tpr_warn("[sem_deadlock] DEADBREAK: breaking deadlock on semid=%d\\n",\n',
    '\t       sma->sem_perm.id);\n',
    '\n',
    '\tspin_lock(&sma->sem_perm.lock);\n',
    '\tlist_for_each_entry(q, &sma->pending_alter, list) {\n',
    '\t\tstruct task_struct *tsk = q->sleeper;\n',
    '\t\tunsigned long rss;\n',
    '\n',
    '\t\tif (!tsk)\n',
    '\t\t\tcontinue;\n',
    '\t\tif (tsk->mm)\n',
    '\t\t\trss = get_mm_rss(tsk->mm) << PAGE_SHIFT;\n',
    '\t\telse\n',
    '\t\t\trss = 0;\n',
    '\t\tpr_info("[sem_deadlock] Candidate: %s PID=%d RSS=%lu\\n",\n',
    '\t\t       tsk->comm, tsk->pid, rss);\n',
    '\t\tif (rss < min_rss && tsk->mm) {\n',
    '\t\t\tmin_rss = rss;\n',
    '\t\t\tvictim = q;\n',
    '\t\t\tvictim_task = tsk;\n',
    '\t\t}\n',
    '\t}\n',
    '\n',
    '\tif (victim && victim_task) {\n',
    '\t\tpr_warn("[sem_deadlock] DEADBREAK: Killing %s PID=%d RSS=%lu\\n",\n',
    '\t\t       victim_task->comm, victim_task->pid, min_rss);\n',
    '\t\tkill_pid(task_pid(victim_task), SIGKILL, 1);\n',
    '\t\twake_up_process(victim->sleeper);\n',
    '\t}\n',
    '\tspin_unlock(&sma->sem_perm.lock);\n',
    '\n',
    '\treturn victim ? 0 : -ESRCH;\n',
    '}\n',
]

# Direct handler in ksys_semctl (does its own lookup)
KSYS_HANDLER = [
    '\tcase 0xDEAD:\n',
    '\tcase 0xBEAC: {\n',
    '\t\tstruct sem_array *sma;\n',
    '\t\trcu_read_lock();\n',
    '\t\tsma = sem_obtain_object_check(ns, semid);\n',
    '\t\tif (IS_ERR(sma)) {\n',
    '\t\t\trcu_read_unlock();\n',
    '\t\t\treturn PTR_ERR(sma);\n',
    '\t\t}\n',
    '\t\tif (cmd == 0xDEAD)\n',
    '\t\t\terr = detect_sem_deadlock(sma);\n',
    '\t\telse\n',
    '\t\t\terr = break_sem_deadlock(sma);\n',
    '\t\trcu_read_unlock();\n',
    '\t\tif (cmd == 0xDEAD)\n',
    '\t\t\tpr_info("[sem_deadlock] DEADCHECK returned: %d\\n", err);\n',
    '\t\telse if (err == 0)\n',
    '\t\t\tpr_warn("[sem_deadlock] DEADBREAK: deadlock broken\\n");\n',
    '\t\treturn err;\n',
    '\t}\n',
]

func = None
result = []

for line in lines:
    # Track function
    if 'static long compat_ksys_semctl' in line:
        func = 'compat'
    elif 'static long ksys_semctl' in line:
        func = 'ksys'
    elif 'static int semctl_down' in line:
        func = 'semctl_down'

    # 1. Headers
    if line.strip() == '#include <linux/rhashtable.h>':
        result.append(line)
        result.extend(HEADER_LINES)
        continue

    # 2. Forward declarations before semctl_down
    if 'static int semctl_down' in line:
        result.append(FORWARD_DECL)
        result.append('\n')

    # 3. Deadlock functions before semctl_down
    if 'static int semctl_down' in line:
        result.extend(DEADLOCK_LINES)
        result.append('\n')

    # 4. Direct handler in ksys_semctl (before default)
    if func == 'ksys' and line.strip() == 'case IPC_SET:':
        # Insert handler BEFORE case IPC_SET
        result.extend(KSYS_HANDLER)
        result.append(line)
        continue

    # 5. Same handler in compat_ksys_semctl
    if func == 'compat' and line.strip() == 'case IPC_SET:':
        result.extend(KSYS_HANDLER)
        result.append(line)
        continue

    result.append(line)

# Write
with open('/tmp/sem.c.patched', 'w') as f:
    f.writelines(result)

content = ''.join(result)
print(f"Lines: {len(lines)} -> {len(result)}")
for tag in ['detect_sem_deadlock', 'break_sem_deadlock', '0xDEAD', '0xBEAC',
            'sem_obtain_object_check']:
    print(f"  {tag}: {content.count(tag)}")
print("Done!")
