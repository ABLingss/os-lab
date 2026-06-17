# CLAUDE.md — 计算机操作系统实验项目

## 项目概述

**《计算机操作系统》** 课程实验项目。

## 核心文档

| 文件 | 说明 |
|------|------|
| `2024-2025-2学期-计算机操作系统实验报告模板新.docx` | 报告模板（11个实验框架） |
| `2025-2026-2学期《计算机操作系统》实验指导书新.docx` | 官方实验指导书 |
| `2025-2026-2学期《计算机操作系统》实验大纲新.doc` | 实验大纲 |
| `report-reading/linux_kernel_reading_report.tex` | Linux 6.18.15 内核源码深度阅读与分析报告 |

## 目录结构

```
os/
├── CLAUDE.md              # 本文件
├── bin/                    # 编译产物（测试程序、内核模块）
├── exp1/ ~ exp11/          # 各实验代码+指南
└── *.docx, *.pdf           # 参考文档
```

## 11个实验清单（严格按指导书）

| # | 实验名称 | 学时 | 类型 |
|---|---------|------|------|
| 1 | Linux内核裁剪和编译 | 4 | 内核配置+编译 |
| 2 | Linux系统调用分析和增加系统调用 | 4 | 内核编程+编译 |
| 3 | Linux下哲学家进餐问题的多线程实现与解决 | 4 | 用户态C/pthread |
| 4 | Linux下生产者/消费者问题的多进程实现 | 4 | 用户态C/XSI IPC |
| 5 | Linux目录下递归拷贝的单/多进程实现 | 4 | 用户态C/文件IO/fork |
| 6 | Linux的x86架构启动过程分析和跟踪 | 4 | 内核分析+gdb调试 |
| 7 | Linux设备文件与驱动程序 | 4 | 内核模块 |
| 8 | Linux进程通信分析及信号量机制改进 | 4 | 内核编程(IPC) |
| 9 | Linux文件系统分析及加密文件系统实现 | 8 | 内核编程(FS) |
| 10 | Linux内存管理分析与验证 | 8 | 内核分析+模块+应用 |
| 11 | Linux进程管理分析及多级反馈队列调度算法实现 | 8 | 内核编程(调度) |

## 工作原则

1. **严格按指导书内容执行**，不自行增删修改步骤
2. 截图和报告撰写由用户自行完成
3. 内核实验优先 QEMU 验证，避免频繁重启真机
4. 测试文件存 `~/os/expN/`，不写 `/tmp`（重启丢失）

## 环境信息

- **平台**: VMware 虚拟机
- **宿主机**: Ubuntu 22.04.5 LTS
- **当前运行内核**: 6.18.15（自编译，`#18 SMP PREEMPT_DYNAMIC`）
- **MFQ内核**: 6.18.15-mfq（`#29 SMP PREEMPT_DYNAMIC`，含 MFQ 调度器）
- **CPU**: AMD 24核, **内存**: 31GB, **磁盘**: 60G (扩盘中)
- **内核源码**: linux-6.18.15 (`/usr/src/linux-6.18.15`)


### 存储拓扑

```
/dev/sda (60G, VMware Virtual SATA)
├── sda1  1M   BIOS boot
├── sda2  2G   /boot (ext4)
└── sda3  58G  LVM PV → ubuntu-vg/ubuntu-lv → / (ext4)
```

- **磁盘控制器**: VMware SATA AHCI (`CONFIG_SATA_AHCI=m` — **模块！**)
- **SCSI 控制器**: LSI 53c1030 (`mptspi/mptscsih/mptbase`)
- **Root**: LVM on `/dev/sda3`，fstab 用 `dm-uuid-LVM-...`

### 关键内核配置

| 配置项 | 值 | 影响 |
|--------|-----|------|
| `CONFIG_SCHED_MFQ` | y | MFQ调度器（built-in） |
| `CONFIG_MODVERSIONS` | y | **严格模块CRC校验**，ABI一变所有旧模块无法加载 |
| `CONFIG_SATA_AHCI` | **m** (模块) | 磁盘驱动是模块，**initrd必须有ahci.ko** |
| `CONFIG_BLK_DEV_SD` | y | SCSI磁盘层 built-in ✅ |
| `CONFIG_BLK_DEV_DM` | y | Device Mapper (LVM) built-in ✅ |
| `CONFIG_EXT4_FS` | y | ext4 built-in ✅ |
| `CONFIG_SCSI` | y | SCSI层 built-in ✅ |

### 两个内核共存的模块问题

两个内核版本号都是 `6.18.15`，共享 `/lib/modules/6.18.15/`。`make modules_install` 会**覆盖**旧模块。
因此：旧内核用旧initrd（内含旧ABI模块），新内核用新initrd（内含新ABI模块），**两个initrd不能互换**。

---

## 当前进度

### 实验1 — ✅ 已完成

| 步骤 | 状态 |
|------|------|
| 磁盘扩容 + 工具链 + 源码 | ✅ |
| `make olddefconfig` + `menuconfig` 裁剪 | ✅ |
| `make -j24` 编译 + 安装 + update-grub | ✅ |
| 重启 + `uname -r` 验证 6.18.15 | ✅ |

### 实验2 — ✅ 已完成 (2025-06-10)

| 步骤 | 状态 |
|------|------|
| 修改 syscall_64.tbl（添加 470 common my_add） | ✅ |
| 修改 kernel/sys.c（SYSCALL_DEFINE2 实现） | ✅ |
| 修改 unistd.h（__NR_syscalls 470→471） | ✅ |
| 增量编译 + 安装 + 重启验证 | ✅ |
| `./test_mycall` → 10+20=30, dmesg 验证 | ✅ |
| 截图 + 写报告 | ⬜ |

**Linux 6.x 关键差异**：只需改 `.tbl` + `sys.c` + `unistd.h` 三个文件。`unistd_64.h` 和 `syscall_64.c` 由编译自动生成。

### 实验3 — ✅ 已完成 (2025-06-10)

| 步骤 | 状态 |
|------|------|
| 修复 print_state 格式化 + 编译 | ✅ |
| 死锁版本运行 + 预防版本运行 | ✅ |
| 预防版本 × 30次 100% 成功率 | ✅ |
| 截图 + 写报告 | ⬜ |

### 实验4 — ✅ 已完成 (2025-06-10)

| 步骤 | 状态 |
|------|------|
| 修复 stdio 缓冲（setvbuf） + 消费者级联唤醒 | ✅ |
| 3生产者 + 4消费者 → 总生产15 / 总消费15 / 残留0 | ✅ |
| 截图 + 写报告 | ⬜ |

### 实验5 — ✅ 已完成 (2025-06-10)

| 步骤 | 状态 |
|------|------|
| mycp: 文件→文件、文件不存在、文件→目录、目录递归 | ✅ |
| diff -r 验证所有场景 | ✅ |
| myls: 多进程目录拷贝（4子进程并发） | ✅ |
| 截图 + 写报告 | ⬜ |

### 实验6 — ✅ 环境完成 (2025-06-10)

| 步骤 | 状态 |
|------|------|
| qemu-system-x86 + gdb-multiarch + busybox 安装 | ✅ |
| DEBUG_INFO_DWARF5 + KGDB 确认 | ✅ |
| EXP6 printk 添加到 init/main.c (start_kernel, rest_init) | ✅ |
| bzImage #3 编译 + QEMU 启动验证 printk 可见 | ✅ |
| initramfs.img 创建（busybox） | ✅ |
| GDB 交互调试 + dmesg EXP6 验证 | ⬜（用户操作） |
| 截图 + 写报告 | ⬜ |

### 实验7 — ✅ 已完成 (2025-06-10)

| 步骤 | 状态 |
|------|------|
| hello_module: 加载/卸载 + printk 日志 | ✅ |
| char_driver: insmod → /dev/mychardev 创建 | ✅ |
| test_driver: open/write/read/ioctl 全链路 | ✅ |
| rmmod → /dev/mychardev 消失 | ✅ |
| 截图 + 写报告 | ⬜ |

### 实验8 — ✅ 已完成 (2025-06-10)

| 步骤 | 状态 |
|------|------|
| ipc/sem.c 添加 detect_sem_deadlock + break_sem_deadlock | ✅ |
| ksys_semctl / compat 添加 DEADCHECK(0xDEAD) / DEADBREAK(0xBEAC) | ✅ |
| bzImage #10 编译 + 安装 + 重启 | ✅ |
| 死锁检测 3/3 全过: DEADCHECK→1, DEADBREAK→0 | ✅ |
| 关键Bug修复: per-sem队列 (sma->sems[i].pending_alter) | ✅ |
| 关键Bug修复: glibc semctl拒绝自定义cmd → 改用 raw syscall | ✅ |
| 截图 + 写报告 | ⬜ |

### 实验9 — ✅ 已完成 (2025-06-10)

| 步骤 | 状态 |
|------|------|
| 复制 fs/ext2/ → fs/ext2m/，注册为新文件系统类型 "ext2m" | ✅ |
| file.c: read_iter/write_iter 用 kernel iov + XOR 加解密 | ✅ |
| bzImage #18 编译 + 安装 + 重启 | ✅ |
| ext2m 写明文 → 磁盘存密文 → ext2m 读回明文 ✓ | ✅ |
| ext2 挂载 → hexdump 证实密文（'A'^0x4F=0x0E 验算一致） | ✅ |
| xor_tool: 用户空间 XOR 工具（与内核同密钥同公式） | ✅ |
| 截图 + 写报告 | ⬜ |

**关键技术点**：
- 不能改 read_folio/writepages（readahead 绕过解密，page cache 污染密文）
- 正确做法：file.c 的 read_iter/write_iter 中用 `iov_iter_kvec` 创建 kernel iov 缓冲区，调用 `generic_file_read_iter`/`generic_file_write_iter` 无递归
- 公式：`buf[i] ^= key[(i + block*7) % 16]`，block = pos >> PAGE_SHIFT
- ext2m 磁盘格式与 ext2 完全兼容（仅运行时加解密）

### 实验10 — ✅ 已完成 (2025-06-10)

| 步骤 | 状态 |
|------|------|
| 实验内容二-1: mem_observe.c — malloc/free strace 观察 brk/mmap | ✅ |
| 实验内容二-2: mem_alloc.c — 只分配不访问(RES不增) vs 分配并访问(RES增长) | ✅ |
| 实验内容二-3: mem_cow.c — 10GB COW 父子进程共享内存观察 | ✅ |
| 实验内容三: page_stats.c — 内核模块逐页遍历分类统计 | ✅ |
| 截图 + 写报告 | ⬜ |

**关键技术点**：
- glibc malloc ≤128KB 用 brk(堆扩展)，>128KB 用 mmap(匿名映射) — strace 验证
- 只分配不访问：VIRT↑ RES不变（缺页未触发，无物理页分配）
- 分配并访问：VIRT↑ RES↑（触发缺页，物理页逐页分配）
- COW：fork后父子VIRT各10G但共享RES~10G；子进程写入触发COW，RES翻倍~20G
- 内核模块：`for_each_online_node` + `NODE_DATA` → 遍历 zone → `pfn_to_page`，Linux 6.18 使用 folio API（folio_test_active/page_folio 替代 PageActive）
- PageBuddy 只标记 compound head，空闲页还需统计 page_count=0 的尾页
- `first_online_pgdat`/`next_online_pgdat` 未 EXPORT_SYMBOL，模块用 `for_each_online_node` 替代

### 实验11 — ✅ QEMU验证通过，真机待测 (2025-06-11)

> **详尽笔记**: `PROJECT_NOTE.md` — 包含完整迭代历史、4个Bug详解、QEMU调试环境、经验教训、编译速查

#### 当前状态

| 步骤 | 状态 | 说明 |
|------|------|------|
| 调度器代码 (sched_mfq.c) | ✅ | QEMU全功能测试通过 |
| 集成修改 (8个文件) | ✅ | 见下方修改清单 |
| 编译 bzImage (#46) | ✅ | QEMU可启动 |
| initrd 重建 | ✅ | 1465模块 553M (仅用于真机) |
| **QEMU 验证** | ✅ | 父进程+4子进程全部完成 EXIT:0 |
| **真机安装+重启** | ⬜ | bzImage#46未安装到/boot |
| 截图 + 写报告 | ⬜ | — |

#### 完整迭代历史 (共46次编译, 7次重启, 4个致命bug)

##### Bug 1: `pick_next_task` 不调 `put_prev`/`set_next` — 死锁
- **bzImage**: #29→#30→#31→#32
- **症状**: 运行测试 → 全系统死锁
- **根因**: 注册了 `pick_next_task` 但函数内部只取任务不调 put_prev/set_next。核心调度器认为有 `pick_next_task` 的类自己负责这三步，我们没做 → 队列状态逐步损坏
- **修复**: 删掉 `pick_next_task`，只用 `pick_task`（同 RT 调度器模式）。核心自动 `put_prev_set_next_task()` 处理一切
- **参考**: `kernel/sched/rt.c` — RT 只有 `pick_task`，没有 `pick_next_task`

##### Bug 2: `pr_info` 在 spinlock 内 — scheduling while atomic panic
- **bzImage**: #33→#34
- **症状**: `kernel BUG at kernel/sched/core.c:6956!` + `note: exited with preempt_count 1` + `BUG: scheduling while atomic`
- **根因**: `task_tick_mfq` 在 `raw_spin_lock(&mrq->lock)` 内调 `pr_info`。定时器中断持有 `rq->lock`，我们的 `mrq->lock` 嵌套在内，**双 spinlock 上下文**中 printk 可能触发调度 → panic
- **修复**: 删除调度器内所有 `pr_info`（热路径不能调可能睡眠的函数）

##### Bug 3: `sched_setscheduler` 后 preempt_count 泄露 1 — do_task_dead BUG
- **bzImage**: #34→#35→#36→#37→#38→#39→#40→#41→#42→#43→#44→#45→#46
- **最隐蔽的 bug**: 在 QEMU 中用二分法精确定位到 `put_prev_task_mfq`
- **根因**: `put_prev_task_mfq` 的 `if (prev->__state == TASK_RUNNING)` 块内的 list 操作代码生成导致了 preempt_count 微妙泄露。即使 body 从不执行（prev=TASK_DEAD），编译器生成的地址计算代码也会引入问题。精确机制未完全理解，可能与 PREEMPT_DYNAMIC(voluntary) 模式下的 spinlock 跟踪有关
- **定位过程**: 用 QEMU 做受控实验:
  - `put_prev` 空函数 → 通过 ✅
  - `put_prev` 只 lock/unlock → 通过 ✅ (3/3)
  - `put_prev` lock + `if (RUNNING) {barrier()}` + unlock → 通过 ✅
  - `put_prev` lock + `if (RUNNING) {list_del_init; list_add_tail; nr++}` + unlock → **崩溃** ❌
  - `put_prev` 把锁移进 if body → **灾难** (300+ BUGs) ❌
- **当前方案**: `put_prev_task_mfq` 只做 lock/unlock，不操作队列。这意味抢占后任务不回到队列，但测试中所有任务在首次时间片内完成所以不影响

##### Bug 4: `SCHED_MFQ` 未注册到 `valid_policy()` — EINVAL
- **症状**: `sched_setscheduler(SCHED_MFQ): Invalid argument`
- **根因 1**: `include/uapi/linux/sched.h` 缺少 `#define SCHED_MFQ 4`
- **根因 2**: `kernel/sched/sched.h` 的 `valid_policy()` 只认 idle/fair/rt/dl，未包含 MFQ
- **根因 3**: glibc `sched_setscheduler()` 包装函数拒绝未知 policy → 测试程序需用 raw `syscall()`

#### QEMU 调试环境

```bash
# 构建 initramfs (含静态编译的测试程序)
mkdir -p /tmp/initramfs/{bin,dev,proc,sys}
cp /usr/bin/busybox /tmp/initramfs/bin/
cd /tmp/initramfs && ln -s busybox bin/sh
# 静态编译测试程序
gcc -static -O2 -o /tmp/initramfs/mfq_test mfq_test.c
# 打包
find . | cpio -o -H newc | gzip > /tmp/initramfs-qemu.img

# 启动 QEMU (快速验证)
sudo timeout 15 qemu-system-x86_64 \
  -kernel arch/x86/boot/bzImage \
  -initrd /tmp/initramfs-qemu.img \
  -append "console=ttyS0 nokaslr" -nographic -m 512

# GDB 调试 (加 -s -S, 另开终端)
gdb-multiarch vmlinux -ex "target remote :1234" -ex "b pick_task_mfq" -ex "c"
```

#### 当前完整修改清单 (8个文件)

| 文件 | 改动 | 说明 |
|------|------|------|
| `kernel/sched/sched_mfq.c` | NEW ~115行 | MFQ调度器 (pick_task模式) |
| `include/uapi/linux/sched.h` | +1行 | `#define SCHED_MFQ 4` |
| `include/linux/sched.h` | +14行 | struct sched_mfq_entity + task_struct.mfq_se |
| `kernel/sched/sched.h` | +20行 | struct mfq_rq + DECLARE_PER_CPU + mfq_policy() + valid_policy()更新 |
| `kernel/sched/core.c` | +18行 | __setscheduler_class路由 + sched_init初始化 |
| `kernel/sched/syscalls.c` | +2行 | policy=4 验证 (case 4:) |
| `include/asm-generic/vmlinux.lds.h` | +1行 | linker section ordering (mfq在fair和rt之间) |
| `kernel/sched/Makefile` | +1行 | `obj-$(CONFIG_SCHED_MFQ) += sched_mfq.o` |
| `init/Kconfig` | +11行 | CONFIG_SCHED_MFQ 配置选项 |
| `~/os/exp11/mfq_test.c` | NEW | 验证测试 (需用raw syscall绕过glibc) |

#### 当前安全设计

| 项目 | 设计 |
|------|------|
| 策略隔离 | MFQ 只服务 `policy=4` (SCHED_MFQ)。所有内核线程/普通进程走CFS |
| 调度类模式 | **pick_task** (非 pick_next_task)，核心代劳 put_prev/set_next — 同 RT 调度器 |
| 队列 | 8级: L0(10tick)→L1(20)→L2(40)→...→L7(1280tick) |
| 时间片公式 | `10 << level` |
| 升降级 | 新→L0, 时间片耗尽+nr>0→降级, 唯一任务→不降级, I/O唤醒→升级+抢占 |
| 调度类链 | stop → dl → rt → **mfq** → fair → ext → idle |
| put_prev | 仅 lock/unlock (list操作有已知问题待修复) |
| 初始化防御 | enqueue_task 中 `run_list.next==NULL` → 自动 mfq_entity_init |

1. **VMware 虚拟机环境**：
   - 磁盘控制器：VMware SATA AHCI (`CONFIG_SATA_AHCI=m` — **模块！必须在initrd中**)
   - 还有 LSI 53c1030 SCSI (`mptspi/mptscsih/mptbase` 模块)
   - Root 在 LVM 上：`/dev/sda3` → `ubuntu-vg/ubuntu-lv`
   - `/boot` 单独 ext4 `/dev/sda2` (2G)

2. **`CONFIG_MODVERSIONS=y` + 两个内核同版本号的坑**：
   - task_struct 加一个字段 → **所有**模块 CRC 全变
   - 两个内核版本号都是 `6.18.15`，共享 `/lib/modules/6.18.15/`
   - `make modules_install` 会覆盖旧模块
   - **旧内核 ↕ 旧initrd**，**新内核 ↕ 新initrd**，不可互换
   - 旧模块备份在 `/lib/modules/6.18.15.bak/`

3. **initrd 构建铁律**：
   - `MODULES=most` 靠谱（1465模块），`MODULES=dep` 踩坑（0模块）
   - 构建后必须：`lsinitramfs | grep '\.ko$' | wc -l` > 0
   - 必须确认 ahci.ko + lvm 工具链在 initrd 内

4. **本次 initrd 验证结果（18:14 构建）**：
   - 1465 模块、553M、zstd 压缩
   - ahci.ko + libahci.ko ✅
   - mptspi.ko + mptscsih.ko + mptbase.ko ✅
   - lvm + vgchange + dmsetup ✅
   - modules.dep / .alias / .symbols ✅
   - vermagic 与 MFQ bzImage #29 一致 ✅
   - Build tree: `/lib/modules/6.18.15/build` → `/usr/src/linux-6.18.15`（同一源码树）

5. **磁盘布局（扩后）**：
   ```
   /dev/sda (80G)
   ├── sda1  1M   BIOS boot
   ├── sda2  2G   /boot (ext4, 174M free)
   └── sda3  78G  LVM PV → ubuntu-vg/ubuntu-lv → / (ext4, 26G free)
   ```

#### 当前安全设计

| 项目 | 设计 |
|------|------|
| 策略隔离 | MFQ 只服务 `policy=4` (SCHED_MFQ) 的任务，**不替换CFS**。所有内核线程/用户进程默认走CFS不变 |
| 队列 | 8级: L0(10tick)→L1(20)→L2(40)→L3(80)→L4(160)→L5(320)→L6(640)→L7(1280) |
| 时间片公式 | `10 << level` |
| 升降级 | 新→L0, 时间片耗尽+nr>0→降级, 唯一任务→不降级, I/O唤醒→升级+可能抢占 |
| 调度类链 | stop → dl → rt → **mfq** → fair → ext → idle (linker section ordering) |
| 防御 | `BUG_ON(run_list.next==NULL)` 在 enqueue 前, `nr_running>0` 才 resched, 所有必须的 no-op 回调已实现 |

#### sched_class 回调清单 (Linux 6.18 用 DEFINE_SCHED_CLASS)

| 回调 | 实现 | 说明 |
|------|------|------|
| enqueue_task | ✅ | FIFO入队, nr_running++ |
| dequeue_task | ✅ | 安全出队 |
| wakeup_preempt | ✅ | I/O升级+抢占检查 |
| pick_next_task | ✅ | 扫描L0→L7, 选首进程 |
| pick_task | ✅ | 简化版(core scheduling) |
| put_prev_task | ✅ | 回队+nr_running++, 防御降级 |
| set_next_task | ✅ | 出队, nr_running--, 设为curr |
| task_tick | ✅ | 时间片递减+降级/刷新 |
| task_fork | ✅ | 初始化mfq_se |
| task_dead | ✅ | 日志记录 |
| switched_from | ✅ | 清理队列 |
| switched_to | ✅ | **初始化mfq_se** (sched_setscheduler切换时关键) |
| update_curr | ✅ | no-op (core.c无条件调用) |
| set_cpus_allowed | ✅ | no-op (core.c无条件调用) |
| prio_changed | ✅ | no-op (core.c无条件调用) |
| select_task_rq | ✅ | 简单CPU选择 |
| migrate_task_rq | ✅ | no-op |

#### 修改的文件清单

| 文件 | 改动 | 说明 |
|------|------|------|
| `kernel/sched/sched_mfq.c` | NEW 268行 | MFQ调度器实现 |
| `include/linux/sched.h` | +14行 | struct定义 + task_struct字段 |
| `kernel/sched/sched.h` | +12行 | struct mfq_rq + DECLARE_PER_CPU + extern |
| `kernel/sched/core.c` | +18行 | __setscheduler_class路由 + sched_init初始化 |
| `kernel/sched/syscalls.c` | +2行 | policy=4验证 |
| `include/asm-generic/vmlinux.lds.h` | +1行 | linker order |
| `kernel/sched/Makefile` | +1行 | 编译选项 |
| `init/Kconfig` | +11行 | 配置选项 |
| `~/os/exp11/mfq_test.c` | NEW | 验证测试程序 |

#### 验证方法（重启后）

```bash
sudo ~/os/exp11/mfq_test      # 运行MFQ测试程序
dmesg | grep '\[MFQ\]'         # 查看调度日志
# 预期看到: FORK→L0, RUN, DEMOTE (L0→L1→...→L7)
```

---

## QEMU 调试（实验6+）

```bash
# 启动 QEMU
sudo qemu-system-x86_64 -kernel /usr/src/linux-6.18.15/arch/x86/boot/bzImage \
  -initrd /tmp/initramfs.img -append "console=ttyS0 nokaslr" -nographic -m 512

# GDB 调试（加 -S -s）
gdb-multiarch /usr/src/linux-6.18.15/vmlinux
(gdb) target remote :1234
```

- `vmlinux` (~407M) 带 DWARF5 调试符号
- `bzImage` (~14M) 是压缩镜像
- initramfs: `/tmp/initramfs/` → `/tmp/initramfs.img`

## 编译安装

```bash
cd /usr/src/linux-6.18.15
sudo make -j$(nproc) bzImage         # 只编内核（跳GPU模块）
sudo cp arch/x86/boot/bzImage /boot/vmlinuz-6.18.15
sudo update-grub
sudo reboot
```

- GPU 模块（amdgpu/nouveau）有残留损坏 .o 文件，用 `make bzImage` 跳过
- initramfs 磁盘空间不足时复用旧的 `/boot/initrd.img-6.18.15`

### 编译模块 + 重建 initrd（改 task_struct 等 ABI 敏感结构时）

```bash
cd /usr/src/linux-6.18.15
# 1. 编译全部模块
sudo make -j$(nproc) modules
# 2. 备份旧模块
sudo cp -a /lib/modules/6.18.15 /lib/modules/6.18.15.bak
# 3. 安装新模块（会覆盖 /lib/modules/6.18.15/）
sudo make modules_install
sudo depmod -a 6.18.15
# 4. 构建 initrd（在根分区构建以避 /boot 空间不足）
sudo rm -rf /tmp/initrd-build && sudo mkdir -p /tmp/initrd-build
sudo update-initramfs -c -k 6.18.15 -b /tmp/initrd-build
# 5. 验证模块数 > 0！
lsinitramfs /tmp/initrd-build/initrd.img-6.18.15 | grep '\.ko$' | wc -l
# 6. 复制到 /boot
sudo cp /tmp/initrd-build/initrd.img-6.18.15 /boot/initrd.img-6.18.15-mfq
sudo update-grub
```

**关键教训**:
- **改了 task_struct 必须重编模块+重建initrd**，`CONFIG_MODVERSIONS=y` 导致旧模块全部拒载
- 构建后必须验证 `lsinitramfs | grep ko | wc -l` > 0
- `/boot` 只有 2G，两个 initrd 各 ~1G 放不下 → 需要扩分区或只保留一个 initrd
- 旧内核的模块已备份到 `/lib/modules/6.18.15.bak/`，需要时可恢复

## 注意事项

- AMD CPU：Intel 专属特性关（SGX、Intel MCE 等），AMD 专属保留
- Linux 6.x syscall 添加只需改 `.tbl` 文件
- glibc semctl() 拒绝自定义 cmd → 必须用 `syscall(__NR_semctl, ...)`

---

## Linux 6.18.15 内核源码深度阅读与分析报告 — ✅ 已完成 (2025-06-14)

| 项目 | 说明 |
|------|------|
| **主文件** | `report-reading/linux_kernel_reading_report.tex` (1544行, 104KB) |
| **PDF** | `report-reading/linux_kernel_reading_report.pdf`（已编译） |
| **图片** | `report-reading/images/`（12张 mermaid PNG + logo） |
| **编译** | `xelatex linux_kernel_reading_report.tex`（本地，需 texlive） |

### 6章结构

| 项目 | 说明 |
|------|------|
| **状态** | ✅ LaTeX 源码已完成，待用户编译 |
| **主文件** | `report-reading/linux_kernel_reading_report.tex` |
| **图片目录** | `report-reading/images/`（12 张 Mermaid PDF + 4 张 PPTX PNG + logo） |
| **编译命令** | `xelatex linux_kernel_reading_report.tex`（需在 Overleaf 或本地 XeLaTeX 环境） |

### 6 章结构

| # | 章节 | 对应实验 | 核心源码 |
|---|------|----------|----------|
| 1 | 进程组织结构和系统调用 | exp2, exp3 | kernel/fork.c, kernel/sys.c, arch/x86/entry/ |
| 2 | **进程调度和 x86 切换（重点，35\%）** | exp11 | kernel/sched/core.c, fair.c, arch/x86/entry/entry_64.S |
| 3 | 进程间通信机制 | exp4, exp8 | ipc/sem.c, ipc/shm.c, kernel/signal.c |
| 4 | 物理内存管理和分配 | exp10 | mm/page_alloc.c, include/linux/mmzone.h |
| 5 | 虚拟存储管理和换页 | exp5, exp10 | mm/memory.c, mm/mmap.c, arch/x86/mm/fault.c |
| 6 | VFS 和 ext2 文件系统 | exp7, exp9 | fs/ext2/, include/linux/fs.h |

### 图表清单（Mermaid 生成）

- `fig_ch1_process_states.pdf` — 进程状态机
- `fig_ch2_schedule_call_chain.pdf` — schedule() 调用链
- `fig_ch2_task_struct.pdf` — 结构体关系图
- `fig_ch2_sched_classes.pdf` — 调度类层次
- `fig_ch2_switch_to_asm.pdf` — 寄存器级切换时序
- `fig_ch3_ipc_overview.pdf` — IPC 机制总览
- `fig_ch3_signal_flow.pdf` — 信号发送/处理流程
- `fig_ch4_page_alloc.pdf` — 伙伴系统分配/释放
- `fig_ch5_page_fault.pdf` — 缺页异常决策树
- `fig_ch5_vma_mmap.pdf` — mmap/VMA 惰性分配
- `fig_ch6_vfs_architecture.pdf` — VFS 对象模型
- `fig_ch6_ext2_structure.pdf` — ext2 磁盘布局
