# 计算机操作系统实验项目 — 完整笔记

> Operating System Course Lab — 2025-2026-2

---

## 目录

1. [环境概览](#1-环境概览)
2. [11个实验状态总览](#2-11个实验状态总览)
3. [实验11: MFQ调度器 — 完整记录](#3-实验11-mfq调度器--完整记录)
4. [内核开发经验教训](#4-内核开发经验教训)
5. [QEMU调试环境](#5-qemu调试环境)
6. [所有修改文件清单](#6-所有修改文件清单)
7. [编译与安装速查](#7-编译与安装速查)

---

## 1. 环境概览

### 硬件/虚拟化

| 项目 | 值 |
|------|-----|
| 平台 | VMware 虚拟机 |
| 宿主 OS | Ubuntu 22.04.5 LTS |
| CPU | AMD 24核 |
| 内存 | 31GB |
| 磁盘 | 80GB Virtual SATA |

### 存储拓扑

```
/dev/sda (80G, VMware Virtual SATA)
├── sda1  1M   BIOS boot
├── sda2  2G   /boot (ext4)
└── sda3  78G  LVM PV → ubuntu-vg/ubuntu-lv → / (ext4)
```

### 关键内核配置

| 配置项 | 值 | 重要性 |
|--------|-----|--------|
| `CONFIG_SATA_AHCI` | **m** (模块) | 磁盘驱动是模块，initrd 必须包含 ahci.ko |
| `CONFIG_BLK_DEV_SD` | y | SCSI 磁盘层 built-in |
| `CONFIG_BLK_DEV_DM` | y | Device Mapper (LVM) built-in |
| `CONFIG_EXT4_FS` | y | ext4 built-in |
| `CONFIG_SCSI` | y | SCSI 层 built-in |
| `CONFIG_MODVERSIONS` | y | 严格模块 CRC 校验 |
| `CONFIG_SCHED_MFQ` | y | MFQ 调度器 (exp11) |

### 两个内核共存

| 内核 | 版本号 | bzImage | initrd | 用途 |
|------|--------|---------|--------|------|
| 原内核 | 6.18.15 #18 | `/boot/vmlinuz-6.18.15` | `/boot/initrd.img-6.18.15` (1.1G) | 日常使用，安全回退 |
| MFQ内核 | 6.18.15 #48 | `/boot/vmlinuz-6.18.15-mfq` | `/boot/initrd.img-6.18.15-mfq` (553M) | exp11 验证 |

**铁律**: 两个内核版本号相同，共享 `/lib/modules/6.18.15/`。旧内核只能配旧 initrd，新内核只能配新 initrd。**永不互换。**

---

## 2. 11个实验状态总览

| # | 实验名称 | 学时 | 状态 | 关键成果 |
|---|---------|------|------|---------|
| 1 | Linux内核裁剪和编译 | 4 | ✅ 完成 | 内核编译+安装，`uname -r` 验证 |
| 2 | 系统调用分析和增加 | 4 | ✅ 完成 | 添加 syscall 470 (my_add)，Linux 6.x 只需改 .tbl |
| 3 | 哲学家进餐多线程 | 4 | ✅ 完成 | 死锁+预防版本，预防版 30/30 成功 |
| 4 | 生产者/消费者多进程 | 4 | ✅ 完成 | XSI IPC, 3生产者+4消费者 |
| 5 | 目录递归拷贝 | 4 | ✅ 完成 | 单/多进程, diff -r 验证 |
| 6 | x86架构启动过程分析 | 4 | ✅ 完成 | QEMU+GDB+busybox initramfs |
| 7 | 设备文件与驱动程序 | 4 | ✅ 完成 | char_driver: open/write/read/ioctl 全链路 |
| 8 | 进程通信及信号量改进 | 4 | ✅ 完成 | 死锁检测 DEADCHECK/DEADBREAK，raw syscall 绕过 glibc |
| 9 | 文件系统及加密实现 | 8 | ✅ 完成 | ext2m XOR加密, read_iter/write_iter 方案 |
| 10 | 内存管理分析与验证 | 8 | ✅ 完成 | brk/mmap/COW观测 + page_stats 内核模块 |
| 11 | MFQ多级反馈队列调度 | 8 | ✅ QEMU通过 | 见下方完整记录 |

> 实验1-10 截图和报告撰写待用户自行完成。

---

## 3. 实验11: MFQ调度器 — 完整记录

### 3.1 实验目标

在 Linux 6.18.15 内核中实现一个**8级多级反馈队列(MFQ)调度器**，作为新的调度类 `SCHED_MFQ` (policy=4)。

### 3.2 调度器设计

```
队列:  L0(最高,10tick) → L1(20) → L2(40) → ... → L7(1280tick)
规则:  新任务→L0
       时间片耗尽 + 有其他任务 → 降级
       唯一任务 → 不降级
       I/O唤醒(WF_SYNC) → 升级 + 可能抢占
```

**调度类链**: `stop → dl → rt → mfq → fair → ext → idle`

**设计原则**:
- 仅服务 policy=4 (SCHED_MFQ)，不替换 CFS
- 使用 `pick_task` 模式（同 RT 调度器），核心代劳 put_prev/set_next
- 内核线程和普通用户进程默认继续使用 CFS

### 3.3 迭代历史

**总统计**: 49次内核编译, 7次真机重启, 4个致命Bug, ~12小时调试

```
时间线:
06/10 13:04  #29  初始 MFQ 内核编译
06/10 16:57       修复 __setscheduler_class 路由
06/10 17:26       编译全部 5591 个模块
06/10 17:42       MODULES=dep initrd → 0 模块 (失败)
06/10 18:11       MODULES=most initrd → 1465 模块 553M
06/10 18:20       重启 → MFQ 内核 #29 成功启动
06/10 18:30       测试死锁 → 硬重启
06/10 18:34       pick_next_task → pick_task 修复 #30→#31
06/10 18:35       valid_policy() + SCHED_MFQ 定义 #32
06/10 18:39       重启 → 死锁 (put_prev 不调 set_next)
06/10 19:16       重启 → scheduling while atomic panic (pr_info 在 spinlock)
06/10 19:30       删 pr_info #34 → 重启 → 死锁 (preempt_count 泄露)
06/10 19:50       搭建 QEMU 调试环境
06/11 17:55       QEMU 重现 preempt_count 泄露
06/11 17:56       对照: SCHED_IDLE 通过 ✅
06/11 17:57       往返: MFQ→CFS→exit 通过 ✅
06/11 17:58       零CPU: MFQ→exit 崩溃 ❌
06/11 18:00       DEBUG_PREEMPT=y 定位 put_prev
06/11 18:10       put_prev 空函数 → 通过 ✅
06/11 18:15       put_prev 只 lock/unlock → 3/3 通过 ✅
06/11 18:16       put_prev + list 操作 → 崩溃 ❌
06/11 18:18       put_prev lock+barrier body → 通过 ✅
06/11 18:30       完整调度器全功能测试 → 通过 ✅
06/11 19:00       v2.1 最终版 3/3 通过 ✅
06/11 19:15       v3 (on_rq 替代 __state) → 失败 ❌
06/11 19:20       回归 v2.1, 定版 #48
```

### 3.4 四个致命 Bug 详解

#### Bug 1: `pick_next_task` 不调 `put_prev`/`set_next` — 全系统死锁

**症状**: 运行 MFQ 测试程序 → 整个 server 冻结，键盘无响应

**根因**: 注册了 `pick_next_task` 回调，但函数只从队列取任务，不调用 `put_prev_task` 和 `set_next_task`。核心调度器认为有 `pick_next_task` 的类自己负责这三步操作，结果队列状态逐步损坏 → 链表指针错乱 → 死锁。

**定位**: 对比 `kernel/sched/rt.c` — RT 调度器只用 `pick_task`，让核心通过 `put_prev_set_next_task()` 统一处理。

**修复**: 从 `DEFINE_SCHED_CLASS` 中删除 `.pick_next_task`，只用 `.pick_task`。

**教训**: 内核调度类接口中，`pick_next_task` 和 `pick_task` 是互斥的两种模式。前者要求类内部完成 put_prev + pick + set_next 全流程（如 CFS），后者只选任务，核心代劳其余（如 RT）。**没有充分理解接口契约就注册回调 = 必死。**

#### Bug 2: `pr_info` 在 spinlock 内 — scheduling while atomic panic

**症状**: `kernel BUG at kernel/sched/core.c:6956!` + `note: exited with preempt_count 1` + `BUG: scheduling while atomic`

**根因**: `task_tick_mfq` 在 `raw_spin_lock(&mrq->lock)` 保护区内调用 `pr_info`。定时器中断上下文持有 `rq->lock`，`mrq->lock` 嵌套在内。printk 在双 spinlock 下可能触发调度 → `scheduling while atomic` panic。

**修复**: 删除调度器热路径上所有 `pr_info`。

**教训**: 调度器热路径（task_tick, enqueue, dequeue, pick, put_prev, set_next）**绝对禁止**调用任何可能睡眠的函数。包括 printk（可能抢 console 信号量）、kmalloc(GFP_KERNEL)、copy_to_user 等。

#### Bug 3: `put_prev_task_mfq` 内 list 操作 — preempt_count 泄露

**症状**: 即使最简单的测试（切到 MFQ 立即 exit），`do_task_dead` 的 `__schedule` 检测到 `preempt_count 1` → `BUG()`。

**定位过程** (QEMU 二分法):
```
put_prev 空函数                          → ✅ 通过
put_prev 只 raw_spin_lock/unlock         → ✅ 通过 (3/3)
put_prev lock + barrier() body           → ✅ 通过
put_prev lock + list_del_init/list_add   → ❌ 崩溃
put_prev 锁移入 if body                  → ❌ 300+ BUGs (灾难)
```

**根因**: `if (prev->__state == TASK_RUNNING)` 块内的 `list_del_init(&prev->mfq_se.run_list)` 和 `list_add_tail(...)` — 即使条件永远不成立（prev=TASK_DEAD），编译器为 list 操作生成的代码仍会导致 preempt_count 异常。精确机制未完全理解，可能与 GCC 11.4 + PREEMPT_DYNAMIC(voluntary) + `-O2` 优化交互有关。

类似问题在 enqueue_task_mfq, dequeue_task_mfq, set_next_task_mfq 等函数中不存在 — 这些函数的 list 操作不在条件块中，而是无条件的。

**当前方案**: `put_prev_task_mfq` 只做 `raw_spin_lock/unlock`，不操作队列。

**已尝试的替代方案** (全部失败):
- `noinline` 辅助函数隔离 list 操作 → ❌
- `prev->on_rq` 替代 `prev->__state` → ❌
- 锁移入 if body → ❌
- `preempt_disable/enable` 替代 `raw_spin_lock/unlock` → 未测试

**功能影响**: 被抢占的 MFQ 任务不会回到就绪队列（时间片内完成的短任务不受影响）。

#### Bug 4: `SCHED_MFQ` 未注册到内核策略系统 — EINVAL

**三个独立根因**:

1. **`include/uapi/linux/sched.h` 缺少 `#define SCHED_MFQ 4`**
   - 内核编译需要的常量未定义
   - 修复: 添加 `#define SCHED_MFQ 4` (占用原 SCHED_ISO 保留位)

2. **`kernel/sched/sched.h` 的 `valid_policy()` 未包含 MFQ**
   - `valid_policy()` 只认 idle/fair/rt/dl → `__sched_setscheduler` 拒绝 policy=4
   - 修复: 添加 `mfq_policy()` 静态函数，加入 `valid_policy()` 白名单

3. **glibc 的 `sched_setscheduler()` 包装函数拒绝未知 policy**
   - glibc 不认识 policy=4 → 在进入内核前返回 EINVAL
   - 修复: 测试程序用 `syscall(__NR_sched_setscheduler, ...)` 绕过 glibc
   - 参考: 实验8 中 `semctl()` 遇到同样问题

### 3.5 最终代码 (v2.1, bzImage #48)

**文件**: `~/os/exp11/sched_mfq_final.c` (134行)

**sched_class 回调清单**:

| 回调 | 实现 | 说明 |
|------|------|------|
| `enqueue_task` | ✅ | 入队, nr_running++, 防御性 mfq_entity_init |
| `dequeue_task` | ✅ | 安全出队 (检查 list_empty) |
| `pick_task` | ✅ | 扫描 L0→L7, 返回首任务 |
| `put_prev_task` | ⚠️ | 仅 lock/unlock (list 操作有 Bug #3) |
| `set_next_task` | ✅ | 出队, nr_running--, 设为 curr |
| `task_tick` | ✅ | 时间片递减, 降级/nr=0刷新, resched_curr |
| `wakeup_preempt` | ✅ | WF_SYNC升级, 高优先级抢占 |
| `task_fork` | ✅ | mfq_entity_init |
| `task_dead` | ✅ | 空 |
| `switched_from` | ✅ | 清理队列残留 |
| `switched_to` | ✅ | mfq_entity_init |
| `select_task_rq` | ✅ | 简单CPU选择 |
| `update_curr` | ✅ | no-op |
| `set_cpus_allowed` | ✅ | no-op |
| `prio_changed` | ✅ | no-op |
| `migrate_task_rq` | ✅ | no-op |

### 3.6 QEMU 验证结果 (#48, v2.1)

```
=== MFQ调度器验证 (SCHED_MFQ=policy 4) ===
[父进程] pid=77 已切换到MFQ
[child-0] pid=78 CPU-bound
[child-1] pid=79 CPU-bound  
[child-2] pid=80 I/O-bound
[child-3] pid=81 I/O-bound
[child-3] pid=81 done
[child-2] pid=80 done
[child-1] pid=79 done
[child-0] pid=78 done
=== 验证完成 ===
EXIT: 0
```

- 连续 3 次运行: 3/3 通过
- 0 kernel BUG, 0 kernel panic
- 4 子进程全部正常完成退出

### 3.7 已知局限

| 局限 | 影响 | 优先级 |
|------|------|--------|
| `put_prev` 不操作队列 | 被抢占的 MFQ 任务丢失（时间片内完成不受影响） | 高 |
| 无 pr_info/dmesg 输出 | 无法通过日志观察调度行为 | 中 |
| 仅在 QEMU 单CPU 验证 | 未在真机 SMP 环境测试 | 高 |
| `mfq_se` 初始化时序 | `enqueue`在`switched_to`之前，防御性 init 存在但非最优 | 低 |

---

## 4. 内核开发经验教训

### 4.1 调度器开发

1. **先用 QEMU 验证，再上真机。** 真机死锁 = 只能硬重启。QEMU 死锁 = 加 `timeout` 自动杀掉。

2. **参考 RT 调度器而非 CFS。** RT 是简单的高优先级调度类（~2000行），CFS 是复杂的主力调度类（~14000行）。新手应该参考 RT。

3. **`pick_task` vs `pick_next_task`** — 只用前者。后者要求类内部处理 put_prev/set_next，容易出错。

4. **调度器代码中禁止睡眠。** 包括 printk, kmalloc(GFP_KERNEL), mutex_lock, copy_to_user。用 `trace_printk` 代替 `pr_info`。

5. **per-CPU 锁的作用。** 在 rq_lock 内部操作时，per-CPU 锁是多余的（rq_lock 已保护 CPU 独占访问）。但它们维护 preempt_count 平衡。去掉锁会导致 preempt_count 不一致。

### 4.2 内核编译与部署

6. **`CONFIG_MODVERSIONS=y` + 改 task_struct = 所有模块 CRC 全变。** 必须重编全部模块 + 重建 initrd。

7. **两个同版本号内核的模块冲突。** 共享 `/lib/modules/<version>/` → `make modules_install` 覆盖旧模块。备份旧模块到 `.bak`。

8. **initrd 构建铁律**: `MODULES=most` (不是 `dep`)、build 后必须 `lsinitramfs | grep ko | wc -l` > 0。`MODULES=dep` 看 `/proc/modules` → 可能漏掉关键模块。

9. **VMware SATA AHCI 是模块！** `CONFIG_SATA_AHCI=m` → initrd 必须有 `ahci.ko`，否则找不到磁盘 → `cannot mount root fs`。

10. **`/boot` 空间不足。** 2G 分区，两个 initrd 各 ~1G → 必须扩磁盘或存根分区。

### 4.3 调试技巧

11. **二分法定位。** put_prev bug 通过逐函数替换（空→锁→锁+条件→锁+条件+list操作）精确到一行代码。

12. **对照实验。** 用 SCHED_IDLE 证明核心调度器没问题，问题在我们代码。

13. **往返测试。** MFQ→CFS→exit 通过 + MFQ→exit 崩溃 = 问题在 MFQ 执行期间。

14. **零CPU测试。** 切换后立即退出，不加 CPU 负载 = 排除定时器/抢占干扰。

15. **`CONFIG_DEBUG_PREEMPT=y`** 启用了 preempt_count 跟踪，精确定位泄露点。

---

## 5. QEMU调试环境

### 5.1 构建 initramfs

```bash
# 最小 initramfs (busybox + 静态测试程序)
mkdir -p /tmp/initramfs/{bin,dev,proc,sys}
cp /usr/bin/busybox /tmp/initramfs/bin/
cd /tmp/initramfs && ln -s busybox bin/sh

# 静态编译测试程序
gcc -static -O2 -o /tmp/initramfs/mfq_test ~/os/exp11/mfq_test.c

# 创建 init 脚本
cat > /tmp/initramfs/init << 'EOF'
#!/bin/sh
mount -t proc none /proc; mount -t sysfs none /sys
mknod /dev/null c 1 3; mknod /dev/console c 5 1
echo "=== 测试开始 ==="
/mfq_test; echo "EXIT: $?"
exec /bin/sh
EOF
chmod +x /tmp/initramfs/init

# 打包
find . | cpio -o -H newc | gzip > /tmp/initramfs-qemu.img
```

### 5.2 快速验证

```bash
sudo timeout 15 qemu-system-x86_64 \
  -kernel /usr/src/linux-6.18.15/arch/x86/boot/bzImage \
  -initrd /tmp/initramfs-qemu.img \
  -append "console=ttyS0 nokaslr" \
  -nographic -m 512
```

### 5.3 GDB 调试

```bash
# 终端1: 启动 QEMU (停止等待 GDB)
sudo qemu-system-x86_64 \
  -kernel arch/x86/boot/bzImage \
  -initrd /tmp/initramfs-qemu.img \
  -append "console=ttyS0 nokaslr" \
  -nographic -m 512 -s -S

# 终端2: GDB
gdb-multiarch vmlinux \
  -ex "target remote :1234" \
  -ex "break pick_task_mfq" \
  -ex "break put_prev_task_mfq" \
  -ex "continue"
```

### 5.4 常用断点

```
break enqueue_task_mfq
break dequeue_task_mfq
break pick_task_mfq
break put_prev_task_mfq
break set_next_task_mfq
break task_tick_mfq
break switched_from_mfq
break switched_to_mfq
```

---

## 6. 所有修改文件清单

### 实验11 (MFQ调度器)

| 文件 | 改动 | 行数 |
|------|------|------|
| `kernel/sched/sched_mfq.c` | NEW — MFQ 调度器实现 | 134 |
| `include/uapi/linux/sched.h` | +1: `#define SCHED_MFQ 4` | 1 |
| `include/linux/sched.h` | +14: struct sched_mfq_entity + task_struct.mfq_se | 14 |
| `kernel/sched/sched.h` | +20: struct mfq_rq + mfq_policy() + valid_policy() | 20 |
| `kernel/sched/core.c` | +18: __setscheduler_class路由 + sched_init | 18 |
| `kernel/sched/syscalls.c` | +2: case 4: 验证 | 2 |
| `include/asm-generic/vmlinux.lds.h` | +1: linker ordering | 1 |
| `kernel/sched/Makefile` | +1: obj-$(CONFIG_SCHED_MFQ) | 1 |
| `init/Kconfig` | +11: CONFIG_SCHED_MFQ 配置 | 11 |
| `~/os/exp11/mfq_test.c` | NEW — 测试程序 (raw syscall) | ~100 |

### 实验8 (信号量改进)

| 文件 | 改动 |
|------|------|
| `ipc/sem.c` | detect_sem_deadlock + break_sem_deadlock |
| (测试程序) | raw syscall 绕过 glibc semctl |

### 实验9 (加密文件系统)

| 文件 | 改动 |
|------|------|
| `fs/ext2m/` (复制的 ext2/) | read_iter/write_iter XOR 加解密 |

### 实验10 (内存管理)

| 文件 | 改动 |
|------|------|
| `~/os/exp10/mem_observe.c` | strace 观察 brk/mmap |
| `~/os/exp10/mem_alloc.c` | 分配+访问 缺页观察 |
| `~/os/exp10/mem_cow.c` | COW 10GB 父子共享 |
| `~/os/exp10/page_stats.c` | 内核模块页分类统计 |

---

## 7. 编译与安装速查

### 仅编译内核 (实验11日常)

```bash
cd /usr/src/linux-6.18.15
sudo make -j$(nproc) bzImage
```

### 改 task_struct 后 (需重建模块+initrd)

```bash
cd /usr/src/linux-6.18.15
sudo make -j$(nproc) modules        # 编译全部模块
sudo cp -a /lib/modules/6.18.15 /lib/modules/6.18.15.bak  # 备份旧模块
sudo make modules_install           # 安装(覆盖!)
sudo depmod -a 6.18.15

# 在根分区构建 initrd (/boot 只有 2G)
sudo rm -rf /tmp/initrd-build && sudo mkdir -p /tmp/initrd-build
sudo update-initramfs -c -k 6.18.15 -b /tmp/initrd-build

# 验证
lsinitramfs /tmp/initrd-build/initrd.img-6.18.15 | grep '\.ko$' | wc -l

# 安装 MFQ 内核的 initrd
sudo cp /tmp/initrd-build/initrd.img-6.18.15 /boot/initrd.img-6.18.15-mfq
sudo update-grub
```

### 安装新 bzImage (不改 task_struct)

```bash
sudo cp /usr/src/linux-6.18.15/arch/x86/boot/bzImage /boot/vmlinuz-6.18.15-mfq
sudo update-grub
sudo reboot
```

### 安全回退

```bash
# grub 启动时选第一个选项: Ubuntu, with Linux 6.18.15
# (非 -mfq 版本)
```

---

## 附录

### A. 有用的命令

```bash
# 查看 QEMU 完整启动日志 (含崩溃信息)
tail -50 /tmp/qemu_run.log

# 搜索特定错误
grep -E "BUG|WARN|panic|Call Trace|RIP:" /tmp/qemu_run.log

# 查看 initrd 内容
lsinitramfs /boot/initrd.img-6.18.15-mfq | grep -E 'ahci|\.ko$' | head

# 确认运行中的内核
uname -a

# 查看内核符号
sudo cat /proc/kallsyms | grep mfq

# 磁盘空间
df -h /boot
```

### B. 参考

- `kernel/sched/rt.c` — RT调度器 (pick_task 模式参考)
- `kernel/sched/fair.c` — CFS调度器 (pick_next_task 模式参考)
- `kernel/sched/sched.h` — DEFINE_SCHED_CLASS 宏和接口定义
- `include/linux/sched.h` — task_struct 定义
- Linux 6.18 源码: `/usr/src/linux-6.18.15/`
