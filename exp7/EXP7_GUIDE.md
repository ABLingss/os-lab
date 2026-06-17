# 实验7：Linux设备文件与驱动程序 — 操作指导

> 严格按指导书「实验七」内容编写 | 内核模块编程

---

## 实验概述

编写两个内核模块，理解 Linux 设备驱动的基本框架：
1. **hello_module** — 基础内核模块（加载/卸载 + printk）
2. **char_driver** — 字符设备驱动（file_operations + 进程遍历）

- **学时**: 4
- **类型**: 内核模块编程
- **难度**: ⭐⭐⭐⭐

**核心知识点**：
- 内核模块结构（`module_init` / `module_exit`）
- 字符设备驱动框架（`file_operations`, `cdev`, 设备号）
- 内核中遍历进程（`for_each_process`）
- 用户态与内核态数据传输（`copy_to_user`, `copy_from_user`）

---

## 代码文件

| 文件 | 说明 |
|------|------|
| `exp7_hello_module.c` | 基础内核模块 |
| `exp7_char_driver.c` | 字符设备驱动（含进程遍历） |
| `exp7_test_driver.c` | 用户态测试程序 |
| `exp7_Makefile` | 内核模块 Makefile |

---

## 步骤

### 0. 确认内核编译环境

```bash
# 确认已编译的内核源码树可用
ls /lib/modules/$(uname -r)/build
# 如果不存在，需要先编译内核（实验1已做）
```

### 1. 编译内核模块 🖼️

```bash
cd ~/os
make -f exp7_Makefile KERNELDIR=/lib/modules/$(uname -r)/build
```

> 📸 **截图1**：hello_module 源码
> 📸 **截图2**：char_driver 源码
> 📸 **截图3**：编译成功（两个 .ko 文件）

### 2. 加载基础模块

```bash
sudo insmod hello_module.ko
lsmod | grep hello
dmesg | tail -3
# 应看到: [hello_module] Hello, Kernel! Module loaded.
```

> 📸 **截图4**：insmod 加载 + lsmod 查看

### 3. 卸载基础模块

```bash
sudo rmmod hello_module
dmesg | tail -3
# 应看到: [hello_module] Goodbye, Kernel! Module unloaded.
```

> 📸 **截图5**：rmmod 卸载 + dmesg 日志

### 4. 加载字符设备驱动

```bash
sudo insmod char_driver.ko
lsmod | grep char_driver
ls -la /dev/mychardev
# 设备文件应自动创建
```

> 📸 **截图6**：insmod + /dev/mychardev 出现

### 5. 编译并运行测试程序

```bash
gcc exp7_test_driver.c -o test_driver
./test_driver
```

输出应包含所有进程的列表（PID、状态、优先级、父进程ID、命令名）。

> 📸 **截图7**：测试程序运行输出

### 6. 查看内核日志

```bash
dmesg | tail -30
# 查看 open/read/write/ioctl 对应的内核打印
```

> 📸 **截图8**：dmesg 查看进程遍历输出

### 7. 卸载字符设备驱动

```bash
sudo rmmod char_driver
# 确认 /dev/mychardev 已消失
ls /dev/mychardev 2>&1
```

> 📸 **截图9**：rmmod 卸载

### 8. Makefile + 源码

> 📸 **截图10**：Makefile
> 📸 **截图11**：测试程序源码

---

## 截图清单（共11张）

| # | 内容 | 步骤 |
|---|------|------|
| 1 | hello_module 源码 | 1 |
| 2 | char_driver 源码 | 1 |
| 3 | make 编译成功 | 1 |
| 4 | insmod + lsmod | 2 |
| 5 | rmmod + dmesg | 3 |
| 6 | insmod char_driver + /dev/mychardev | 4 |
| 7 | 测试程序运行 | 5 |
| 8 | dmesg 进程遍历输出 | 6 |
| 9 | rmmod char_driver | 7 |
| 10 | Makefile | 8 |
| 11 | 测试程序源码 | 8 |

---

## 实验原理要点（供写报告参考）

### 内核模块 vs 用户态程序
- 内核模块运行在内核空间（Ring 0），可直接访问内核函数和数据结构
- 没有 `main()`，以 `module_init()` / `module_exit()` 作为入口/出口
- 不能使用 C 标准库（libc），只能用内核提供的 API

### file_operations 结构体
字符设备驱动核心是实现 `struct file_operations`：`open`, `read`, `write`, `unlocked_ioctl`, `release`。用户态的 `open()`/`read()`/`write()`/`ioctl()` 系统调用最终调用这些函数指针。

### for_each_process 宏
遍历内核中所有进程的 `task_struct`，可获取 PID、进程状态、优先级、父进程ID、进程名等信息。

---

## 本目录文件

| 文件 | 说明 |
|------|------|
| `exp7_hello_module.c` | 基础内核模块源码 |
| `exp7_char_driver.c` | 字符设备驱动源码 |
| `exp7_test_driver.c` | 用户态测试程序 |
| `exp7_Makefile` | 内核模块 Makefile |
| `EXP7_GUIDE.md` | 本文件 |
