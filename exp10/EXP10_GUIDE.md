# 实验10：Linux内存管理分析与验证 — 操作指导

> 严格按指导书「实验十」内容编写 | 内核分析 + 模块 + 应用

---

## 实验概述

通过 3 个用户态程序和 1 个内核模块，观察验证 Linux 内存管理的核心机制：brk vs mmap、延迟分配（lazy allocation）、Copy-On-Write（COW）、物理页面分类。

- **学时**: 8
- **类型**: 内核分析 + 模块 + 应用
- **难度**: ⭐⭐⭐⭐

**核心知识点**：
- 虚拟内存 vs 物理内存
- 缺页中断（Page Fault）与延迟分配
- Copy-On-Write 写时复制
- brk() vs mmap() 内存分配
- 物理页面分类（空闲/锁定/Slab/文件缓存/匿名页等）

---

## 代码文件

| 文件 | 说明 |
|------|------|
| `exp10_brk_mmap.c` | 程序1：brk vs mmap 分配观察 |
| `exp10_alloc_resident.c` | 程序2：分配 vs 驻留观察 |
| `exp10_cow.c` | 程序3：COW 写时复制观察 |
| `exp10_page_stats.c` | 内核模块：物理页面统计 |

---

## 步骤

### 程序1：brk vs mmap 观察

#### 编译

```bash
gcc exp10_brk_mmap.c -o brk_mmap
```

#### 运行 strace 观察系统调用

```bash
strace ./brk_mmap 2>&1 | grep -E "brk|mmap|munmap"
```

观察：小内存 malloc(128KB) → 使用 `brk()` 扩展堆，大内存 malloc(2MB) → 使用 `mmap()`。

> 📸 **截图1**：程序1源码
> 📸 **截图2**：strace 输出（brk vs mmap）

---

### 程序2：分配 vs 驻留观察

#### 编译

```bash
gcc exp10_alloc_resident.c -o alloc_resident
```

#### 运行并观察

终端1：
```bash
./alloc_resident
# 按 Enter 依次进入两个阶段
```

终端2：
```bash
top -p $(pgrep alloc_resident)
# 观察 VIRT 列（虚拟内存）和 RES 列（驻留内存）
```

观察要点：
- 阶段1（malloc不访问）：VIRT ~1GB, RES ~几MB
- 阶段2（逐步访问）：RES 逐渐增长至 1GB（每步触发缺页中断分配物理页）

> 📸 **截图3**：程序2源码
> 📸 **截图4**：top 输出（阶段1: RES小 / 阶段2: RES增长）
> 📸 **截图5**：top 输出（RES接近VIRT）

---

### 程序3：COW 观察

#### 编译

```bash
gcc exp10_cow.c -o cow
```

#### 运行

```bash
./cow
```

终端2：
```bash
top -p <父进程PID>,<子进程PID>
```

观察要点：
- fork 后子进程 RES 很小（页面通过 COW 共享父进程）
- 子进程写入后 RES 增长（COW 触发页面复制）

> 📸 **截图6**：程序3源码
> 📸 **截图7**：top 输出（父子进程COW前后对比）

---

### 内核模块：物理页面统计

#### 复制源文件并编译

```bash
cp exp10_page_stats.c page_stats.c
# 添加到 exp7_Makefile 的 obj-m 列表，或直接:
make -C /lib/modules/$(uname -r)/build M=$(pwd) obj-m=page_stats.o modules
# 或将 page_stats.o 加入 Makefile
```

#### 加载模块

```bash
sudo insmod page_stats.ko
dmesg | tail -40
```

> 📸 **截图8**：dmesg 物理页面统计结果

#### 与 /proc/meminfo 对比

```bash
cat /proc/meminfo
```

对比 dmesg 输出和 /proc/meminfo 中的值。

---

## 截图清单（共8张）

| # | 内容 | 步骤 |
|---|------|------|
| 1 | 程序1源码 | 程序1 |
| 2 | strace 输出 (brk vs mmap) | 程序1 |
| 3 | 程序2源码 | 程序2 |
| 4 | top (分配不访问：RES小) | 程序2 |
| 5 | top (分配+访问：RES大) | 程序2 |
| 6 | 程序3源码 | 程序3 |
| 7 | top (COW前后父子RES对比) | 程序3 |
| 8 | dmesg 物理页面统计 | 内核模块 |

---

## 实验原理要点（供写报告参考）

### 延迟分配 (Lazy Allocation)
`malloc()` 只分配虚拟地址空间，不分配物理内存。首次访问时才通过**缺页中断**分配物理页。这样避免了"分配了但没用"的内存浪费。

### Copy-On-Write (COW)
`fork()` 后子进程共享父进程的物理页面，标记为只读。当任一进程写入时，触发缺页中断，内核复制该页面。在 `fork() + exec()` 场景下大大节省内存（exec 会替换地址空间，无需复制）。

### brk vs mmap
- `brk()`：调整堆顶指针（program break），小块内存，连续地址
- `mmap()`：在地址空间中映射新区域，大块内存，可独立释放
- glibc `malloc()` 自动选择：小请求用 brk，大请求用 mmap（阈值约 128KB）

---

## 本目录文件

| 文件 | 说明 |
|------|------|
| `exp10_brk_mmap.c` | 程序1：brk vs mmap 观察 |
| `exp10_alloc_resident.c` | 程序2：分配 vs 驻留观察 |
| `exp10_cow.c` | 程序3：COW 观察 |
| `exp10_page_stats.c` | 内核模块：物理页面统计 |
| `EXP10_GUIDE.md` | 本文件 |
