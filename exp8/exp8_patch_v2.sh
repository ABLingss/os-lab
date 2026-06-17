#!/bin/bash
# 实验8：修改 ipc/sem.c — 修正版
set -e
KERNEL_SRC=/usr/src/linux-6.18.15
SEMC=$KERNEL_SRC/ipc/sem.c

echo "=== Step 1: 添加头文件 ==="
sudo sed -i '/^#include <linux\/rhashtable.h>/a\#include <linux\/sched\/signal.h>\n#include <linux\/mm.h>' $SEMC
grep -q "sched/signal.h" $SEMC && echo "  OK: headers added" || echo "  FAIL"

echo "=== Step 2: 写入函数到临时文件 ==="
cat > /tmp/deadlock_funcs.c << 'EOF'
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
		pr_warn("[sem_deadlock] DEADLOCK DETECTED! semid=%d waiters=%d nsems=%d\n",
		       sma->sem_perm.id, waiters, sma->sem_nsems);
		spin_lock(&sma->sem_perm.lock);
		list_for_each_entry(q, &sma->pending_alter, list)
			pr_warn("[sem_deadlock] Waiter: %s PID=%d\n",
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

echo "=== Step 3: 插入函数（在 ksys_semctl 之前）==="
KSYS_LINE=$(grep -n "^static long ksys_semctl" $SEMC | head -1 | cut -d: -f1)
PREV=$((KSYS_LINE - 1))
sudo sed -i "${PREV}r /tmp/deadlock_funcs.c" $SEMC
grep -q "detect_sem_deadlock" $SEMC && echo "  OK: detect_sem_deadlock inserted" || echo "  FAIL"

echo "=== Step 4: 在 ksys_semctl (第一个 case IPC_RMID) 添加分发 ==="
# 只在 ksys_semctl 函数内的 IPC_RMID 前添加
sudo cp $SEMC $SEMC.bak
python3 << 'PYEOF'
import re

with open('/usr/src/linux-6.18.15/ipc/sem.c', 'r') as f:
    content = f.read()

# Count occurrences of "case IPC_RMID:" before replacement
count = content.count('case IPC_RMID:')
print(f"Found {count} 'case IPC_RMID:' occurrences")

# Replace the FIRST occurrence only
new_case = 'case 0xDEAD:\n\t\tcase 0xBEAC:\n\t\t\treturn semctl_down(ns, semid, cmd, &semid64);\n\t\tcase IPC_RMID:'
content = content.replace('case IPC_RMID:', new_case, 1)

with open('/usr/src/linux-6.18.15/ipc/sem.c', 'w') as f:
    f.write(content)
print("Step 4 done: ksys_semctl dispatch added")
PYEOF

echo "=== Step 5: 在 compat_ksys_semctl (现在第二个 case IPC_RMID) 添加分发 ==="
python3 << 'PYEOF'
with open('/usr/src/linux-6.18.15/ipc/sem.c', 'r') as f:
    content = f.read()

count = content.count('case IPC_RMID:')
print(f"Remaining 'case IPC_RMID:' occurrences: {count}")

# The first one (inside semctl_down) should NOT get the dispatch.
# The second one (compat_ksys_semctl) SHOULD.
# But actually: after step 4, the FIRST case IPC_RMID: was split.
# semctl_down has its own case IPC_RMID:
# compat_ksys_semctl has its own case IPC_RMID:

# In the original, semctl_down had case IPC_RMID at ~line 1625
# The first occurrence in ksys_semctl was replaced.
# Now we need to find compat_ksys_semctl's IPC_RMID.
# Let's find the one that's near "return semctl_down(ns, semid, cmd, &semid64)"

# Actually, let me just find the function context
lines = content.split('\n')
found_compat = False
for i, line in enumerate(lines):
    if 'static long compat_ksys_semctl' in line:
        found_compat = True
    if found_compat and line.strip() == 'case IPC_RMID:':
        # This is the compat one, replace it
        indent = line[:len(line) - len(line.lstrip())]
        new_lines = [
            indent + 'case 0xDEAD:',
            indent + 'case 0xBEAC:',
            indent + '\treturn semctl_down(ns, semid, cmd, &semid64);',
            indent + 'case IPC_RMID:',
        ]
        lines = lines[:i] + new_lines + lines[i+1:]
        break

content = '\n'.join(lines)
with open('/usr/src/linux-6.18.15/ipc/sem.c', 'w') as f:
    f.write(content)
print("Step 5 done: compat_ksys_semctl dispatch added")
PYEOF

echo "=== Step 6: 在 semctl_down 中添加 DEADCHECK/DEADBREAK 处理 ==="
python3 << 'PYEOF'
with open('/usr/src/linux-6.18.15/ipc/sem.c', 'r') as f:
    content = f.read()

# Find semctl_down function and its first 'case IPC_RMID:'
# The semctl_down function starts with 'static int semctl_down'
lines = content.split('\n')
in_semctl_down = False
found_case = False
for i, line in enumerate(lines):
    if 'static int semctl_down' in line:
        in_semctl_down = True
    if in_semctl_down and line.strip() == 'case IPC_RMID:':
        # Replace this one
        indent = line[:len(line) - len(line.lstrip())]
        new_lines = [
            indent + 'case 0xDEAD: {',
            indent + '\tint dl = detect_sem_deadlock(sma);',
            indent + '\terr = dl;',
            indent + '\tif (dl)',
            indent + '\t\tpr_warn("[sem_deadlock] DEADCHECK: deadlock on semid=%d\\n", sma->sem_perm.id);',
            indent + '\telse',
            indent + '\t\tpr_info("[sem_deadlock] DEADCHECK: no deadlock on semid=%d\\n", sma->sem_perm.id);',
            indent + '\tgoto out_unlock1;',
            indent + '}',
            indent + 'case 0xBEAC: {',
            indent + '\terr = break_sem_deadlock(sma);',
            indent + '\tif (err == 0)',
            indent + '\t\tpr_warn("[sem_deadlock] DEADBREAK: broken on semid=%d\\n", sma->sem_perm.id);',
            indent + '\tgoto out_unlock1;',
            indent + '}',
            indent + 'case IPC_RMID:',
        ]
        lines = lines[:i] + new_lines + lines[i+1:]
        break

content = '\n'.join(lines)
with open('/usr/src/linux-6.18.15/ipc/sem.c', 'w') as f:
    f.write(content)
print("Step 6 done: semctl_down handler added")
PYEOF

echo ""
echo "=== 验证 ==="
grep -c "0xDEAD\|0xBEAC\|detect_sem_deadlock\|break_sem_deadlock" $SEMC
echo "(应有 5+ 处匹配)"
echo ""
echo "开始编译..."
