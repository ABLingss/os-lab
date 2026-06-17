#!/usr/bin/env python3
"""实验8：修改 ipc/sem.c — 精确版"""
import sys

SEMC = '/usr/src/linux-6.18.15/ipc/sem.c'

with open(SEMC, 'r') as f:
    lines = f.readlines()

# Headers to add after rhashtable.h
HEADER_LINES = [
    '#include <linux/sched/signal.h>\n',
    '#include <linux/mm.h>\n',
]

# Deadlock functions
DEADLOCK_LINES = [
    '\n',
    '/*\n',
    ' * 实验8：死锁检测\n',
    ' */\n',
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
    '/*\n',
    ' * 实验8：死锁解除\n',
    ' */\n',
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

# New case labels for ksys_semctl and compat dispatch (1-tab indent)
DISPATCH_LINES = [
    "\tcase 0xDEAD:\n",
    "\tcase 0xBEAC:\n",
    "\t\treturn semctl_down(ns, semid, cmd, &semid64);\n",
]

# New case handler for semctl_down (2-tab indent inside switch)
HANDLER_LINES = [
    "\t\tcase 0xDEAD: {\n",
    "\t\t\tint dl = detect_sem_deadlock(sma);\n",
    "\t\t\terr = dl;\n",
    "\t\t\tif (dl)\n",
    "\t\t\t\tpr_warn(\"[sem_deadlock] DEADCHECK: deadlock on semid=%d\\n\", sma->sem_perm.id);\n",
    "\t\t\telse\n",
    "\t\t\t\tpr_info(\"[sem_deadlock] DEADCHECK: no deadlock on semid=%d\\n\", sma->sem_perm.id);\n",
    "\t\t\tgoto out_unlock1;\n",
    "\t\t}\n",
    "\t\tcase 0xBEAC: {\n",
    "\t\t\terr = break_sem_deadlock(sma);\n",
    "\t\t\tif (err == 0)\n",
    "\t\t\t\tpr_warn(\"[sem_deadlock] DEADBREAK: broken on semid=%d\\n\", sma->sem_perm.id);\n",
    "\t\t\tgoto out_unlock1;\n",
    "\t\t}\n",
]

# Track which function we're in
# semctl_down (1603) -> ksys_semctl (1651) -> compat_ksys_semctl (1774)
func = None

result = []
for line in lines:
    # Detect function boundaries (order matters: compat before ksys!)
    if 'static int semctl_down' in line:
        func = 'semctl_down'
    elif 'static long compat_ksys_semctl' in line:
        func = 'compat'
    elif 'static long ksys_semctl' in line:
        func = 'ksys'

    # 1. Add headers after rhashtable.h include
    if line.strip() == '#include <linux/rhashtable.h>':
        result.append(line)
        result.extend(HEADER_LINES)
        continue

    # 2. Insert deadlock functions BEFORE semctl_down (which uses them)
    if 'static int semctl_down' in line:
        result.extend(DEADLOCK_LINES)
        result.append('\n')

    # 3. ksys_semctl: add dispatch before case IPC_RMID
    if func == 'ksys' and line.strip() == 'case IPC_RMID:':
        result.extend(DISPATCH_LINES)
        result.append(line)
        continue

    # 4. compat_ksys_semctl: add dispatch before case IPC_RMID
    if func == 'compat' and line.strip() == 'case IPC_RMID:':
        result.extend(DISPATCH_LINES)
        result.append(line)
        continue

    # 5. semctl_down: add handler before case IPC_RMID
    if func == 'semctl_down' and line.strip() == 'case IPC_RMID:':
        result.extend(HANDLER_LINES)
        result.append(line)
        continue

    result.append(line)

# Write output
with open('/tmp/sem.c.patched', 'w') as f:
    f.writelines(result)

content = ''.join(result)
print(f"Lines: {len(lines)} -> {len(result)}")

checks = {
    'detect_sem_deadlock': 1,
    'break_sem_deadlock': 1,
    '0xDEAD': 3,       # 1 dispatch + 1 handler + 1 compat
    '0xBEAC': 3,
    'sched/signal.h': 1,
    'linux/mm.h': 1,
}

ok = True
for tag, expected in checks.items():
    actual = content.count(tag)
    status = "OK" if actual == expected else f"EXPECTED {expected}"
    if actual != expected:
        ok = False
    print(f"  {tag}: {actual} ({status})")

if ok:
    print("\nAll checks passed!")
    sys.exit(0)
else:
    print("\nSome checks FAILED!")
    sys.exit(1)
