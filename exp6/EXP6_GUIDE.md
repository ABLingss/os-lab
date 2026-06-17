# 实验6：Linux的x86架构启动过程分析和跟踪 — 操作指导

> 严格按指导书「实验六」内容编写 | 内核分析 + GDB调试

---

## 实验概述

阅读 Linux 内核启动代码，使用 GDB + QEMU 调试内核启动过程，在关键位置设置断点观察启动流程。

- **学时**: 4
- **类型**: 内核分析 + GDB调试
- **难度**: ⭐⭐⭐⭐⭐

**核心知识点**：
- x86_64 架构启动流程（BIOS → GRUB → 内核解压 → start_kernel → rest_init）
- GDB 远程调试内核
- QEMU 虚拟化运行内核
- 内核启动关键函数：`start_kernel()`, `rest_init()`, `kernel_init()`

---

## 需要安装的工具

```bash
sudo apt install -y qemu-system-x86 gdb-multiarch
```

---

## 内核启动流程概览

```
BIOS/UEFI
  └─→ GRUB2 (bootloader)
       └─→ arch/x86/boot/header.S     (内核映像头)
            └─→ arch/x86/boot/main.c        (实模式初始化)
                 └─→ arch/x86/boot/pm.c           (切换到保护模式)
                      └─→ arch/x86/boot/compressed/head_64.S  (长模式+解压)
                           └─→ arch/x86/kernel/head_64.S      (早期内核入口)
                                └─→ init/main.c: start_kernel()   ← C语言入口
                                     └─→ arch/x86/kernel/setup.c    (架构初始化)
                                     └─→ init/main.c: rest_init()    (创建init进程)
                                          └─→ kernel_init()          (第一个用户进程)
                                               └─→ /sbin/init        (用户态init)
```

## 关键源码文件

| 文件 | 作用 |
|------|------|
| `arch/x86/boot/header.S` | 内核映像头，GRUB入口 |
| `arch/x86/boot/pm.c` | 保护模式切换 |
| `arch/x86/boot/compressed/head_64.S` | 64位长模式 + 解压 |
| `arch/x86/kernel/head_64.S` | 早期汇编入口 |
| `init/main.c` | `start_kernel()` — C语言启动入口 |
| `arch/x86/kernel/setup.c` | x86架构初始化 |

---

## 步骤

### 1. 编译带调试符号的内核

```bash
cd /usr/src/linux-6.18.15

# 确保开启调试选项
sudo make menuconfig
# Kernel hacking → Compile-time checks and compiler options → Compile the kernel with debug info [*]
# Kernel hacking → Generic Kernel Debugging Instruments → KGDB: kernel debugger [*]

# 重新编译（增量编译很快）
sudo make -j$(nproc)
```

编译产物：
- `vmlinux` — 带调试符号的ELF内核映像（GDB用）
- `arch/x86/boot/bzImage` — 压缩内核映像（QEMU用）

> 📸 **截图1**：menuconfig 中开启 KGDB 和 DEBUG_INFO

### 2. 用 QEMU 启动内核（无GDB）

先验证 QEMU 能正常启动内核：

```bash
# 创建简单 initramfs（最小根文件系统）
cd /tmp
mkdir -p initramfs/{bin,sbin,dev,proc,sys}
# 复制 busybox（如无则先安装: sudo apt install busybox-static）
cp /bin/busybox initramfs/bin/
cd initramfs
ln -s bin/busybox init
mkdir -p bin && cd bin
for cmd in sh ls cat mount echo; do ln -s busybox $cmd; done
cd /tmp/initramfs
find . | cpio -o -H newc | gzip > /tmp/initramfs.img

# 用 QEMU 启动内核
qemu-system-x86_64 \
    -kernel /usr/src/linux-6.18.15/arch/x86/boot/bzImage \
    -initrd /tmp/initramfs.img \
    -append "console=ttyS0 nokaslr" \
    -nographic
```

> 📸 **截图2**：QEMU 成功启动到 busybox shell

### 3. 用 GDB 连接 QEMU 调试

#### 终端1：启动 QEMU（挂起等待 GDB）

```bash
qemu-system-x86_64 \
    -kernel /usr/src/linux-6.18.15/arch/x86/boot/bzImage \
    -initrd /tmp/initramfs.img \
    -append "console=ttyS0 nokaslr" \
    -nographic \
    -S -s
# -S: 启动时挂起，等待 GDB 连接
# -s: 监听 tcp:1234（等价于 -gdb tcp::1234）
```

#### 终端2：启动 GDB 连接

```bash
cd /usr/src/linux-6.18.15
gdb-multiarch vmlinux
```

在 GDB 中：

```
(gdb) target remote :1234
(gdb) b start_kernel        # 断点：C语言入口
(gdb) c                     # 继续执行
```

> 📸 **截图3**：GDB 连接 QEMU，断点命中 `start_kernel`

### 4. 关键断点跟踪

#### 4.1 在 start_kernel 处查看调用栈

```
(gdb) bt                     # 栈回溯
(gdb) info registers         # 寄存器状态
(gdb) list                   # 查看源码
```

> 📸 **截图4**：`bt` 栈回溯
> 📸 **截图5**：`info registers` 寄存器状态

#### 4.2 断点其他关键函数

```
(gdb) b rest_init            # init进程创建
(gdb) b kernel_init          # 第一个内核线程
(gdb) c
```

### 5. 添加 printk 打印启动信息

在 `init/main.c` 的 `start_kernel()` 函数中添加：

```c
printk(KERN_INFO "[EXP6] === start_kernel() called ===\n");
printk(KERN_INFO "[EXP6] Booting Linux 6.18.15 on x86_64\n");
```

在 `rest_init()` 中添加：

```c
printk(KERN_INFO "[EXP6] === rest_init() — creating init process ===\n");
```

重新编译内核，启动后用 `dmesg` 查看：

```bash
dmesg | grep EXP6
```

> 📸 **截图6**：dmesg 输出启动过程打印信息

### 6. 绘制启动流程图

根据 GDB 跟踪结果和源码阅读，绘制启动过程函数调用流程图。

> 📸 **截图7**：启动过程函数调用流程图（可手绘或 draw.io）

---

## 截图清单（共7张）

| # | 内容 | 对应步骤 |
|---|------|---------|
| 1 | menuconfig 开启 KGDB/DEBUG_INFO | 1 |
| 2 | QEMU 启动到 busybox | 2 |
| 3 | GDB 连接 QEMU，断点命中 start_kernel | 3 |
| 4 | `bt` 栈回溯 | 4.1 |
| 5 | `info registers` 寄存器状态 | 4.1 |
| 6 | dmesg 查看启动打印信息 | 5 |
| 7 | 启动过程函数调用流程图 | 6 |

---

## 常用 GDB 调试命令

| 命令 | 说明 |
|------|------|
| `b start_kernel` | 在 start_kernel 处设断点 |
| `b *0x...` | 在指定地址设断点 |
| `c` | 继续执行 |
| `si` | 单步汇编指令 |
| `ni` | 单步（跳过调用） |
| `bt` | 显示调用栈 |
| `info registers` | 显示寄存器 |
| `info frame` | 显示栈帧 |
| `disas` | 反汇编当前函数 |
| `p variable` | 打印变量 |
| `hbreak` | 硬件断点（适用于ROM） |

---

## 实验原理要点（供写报告参考）

### x86_64 启动阶段
1. **实模式** (16-bit): BIOS 加载 GRUB，GRUB 加载内核 bzImage
2. **保护模式** (32-bit): 内核解压，初始化基本硬件
3. **长模式** (64-bit): 跳转到 `start_kernel()`，全面初始化系统

### start_kernel() 主要工作
- 初始化调度器 (`sched_init`)
- 初始化内存管理 (`mm_init`)
- 初始化中断 (`init_IRQ`, `softirq_init`)
- 初始化定时器 (`time_init`)
- 初始化控制台 (`console_init`)
- 最后调用 `rest_init()`

---

## 本目录文件

| 文件 | 说明 |
|------|------|
| `EXP6_GUIDE.md` | 本文件 |
