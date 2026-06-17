# 实验8：Linux进程通信分析及信号量机制改进 — 操作指导

> 严格按指导书「实验八」内容编写 | 内核编程（IPC信号量）

---

## 实验概述

在 Linux 内核的 XSI 信号量机制（`ipc/sem.c`）中增加**死锁检测**和**死锁解除**功能。编写用户态程序构造死锁状态并测试新功能。

- **学时**: 4
- **类型**: 内核编程（IPC）
- **难度**: ⭐⭐⭐⭐⭐

**核心知识点**：
- Linux XSI 信号量内核实现（`ipc/sem.c`）
- 信号量等待队列与死锁检测算法
- 资源分配图与循环等待判定
- 向内核添加新 `semctl()` 命令

---

## 需要修改的内核文件

| # | 文件 | 修改内容 |
|---|------|---------|
| 1 | `ipc/sem.c` | 添加 `detect_sem_deadlock()` + `break_sem_deadlock()` 函数 |
| 2 | `ipc/sem.c` | 在 `semctl()` 中添加 `DEADCHECK` 和 `DEADBREAK` 分支 |

---

## 步骤

### 1. 修改内核源码

参考 `exp8_sem_deadlock_ref.c` 中的代码，修改 `/usr/src/linux-6.18.15/ipc/sem.c`：

1. **添加死锁检测函数** `detect_sem_deadlock()`：检查信号量等待队列中是否存在循环等待
2. **添加死锁解除函数** `break_sem_deadlock()`：找到等待进程中内存占用最小的，发送 SIGKILL
3. **在 `semctl()` 中添加两个新命令**：`DEADCHECK` 和 `DEADBREAK`

```bash
cd /usr/src/linux-6.18.15
sudo vim ipc/sem.c
```

> 📸 **截图1**：修改后的 sem.c 代码片段（detect_sem_deadlock 函数）
> 📸 **截图2**：修改后的 sem.c 代码片段（semctl 中的新分支）

### 2. 重新编译内核

```bash
cd /usr/src/linux-6.18.15
sudo make -j$(nproc)
sudo make modules_install
sudo make install
sudo update-grub
sudo reboot
```

> 📸 **截图3**：编译内核成功

### 3. 编译用户态测试程序

```bash
cd ~/os
gcc exp8_deadlock_test.c -o deadlock_test
```

> 📸 **截图4**：编译测试程序

### 4. 运行死锁测试

```bash
./deadlock_test
```

程序执行流程：
1. 创建信号量集（2个信号量，初值各为1）
2. 进程1 持有资源[0]，等待资源[1]
3. 进程2 持有资源[1]，等待资源[0]
4. 进程3 等待资源[0]
5. 父进程调用 `DEADCHECK` 检测死锁
6. 父进程调用 `DEADBREAK` 解除死锁

> 📸 **截图5**：运行测试（检测到死锁）
> 📸 **截图6**：运行测试（死锁被解除）

### 5. 查看内核日志

```bash
dmesg | grep -i deadlock
```

### 6. 查看源码

> 📸 **截图7**：测试程序源码

---

## 截图清单（共7张）

| # | 内容 | 步骤 |
|---|------|------|
| 1 | 修改后的 sem.c（死锁检测函数） | 1 |
| 2 | 修改后的 sem.c（semctl 新分支） | 1 |
| 3 | 编译内核成功 | 2 |
| 4 | 编译测试程序 | 3 |
| 5 | 运行测试 — 检测到死锁 | 4 |
| 6 | 运行测试 — 死锁被解除 | 4 |
| 7 | 测试程序源码 | 6 |

---

## 死锁检测原理

### 资源分配图
```
进程1: 持有[0] ──→ 等待[1]
进程2: 持有[1] ──→ 等待[0]
         ↑              |
         └──────────────┘  ← 循环等待！
```

### 死锁解除策略
选择等待进程中**内存占用最小**的进程发送 SIGKILL，破坏死锁的"不可抢占"条件，释放其持有的资源。

---

## 本目录文件

| 文件 | 说明 |
|------|------|
| `exp8_sem_deadlock_ref.c` | 内核 ipc/sem.c 修改参考代码 |
| `exp8_deadlock_test.c` | 用户态死锁测试程序 |
| `EXP8_GUIDE.md` | 本文件 |
