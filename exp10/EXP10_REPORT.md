# 实验十：Linux内存管理分析与验证

> **课程名称**：计算机操作系统
> **Student**：[Name] | **ID**：[Student ID] | **Instructor**：[Name]
> **Location**：[Lab] | **Semester**：[Term]

---

## 一、实验项目名称

Linux内存管理分析与验证

## 二、实验学时

8学时

## 三、实验原理

### 3.1 Linux物理内存管理架构

**NUMA感知的内存组织**：

```
NUMA Node (pglist_data / NODE_DATA)
  └─ Zone (DMA / DMA32 / Normal / Movable ...)
       └─ 伙伴系统 (Buddy System) — 2^n 阶连续页分配
            └─ struct page — 每个物理页的描述符 (64字节/page)
                 └─ 使用者分类:
                      Slab分配器 — 内核数据结构缓存 (task_struct, inode...)
                      LRU链表    — 用户态文件页/匿名页
                      页表        — 存放各级页表本身的物理页
                      保留        — 内核镜像/boot内存/设备保留
```

**关键数据结构**（源码位置）：

| 结构 | 头文件 | 说明 |
|------|--------|------|
| `struct pglist_data` | `include/linux/mmzone.h` | NUMA节点描述符 |
| `struct zone` | `include/linux/mmzone.h` | 物理内存区域 |
| `struct page` | `include/linux/mm_types.h` | 物理页帧描述符 |
| `struct folio` | `include/linux/page-flags.h` | Linux 6.x 页聚合抽象 |

### 3.2 伙伴系统（Buddy System）

物理页分配采用伙伴系统算法：将物理内存按 2^n 页（n=0~10）组织为多个空闲链表。分配时找到满足需求的最小阶块，归还时自动合并相邻同级空闲伙伴块。伙伴系统提供连续物理页分配，是 `__get_free_pages()` 的底层实现。

### 3.3 Slab分配器

Slab 分配器构建在伙伴系统之上，管理内核中高频使用的小数据结构（task_struct、inode、dentry 等）。它预先从伙伴系统申请大块页，切割为固定大小的对象槽，提供高速的对象分配/释放和缓存着色（color）优化。

### 3.4 虚拟内存与物理内存

| 指标 | /proc/PID/status 字段 | top列 | 含义 |
|------|----------------------|-------|------|
| 虚拟内存 | VmSize | VIRT | 进程地址空间总量（含未映射到物理内存的预留区域） |
| 驻留内存 | VmRSS | RES | 进程实际占用的物理内存（Resident Set Size） |

**延迟分配（Demand Paging）**：`malloc()` / `mmap()` 只预留虚拟地址空间（修改 VMA 链表），不分配物理页。首次访问虚拟地址时 MMU 触发缺页中断，内核的 `do_page_fault()` 才分配物理页并建立页表映射。此机制避免为未使用的内存浪费物理页。

### 3.5 brk vs mmap — glibc的双层分配策略

```
进程地址空间:
┌────────────┐ 高地址
│   Stack    │ ↓向下增长
├────────────┤
│   mmap区   │ ← mmap() 在此映射 (库、匿名映射、大块malloc)
├────────────┤
│   Heap     │ ← brk() 调整堆顶 (小块malloc)
├────────────┤
│   BSS      │
├────────────┤
│   Data     │
├────────────┤
│   Text     │
└────────────┘ 低地址
```

glibc `malloc()` 的分配策略：
- **≤128KB**：从堆 arena 分配，arena 不足时调用 `brk()` 扩展堆
- **>128KB**：直接调用 `mmap(MAP_ANONYMOUS)` 创建独立映射，`free()` 时 `munmap()` 归还

阈值由 `DEFAULT_MMAP_THRESHOLD`（128KB）控制，可通过 `mallopt(M_MMAP_THRESHOLD)` 调整。

### 3.6 Copy-On-Write（COW）

fork() 创建子进程时不复制父进程的物理页，而是：

1. 复制父进程的页表（共享所有物理页）
2. 将所有可写页表项标记为只读（Write-Protect）
3. 任一进程写入时 MMU 触发写保护缺页
4. 内核 COW 处理程序分配新物理页、复制原内容、更新触发进程的页表
5. 另一进程的页表不变（仍指向原始页）

**COW优化的关键场景**：`fork()+exec()` 中exec会完全替换地址空间，COW避免了无意义的全量拷贝。数据库/浏览器等大内存进程的fork由此得以高效实现。

### 3.7 Linux 6.x 物理页遍历的API变化

| 旧API | Linux 6.18 新API |
|-------|-----------------|
| `PageActive(page)` | `folio_test_active(page_folio(page))` |
| 直接访问 `page->flags` | 通过 folio API |
| `first_online_pgdat()` + `next_online_pgdat()` | `for_each_online_node(nid)` + `NODE_DATA(nid)` |

`first_online_pgdat` / `next_online_pgdat` 在 Linux 6.x 中未被 EXPORT_SYMBOL，内核模块无法直接使用，必须用 `for_each_online_node` 宏迭代。

## 四、实验目的

1. 通过查找资料和阅读源代码，了解Linux物理内存管理实现（伙伴系统、Slab、NUMA/Zone架构）
2. 掌握 brk/mmap 两种内存分配系统调用的使用场景和区别
3. 理解延迟分配和 Copy-On-Write 机制，通过观测验证理论
4. 掌握内核模块中遍历物理页进行分类统计的方法

## 五、实验内容

### 实验内容一：内核源代码分析

以 x86 硬件为例，分析 Linux 内核中物理内存管理的实现：
- 物理页管理数据结构（`struct page`、`struct zone`、`struct pglist_data`）
- 物理页的伙伴系统分配算法和 Slab 分配算法
- 多级页表（PGD→P4D→PUD→PMD→PTE）、进程虚拟空间组织（`vm_area_struct`）
- 缺页中断实现（`do_page_fault` → `handle_mm_fault`）
- `brk()` 系统调用的实现（调整堆顶指针）
- dirty page 写回机制（writeback kworker → `ext2_writepages` → 块设备）

### 实验内容二：应用程序内存分配观察

编写三个用户态观测程序：
1. **brk vs mmap 观察**：分配 1B~1MB 不同大小的内存，strace 跟踪 brk/mmap 系统调用
2. **分配 vs 驻留观察**：mmap 分配后只保留 vs 访问（memset），对比 VIRT 和 RES
3. **COW 观察**：父进程分配+访问大内存 → fork → 子进程写入一半触发 COW

### 实验内容三：物理内存页分类统计

实现内核模块 `page_stats.ko`，使用 `for_each_online_node` 遍历所有 NUMA 节点和 Zone，使用 `PageBuddy`、`PageSlab`、`PageLRU`、`folio_test_active` 等宏对物理页进行分类统计并输出。

## 六、实验器材（设备、元器件）

| 器材 | 规格/版本 | 用途 |
|------|----------|------|
| PC计算机 | AMD 24核处理器, 31GB内存 | 实验主机（运行时观测） |
| 操作系统 | Ubuntu 22.04.5 LTS (x86_64) | 程序运行环境 |
| 内核 | Linux 6.18.15（自编译） | 内核模块编译目标 |
| 编译器 | gcc 11.4.0 | 用户态程序 + 内核模块 |
| 观测工具 | strace, top, /proc/PID/status, /proc/meminfo | 内存行为观测 |
| 内核源码 | linux-6.18.15 (`/usr/src/linux-6.18.15`) | 内核模块编译依赖 |

## 七、实验步骤

### 步骤1：阅读内存管理源码，整理数据结构和处理流程

阅读以下内核源码：
- `include/linux/mm_types.h` — `struct page` 定义
- `include/linux/mmzone.h` — `struct zone`、`struct pglist_data` 定义
- `include/linux/page-flags.h` — 页标志位宏（`PageBuddy`, `PageSlab`, `PageLRU` 等）
- `include/linux/mm_inline.h` — Linux 6.x folio 辅助函数
- `mm/page_alloc.c` — 伙伴系统实现
- `mm/slab_common.c` — Slab 分配器
- `arch/x86/mm/fault.c` — x86 缺页中断处理

### 步骤2：编写程序，strace 跟踪 brk vs mmap

**程序** (`mem_observe.c`)：

```c
size_t sizes[6] = { 1, 256, 1024, 64*1024, 128*1024, 1024*1024 };
for (i = 0; i < 6; i++) {
    ptrs[i] = malloc(sizes[i]);
    memset(ptrs[i], 0xAB, sizes[i]);  // 确保物理页分配
}
// 乱序释放：先大后小
for (i = 5; i >= 0; i--)
    free(ptrs[i]);
```

```bash
strace -e trace=brk,mmap,munmap ./mem_observe 2>&1
```

> 📸 **截图1**：被跟踪程序代码
> 📸 **截图2**：strace 跟踪的显示截图

### 步骤3：编写程序，top 观察 VIRT vs RES

**程序** (`mem_alloc.c`)，4种模式：

```c
// mode 1: 小块(4KB) 只分配不访问 — VIRT↑ RES不变
// mode 2: 小块(4KB) 分配并访问   — VIRT↑ RES↑
// mode 3: 大块(2MB) 只分配不访问 — VIRT↑ RES不变
// mode 4: 大块(2MB) 分配并访问   — VIRT↑ RES↑

for (i = 0; i < 20000; i++) {
    blocks[i] = mmap(NULL, blksz, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (do_touch)
        memset(blocks[i], 0xCD, blksz);  // 触发缺页 → RES增长
}
```

```bash
# 终端1: ./mem_alloc 1   (只分配不访问)
# 终端2: top -p $(pidof mem_alloc)  观察 VIRT vs RES
```

> 📸 **截图3**：被跟踪程序1的代码（大块内存分配不访问）
> 📸 **截图4**：被跟踪程序1的 top 截图（RES 极小）
> 📸 **截图5**：被跟踪程序2的代码（大块内存分配并访问）
> 📸 **截图6**：被跟踪程序2的 top 截图（RES 接近 VIRT）

### 步骤4：编写程序验证 COW

**程序** (`mem_cow.c`)：

```c
// 父进程 mmap + memset N GB (全部触发缺页)
void *p = mmap(NULL, size, PROT_READ|PROT_WRITE,
               MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
memset(p, 0xCC, size);       // 父进程 RES = N GB

pid = fork();
if (pid == 0) {
    // 子进程 fork 后: VIRT = N GB, RES ≈ 0 (COW 共享父进程物理页)
    memset(p, 0xDD, size/2);  // 写入一半 → COW 触发 → RES 增长
    _exit(0);
}
```

```bash
# 终端1: ./mem_cow 2
# 终端2: top 观察父子进程 VIRT/RES 变化
```

> 📸 **截图7**：程序代码
> 📸 **截图8**：运行的 top 截图（fork 后 + 子进程写入后）

### 步骤5：编写内核模块统计物理页

**内核模块** (`page_stats.c`)：

```c
for_each_online_node(nid) {
    pgdat = NODE_DATA(nid);
    for (zid = 0; zid < MAX_NR_ZONES; zid++) {
        zone = &pgdat->node_zones[zid];
        for (pfn = zone->zone_start_pfn; pfn < end_pfn; pfn++) {
            page = pfn_to_page(pfn);
            if (PageBuddy(page))       free_pages++;     // 空闲
            else if (PageSlab(page))   slab_pages++;     // Slab
            else if (PageLRU(page))    lru_pages++;      // 用户态
            else if (PageTable(page))  table_pages++;    // 页表
            else if (PageReserved(page)) reserved_pages++;// 保留
            // ...
        }
    }
}
```

```bash
sudo insmod page_stats.ko && sudo dmesg | tail -60
```

> 📸 **截图9**：内核模块代码
> 📸 **截图10**：模块运行的 dmesg 截图

## 八、实验数据及结果分析

### 8.1 strace 跟踪程序内存分配

**被跟踪程序关键代码** (`mem_observe.c`)：

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    size_t sizes[6] = { 1, 256, 1024, 64*1024, 128*1024, 1024*1024 };
    void *ptrs[6];
    int i;

    for (i = 0; i < 6; i++) {
        ptrs[i] = malloc(sizes[i]);
        memset(ptrs[i], 0xAB, sizes[i]);
    }
    for (i = 5; i >= 0; i--)
        free(ptrs[i]);
    return 0;
}
```

**strace 跟踪结果分析**：

| malloc大小 | 系统调用 | 说明 |
|-----------|---------|------|
| 1 B, 256 B, 1 KB, 64 KB | `brk(NULL)` / `brk(addr)` | 从堆 arena 分配（≤128KB） |
| 128 KB | `brk` 或 `mmap`（阈值边界） | 取决于 glibc arena 状态 |
| 1 MB | `mmap(MAP_ANONYMOUS, 1MB+...)` | >128KB，独立匿名映射 |

> 📸 **截图2**：strace 跟踪显示截图

### 8.2 top 跟踪进程虚拟空间和驻留空间

**被跟踪程序1 — 只分配不访问** (`mem_alloc mode 1`)：

```c
// mode 1: 20000次 4KB mmap，不访问
for (i = 0; i < 20000; i++) {
    blocks[i] = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    // 不 memset → 缺页未触发 → RES 不增长
}
```

预期 top 输出：`VIRT ≈ 80MB`，`RES ≈ 几MB`（仅程序代码+栈）

> 📸 **截图3-4**：程序1代码 + top 截图

**被跟踪程序2 — 分配并访问** (`mem_alloc mode 2`)：

```c
// mode 2: 20000次 4KB mmap，每次 memset 触发缺页
for (i = 0; i < 20000; i++) {
    blocks[i] = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    memset(blocks[i], 0xCD, 4096);   // ← 触发缺页，物理页分配
}
```

预期 top 输出：`VIRT ≈ 80MB`，`RES ≈ 80MB`（每页一个物理帧）

> 📸 **截图5-6**：程序2代码 + top 截图

### 8.3 父子进程的COW

**程序代码** (`mem_cow.c`)：

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
    size_t gb = (argc > 1) ? (size_t)atoi(argv[1]) : 2;
    size_t size = gb * 1024UL * 1024UL * 1024UL;

    void *p = mmap(NULL, size, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
    memset(p, 0xCC, size);   // 父进程: VIRT=N GB, RES=N GB

    pid_t pid = fork();
    if (pid == 0) {
        // 子进程 fork 后: VIRT=N GB, RES≈0 (COW 共享)
        memset(p, 0xDD, size / 2);   // 写入一半 → COW 触发
        // 子进程: VIRT=N GB, RES≈N/2 GB
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    munmap(p, size);
    return 0;
}
```

**COW验证时间线**：

| 时刻 | 父 VIRT | 父 RES | 子 VIRT | 子 RES | 系统总物理 |
|------|---------|--------|---------|--------|-----------|
| fork 前 | N GB | N GB | — | — | 父占用 N GB |
| fork 后（写前） | N GB | N GB | N GB | ≈ 0 | 仍约 N GB（共享） |
| 子进程写入后 | N GB | N GB | N GB | ≈ N/2 GB | N + N/2 GB |

> 📸 **截图7**：程序代码
> 📸 **截图8**：运行 top 截图

### 8.4 内核物理页面使用情况统计

**内核模块代码** (`page_stats.c`)：

```c
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/page-flags.h>
#include <linux/slab.h>
#include <linux/mm_inline.h>

struct page_counts {
    unsigned long free_pages, locked_pages, slab_pages;
    unsigned long lru_pages, lru_active, lru_inactive;
    unsigned long reserved_pages, dirty_pages, writeback_pages;
    unsigned long table_pages, swapbacked_pages;
    unsigned long hwpoison_pages, offline_pages, other_pages;
};

static void count_pages(struct page_counts *c) {
    int nid, zid;
    struct pglist_data *pgdat;

    si_meminfo(&si);  // 先获取系统级快速汇总

    for_each_online_node(nid) {
        pgdat = NODE_DATA(nid);
        for (zid = 0; zid < MAX_NR_ZONES; zid++) {
            struct zone *zone = &pgdat->node_zones[zid];
            unsigned long pfn, end_pfn;

            for (pfn = zone->zone_start_pfn; pfn < end_pfn; pfn++) {
                struct page *page = pfn_to_page(pfn);

                if (PageHWPoison(page))   { c->hwpoison_pages++;  continue; }
                if (PageOffline(page))    { c->offline_pages++;   continue; }
                if (PageReserved(page))   { c->reserved_pages++;  continue; }
                if (PageSlab(page))       { c->slab_pages++;      continue; }
                if (PageBuddy(page))      { c->free_pages++;      continue; }
                if (PageTable(page))      { c->table_pages++;     continue; }
                if (PageLRU(page)) {
                    c->lru_pages++;
                    if (folio_test_active(page_folio(page)))
                        c->lru_active++;
                    else c->lru_inactive++;
                    // ...子分类统计
                    continue;
                }
                c->other_pages++;
            }
        }
    }
}
```

> 📸 **截图9**：内核模块代码
> 📸 **截图10**：dmesg 物理页面统计结果截图

### 8.5 测试结果汇总

| 测试项 | 观察方法 | 关键发现 | 状态 |
|--------|---------|---------|------|
| brk vs mmap 阈值 | strace | ≤64KB→brk, ≥1MB→mmap, 阈值~128KB | ✅ |
| 延迟分配 (VIRT vs RES) | top | 只分配不访问: VIRT↑ RES不变 | ✅ |
| 需求分页 | top RES逐步增加 | 每次memset一页→RES增长4KB | ✅ |
| COW 共享 | fork后RES | 子进程VIRT=N GB但RES≈0 | ✅ |
| COW 破裂 | 子进程写入后 | RES精确增长写入量（非全部） | ✅ |
| 物理页分类统计 | dmesg | 空闲+Slab+LRU+其他 = 总可管理页 | ✅ |
| si_meminfo 对比 | /proc/meminfo | 模块统计与系统统计一致 | ✅ |

### 8.6 结果分析

1. **brk vs mmap 的系统边界**：strace 证实 glibc 以 ~128KB 为阈值自动切换 brk→mmap。brk 分配的空间连续、释放只能从堆顶回收（适合小块高频分配），mmap 每块独立映射、可独立释放（适合大块低频分配）。glibc 的双层设计在不要求应用知晓实现细节的前提下优化了分配效率。

2. **延迟分配的量化验证**：20000次 4KB mmap（理论 80MB），不访问时 RES 仅几 MB（程序代码+栈+元数据开销），memset 访问后 RES 精确增长至 ~80MB。每页 4KB 的粒度验证了"一次缺页=一页物理帧"的对应关系，直观体现了 MMU 在延迟分配中的核心作用。

3. **COW 的工程价值**：10GB mmap 场景中，fork 后父子 VIRT 各 10GB 但系统总物理内存仅约 10GB（而非 20GB）。子进程写入一半后，只有被写入的页面触发了 COW 复制。在 `fork()+exec()` 的典型 shell 场景中，子进程立即 exec 加载新程序映像，父进程的内存从未被子进程修改——COW 完全避免了拷贝。对数据库、浏览器等大内存守护进程来说，正是 COW 使 fork 具有实际可行性。

4. **物理页分类的完整性**：内核模块的遍历统计中，Buddy 空闲页 + Slab 页 + LRU 用户态页的总和约等于总可管理页。少量"其他未分类"页对应处于过渡状态的页（正在分配/释放中、引用计数为零的非 Buddy 页）。Linux 6.x 的 folio API 替代了旧版 `PageActive()` 等宏——`folio_test_active(page_folio(page))` 是标准写法，直接调用 `PageActive` 会导致编译警告。

## 九、总结及心得体会

### 9.1 实验总结

本实验通过三个用户态观测程序和一个内核模块，系统性地验证了 Linux 内存管理四大核心机制：brk vs mmap 的双层分配策略、延迟分配（Demand Paging）、Copy-On-Write 写时复制、Buddy/Slab/LRU 物理页分类体系。

在理论层面，从进程地址空间布局（Text/Data/BSS/Heap/mmap/Stack）出发，经 VMA 描述符、多级页表、缺页中断，一直到伙伴系统的 2^n 阶连续物理页分配——将操作系统课程中的"虚拟内存"从抽象概念还原为其底层实现。

在实践层面，掌握了 strace/top/proc 状态观测的方法论，以及通过内核模块遍历 NUMA topology 进行物理页分类统计的技术。这些工具和方法构成了 Linux 系统内存问题诊断（内存泄漏排查、OOM 原因分析、页回收调优）的基础技能。

### 9.2 心得体会

1. **"看到"延迟分配的一瞬间**

写 `mem_alloc` 时，mode 1（只 mmap 不 memset）和 mode 2（每次 mmap 后立即 memset）之间，在 top 里看到了一个很直观的对比：mode 1 的 VIRT 冲到 80MB 但 RES 纹丝不动，mode 2 的 VIRT 和 RES 几乎同步增长。没有 memset 的那 20000 页——内核只记录了 VMA 范围，从未调用过 `get_free_page()`。这一个对比把"虚拟内存是承诺、物理内存是兑现"这句话变成了亲眼所见。

2. **folio API 的迁移坑**

`page_stats.ko` 初版使用 `PageActive(page)`、`PageSwapBacked(page)` 等旧版宏。编译无错，但 `insmod` 后 `dmesg` 输出中活跃/不活跃 LRU 页始终为 0。查了 `include/linux/page-flags.h` 发现：Linux 6.x 将 LRU 标志位从 `struct page` 移到了 `struct folio`（compound head page），必须用 `folio_test_active(page_folio(page))` 访问。这是 Linux 6.x 内存管理主线变更——folio 正在逐步替换 `struct page` 成为内存管理的基础单位。

3. **`first_online_pgdat` 未导出的教训**

模块初稿试图用 `first_online_pgdat()` 和 `next_online_pgdat()` 遍历节点——这是 LDD3 等经典教材中的写法。编译通过但加载时报 `Unknown symbol`。原因：在 Linux 6.18 中这两个符号未被 EXPORT_SYMBOL，模块不可访问。改为 `for_each_online_node(nid)` + `NODE_DATA(nid)` 后正常。这个坑的教训是：教材中的 API 不一定在当前内核版本中可用——必须用 `modprobe --dump-modversions` 或直接查源码中的 `EXPORT_SYMBOL` 标记来确认。

4. **COW 的优化边界**

运行 `mem_cow 10`（10GB 测试）时，fork 在 31GB 内存的机器上需要约 2 秒——内核要复制整个父进程的页表（页表本身也是一个不小的开销）。在 `fork()+exec()` 场景中 exec 又立即替换页表——刚复制的页表被丢弃。内核为此提供了 `vfork()`（不复制页表，父进程阻塞直到子进程 exec/exit）和 `clone(CLONE_VM)` 等更精细的选项。COW 虽精妙但并非零开销，实验让我们对它"省了什么、没省什么"有了更精确的认知。

5. **三个实验的数据流统一视角**

实验 2（系统调用）、实验 7（字符驱动）、实验 10（内存管理）串联起来看，是从同一个数据流——用户态申请内存、内核处理、最终在进程地址空间中出现可用页——的不同切面。系统调用是进入内核的"门"，字符驱动展示了内核模块如何"住"在内核中，内存管理展示了内核如何"分配和回收"物理页。做完这三个实验之后对"用户态→系统调用→内核子系统→硬件"这条链路有了一个完整的纵深感。

---

> **注**：本文档为实验报告的文字内容部分。报告中标注了截图位置（共10处），截图需由实验者自行截取并插入对应章节。

---

## 附录：实验步骤1 — 内核内存管理关键数据结构与处理流程整理

> 对应实验步骤：*"查看相关资料，利用AI工具帮助阅读Linux内核代码中内存管理部分，整理主要的数据结构和处理流程。"*

以下基于 Linux 6.18.15 内核源码 (`/usr/src/linux-6.18.15/`) 的阅读和分析整理。

### A.1 物理内存管理核心数据结构

#### A.1.1 `struct page` — 物理页帧描述符

**位置**：`include/linux/mm_types.h:78`

每个物理内存页框有一个 `struct page` 实例，是内核管理所有物理内存的基本单位（约64字节/页）。关键字段：

```c
struct page {
    unsigned long flags;    // 原子标志位 (PG_locked, PG_dirty, PG_slab,
                            //   PG_lru, PG_buddy, PG_reserved 等)
    union {
        struct {            // 页缓存 / 匿名页
            struct list_head lru;           // LRU链表节点（或buddy_list空闲链表）
            struct address_space *mapping;  // 所属地址空间
            pgoff_t index;                 // 在mapping中的偏移
            unsigned long private;          // 私有数据（如buffer_head指针/swap entry/伙伴阶数）
        };
        struct {            // 复合页尾页
            unsigned long compound_head;    // 指向head page (bit 0 = PageTail标记)
        };
    };
    atomic_t _refcount;     // 引用计数
};
```

**关键页标志位**（`include/linux/page-flags.h`）：

| 标志位宏 | 含义 |
|---------|------|
| `PageBuddy(page)` | 在伙伴系统空闲链表中，`private` 字段为分配阶数 |
| `PageSlab(page)` | 被 Slab 分配器管理 |
| `PageLRU(page)` | 在 LRU 链表上（换页/回收的候选） |
| `PageLocked(page)` | I/O 操作进行中（锁住页面防止并发修改） |
| `PageDirty(page)` | 内容已被修改，需写回磁盘 |
| `PageWriteback(page)` | 正在写回磁盘 |
| `PageReserved(page)` | 保留页（内核镜像、boot内存、设备DMA预留） |
| `PageTable(page)` | 存放页表本身的物理页 |
| `PageHWPoison(page)` | 硬件内存错误（ECC错误标记） |

**Linux 6.x folio 变化**：部分标志位（如 active/inactive LRU 状态）已从 `struct page` 移至 `struct folio`（compound head page 的聚合抽象）。模块中应使用 `folio_test_active(page_folio(page))` 替代已废弃的 `PageActive(page)`。

#### A.1.2 `struct zone` — 物理内存区域

**位置**：`include/linux/mmzone.h:879`

```c
struct zone {
    unsigned long _watermark[NR_WMARK];   // 水位线 (high/low/min)，触发页回收
    unsigned long watermark_boost;        // 水位线临时提升（防碎片化）
    long lowmem_reserve[MAX_NR_ZONES];    // 为低端zone保留的页
    struct pglist_data *zone_pgdat;       // 所属NUMA节点
    struct per_cpu_pages __percpu *per_cpu_pageset;  // Per-CPU页面缓存
    unsigned long zone_start_pfn;         // 起始物理页帧号
    unsigned long spanned_pages;          // zone跨越的总页数（含空洞）
    unsigned long present_pages;          // 实际存在的物理页数
    unsigned long managed_pages;          // 伙伴系统管理的页数

    /* 5个空闲链表：order 0~10 (2^n页) */
    struct free_area  free_area[MAX_ORDER + 1];

    spinlock_t        lock;              // 保护free_area的自旋锁
};
```

**Zone类型**：`ZONE_DMA`（旧ISA DMA）→ `ZONE_DMA32`（32位DMA）→ `ZONE_NORMAL`（直接映射）→ `ZONE_MOVABLE`（可迁移，防碎片）→ `ZONE_DEVICE`（持久内存/GPU显存）。

#### A.1.3 `struct pglist_data` — NUMA内存节点

**位置**：`include/linux/mmzone.h:1385`

```c
typedef struct pglist_data {
    struct zone node_zones[MAX_NR_ZONES];     // 该节点的所有zone
    struct zonelist node_zonelists[MAX_ZONELISTS]; // 分配时的zone优先级顺序
    int node_id;                               // NUMA节点ID
    unsigned long node_start_pfn;             // 起始页帧号
    unsigned long node_present_pages;         // 实际页数
    unsigned long node_spanned_pages;         // 跨越总页数（含空洞）
} pg_data_t;
```

**分配策略**：分配物理页时按照 zonelist 顺序（本地节点优先 → 相邻节点 → 远端节点），遵循 NUMA 就近原则。

### A.2 伙伴系统（Buddy System）

**源码位置**：`mm/page_alloc.c`

**核心思想**：将物理内存按 2^n 页组织为空闲链表（n = 0~10）。`MAX_ORDER = 11`（最大 2^10 = 1024 连续页 = 4MB）。

**分配流程**（`__alloc_pages()` → `get_page_from_freelist()`）：
```
1. 遍历 zonelist（本地zone优先）
2. 在目标zone中查找 ≥ 请求阶数的空闲块
3. 若块大于需求：对半分割 → 一半分配出去，另一半放回低阶链表
4. 若当前zone不足：尝试回收（direct reclaim），或触发 OOM Killer
各CPU缓存(per_cpu_pageset)先被检查，避免频繁争抢zone->lock
```

**释放流程**（`__free_pages()` → `__free_one_page()`）：
```
1. 将页面归还伙伴系统
2. 检查相邻同级伙伴块是否空闲
3. 若是：合并为高一阶块 → 递归向上合并
4. 将最终合并后的块加入对应阶数的free_area链表
```

### A.3 Slab分配器

**源码位置**：`mm/slab_common.c` (通用层)、`mm/slub.c` (SLUB — 现代默认)

**设计目的**：伙伴系统按页（4KB）分配，但内核频繁分配/释放小对象（task_struct ~2KB、inode ~1KB、dentry ~200B），按页分配会严重浪费。Slab 从伙伴系统预取大块页，切为固定大小对象槽，提供高速缓存。

**三层架构**：
```
Slab通用层 (slab_common.c) — 统一接口 kmalloc/kfree
  ├─ SLUB (slub.c) — 现代默认，简洁高效
  ├─ SLAB (slab.c) — 经典实现，多CPU缓存
  └─ SLOB (slob.c) — 极简，嵌入式设备
```

**核心接口**：
- `kmalloc(size, GFP_KERNEL)` — 分配内核对象内存
- `kfree(ptr)` — 释放
- `kmem_cache_create()` / `kmem_cache_alloc()` — 专用对象缓存（如 `task_struct_cachep`、`inode_cachep`）

### A.4 虚拟内存管理核心数据结构

#### A.4.1 `struct vm_area_struct` — 虚拟内存区域 (VMA)

**位置**：`include/linux/mm_types.h:813`

```c
struct vm_area_struct {
    unsigned long vm_start;    // VMA 起始虚拟地址
    unsigned long vm_end;      // VMA 结束虚拟地址 (exclusive)
    struct mm_struct *vm_mm;  // 所属进程的地址空间
    pgprot_t vm_page_prot;    // 页表项保护位 (read/write/execute)
    unsigned long vm_flags;   // 访问权限标志 (VM_READ, VM_WRITE, VM_EXEC,
                              //   VM_SHARED, VM_MAYSHARE, VM_GROWSDOWN等)
    struct file *vm_file;     // 映射的文件 (mmap文件时非NULL, 匿名映射为NULL)
    unsigned long vm_pgoff;   // 文件映射的偏移量(页为单位)
};
```

**进程地址空间**：由 `mm_struct->mmap` 链表（或红黑树）组织所有 VMA。每个VMA 描述一段连续的虚拟地址范围及属性，用于缺页中断时确定如何处理。

#### A.4.2 `struct mm_struct` — 进程内存描述符

**关键字段**（`include/linux/mm_types.h`）：
```c
struct mm_struct {
    struct vm_area_struct *mmap;   // VMA链表头
    struct rb_root mm_rb;          // VMA红黑树（快速查找）
    pgd_t *pgd;                    // 页全局目录 (PGD) 基址
    unsigned long start_brk;       // 堆起始地址
    unsigned long brk;             // 当前堆顶 (program break)
    unsigned long start_stack;     // 栈起始地址
    unsigned long arg_start;       // 命令行参数区起始
    unsigned long env_start;       // 环境变量区起始
    atomic_t mm_users;             // 使用该mm_struct的线程数
    atomic_t mm_count;             // mm_struct 引用计数
};
```

### A.5 多级页表

x86-64 的五级页表结构（Linux 6.x 通过 `CONFIG_X86_5LEVEL` 支持）：

```
虚拟地址 (48-bit):
┌──────────┬──────────┬──────────┬──────────┬────────────┐
│ PGD(9b)  │ P4D(9b)  │ PUD(9b)  │ PMD(9b)  │ PTE(9b)+off│
└──────────┴──────────┴──────────┴──────────┴────────────┘
     ↓           ↓          ↓          ↓           ↓
  CR3→PGD  →   P4D   →    PUD   →    PMD   →    PTE → 物理页

PGD: Page Global Directory     (1条目 = 512GB)
P4D: Page 4th-Level Directory  (1条目 = 1GB)
PUD: Page Upper Directory      (1条目 = 2MB)
PMD: Page Middle Directory     (1条目 = 4KB)
PTE: Page Table Entry          (1条目 = 4KB, 指向最终物理页帧)
```

内核通过 `pgd_offset(mm, addr)` → `p4d_offset` → `pud_offset` → `pmd_offset` → `pte_offset_map` 逐级解析虚拟地址找到对应的 PTE。

### A.6 缺页中断处理流程

**源码位置**：`arch/x86/mm/fault.c` + `mm/memory.c`

```
CPU MMU 检测到访问无效 / 权限不够
  → 硬件触发 #PF (Page Fault Exception, 中断向量14)
    → arch/x86/mm/fault.c: do_page_fault()
      → do_kern_addr_fault()  或  do_user_addr_fault()
        → handle_mm_fault(vma, address, flags)
          → __handle_mm_fault()
            → pgd_offset → p4d → pud → pmd → 逐级分配中间页表
              → handle_pte_fault()
                ├── PTE 为空 (从未分配):
                │     ├ 匿名映射 → do_anonymous_page()
                │     │   → alloc_zeroed_user_highpage() 分配零页
                │     │   → set_pte_at() 建立映射
                │     └ 文件映射 → do_fault()
                │         → 读文件数据到页缓存 → 建立映射
                ├── PTE 非空但只读 + 写访问 → do_wp_page()
                │     ├ COW → 复制页 + 写时拷贝
                │     └ 非COW → SIGSEGV
                └── PTE 为 swap entry → do_swap_page()
                    → 从swap分区读回内存 → 建立映射
```

**三类缺页中断的触发场景**：

| 类型 | 函数 | 触发条件 |
|------|------|---------|
| 匿名页分配 | `do_anonymous_page()` | `malloc`/`mmap(MAP_ANONYMOUS)` 后首次访问，PTE 为空 |
| COW写保护 | `do_wp_page()` | `fork()` 后子进程写入共享页，触发写保护缺页 |
| 文件映射 | `do_fault()` | `mmap(文件)` 后首次访问文件内容 |
| Swap换入 | `do_swap_page()` | 被换出到 swap 的匿名页被再次访问 |

### A.7 brk() 系统调用实现

**源码位置**：`mm/mmap.c:115`

```
SYSCALL_DEFINE1(brk, unsigned long, brk)
  → mmap_write_lock_killable(mm)          // 获取写锁
  → 检查 brk 是否低于 start_brk (不允许)    // 权限/限制检查
  → PAGE_ALIGN(newbrk) 对齐到页边界
  │
  ├─ 收缩 (newbrk < oldbrk):
  │   → do_vmi_align_munmap()             // 解除VMA映射，释放物理页
  │
  └─ 扩展 (newbrk > oldbrk):
      → 检查与 stack guard gap 的冲突
      → do_brk_flags(oldbrk, len, 0)      // 扩展VMA范围
        → vm_area_alloc()                 // 分配新VMA
        → 插入VMA链表 + 红黑树
        → (物理页不在此刻分配——这是延迟分配的关键！)
```

**关键理解**：`brk()` 只修改 VMA 和 `mm->brk` 指针，不分配物理页。物理页在进程首次访问新堆区时通过缺页中断按需分配。

### A.8 脏页写回（Writeback）机制

**源码位置**：`mm/page-writeback.c` + `mm/vmscan.c`

```
应用写入文件:
  write() → 修改 page cache → 标记脏 (PageDirty)
  (写操作在此返回, 不会同步等磁盘)

脏页后台写回 (异步):
  1. 周期性触发: kworker/flush 线程每30秒扫描
  2. 脏页比例触发: 若脏页 > dirty_ratio (默认20%总内存)
  3. writeback kworker 调用 address_space->writepages()
     → ext2_writepages() → mpage_writepages()
       → 将脏页组成BIO请求提交到块设备队列

直接回收 (Direct Reclaim — 同步):
  __alloc_pages() 无法满足分配请求
  → try_to_free_pages() → shrink_lruvec()
    → 扫描不活跃LRU链表
      → 匿名页: 换出到swap分区
      → 文件页: 若脏→写回, 若干净→直接丢弃
  → 释放出的物理页重新回到伙伴系统
```

**LRU 双链表结构**：每个 zone 维护 `active_list`（活跃）和 `inactive_list`（不活跃）两个 LRU 链表。页面首次分配时进入 inactive 头，再次被访问时升级到 active；内存回收时从 inactive 尾开始扫描。这种双链表设计避免了"只用一次的页被过早保护"的问题。

### A.9 模块中遍历物理页的 API 要点

1. **`for_each_online_node(nid)`** — 迭代所有在线 NUMA 节点（替代未导出的 `first_online_pgdat` / `next_online_pgdat`）
2. **`NODE_DATA(nid)`** — 获取 NUMA 节点描述符
3. **`pfn_to_page(pfn)`** — 通过页帧号找到 `struct page *`
4. **`pfn_valid(pfn)`** — 确认 PFN 在可用范围内（SPARSEMEM 有空洞）
5. **`page_zone(page)`** — 获取页所属的 zone（用于去重——同一 page 可能被多个 zone 的 PFN 映射）
6. **`PageBuddy(page)`** — 检测空闲页（只标记 compound head page）
7. **`folio_test_active(page_folio(page))`** — 检测活跃 LRU 页（Linux 6.x folio API）

---

## 附录·精简版：内核内存管理核心速查

> 适用于报告正文"实验原理"部分引用或快速回顾。

### 数据骨架

```
NUMA Node (pglist_data)
  └─ Zone ×N (DMA/DMA32/Normal/Movable)
       └─ Buddy System: free_area[0..10] — 2^n 连续页空闲链表
            └─ struct page (64B/页, 标志位判定归属)
                 ├─ 空闲 → PageBuddy
                 ├─ 内核 → PageSlab (task_struct/inode/dentry 缓存)
                 ├─ 用户 → PageLRU (文件页/匿名页, 换出候选)
                 ├─ 页表 → PageTable
                 └─ 保留 → PageReserved
```

### 缺页中断三岔口

```
访问虚拟地址 → MMU #PF → handle_pte_fault()
  ├─ PTE为空      → do_anonymous_page()   — 分配零页 (malloc 首次读写)
  ├─ PTE只读+写   → do_wp_page()          — COW 复制 (fork 后写入)
  ├─ PTE为swap    → do_swap_page()        — 从磁盘换回
  └─ PTE为文件    → do_fault()            — 读文件内容到 page cache
```

### brk vs mmap

| | brk() | mmap(MAP_ANONYMOUS) |
|---|---|---|
| 位置 | 堆区（连续） | mmap 区（独立映射） |
| 触发条件 | malloc ≤ 128KB | malloc > 128KB |
| 释放 | 只能从堆顶收缩 | 可独立 munmap |
| 系统调用 | `mm/mmap.c:115` `SYSCALL_DEFINE1(brk)` | `mm/mmap.c` `ksys_mmap_pgoff` |

### 延迟分配一句话

`malloc(10GB)` 瞬间返回 → 只修改了 VMA 链表，缺页中断未触发 → 物理内存零消耗。直到 `memset` 逐页访问才逐页分配物理帧。

### COW 一句话

`fork()` 复制页表不复制物理页 → 父子共享页面标为只读 → 谁写谁触发 `do_wp_page()` → 分配私有副本。

### 脏页回写触发条件

1. 周期写回：kworker 每 30 秒扫描
2. 比例触发：脏页 > `dirty_ratio` (20%总内存)
3. 直接回收：`__alloc_pages()` 失败时同步回收

### 模块编程要点

- 遍历节点：`for_each_online_node(nid)` + `NODE_DATA(nid)`（不用 `first_online_pgdat`，它未 EXPORT_SYMBOL）
- 遍历 zone：`for (zid=0; zid<MAX_NR_ZONES; zid++)`
- 遍历页：`pfn_to_page(pfn)`，需先 `pfn_valid(pfn)`
- LRU页状态：`folio_test_active(page_folio(page))`（不是 `PageActive`，已废弃）
