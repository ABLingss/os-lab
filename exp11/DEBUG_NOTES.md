# Exp11 MFQ调度器 — 调试笔记

## 时间线

| 日期 | 事件 | bzImage |
|------|------|---------|
| 6/10 13:04 | 初始MFQ内核编译 | #29 |
| 6/10 16:57 | 修复__setscheduler_class路由 | #29 |
| 6/10 17:26 | 编译全部模块 + make modules_install | — |
| 6/10 17:42 | MODULES=dep initrd (0模块! 失败) | — |
| 6/10 18:11 | MODULES=most initrd (1465模块 553M) | — |
| 6/10 18:14 | 新initrd安装到/boot | — |
| 6/10 18:20 | 重启→MFQ内核#29 成功启动 | #29 |
| 6/10 18:30 | 测试死锁 → 硬重启 | — |
| 6/10 18:34 | pick_next_task→pick_task修复 | #30→#31 |
| 6/10 18:35 | valid_policy() + SCHED_MFQ定义修复 | #32 |
| 6/10 18:38 | 修复glibc拒绝policy=4 (raw syscall) | — |
| 6/10 18:39 | #30重启→死锁 | #30 |
| 6/10 19:16 | #32重启→scheduling while atomic panic | #32 |
| 6/10 19:30 | 删除pr_info (#34)→重启→死锁 | #34 |
| 6/10 19:50 | 创建QEMU调试环境 | — |
| 6/11 17:55 | QEMU中重现preempt_count泄露 | #34 |
| 6/11 17:56 | 对照实验: SCHED_IDLE通过 ✅ | #34 |
| 6/11 17:57 | 往返测试(MFQ→CFS→exit)通过 ✅ | #36 |
| 6/11 17:58 | 零CPU测试(MFQ→exit)崩溃 ❌ | #36 |
| 6/11 17:59 | 骨架调度器 崩溃 ❌ | #36 |
| 6/11 18:00 | DEBUG_PREEMPT=y → 定位put_prev | #35→#37 |
| 6/11 18:10 | put_prev空函数→通过 ✅ | #39 |
| 6/11 18:15 | put_prev只lock/unlock→通过 ✅ (3/3) | #41→#43 |
| 6/11 18:16 | put_prev+list ops→崩溃 ❌ | #42 |
| 6/11 18:17 | put_prev锁移入if body→300+BUGs ❌ | #44 |
| 6/11 18:18 | put_prev lock+if(RUNNING){barrier()}+unlock→通过 ✅ | #45 |
| 6/11 18:30 | 完整调度器+全功能测试→通过 ✅ EXIT:0 | #46 |

## 四个致命Bug

### Bug 1: pick_next_task 不调 put_prev/set_next → 死锁
- **根因**: 注册了 pick_next_task 但只取任务不放回。核心调度器不代劳。
- **修复**: 删 pick_next_task，只用 pick_task（同 RT 调度器）。
- **参考**: `kernel/sched/rt.c`

### Bug 2: pr_info 在 spinlock 内 → scheduling while atomic
- **根因**: task_tick_mfq 在双 spinlock(rq+mrq) 中调 pr_info(printk可能睡眠)
- **修复**: 删除所有 pr_info

### Bug 3: put_prev_task_mfq 内 list 操作 → preempt_count 泄露
- **症状**: do_task_dead → BUG(), exited with preempt_count 1
- **定位**: QEMU二分法 → put_prev_task_mfq 是唯一罪魁
- **根因**: `if (prev->__state == TASK_RUNNING)` 块内的 list_del_init/list_add_tail 代码生成导致 preempt_count 异常（即使 body 不执行）
- **当前方案**: put_prev 只 lock/unlock
- **精确复现条件**: lock + if(RUNNING){list ops} + unlock → 崩溃; lock + if(RUNNING){barrier()} + unlock → 通过

### Bug 4: SCHED_MFQ 未注册
- `include/uapi/linux/sched.h`: 缺 `#define SCHED_MFQ 4`
- `kernel/sched/sched.h`: valid_policy() 未包含 mfq_policy()
- glibc sched_setscheduler() 拒绝自定义 policy → 需要 raw syscall

## 修改的文件

| 文件 | 改动 |
|------|------|
| `include/uapi/linux/sched.h` | `#define SCHED_MFQ 4` |
| `include/linux/sched.h` | +struct mfq_se, +task_struct字段 |
| `kernel/sched/sched.h` | struct mfq_rq, mfq_policy(), valid_policy()更新 |
| `kernel/sched/core.c` | __setscheduler_class路由, sched_init初始化 |
| `kernel/sched/syscalls.c` | policy=4 验证 |
| `kernel/sched/sched_mfq.c` | 调度器实现 (~115行) |
| `include/asm-generic/vmlinux.lds.h` | linker顺序 |
| `kernel/sched/Makefile` | sched_mfq.o |
| `init/Kconfig` | CONFIG_SCHED_MFQ |

## 当前局限性

- `put_prev_task_mfq` 只能 lock/unlock，不操作队列
- 意味着被抢占的 MFQ 任务不会回到队列（在时间片内完成的任务不受影响）
- 没有 pr_info/dmesg 输出（删了，spinlock冲突）
- 只在 QEMU 单 CPU 验证过，未在真机 SMP 测试

## QEMU 验证命令

```bash
# 编译
cd /usr/src/linux-6.18.15 && sudo make -j$(nproc) bzImage

# 构建initramfs (含静态编译的测试程序)
# ... (见 CLAUDE.md QEMU章节)

# 运行
sudo timeout 15 qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -initrd /tmp/initramfs-qemu.img -append "console=ttyS0 nokaslr" \
  -nographic -m 512

# GDB调试
sudo qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -initrd /tmp/initramfs-qemu.img -append "console=ttyS0 nokaslr" \
  -nographic -m 512 -s -S
# 另开终端:
gdb-multiarch vmlinux -ex "target remote :1234"
```
