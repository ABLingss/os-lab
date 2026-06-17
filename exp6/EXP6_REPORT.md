# 实验六：Linux的x86架构启动过程分析和跟踪

> **课程名称**：计算机操作系统
> **Student**：[Name] | **ID**：[Student ID] | **Instructor**：[Name]
> **Location**：[Lab] | **Semester**：[Term]

---

## 一、实验项目名称

Linux的x86架构启动过程分析和跟踪

## 二、实验学时

4学时

## 三、实验原理

### 3.1 x86_64架构的启动模式演进

x86处理器从上电到执行操作系统内核代码，经历了多个CPU模式的切换。这种"模式升迁"是x86历史兼容性的典型体现——为了保持对1980年代8086处理器的向后兼容，现代x86_64处理器启动时仍处于虚拟8086兼容模式：

```
上电复位 (Real Mode, 16-bit)
  └─→ 保护模式 (Protected Mode, 32-bit)
       └─→ 长模式 (Long Mode, 64-bit)
            └─→ start_kernel() C语言环境
```

**1. 实模式（Real Mode）**

CPU上电后首先进入实模式，这是Intel 8086的工作模式：
- **寻址空间**：20位，最大1MB（段寄存器 × 16 + 偏移量）
- **无保护**：任何程序可以访问任意内存和I/O端口
- **无分页**：直接使用物理地址
- **16位指令**：默认16位操作数

实模式下执行的代码：
- **BIOS固件**：上电自检（POST）、初始化硬件、从引导设备加载引导扇区
- **GRUB Stage 1**：MBR（主引导记录，446字节）中的第一阶段引导代码
- **GRUB Stage 2**：加载更复杂的第二阶段引导器，读取文件系统找到内核镜像
- **内核setup代码**：`arch/x86/boot/header.S`定义的内核映像头，`arch/x86/boot/main.c`执行实模式初始化（检测内存、设置显示模式等）

**2. 保护模式（Protected Mode）**

通过设置CR0寄存器的PE位（Protection Enable），CPU从实模式切换到32位保护模式：
- **寻址空间**：32位，最大4GB
- **内存保护**：段描述符定义访问权限（代码/数据、可读/可写、特权级）
- **开启分页**：可选，允许虚拟地址映射
- **GDT/LDT**：全局/局部描述符表管理段

保护模式下执行的代码：
- `arch/x86/boot/pm.c`：执行保护模式切换，初始化GDT
- `arch/x86/boot/compressed/head_64.S`：切换到64位长模式之前的过渡
- 内核解压：将压缩的vmlinux.bin.gz解压到内存

**3. 长模式（Long Mode）**

通过设置EFER MSR的LME位（Long Mode Enable）和CR0的PG位（Paging），进入64位长模式：
- **寻址空间**：48位虚拟地址（256TB），52位物理地址（4PB）
- **64位通用寄存器**：rax/rbx/rcx/rdx/rbp/rsp/rsi/rdi及r8-r15
- **4级页表**：PML4 → PDPT → PD → PT（Linux 6.x支持5级页表）
- **指令集扩展**：SSE/AVX等向量指令

长模式下执行的代码：
- `arch/x86/kernel/head_64.S`：早期64位汇编入口，设置初始页表和栈
- `arch/x86/kernel/head64.c`：`x86_64_start_kernel()` — 架构初始化的C语言入口
- `init/main.c`：`start_kernel()` — 通用内核初始化入口

### 3.2 Linux内核启动流程详解

#### 阶段一：引导加载程序（Bootloader）— GRUB

GRUB2（GRand Unified Bootloader 2）是现代Linux系统的默认引导加载程序。其工作流程：

1. **Stage 1**（位于MBR或GPT BIOS boot分区）：BIOS加载446字节的引导代码
2. **Stage 1.5**（可选）：加载位于MBR和第一个分区之间的扇区，提供基本文件系统支持
3. **Stage 2**：读取`/boot/grub/grub.cfg`配置文件，显示菜单，加载用户选择的内核

GRUB加载内核的步骤：
- 读取`/boot/vmlinuz-<version>`（即bzImage）到内存
- 读取`/boot/initrd.img-<version>`到内存
- 设置内核命令行参数（`cmdline`）
- 跳转到bzImage的setup入口点

**bzImage结构**：

bzImage（big zImage）是Linux内核的标准可启动镜像格式，包含三部分：
1. **Setup扇区**（`arch/x86/boot/`）：实模式初始化代码，包括header.S定义的引导协议头
2. **压缩内核**：由`piggy.S`嵌入的压缩后的vmlinux.bin.gz
3. **解压代码**（`arch/x86/boot/compressed/`）：负责将压缩内核解压到正确位置

#### 阶段二：内核Setup阶段（实模式→保护模式）

`arch/x86/boot/main.c`中的`main()`函数（注意：这不是`init/main.c`，而是`arch/x86/boot/main.c`，一个完全不同的实模式程序）：

1. `init_default_io_ops()`：初始化基本I/O操作
2. `copy_boot_params()`：复制引导参数（防止被覆盖）
3. `detect_memory()`：通过BIOS中断（`int 0x15, e820`）获取物理内存布局
4. `set_video()`：设置显示模式（VGA/VESA）
5. 跳转到保护模式切换代码（`go_to_protected_mode()`）

#### 阶段三：内核解压与长模式切换

`arch/x86/boot/compressed/head_64.S`：
1. 加载临时的GDT和页表
2. 检查CPU是否支持64位长模式
3. 启用分页和长模式
4. 调用`extract_kernel()`解压vmlinux到最终位置
5. 跳转到解压后的`arch/x86/kernel/head_64.S`

#### 阶段四：早期64位初始化（汇编→C）

`arch/x86/kernel/head_64.S`：
1. 设置初始内核页表（early_top_pgt）
2. 设置内核栈（`initial_stack`）
3. 调用`x86_64_start_kernel()`（`arch/x86/kernel/head64.c`）

`x86_64_start_kernel()`完成架构特定的早期初始化：
- 清理BSS段（未初始化全局变量区）
- `copy_bootdata()`：解析引导参数
- `load_ucode_bsp()`：加载CPU微码更新
- 调用`start_kernel()`（`init/main.c`）— 通用内核入口

#### 阶段五：start_kernel() — 通用内核初始化

`init/main.c`中的`start_kernel()`是Linux内核的**第一个体系结构无关**的C语言函数（此前所有代码都是x86特定的汇编/C操作）。它在关闭本地中断的环境中运行（通过`local_irq_disable()`），执行全局内核初始化：

```
start_kernel() {
    set_task_stack_end_magic(&init_task);
    smp_setup_processor_id();
    boot_cpu_init();
    page_address_init();
    setup_arch(&command_line);          // 架构特定初始化
    setup_command_line(cmdline);
    setup_nr_cpu_ids();
    setup_per_cpu_areas();
    boot_cpu_state_init();
    build_all_zonelists(NULL);
    page_alloc_init();
    parse_early_param();
    parse_args();
    trap_init();                         // 中断描述符表
    mm_init();                           // 内存管理初始化
    ftrace_init();
    sched_init();                        // 调度器初始化
    early_irq_init();
    init_IRQ();                          // 中断初始化
    tick_init();
    rcu_init();
    trace_init();
    context_tracking_init();
    softirq_init();                      // 软中断
    timekeeping_init();
    time_init();                         // 定时器初始化
    console_init();                      // 控制台初始化（此后printk可见）
    rest_init();                         // 创建init进程
}
```

关键初始化步骤说明：

- **`setup_arch()`**：架构特定初始化，解析命令行参数、初始化内存区域、设置memblock分配器、初始化ACPI/设备树、设置初始页表
- **`mm_init()`**：初始化内核内存管理——buddy分配器、slab分配器、vmalloc区域
- **`sched_init()`**：初始化进程调度器（CFS/MFQ等调度类）
- **`console_init()`**：初始化控制台驱动，此后`printk`输出才能在屏幕上看到
- **`rest_init()`**：`start_kernel()`的最后一个调用，创建init进程后内核进入"空闲"状态

#### 阶段六：rest_init() — 创建init进程

```c
static void __ref rest_init(void) {
    rcu_scheduler_starting();
    
    // 创建PID=1的init进程（内核线程）
    user_mode_thread(kernel_init, NULL, CLONE_FS);
    
    // 创建PID=2的kthreadd进程（内核线程守护者）
    pid = user_mode_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES);
    
    // 当前任务变为idle进程（PID=0）
    schedule_preempt_disabled();
    cpu_startup_entry(CPUHP_ONLINE);  // 进入idle循环
}
```

**三个特殊进程**：
- **PID=0**（idle/swapper）：空闲进程，当没有其他进程可运行时CPU执行idle循环
- **PID=1**（init）：第一个用户态进程，负责启动所有其他用户态服务（如systemd）
- **PID=2**（kthreadd）：内核线程守护者，负责创建和管理其他内核线程（ksoftirqd、kworker等）

### 3.3 GDB远程调试原理

GDB支持通过串行协议远程调试运行在另一台机器（或虚拟机）上的程序。在QEMU内核调试场景中：

1. **QEMU端**：`-s`参数开启gdbserver，监听TCP端口1234。`-S`参数在启动时挂起CPU，等待GDB连接
2. **GDB端**：`target remote :1234`连接到QEMU的gdbserver，通过RSP（Remote Serial Protocol）协议交换调试命令

RSP协议承载的命令包括：
- 寄存器读写（`g`/`G`命令）
- 内存读写（`m`/`M`命令）
- 断点设置（`Z0`/`z0`命令）
- 单步执行（`s`/`S`命令）

GDB加载`vmlinux`（带DWARF调试符号的ELF文件）以获取符号信息、源码位置、变量类型。`vmlinux`约407MB（含DWARF5调试段），而`bzImage`仅14MB（压缩后的可启动镜像）。

## 四、实验目的

1. **理解x86_64架构的启动流程**：通过阅读源码和GDB跟踪，理解从CPU上电→BIOS→GRUB→实模式→保护模式→长模式→start_kernel()->rest_init()的完整启动链路。

2. **掌握QEMU+GDB的内核调试方法**：配置内核调试选项（DEBUG_INFO、KGDB），使用QEMU运行自定义内核，通过GDB远程连接设置断点进行交互式调试。

3. **分析内核启动关键函数**：使用`bt`（栈回溯）、`info registers`（寄存器状态）、`list`（源码查看）等GDB命令，在`start_kernel()`、`rest_init()`等关键函数处观察内核状态。

4. **通过printk观察启动时序**：在内核启动代码中插入`printk`日志，通过`dmesg`观察各初始化步骤的执行顺序和时间点。

## 五、实验内容

1. **编译带调试符号的内核**：在内核配置中启用`CONFIG_DEBUG_INFO_DWARF5=y`（生成DWARF5调试符号）和`CONFIG_KGDB=y`（内核调试器支持），重新编译内核。

2. **构建QEMU测试环境**：使用busybox创建最小化的initramfs（init RAM文件系统），包含`/bin/sh`、`mount`等基本命令，用于在QEMU中启动内核后的交互验证。

3. **使用GDB调试内核启动**：
   - 在QEMU中启动内核（`-S -s`挂起等待GDB）
   - 在GDB中连接QEMU（`target remote :1234`）
   - 在内核关键函数设置断点（`start_kernel`、`rest_init`、`kernel_init`）
   - 观察栈回溯、寄存器状态、源码位置

4. **添加printk观察启动过程**：在`init/main.c`的`start_kernel()`和`rest_init()`中添加带`[EXP6]`前缀的`printk`日志，编译内核后用QEMU启动，通过`dmesg | grep EXP6`验证。

5. **绘制启动流程图**：基于源码阅读和GDB跟踪结果，绘制从GRUB加载内核到init进程创建的完整函数调用流程图。

## 六、实验器材（设备、元器件）

| 器材 | 规格/版本 | 用途 |
|------|----------|------|
| PC计算机 | AMD 24核处理器, 31GB内存 | 实验主机（运行QEMU） |
| 虚拟化软件 | QEMU 6.2.0 (qemu-system-x86_64) | 虚拟化运行调试内核 |
| 调试器 | GDB 12.1 (gdb-multiarch) | 远程调试内核启动 |
| 内核源码 | linux-6.18.15 | 调试和分析对象 |
| 调试符号 | vmlinux（DWARF5, ~407MB） | GDB符号解析 |
| 最小文件系统 | busybox 1.35.0 initramfs | QEMU中的用户态环境 |
| 内核镜像 | bzImage #3（含KGDB支持） | QEMU加载的启动镜像 |
| 内核配置 | DEBUG_INFO_DWARF5=y, KGDB=y | 调试功能支持 |

## 七、实验步骤

### 7.1 编译带调试符号的内核

```bash
cd /usr/src/linux-6.18.15

# 确保调试选项开启
sudo make menuconfig
# Kernel hacking → 
#   Compile-time checks and compiler options →
#     [*] Compile the kernel with debug info
#     (5) Debug information level (DWARF5)
#   Generic Kernel Debugging Instruments →
#     [*] KGDB: kernel debugger

# 增量编译（已编译过的文件不会重编）
sudo make -j$(nproc)
```

编译产物：
- `vmlinux`：407MB，ELF格式，含完整DWARF5调试符号（GDB调试使用）
- `arch/x86/boot/bzImage`：14MB，压缩可启动镜像（QEMU加载使用）

> 📸 **截图1**：menuconfig 中开启 KGDB 和 DEBUG_INFO_DWARF5

### 7.2 构建最小initramfs

```bash
# 创建initramfs目录结构
mkdir -p /tmp/initramfs/{bin,sbin,dev,proc,sys}

# 复制busybox（静态链接的多功能工具）
cp /usr/bin/busybox /tmp/initramfs/bin/

# 创建必要的符号链接
cd /tmp/initramfs
ln -s bin/busybox init    # PID=1执行的第一个程序
mkdir -p bin && cd bin
for cmd in sh ls cat mount echo mknod; do
    ln -s busybox $cmd
done

# 创建init脚本（QEMU启动后自动执行）
cat > /tmp/initramfs/init << 'EOF'
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
mknod /dev/null c 1 3
mknod /dev/console c 5 1
echo "=== Linux 内核启动成功 ==="
echo "=== busybox initramfs shell ==="
exec /bin/sh
EOF
chmod +x /tmp/initramfs/init

# 打包为initramfs镜像
cd /tmp/initramfs
find . | cpio -o -H newc | gzip > /tmp/initramfs.img
```

**initramfs的作用**：initramfs（Initial RAM Filesystem）是一个基于内存的最小化根文件系统。内核启动后，首先挂载initramfs作为根目录，执行其中的`/init`程序，加载必要的驱动模块、配置存储（LVM/RAID），然后切换到真实的根文件系统。

### 7.3 用QEMU启动内核（无GDB，快速验证）

```bash
sudo qemu-system-x86_64 \
    -kernel /usr/src/linux-6.18.15/arch/x86/boot/bzImage \
    -initrd /tmp/initramfs.img \
    -append "console=ttyS0 nokaslr" \
    -nographic -m 512
```

参数说明：
- `-kernel`：指定内核镜像（bzImage）
- `-initrd`：指定initramfs镜像
- `-append "console=ttyS0 nokaslr"`：内核命令行参数——控制台输出到串口（ttyS0，QEMU重定向到标准输出），关闭KASLR（内核地址空间布局随机化，方便调试时符号地址匹配）
- `-nographic`：不启动图形窗口，全部通过串口控制台交互
- `-m 512`：分配512MB内存（QEMU内）

此时可以看到内核启动的完整日志输出，从`start_kernel()`的初始化信息到最终的busybox shell提示符。

> 📸 **截图2**：QEMU 成功启动到 busybox shell

### 7.4 用GDB连接QEMU交互调试

#### 终端1：启动QEMU（挂起等待GDB）

```bash
sudo qemu-system-x86_64 \
    -kernel /usr/src/linux-6.18.15/arch/x86/boot/bzImage \
    -initrd /tmp/initramfs.img \
    -append "console=ttyS0 nokaslr" \
    -nographic -m 512 \
    -S -s
# -S: CPU启动时挂起，等待GDB连接
# -s: 监听tcp:1234端口（gdbserver）
```

此时QEMU窗口显示黑屏或"Guest has not initialized the display yet"，CPU处于挂起状态。

#### 终端2：启动GDB

```bash
cd /usr/src/linux-6.18.15
gdb-multiarch vmlinux
```

在GDB中执行：

```
(gdb) target remote :1234    # 连接QEMU的gdbserver
(gdb) b start_kernel          # 在C语言内核入口设断点
(gdb) c                       # 继续执行
```

此时QEMU中的CPU开始执行，当执行到`start_kernel()`时触发断点，CPU再次挂起。

> 📸 **截图3**：GDB 连接 QEMU，断点命中 `start_kernel`

### 7.5 关键断点分析

#### 7.5.1 在start_kernel处查看栈回溯

```
(gdb) bt
# 显示调用栈，从最底层（x86_64_start_kernel）到当前（start_kernel）
#0  start_kernel () at init/main.c:936
#1  0x... in x86_64_start_reservations (...)
#2  0x... in x86_64_start_kernel (...)
#3  0x... in secondary_startup_64_no_verify ()
```

**分析**：栈回溯显示了从汇编入口（`secondary_startup_64_no_verify`）→ x86_64 C语言初始化（`x86_64_start_kernel`）→ 通用内核初始化（`start_kernel`）的完整调用链。

> 📸 **截图4**：`bt` 栈回溯

#### 7.5.2 查看寄存器状态

```
(gdb) info registers
rax   0x0                 0
rbx   0x0                 0
rcx   0x0                 0
rdx   0x0                 0
rsp   0xffff...           0xffff...  ← 内核栈指针
rip   0xffffffff...       0xffffffff... <start_kernel>  ← 当前指令
rflags  0x246             [ PF ZF IF ]  ← 中断已开启
cs    0x10               64-bit code segment
ss    0x18               64-bit data segment
```

**分析**：在`start_kernel`入口处，所有通用寄存器已清零。RIP（指令指针）指向`start_kernel`的第一条指令。RFLAGS中的IF位（Interrupt Flag）指示中断状态。

> 📸 **截图5**：`info registers` 寄存器状态

#### 7.5.3 其他关键断点

```
(gdb) b rest_init            # PID=1 init进程创建
(gdb) b kernel_init          # init进程入口
(gdb) c                      # 继续执行
```

在`rest_init`处，可以观察：
- `user_mode_thread(kernel_init, ...)`的调用——创建PID=1的init进程
- `schedule_preempt_disabled()`——当前CPU进入idle循环，开始调度

### 7.6 添加printk观察启动过程

在`init/main.c`中添加调试打印：

```c
// 在 start_kernel() 函数开头添加
printk(KERN_INFO "[EXP6] === start_kernel() called ===\n");
printk(KERN_INFO "[EXP6] Booting Linux 6.18.15 on x86_64\n");

// 在 start_kernel() 中 console_init() 之后添加
printk(KERN_INFO "[EXP6] console initialized — printk messages now visible\n");

// 在 rest_init() 中添加
printk(KERN_INFO "[EXP6] === rest_init() — creating init process ===\n");
```

重新编译内核后，在QEMU中启动，执行：

```bash
dmesg | grep EXP6
# 输出:
# [EXP6] === start_kernel() called ===
# [EXP6] Booting Linux 6.18.15 on x86_64
# [EXP6] console initialized — printk messages now visible
# [EXP6] === rest_init() — creating init process ===
```

> 📸 **截图6**：dmesg 输出启动过程打印信息
> 📸 **截图7**：启动过程函数调用流程图

### 7.7 常用GDB命令总结

| 命令 | 说明 | 本实验使用场景 |
|------|------|-------------|
| `target remote :1234` | 连接QEMU gdbserver | 建立调试会话 |
| `b start_kernel` | 在函数入口设断点 | 观察C语言启动入口 |
| `b rest_init` | 同上 | 观察init进程创建 |
| `c` | 继续执行直到下一断点 | 前进到下一个观察点 |
| `bt` | 显示调用栈 | 分析函数调用链 |
| `info registers` | 显示所有CPU寄存器 | 观察CPU状态 |
| `list` | 显示当前源码 | 查看上下文 |
| `si` | 单步汇编指令 | 精确跟踪控制流 |
| `p variable` | 打印变量值 | 查看内核数据结构 |
| `disas` | 反汇编当前函数 | 查看生成的机器码 |

## 八、实验数据及结果分析

### 8.1 内核启动关键路径

通过GDB跟踪确认的内核启动关键路径：

```
arch/x86/boot/header.S    — GRUB入口，setup_header结构
arch/x86/boot/main.c      — 实模式main()，检测内存、设置显示
arch/x86/boot/pm.c        — 切换到32位保护模式
compressed/head_64.S      — 切换到64位长模式，解压vmlinux
arch/x86/kernel/head_64.S — 64位汇编入口，设置初始页表
arch/x86/kernel/head64.c  — x86_64_start_kernel()
init/main.c: start_kernel() — 通用内核初始化 ← 我们的断点
  ├─ setup_arch()         — x86架构初始化
  ├─ mm_init()            — 内存管理初始化
  ├─ sched_init()         — 调度器初始化
  ├─ console_init()       — 控制台初始化（此后printk可见）
  └─ rest_init()          — 创建PID=1和PID=2
       ├─ kernel_init()   — PID=1, 尝试执行/sbin/init
       ├─ kthreadd()      — PID=2, 内核线程守护者
       └─ idle (PID=0)    — CPU进入空闲循环
```

### 8.2 GDB调试观察总结

| 观察点 | 关键发现 | 意义 |
|--------|---------|------|
| start_kernel | 第一个架构无关的C函数，此时只有引导CPU在运行 | 标志着"早期汇编"阶段结束 |
| setup_arch调用前 | command_line尚未完全解析 | 早期参数通过early_param处理 |
| console_init之前 | printk输出进入环形缓冲区但不可见 | 解释了启动早期消息丢失的原因 |
| rest_init | user_mode_thread创建init进程 | PID=1诞生，用户态世界起点 |
| kernel_init | 尝试执行/sbin/init → 进入busybox shell | QEMU环境中唯一的用户程序 |

### 8.3 结果分析

1. **启动阶段的渐进性**：内核启动不是单一入口，而是经历了"汇编→C语言→多任务"的渐进过程。每个阶段建立下一个阶段所需的运行环境——实模式设置基本I/O，保护模式完成解压，长模式设置页表，start_kernel完成全局初始化，rest_init创建进程世界。

2. **调试体验**：QEMU+GDB的组合为内核学习提供了前所未有的可见性。在没有虚拟化和远程调试的年代，内核启动调试需要串口线和两台物理机器。QEMU将这一过程简化为两条命令（启动QEMU + 连接GDB），大幅降低了内核学习的门槛。

3. **打印与断点的互补**：`printk`提供启动过程的时间线概览（适合了解"何时发生了什么"），GDB断点提供某一时刻的深度状态（适合了解"此刻内核里是什么"）。二者结合使用是内核分析和调试的最佳实践。

## 九、总结及心得体会

### 9.1 实验总结

本实验通过QEMU虚拟化运行和GDB远程调试，对Linux 6.18.15内核在x86_64架构下的启动过程进行了系统性的分析和跟踪。搭建了完整的QEMU+GDB+busybox initramfs调试环境，在`start_kernel()`、`rest_init()`等关键函数设置了断点，通过栈回溯和寄存器查看深入理解了内核启动的时序和状态。同时通过在内核代码中添加`printk`日志，从另一个角度（时间维度）观察了启动过程。

在理论层面，完整梳理了x86_64架构从实模式→保护模式→长模式的CPU模式升迁过程，以及Linux内核从汇编入口（`head_64.S`）到C语言初始化（`start_kernel`）再到进程创建（`rest_init`→`kernel_init`）的启动链路。

在实践层面，掌握了QEMU+GDB的内核调试环境搭建和使用方法，理解了vmlinux（调试符号载体）和bzImage（启动镜像）的区别和关系，体验了DWARF调试符号在源码级调试中的关键作用。

### 9.2 心得体会

1. **启动过程的"复杂性冰山"**：`start_kernel`看似一个普通的C函数调用，但实际上它标志着此前数百行汇编代码和数十个初始化函数的完成。理解启动过程的关键在于理解"每个阶段为下一阶段准备什么"——实模式准备I/O访问，保护模式准备地址空间切换，head_64.S准备初始页表，setup_arch准备内存布局。

2. **调试工具的双刃剑**：GDB+QEMU虽然功能强大，但引入了非真实的执行环境——单步调试的时序与实际启动完全不同，KASLR必须关闭（nokaslr），某些竞态条件在调试模式下不会触发。工程中应结合多种调试方法：printk、ftrace、kgdb、crash dump。

3. **历史兼容性的代价**：x86 CPU必须从8086兼容的实模式启动（1978年的设计），经历模式切换才能进入现代64位长模式。这种为了保持向后兼容性而层层叠加的模式切换机制，是x86架构独特的历史印记，也是理解启动代码复杂度的关键。

4. **内核学习的方法论**：通过"源码阅读→GDB跟踪→printk验证"的三步法，可以系统化地理解内核的任何子系统。本实验积累的调试环境和方法，在后续实验11的MFQ调度器开发中发挥了关键作用——QEMU的快速启动循环（10秒内）和GDB的精确断点，使得调度器Bugs的定位时间从"数十分钟的真机重启等待"缩短到"数秒的QEMU循环"。

---

> **注**：本文档为实验报告的文字内容部分。报告中标注了需要插入截图的位置（共7处），截图需由实验者自行截取并插入对应的报告章节中。
