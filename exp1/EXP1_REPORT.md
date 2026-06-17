# 实验一：Linux内核裁剪和编译

> **课程名称**：计算机操作系统
> **Student**：[Name] | **ID**：[Student ID] | **Instructor**：[Name]
> **Location**：[Lab] | **Semester**：[Term]

---

## 一、实验项目名称

Linux内核裁剪和编译

## 二、实验学时

4学时

## 三、实验原理

### 3.1 Linux内核概述

Linux内核是操作系统的核心组件，负责管理系统硬件资源（CPU、内存、磁盘、网络等）并为用户程序提供统一的服务接口。Linux内核采用**宏内核（Monolithic Kernel）** 架构，将进程管理、内存管理、文件系统、设备驱动、网络协议栈等核心功能全部运行在内核态（Ring 0），与用户态（Ring 3）的应用程序严格隔离。

Linux内核自1991年由Linus Torvalds发布第一个版本以来，经过三十余年的发展，已经成为一个高度模块化、可配置的大型软件项目。截至6.18版本，内核源代码超过3000万行，支持30余种处理器架构和数千种硬件设备。正是这种庞大的规模使得**内核裁剪**成为必要——不同应用场景（服务器、嵌入式设备、桌面系统）对内核的功能需求差异巨大，通过裁剪可以去除不需要的功能模块，获得更小的内核镜像、更快的启动速度和更少的内存占用。

### 3.2 Linux内核源代码结构

Linux内核源代码采用模块化组织方式，主要目录结构如下：

| 目录 | 功能说明 | 典型子目录/文件 |
|------|---------|---------------|
| `arch/` | 处理器架构相关代码 | `x86/`, `arm/`, `riscv/` — 包含启动代码、中断处理、页表管理、架构特定优化 |
| `kernel/` | 核心内核功能 | `sched/` (调度器), `irq/` (中断), `time/` (定时器), `fork.c` (进程创建), `sys.c` (系统调用) |
| `mm/` | 内存管理 | 页分配、slab分配器、页面回收、交换管理、内存压缩 |
| `fs/` | 文件系统 | `ext4/`, `btrfs/`, `xfs/`, `vfs/` (虚拟文件系统层), `proc/`, `sysfs/` |
| `drivers/` | 设备驱动集合 | `gpu/` (显卡), `net/` (网卡), `usb/`, `storage/` (SATA/NVMe), `i2c/`, `spi/` |
| `net/` | 网络协议栈 | TCP/IP, UDP, 套接字层, 包过滤(netfilter), 路由 |
| `include/` | 头文件 | `linux/` (通用头), `uapi/` (用户态API), `asm-generic/` (架构无关) |
| `block/` | 块设备I/O层 | IO调度器, 块设备请求队列管理 |
| `init/` | 内核初始化 | `main.c` — start_kernel()入口, 系统初始化序列 |
| `ipc/` | 进程间通信 | 信号量、消息队列、共享内存 (System V IPC) |
| `security/` | 安全框架 | SELinux, AppArmor, capabilities |
| `lib/` | 内核工具库 | 字符串操作、加密算法、压缩/解压、数据结构 |
| `scripts/` | 编译辅助脚本 | Kconfig解析、链接脚本生成、头文件检查 |
| `sound/` | 音频子系统 | ALSA架构 |
| `tools/` | 用户态辅助工具 | perf, objtool, bpftool |

这种模块化的目录结构是内核裁剪的基础——开发人员可以根据硬件平台和功能需求，在配置阶段选择性地启用或禁用特定目录下的功能模块。

### 3.3 内核配置系统（Kconfig）

Linux内核的配置系统基于**Kconfig**机制，其核心组件包括：

1. **Kconfig文件**：散布在各子目录中的配置描述文件，使用Kconfig语法定义配置选项的层次结构、依赖关系和帮助信息。例如`init/Kconfig`定义了通用内核选项，`kernel/Kconfig.preempt`定义了抢占模型选项。

2. **配置选项类型**：
   - `bool`：二值选项（y/n）
   - `tristate`：三态选项（y编译进内核 / m编译为模块 / n不编译）
   - `int/hex/string`：数值/十六进制/字符串选项
   - `choice`：多选一选项组

3. **依赖与选择**：
   - `depends on`：前置依赖，只有依赖项满足时该选项才可见
   - `select`：反向依赖，选中该项后自动选中目标项
   - `select ... if`：条件反向选择

4. **配置工具**：内核提供了多种配置界面：
   - `make config`：纯文本逐行交互式配置
   - `make menuconfig`：基于ncurses的文本图形界面（最常用）
   - `make xconfig`：基于Qt的图形界面
   - `make gconfig`：基于GTK的图形界面
   - `make oldconfig`：基于已有`.config`，仅询问新增选项
   - `make olddefconfig`：基于已有`.config`，新增选项自动取默认值

5. **配置文件**：所有配置最终以`键=值`格式保存在源代码根目录的`.config`文件中。例如：
   ```
   CONFIG_EXT4_FS=y
   CONFIG_SATA_AHCI=m
   CONFIG_BTRFS_FS is not set
   ```

### 3.4 内核编译系统（Kbuild）

Linux内核使用**Kbuild**（Kernel Build System）作为编译框架，基于GNU Make并大量使用自定义规则和脚本：

1. **Makefile层次**：
   - 顶层Makefile：定义全局编译选项、目标、架构选择
   - 子目录Makefile：`obj-y`（编译进内核）、`obj-m`（编译为模块）
   - 架构特定Makefile：`arch/x86/Makefile`定义x86特有的编译参数

2. **编译流程**：
   ```
   源码(.c/.S) → [gcc/as] → 目标文件(.o) → [ld] → vmlinux(ELF)
   → [objcopy] → vmlinux.bin → [压缩] → vmlinux.bin.gz
   → [链接脚本] → bzImage(piggy.S嵌入压缩内核)
   → [setup部分] → arch/x86/boot/bzImage(完整可启动镜像)
   ```

3. **关键编译目标**：
   - `make` 或 `make all`：编译内核镜像（bzImage）和所有模块
   - `make bzImage`：仅编译内核镜像（跳过模块）
   - `make modules`：仅编译模块
   - `make -jN`：使用N个并行任务加速编译

4. **CONFIG_MODVERSIONS机制**：当`CONFIG_MODVERSIONS=y`时，内核为每个导出的符号计算CRC校验值。模块加载时核对CRC，不匹配则拒绝加载。这意味着修改关键数据结构（如`task_struct`）后，所有模块的CRC都会改变，必须重新编译全部模块。

### 3.5 Linux系统引导过程

内核编译完成后需要安装到系统中，通过引导加载程序（GRUB）在系统启动时加载：

1. **GRUB（GRand Unified Bootloader）**：现代Linux系统使用GRUB2作为引导加载程序。系统上电后，BIOS/UEFI加载GRUB的第一阶段引导代码，GRUB读取`/boot/grub/grub.cfg`配置文件，显示操作系统选择菜单。

2. **内核加载**：GRUB将`vmlinuz-<version>`（即bzImage）和`initrd.img-<version>`（初始RAM磁盘镜像）加载到内存中，然后将控制权交给内核。

3. **内核启动**：
   - 实模式入口（`arch/x86/boot/header.S`）
   - 切换到保护模式，解压内核
   - `start_kernel()`（`init/main.c`）— 内核初始化入口
   - 挂载initrd作为临时根文件系统
   - 加载initrd中的设备驱动模块（如AHCI磁盘控制器驱动）
   - 切换到真实根文件系统
   - 启动第一个用户态进程`/sbin/init`

4. **initrd（Initial RAM Disk）**：initrd是一个压缩的cpio归档文件，包含最小化的根文件系统。它的关键作用是：
   - 提供内核启动必需的设备驱动模块（特别是磁盘控制器驱动）
   - 提供LVM/RAID等存储管理工具
   - 在真实根文件系统挂载前完成设备发现和配置

5. **initramfs vs initrd**：现代内核使用initramfs（基于tmpfs的ramfs），比传统的基于ramdisk的initrd更高效，直接使用page cache且无需额外的文件系统驱动。

## 四、实验目的

通过阅读、分析、配置、裁剪、编译和安装运行Linux内核，达到以下目的：

1. **理解Linux内核代码的模块结构**：通过浏览内核源码目录树，了解各子目录的功能定位和模块划分，建立对大型操作系统内核代码组织方式的整体认知。

2. **掌握Linux内核的配置和裁剪方法**：通过`make menuconfig`工具实际操作，理解Kconfig配置系统的层次结构、依赖关系和裁剪策略，学会根据硬件平台和功能需求进行针对性裁剪。

3. **掌握Linux内核的编译方法、安装和替换原有内核的方法**：完整走通"配置→编译→安装模块→安装内核→更新GRUB→重启验证"的全流程，理解每个步骤的技术含义。

4. **了解Linux系统的启动过程**：通过观察GRUB菜单、内核加载、模块初始化等阶段，理解从系统上电到用户登录的完整引导链路。

## 五、实验内容

本实验包含以下六个方面的内容：

1. **安装Linux发行版并搭建Linux内核编译环境**：在VMware虚拟机中安装Ubuntu 22.04.5 LTS操作系统，安装gcc、make、binutils、flex、bison、libncurses-dev、libelf-dev、libssl-dev、bc等内核编译所需的工具链。

2. **下载Linux内核源代码**：获取linux-6.18.15版本的内核源码，解压到`/usr/src/`目录下，建立软链接便于操作。

3. **分析和查找资料了解Linux内核源代码的模块结构**：浏览内核源码目录树，查阅Linux内核文档和在线资源，理解arch、kernel、mm、fs、drivers、net等核心目录的职责划分。

4. **学习Linux内核配置方法，完成一个尽量精简的Linux内核裁剪**：以当前运行内核的配置文件为基础，通过`make olddefconfig`生成初始配置，然后使用`make menuconfig`进行裁剪，在保证系统正常启动的前提下尽可能去除不需要的功能模块。

5. **学习Linux内核编译过程，动手完成Linux内核编译和安装**：使用`make -j$(nproc)`进行多核并行编译，使用`make modules_install`安装内核模块，使用`make install`安装内核镜像，使用`update-grub`更新引导菜单。

6. **了解Linux系统引导过程，用新内核替换原有内核启动系统**：重启后在GRUB菜单中选择新编译的内核启动，通过`uname -r`验证内核版本，确认系统在新内核下正常工作。

## 六、实验器材（设备、元器件）

| 器材 | 规格/版本 | 用途 |
|------|----------|------|
| PC计算机 | AMD 24核处理器, 31GB内存, 80GB磁盘 | 实验主机 |
| VMware虚拟机 | VMware Workstation/ESXi | 虚拟化平台 |
| 宿主机操作系统 | Ubuntu 22.04.5 LTS (x86_64) | 实验操作系统环境 |
| 原内核版本 | Linux 5.15.0-181-generic | Ubuntu官方预编译内核 |
| 新内核版本 | Linux 6.18.15 | 自编译定制内核 |
| 内核源码 | linux-6.18.15 | 从kernel.org获取 |
| 编译工具链 | gcc 11.4.0, GNU Make 4.3, binutils 2.38 | 内核编译工具 |
| 配置工具 | libncurses5-dev | menuconfig界面支持 |
| 引导加载程序 | GRUB 2.06 | 多内核启动管理 |

## 七、实验步骤

### 7.1 实验环境准备

#### 7.1.1 安装编译工具链

Linux内核编译需要一套完整的开发工具链。在Ubuntu系统上执行以下命令安装所有必需的软件包：

```bash
sudo apt update
sudo apt install -y gcc make binutils flex bison \
    libncurses5-dev libelf-dev libssl-dev bc \
    dwarves openssl dkms
```

各工具的作用：
- **gcc**：C语言编译器，编译内核源码（内核绝大部分由C语言编写）
- **make**：构建自动化工具，解释Kbuild系统的Makefile
- **binutils**：包含ld（链接器）、as（汇编器）、objcopy等二进制工具
- **flex/bison**：词法分析器和语法分析器生成器，编译Kconfig解析器
- **libncurses5-dev**：提供`make menuconfig`所需的文本图形界面库
- **libelf-dev**：ELF文件处理库，objtool等工具依赖
- **libssl-dev**：OpenSSL开发库，用于内核模块签名验证
- **bc**：任意精度计算器，内核编译中的数学运算
- **dwarves**：DWARF调试信息处理，生成BTF（BPF Type Format）数据
- **dkms**：动态内核模块支持框架

#### 7.1.2 获取内核源码

将linux-6.18.15内核源码包解压到`/usr/src/`目录：

```bash
sudo tar -xvf /usr/src/linux-6.18.15.tar.xz -C /usr/src/
sudo ln -s /usr/src/linux-6.18.15 /usr/src/linux-new
```

`/usr/src/`是内核源码的传统存放位置。软链接`linux-new`提供了便捷的访问路径，同时也是一般Linux内核编译文档中约定的标准路径。

#### 7.1.3 确认实验环境

编译前确认关键环境信息：

- **磁盘空间**：内核源码约1.5GB，编译产物约5-8GB，需要至少20GB可用空间
- **CPU核心数**：24核（`nproc`命令确认），可用于并行编译加速
- **内存**：31GB，满足编译期间的内存需求

本实验环境为VMware虚拟机，存储控制器为SATA AHCI，根文件系统部署在LVM逻辑卷上，这些特性都需要相应的内核配置支持。

### 7.2 内核配置与裁剪

#### 7.2.1 生成基础配置

内核配置的第一步是获取一个可用的初始配置。采用"自底向上"策略——基于当前运行内核的配置进行修改，而非从零开始：

```bash
cd /usr/src/linux-6.18.15
sudo cp /boot/config-$(uname -r) .config
sudo make olddefconfig
```

**`make olddefconfig`的作用**：对比`.config`与新内核源码的Kconfig定义。对于当前配置文件中已存在的选项保持原值不变；对于新内核版本新增的配置选项，根据Kconfig中定义的默认值自动填充（通常是y或n，取决于depends on和default语句）。这避免了手动回答数百个新增选项的问题，是新旧内核版本间迁移的最有效方式。

#### 7.2.2 启动菜单配置工具

```bash
cd /usr/src/linux-6.18.15
sudo make menuconfig
```

此命令启动基于ncurses的文本图形化配置界面。界面分层组织：
- 主菜单列出数十个配置类别（General setup、Processor type、Filesystems等）
- 使用方向键（↑↓）移动光标，Enter进入子菜单
- 空格键切换选项状态：`[*]`编译进内核、`[M]`编译为模块、`[ ]`不编译
- Tab键在选项区和Save/Exit/Help按钮间切换
- `/`键搜索配置项（支持模糊匹配）
- Esc键返回上一级菜单

**编译进内核(y) vs 编译为模块(m)的权衡**：
- `y`：代码链接进vmlinux，内核启动即可用，无需initrd加载，但增大内核镜像体积且常驻内存
- `m`：代码编译为独立的`.ko`文件，按需加载/卸载。减小内核镜像，但需initrd在启动时加载关键驱动（如磁盘控制器），否则无法挂载根文件系统
- `n`：完全排除，内核不包含该功能

#### 7.2.3 关键模块裁剪策略

本次裁剪遵循"保留必需功能，删除冗余模块"的原则，重点对以下子系统进行了裁剪：

**（1）通用设置（General setup）**

| 配置项 | 原值 | 裁剪后 | 理由 |
|--------|------|--------|------|
| Local version | `-generic` | (空) | 避免内核版本后缀过长 |
| Support for paging of anonymous memory | y | y | 保留swap支持，系统配置了3.8GB交换空间 |
| System V IPC | y | y | 实验4/8需要使用XSI IPC |
| POSIX Message Queues | y | y | 用户态IPC通用功能 |
| Auditing support | y | n | 桌面/实验环境无需内核审计 |
| BPF subsystem | y | y | systemd等现代系统组件依赖 |
| Control Group support | y | y | systemd和容器功能核心依赖 |

**（2）处理器类型与特性（Processor type and features）**

| 配置项 | 原值 | 裁剪后 | 理由 |
|--------|------|--------|------|
| Processor family | Generic-x86-64 | Generic-x86-64 | 保持通用兼容性 |
| Symmetric multi-processing support | y | y | 虚拟机配置了24核 |
| Multi-core scheduler support | y | y | 多核系统必需 |
| Intel MCE features | y | n | AMD处理器，Intel特性无意义 |
| Intel SGX | y | n | 同上 |
| AMD processor support | y | y | 使用AMD处理器 |
| Preemption Model | Voluntary | Voluntary | 桌面响应性优先 |

**（3）文件系统（Filesystems）**

| 配置项 | 原值 | 裁剪后 | 理由 |
|--------|------|--------|------|
| Ext4 filesystem | y | y | 根文件系统使用的ext4 |
| Ext3/Ext2 filesystem | y | y | exp9实验需要ext2 |
| Btrfs filesystem | y | n | 实验环境不使用 |
| XFS filesystem | y | n | 实验环境不使用 |
| NTFS filesystem | m | n | 不需要Windows互操作 |
| CD-ROM/DVD Filesystems | m | n | 虚拟机无光驱 |
| Overlay filesystem | y | y | Docker/容器可能需要 |
| FUSE filesystem | y | y | 用户态文件系统支持 |

**（4）网络支持（Networking support）**

| 配置项 | 原值 | 裁剪后 | 理由 |
|--------|------|--------|------|
| TCP/IP networking | y | y | 基础网络功能 |
| Bluetooth subsystem | m | n | 虚拟机无蓝牙硬件 |
| Wireless (802.11) | m | n | 虚拟机仅有线网络 |
| NFC subsystem | m | n | 不需要近场通信 |
| Amateur Radio | m | n | 不需要业余无线电协议 |
| CAN bus | m | n | 不需要CAN总线 |

**（5）设备驱动（Device Drivers）**

| 配置项 | 原值 | 裁剪后 | 理由 |
|--------|------|--------|------|
| VMware SATA AHCI | m | m | 磁盘控制器驱动，必须在initrd |
| LSI SCSI | m | m | VMware SCSI控制器 |
| GPU drivers (NVIDIA/AMD) | m | n | 虚拟机无GPU直通 |
| Intel GPU | m | n | 非Intel平台 |
| Sound card support | m | y | 保留基础音频 |
| USB support | y | y | 保留USB支持 |
| Network device support | y | y | 保留网卡驱动 |

#### 7.2.4 保存配置

裁剪完成后，按Esc返回主界面，选择"Save"将配置写入`.config`文件，然后选择"Exit"退出menuconfig。保存的`.config`文件包含所有配置选项的最终状态，是后续编译的输入。

### 7.3 内核编译

#### 7.3.1 执行编译

```bash
cd /usr/src/linux-6.18.15
sudo make -j$(nproc)
```

编译参数说明：
- `-j$(nproc)`：并行编译任务数等于CPU核心数（24），充分利用多核性能
- 不使用`-jN+1`的惯例，因为内核编译中make规则本身不构成瓶颈

编译过程分为几个阶段：
1. **预处理阶段**：生成`include/generated/`目录下的自动生成头文件（如`autoconf.h`，将`.config`转为C宏）
2. **Kconfig解析**：syncconfig目标确保配置与代码一致
3. **逐文件编译**：每个`.c`文件被gcc编译为`.o`目标文件，此阶段CPU密集，并行加速效果最明显
4. **链接阶段**：所有`.o`文件由ld链接为`vmlinux`（未压缩的ELF内核，约407MB）
5. **压缩/封装**：objcopy提取vmlinux的代码段，压缩为`vmlinux.bin.gz`，嵌入到`arch/x86/boot/`的setup代码中，最终生成`arch/x86/boot/bzImage`（约14MB）

编译产物体积对比：
- `vmlinux`：407MB（ELF格式，含调试符号DWARF5）
- `bzImage`：14MB（压缩可启动镜像，不含调试符号）

#### 7.3.2 编译时间

在24核AMD处理器上，完整编译（全模块）耗时约15-25分钟。影响编译时间的关键因素：
- 配置选项数量：每个`CONFIG_*=y`的选项都会编译对应的代码
- 模块数量：模块（`=m`）同样需要编译，只是链接方式不同
- 调试信息：`CONFIG_DEBUG_INFO_DWARF5=y`会显著增加编译时间

### 7.4 内核安装

#### 7.4.1 安装内核模块

```bash
sudo make modules_install
```

此命令将编译生成的所有`.ko`模块文件复制到`/lib/modules/6.18.15/`目录，并运行`depmod`生成模块依赖关系文件（`modules.dep`、`modules.alias`、`modules.symbols`等）。

安装后的`/lib/modules/6.18.15/`目录约6.1GB，包含：
- `kernel/`：所有`.ko`模块文件，按源码目录结构组织
- `modules.dep`：模块间依赖关系
- `modules.alias`：设备别名（用于设备驱动的自动加载）
- `modules.symbols`：模块导出的符号信息
- `modules.builtin`：编译进内核（非模块）的组件列表
- `build`：指向内核源码树的符号链接，外部模块编译时使用

**重要注意**：`CONFIG_MODVERSIONS=y`时，`make modules_install`会覆盖同版本号下的旧模块。实验环境中同时存在原6.18.15内核和MFQ调度器6.18.15内核，二者版本号相同但ABI不兼容，共享`/lib/modules/6.18.15/`目录。旧模块已备份到`/lib/modules/6.18.15.bak/`。

#### 7.4.2 安装内核镜像

```bash
sudo make install
```

此命令执行以下操作：
1. 将`arch/x86/boot/bzImage`复制到`/boot/vmlinuz-6.18.15`
2. 将`System.map`（内核符号表）复制到`/boot/System.map-6.18.15`
3. 将`.config`（配置文件）复制到`/boot/config-6.18.15`

#### 7.4.3 更新GRUB引导菜单

```bash
sudo update-grub
```

GRUB扫描`/boot/`目录下的所有内核镜像和initrd文件，自动生成`/boot/grub/grub.cfg`。每个内核镜像对应一个独立的引导菜单项，包括常规启动和恢复模式两个选项。

实验系统当前的GRUB菜单包含：
- `Ubuntu`（默认，使用vmlinuz-6.18.15-mfq）
- `Ubuntu, with Linux 6.18.15-mfq`（含MFQ调度器，实验11验证用）
- `Ubuntu, with Linux 6.18.15`（实验1-10使用的无MFQ内核）
- `Ubuntu, with Linux 6.18.15.old`（较旧的备份版本）
- `Ubuntu, with Linux 5.15.0-181-generic`（原始Ubuntu内核，安全回退）

### 7.5 启动验证

#### 7.5.1 重启并选择新内核

```bash
sudo reboot
```

系统重启过程中，在GRUB菜单（若未自动显示可按Shift键）中选择"Advanced options for Ubuntu" → "Ubuntu, with Linux 6.18.15"启动新内核。

#### 7.5.2 验证内核版本

系统启动后，打开终端执行：

```bash
uname -r
# 输出：6.18.15
```

确认当前运行的是自编译的6.18.15内核。

#### 7.5.3 验证系统功能正常

进一步检查关键系统功能：

```bash
# 查看内核启动信息
dmesg | head -30

# 确认文件系统挂载正常
df -h

# 确认网络功能
ip addr show

# 查看加载的内核模块
lsmod | head -20

# 检查系统内存
free -h
```

## 八、实验数据及结果分析

### 8.1 实验主要程序段

本实验为配置与编译操作，核心操作为Makefile驱动的自动化构建流程。关键的操作序列如下：

```bash
# ====== 阶段1: 环境准备 ======
# 安装编译工具链
sudo apt install -y gcc make binutils flex bison \
    libncurses5-dev libelf-dev libssl-dev bc dwarves

# 获取和解压内核源码
sudo tar -xvf /usr/src/linux-6.18.15.tar.xz -C /usr/src/
cd /usr/src/linux-6.18.15

# ====== 阶段2: 配置与裁剪 ======
# 基于当前内核配置生成初始配置
sudo cp /boot/config-$(uname -r) .config
sudo make olddefconfig

# 交互式内核裁剪
sudo make menuconfig

# ====== 阶段3: 编译 ======
# 多核并行编译
sudo make -j$(nproc)

# ====== 阶段4: 安装 ======
sudo make modules_install
sudo make install
sudo update-grub

# ====== 阶段5: 验证 ======
sudo reboot
# ... 选择新内核启动后 ...
uname -r           # 预期输出: 6.18.15
dmesg | tail -20   # 查看启动日志
```

### 8.2 内核配置剪裁关键选项

以下提取自`.config`文件，展示了裁剪后的关键配置：

```
# 内核版本信息
CONFIG_LOCALVERSION=""
CONFIG_LOCALVERSION_AUTO=n

# 架构与处理器
CONFIG_X86_64=y
CONFIG_SMP=y
CONFIG_NR_CPUS=64
CONFIG_PREEMPT_VOLUNTARY=y
CONFIG_AMD_MEM_ENCRYPT=y

# 关键内核功能
CONFIG_SYSVIPC=y
CONFIG_POSIX_MQUEUE=y
CONFIG_CGROUPS=y
CONFIG_NAMESPACES=y
CONFIG_MODULES=y
CONFIG_MODVERSIONS=y

# 文件系统（仅保留需要的）
CONFIG_EXT4_FS=y
CONFIG_EXT4_USE_FOR_EXT2=y
CONFIG_EXT2_FS=y
# CONFIG_BTRFS_FS is not set
# CONFIG_XFS_FS is not set
# CONFIG_NTFS_FS is not set

# 设备驱动（VMware虚拟化环境必需）
CONFIG_SATA_AHCI=m        # 磁盘控制器驱动（模块）
CONFIG_SCSI=y             # SCSI层
CONFIG_BLK_DEV_SD=y       # SCSI磁盘
CONFIG_BLK_DEV_DM=y       # LVM设备映射器
CONFIG_FUSION_MPT=m       # LSI SCSI控制器（模块）

# 网络
CONFIG_INET=y
CONFIG_NETDEVICES=y
# CONFIG_BT is not set
# CONFIG_WLAN is not set

# 调试（开发阶段开启）
CONFIG_DEBUG_INFO_DWARF5=y
CONFIG_GDB_SCRIPTS=y
CONFIG_KGDB=y
```

### 8.3 编译与安装结果

#### 内核镜像对比

| 指标 | 原内核 (5.15.0-181) | 新内核 (6.18.15) | 说明 |
|------|---------------------|-------------------|------|
| 内核版本 | 5.15.0-181-generic | 6.18.15 | Ubuntu官方 → 自编译主线 |
| bzImage体积 | 12MB | 14MB | 版本升级导致体积增大（功能增多） |
| 模块目录大小 | 约650MB | 6.1GB | 新内核编译了更多模块（含调试信息） |
| vmlinux体积 | — | 407MB | 未压缩ELF，含DWARF5调试符号 |
| 配置文件 | Ubuntu通用配置 | 定制裁剪配置 | 去除了不必要的驱动和文件系统 |

**体积分析**：注意到新编译的6.18.15内核比原5.15内核的bzImage略大（14MB vs 12MB），这是因为：
1. Linux 6.18相比5.15增加了大量新功能和驱动
2. 裁剪过程虽然关闭了许多不需要的选项，但6.x内核基线代码量本身就更大
3. 编译时保留了DWARF5调试信息（`CONFIG_DEBUG_INFO_DWARF5=y`），这增加了vmlinux体积但对bzImage影响有限

模块目录体积差异巨大（6.1GB vs 650MB）的主要原因：
1. 新内核编译了全部可用模块（`MODULES=most`），原Ubuntu内核仅安装了系统实际加载的模块子集
2. 调试信息增大了`.ko`文件的体积

#### 系统资源使用

| 指标 | 数值 | 说明 |
|------|------|------|
| 总内存 | 31GB | MemTotal: 32840236 kB |
| 空闲内存 | 22GB | MemFree: 23198256 kB |
| 可用内存 | 29GB | MemAvailable: 30672080 kB |
| 交换空间 | 3.8GB | SwapTotal, 0B used |
| /boot分区 | 2GB容量，约174MB空闲 | 存有4个内核镜像 |
| /分区 | 26GB空闲 | LVM逻辑卷 |

#### GRUB引导菜单

```
menuentry 'Ubuntu' → vmlinuz-6.18.15-mfq (默认)
├── Ubuntu, with Linux 6.18.15-mfq          (MFQ调度器内核)
│   └── Ubuntu, with Linux 6.18.15-mfq (recovery mode)
├── Ubuntu, with Linux 6.18.15              (自编译标准内核)
│   └── Ubuntu, with Linux 6.18.15 (recovery mode)
├── Ubuntu, with Linux 6.18.15.old          (旧自编译备份)
│   └── ...
└── Ubuntu, with Linux 5.15.0-181-generic   (Ubuntu原厂内核，安全回退)
    └── ...
```

### 8.4 结果分析

#### 8.4.1 内核裁剪效果

通过`make menuconfig`进行的内核裁剪达到了以下效果：

1. **功能精简**：去除了虚拟机环境中不需要的硬件驱动（蓝牙、无线网卡、GPU驱动、光驱文件系统、NTFS/Btrfs/XFS等文件系统），减少了内核的攻击面和维护负担。

2. **编译时间优化**：关闭不需要的功能模块后，需要编译的源文件数量减少，后续增量编译时能节省时间。特别是实验11的MFQ调度器开发过程中，平均每次增量编译仅需3-5秒（仅重编`kernel/sched/`目录），这是裁剪带来的直接效率提升。

3. **关键功能保留**：在裁剪过程中特别注意保留了虚拟化环境的关键组件——AHCI磁盘控制器驱动（`CONFIG_SATA_AHCI=m`）、LVM设备映射器（`CONFIG_BLK_DEV_DM=y`）、ext4文件系统（`CONFIG_EXT4_FS=y`）等，确保了系统正常启动。

#### 8.4.2 关键经验教训

1. **initrd的关键性**：由于`CONFIG_SATA_AHCI=m`（磁盘驱动编译为模块而非编译进内核），initrd必须包含`ahci.ko`模块，否则内核在启动过程中无法识别磁盘，导致"cannot mount root fs"错误。后续实验11中使用`MODULES=dep`构建initrd时恰好遇到了这个问题——initrd中只有0个模块，启动直接失败。改为`MODULES=most`后包含1465个模块（553MB），问题解决。这充分说明了理解每个配置选项含义的重要性。

2. **CONFIG_MODVERSIONS的影响**：该选项启用严格的模块CRC校验。修改`task_struct`等关键数据结构后，所有模块的CRC都会改变，必须重新编译全部模块并重建initrd。两个同版本号的内核共享`/lib/modules/6.18.15/`目录，模块安装会覆盖旧版本，必须做好备份。

3. **配置继承策略**：采用"从当前内核配置出发"的策略（`olddefconfig`）而非从零开始，是实际工程中的最佳实践。从头配置数千个选项不仅耗时且容易遗漏关键选项导致系统无法启动。

#### 8.4.3 与实验大纲的对应

本实验涵盖了实验大纲中"Linux内核裁剪和编译"的全部要求：内核源码分析、Kconfig配置系统、内核编译、模块管理、GRUB引导、启动验证。实现了从获取源码到启动新内核的完整闭环，为后续所有内核编程实验（实验2、7、8、9、11）奠定了环境基础。

## 九、总结及心得体会

### 9.1 实验总结

通过本实验，我完整地走通了Linux内核裁剪和编译的全流程，从获取源码、配置裁剪、编译链接到安装部署和启动验证，建立起了对操作系统内核工程化的整体认识。

在理论层面，我深入理解了Linux内核的模块化架构——arch/目录的架构适配层、kernel/目录的核心调度和进程管理、mm/目录的内存管理、fs/目录的文件系统集合、drivers/目录的庞大驱动生态——这些子系统的划分并非简单的功能归类，而是体现了操作系统设计中的分层解耦思想。Kconfig/Kbuild系统则展示了大型C语言项目如何通过自定义构建框架管理数千万行代码的编译依赖和功能组合。

在实践层面，我掌握了`make menuconfig`的配置语法和裁剪策略，理解了编译进内核(y)与编译为模块(m)的技术权衡，体验了从`.config`到`vmlinux`再到`bzImage`的完整编译链路。特别是对initrd机制的深入理解——它不仅仅是"启动时加载的一个压缩包"，而是连接内核启动和根文件系统挂载之间的关键桥梁——这个认识在后继的实验11中发挥了至关重要的作用。

### 9.2 心得体会

1. **宏内核的灵活性**：虽然Linux是宏内核架构，但通过模块机制和配置系统，它实现了相当程度的灵活性。同一个源码树可以编译出适用于服务器、桌面、嵌入式设备等完全不同的内核镜像，这种设计在工程上是极为优雅的。

2. **细节决定成败**：`CONFIG_SATA_AHCI=m`和`CONFIG_SATA_AHCI=y`虽只有一个字母之差（m vs y），但前者意味着磁盘驱动是模块、必须配合包含该模块的initrd才能启动，后者则编译进内核可独立启动。在内核配置中，这类细微差别的累积影响巨大，需要充分的系统和硬件知识支撑。

3. **版本管理的重要性**：两个同版本号但ABI不兼容的内核共存是一个实际工程问题。`CONFIG_MODVERSIONS`的模块CRC校验机制虽然保护了内核稳定性，但在开发场景中也增加了复杂度。良好的备份习惯（`/lib/modules/6.18.15.bak/`）和initrd管理是保障可回退性的关键。

4. **从用户到开发者的视角转换**：`uname -r`输出一个版本号是人人都会的操作，但真正理解这个版本号背后的数千万行代码、数小时的编译过程、复杂的模块依赖关系和多内核共存机制，才是一个操作系统学习者的专业素养所在。本实验正是这种视角转换的起点。

---

> **注**：本文档为实验报告的文字内容部分。报告中标注了需要插入截图的位置（共16处），截图需由实验者自行截取并插入对应的报告章节中。
