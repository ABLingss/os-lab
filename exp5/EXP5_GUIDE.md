# 实验5：Linux目录下递归拷贝的单/多进程实现 — 操作指导

> 严格按指导书「实验五」内容编写 | 用户态编程 (C/文件IO/fork)

---

## 实验概述

实现两个程序：
1. **mycp** — 单进程递归拷贝（文件↔文件、文件→目录、目录↔目录）
2. **myls** — 多进程目录遍历器，fork子进程+exec mycp并发拷贝

- **学时**: 4
- **类型**: 用户态 C 编程
- **难度**: ⭐⭐⭐

**核心知识点**：
- 文件 I/O: `open`, `read`, `write`, `close`, `lseek`
- 目录操作: `opendir`, `readdir`, `closedir`
- 文件信息: `stat`, `lstat`, `mkdir`
- 进程控制: `fork`, `exec`, `wait`
- 符号链接处理: `readlink`, `symlink`

---

## 代码文件

| 文件 | 说明 | 行数 |
|------|------|------|
| `exp5_mycp.c` | 单进程递归拷贝程序 | ~200行 |
| `exp5_myls.c` | 多进程目录遍历+拷贝调度 | ~150行 |

---

## 步骤

### 第一部分：单进程 mycp

#### 1. 编译 mycp

```bash
gcc exp5_mycp.c -o mycp
```

> 📸 **截图1**：mycp 编译成功

#### 2. 测试文件到文件拷贝

```bash
echo "Hello, OS Experiment 5!" > test_src.txt
./mycp test_src.txt test_dst.txt
diff test_src.txt test_dst.txt && echo "拷贝正确！"
```

> 📸 **截图2**：文件到文件拷贝 + diff 验证

#### 3. 测试错误提示（源文件不存在）

```bash
./mycp nonexistent.txt target.txt
# 应输出: 错误: 源路径 'nonexistent.txt' 不存在或无法访问
```

> 📸 **截图3**：错误提示

#### 4. 测试文件到目录拷贝

```bash
mkdir -p test_dir
./mycp test_src.txt test_dir/
ls -la test_dir/
```

> 📸 **截图4**：文件到目录拷贝

#### 5. 测试目录递归拷贝

```bash
mkdir -p test_srcdir/subdir
echo "file1" > test_srcdir/file1.txt
echo "file2" > test_srcdir/subdir/file2.txt
./mycp test_srcdir test_dstdir
diff -r test_srcdir test_dstdir && echo "目录递归拷贝正确！"
```

> 📸 **截图5**：目录递归拷贝 + diff -r 验证

#### 6. 测试目标存在时的交互提示

```bash
echo "different content" > test_dst.txt
./mycp test_src.txt test_dst.txt
# 提示: 目标文件已存在，是否覆盖？
```

> 📸 **截图6**：交互提示

---

### 第二部分：多进程 myls + mycp

#### 7. 编译 myls

```bash
gcc exp5_myls.c -o myls
```

> 📸 **截图7**：myls 编译成功

#### 8. 确保 mycp 在 PATH 中

```bash
# mycp 需要在当前目录下（myls 通过 ./mycp 调用）
ls -la ./mycp
```

#### 9. 运行多进程拷贝

```bash
# 准备测试目录
rm -rf test_multisrc test_multidst
mkdir -p test_multisrc/sub1/sub2
echo "data1" > test_multisrc/file1.txt
echo "data2" > test_multisrc/file2.txt
echo "data3" > test_multisrc/sub1/file3.txt
echo "data4" > test_multisrc/sub1/sub2/file4.txt

# 运行多进程拷贝
./myls test_multisrc test_multidst
```

> 📸 **截图8**：多进程拷贝运行输出（显示各子进程PID）

#### 10. 验证结果

```bash
diff -r test_multisrc test_multidst && echo "多进程拷贝验证通过！"
```

> 📸 **截图9**：diff -r 验证多进程拷贝结果

#### 11. 用 ps 查看多进程并发

在另一个终端执行（或在 myls 运行时快速执行）：

```bash
ps aux | grep -E "mycp|myls" | grep -v grep
```

> 📸 **截图10**：ps 查看并发 mycp 进程

#### 12. 查看源码

> 📸 **截图11**：mycp 源码
> 📸 **截图12**：myls 源码

---

## 截图清单（共12张）

| # | 内容 | 对应步骤 |
|---|------|---------|
| 1 | mycp 编译成功 | 1 |
| 2 | 文件到文件拷贝 + diff 验证 | 2 |
| 3 | 源文件不存在的错误提示 | 3 |
| 4 | 文件到目录拷贝 | 4 |
| 5 | 目录递归拷贝 + diff -r 验证 | 5 |
| 6 | 目标存在时的覆盖提示 | 6 |
| 7 | myls 编译成功 | 7 |
| 8 | 多进程拷贝运行输出（子进程PID） | 9 |
| 9 | diff -r 验证多进程结果 | 10 |
| 10 | ps 查看并发进程 | 11 |
| 11 | mycp 源码 | — |
| 12 | myls 源码 | — |

---

## mycp 功能清单

| 场景 | 行为 |
|------|------|
| 普通文件 → 普通文件（目标不存在） | 直接拷贝 |
| 普通文件 → 普通文件（目标存在） | 提示是否覆盖 |
| 普通文件 → 目录 | 将文件放入目录中 |
| 普通文件 → 不存在的目录（dst以/结尾） | 报错：目标目录不存在 |
| 目录 → 目录（目标不存在） | 创建目录并递归拷贝 |
| 目录 → 目录（目标存在） | 提示是否合并 |
| 目录 → 文件（目标存在且为文件） | 报错：不能将目录拷贝为文件 |
| 源路径不存在 | 报错：源路径不存在 |
| 符号链接 | 复制符号链接本身 |
| 递归子目录 | 递归处理所有子目录和文件，保留文件权限 |

---

## myls 多进程设计

```
myls (父进程)
 ├─ 遍历源目录
 ├─ 遇到普通文件 → fork() → 子进程 exec(mycp) 拷贝
 ├─ 遇到子目录 → 递归进入
 └─ 等待所有子进程结束 (wait)
```

**并发控制**: 最多同时运行 16 个子进程，超过则等待。

---

## 实验原理要点（供写报告参考）

### 单进程 vs 多进程
- **单进程**: 顺序拷贝，简单直接，总耗时 = 所有文件拷贝时间之和
- **多进程**: 并发拷贝多个文件，I/O密集型任务可获得加速（多个磁盘请求同时进行）

### fork + exec 模式
- `fork()`: 创建子进程，子进程获得父进程地址空间副本
- `exec()`: 用新程序替换子进程的地址空间
- `wait()`: 父进程回收子进程，避免僵尸进程

### 递归目录遍历
使用 `opendir()` → `readdir()` → `lstat()` 循环，遇到子目录递归进入，遇到普通文件拷贝。

---

## 本目录文件

| 文件 | 说明 |
|------|------|
| `exp5_mycp.c` | 单进程递归拷贝程序 |
| `exp5_myls.c` | 多进程目录遍历+拷贝调度程序 |
| `EXP5_GUIDE.md` | 本文件 |

---

## 清理测试文件

```bash
rm -f test_src.txt test_dst.txt test_dst2.txt
rm -rf test_dir test_srcdir test_dstdir
rm -rf test_multisrc test_multidst
rm -f philosophers producer_consumer mycp myls test_mycall
```
