# 实验2：Linux系统调用分析和增加系统调用 — 操作指导

> 严格按指导书「实验二」内容编写 | 内核版本：linux-6.18.15

---

## 实验概述

在 Linux 6.18.15 内核中**添加一个自定义系统调用 `sys_my_add`**，实现两个整数相加并用 `printk` 打印内核日志 *"This is a new syscall"*，然后编写用户态程序通过 `syscall()` 调用验证。

- **学时**: 4
- **类型**: 内核编程 + 编译
- **难度**: ⭐⭐⭐（中等偏低）

---

## 原始指导书 vs Linux 6.x 差异说明 ⚠️

> **很重要**：原始指导书基于 Linux 5.x 旧内核编写，部分文件路径和修改方式在 6.x 中已改变。以下是关键差异对照表：

| 指导书要求修改的文件 | 旧内核(5.x) 做法 | Linux 6.18.15 做法 |
|---|---|---|
| `arch/x86/include/uapi/asm/unistd_64.h` | 手动编辑，添加 `#define __NR_my_add 450` 和 `__SYSCALL(__NR_my_add, sys_my_add)` | **该文件在6.x中不存在**，编译时从 `.tbl` 自动生成到 `arch/x86/include/generated/uapi/asm/unistd_64.h` |
| `arch/x86/entry/syscall_64.c` | 手动在 `sys_call_table[]` 数组中添加 `[__NR_my_add] = sys_my_add` | **不需要手动编辑**，该文件通过 `#include <asm/syscalls_64.h>` 引用自动生成的表项 |
| `include/uapi/asm-generic/unistd.h` | 更新 `__NR_syscalls` | 同样需要更新 `__NR_syscalls`（相同） |
| `arch/x86/entry/syscalls/syscall_64.tbl` | 指导书未提及（旧内核无此文件或不依赖它） | **6.x 的核心入口**：只需编辑这一个 `.tbl` 文件即可驱动上述所有文件的自动生成 |
| `kernel/sys.c` | 使用 `asmlinkage long sys_my_add(int a, int b)` | 使用现代宏 `SYSCALL_DEFINE2(my_add, int, a, int, b)`（推荐） |

> **报告处理建议**：在实验报告中，按指导书要求描述 `unistd_64.h` 和 `syscall_64.c` 的作用和修改方法（原理部分），但实际操作截图展示 `.tbl` 文件和 `kernel/sys.c` 的修改（操作部分）。

---

## 需要修改的内核文件（实际操作）

| # | 文件 | 修改内容 |
|---|------|---------|
| 1 | `arch/x86/entry/syscalls/syscall_64.tbl` | 添加 syscall 表项 `470 common my_add sys_my_add` |
| 2 | `kernel/sys.c` | 添加 `sys_my_add()` 实现（`SYSCALL_DEFINE2` 宏） |
| 3 | `include/uapi/asm-generic/unistd.h` | 更新系统调用总数 `__NR_syscalls 470 → 471` |

---

## 系统调用实现涉及的关键文件（用于报告原理部分）

| 文件（指导书路径） | 6.18.15 实际路径 | 作用 |
|---|---|---|
| `arch/x86/include/uapi/asm/unistd_64.h` | `arch/x86/include/generated/uapi/asm/unistd_64.h`（自动生成） | x86_64 系统调用号宏定义 `__NR_xxx` |
| `arch/x86/entry/syscall_64.c` | `arch/x86/entry/syscall_64.c` | 系统调用表 `sys_call_table[]` 和分发函数 `x64_sys_call()` |
| `arch/x86/entry/syscalls/syscall_64.tbl` | 同左 | **系统调用表源文件**，编译时生成上述两个文件 |
| `arch/x86/entry/entry_64.S` | 同左 | 汇编入口 `system_call`，保存/恢复现场 |
| `include/uapi/asm-generic/unistd.h` | 同左 | 通用系统调用号范围上限 |
| `kernel/sys.c` | 同左 | 系统调用实现函数（在此添加自定义函数） |

---

## 步骤

### 1. 实验前准备

确认实验1编译的内核环境可用：

```bash
uname -r                    # 应输出: 6.18.15
ls /usr/src/linux-6.18.15/kernel/sys.c
ls /usr/src/linux-6.18.15/arch/x86/entry/syscalls/syscall_64.tbl
```

本实验在**实验1已编译成功的内核源码树**基础上做增量修改。

---

### 2. 修改内核源码 🖼️

#### 2.1 确定系统调用号

查看当前系统调用表末尾，确认可用的系统调用号：

```bash
# 查看最后一个 common 类型的系统调用号
grep -E '^[0-9]+' /usr/src/linux-6.18.15/arch/x86/entry/syscalls/syscall_64.tbl | \
  awk -F'\t' '$2=="common"' | tail -1
```

当前 Linux 6.18.15 最后一个 common 系统调用号为 **469** (`file_setattr`)，我们使用 **470**。

#### 2.2 添加系统调用表项

编辑 `/usr/src/linux-6.18.15/arch/x86/entry/syscalls/syscall_64.tbl`：

```bash
sudo vim /usr/src/linux-6.18.15/arch/x86/entry/syscalls/syscall_64.tbl
```

在 469 那行之后、`# This is the end...` 注释之前添加：

```
470	common	my_add		sys_my_add
```

> 格式说明：`<号>  <ABI类型>  <调用名>  <内核函数名>`，列之间用 **Tab** 分隔。

📸 **截图1**：修改后的 `syscall_64.tbl`（显示新增行上下文）

#### 2.3 添加系统调用实现函数

编辑 `/usr/src/linux-6.18.15/kernel/sys.c`：

```bash
sudo vim /usr/src/linux-6.18.15/kernel/sys.c
```

在文件末尾添加：

```c
/*
 * 自定义系统调用：返回两个整数的和
 * 同时 printk 输出 "This is a new syscall" 到内核日志
 */
SYSCALL_DEFINE2(my_add, int, a, int, b)
{
    printk(KERN_INFO "This is a new syscall: my_add(%d, %d) called\n", a, b);

    /* 参数合法性检查 */
    if (a < 0 || b < 0)
        return -EINVAL;

    return (long)(a + b);
}
```

> 注：指导书原要求只打印 `"This is a new syscall"`，此处额外打印了参数信息便于调试。

📸 **截图2**：修改后的 `kernel/sys.c`（显示新增函数上下文）

#### 2.4 更新系统调用总数

编辑 `/usr/src/linux-6.18.15/include/uapi/asm-generic/unistd.h`：

```bash
sudo vim /usr/src/linux-6.18.15/include/uapi/asm-generic/unistd.h
```

找到 `#define __NR_syscalls`（约第 862 行），将值从 `470` 改为 `471`：

```c
#define __NR_syscalls 471
```

📸 **截图3**：修改后的 `unistd.h`（显示 `__NR_syscalls` 行及上下文）

---

### 3. 重新编译内核 🖼️

由于只修改了少量文件，增量编译很快（几分钟内完成）。

```bash
cd /usr/src/linux-6.18.15
sudo make -j$(nproc)
```

编译过程中会自动从 `.tbl` 文件生成：
- `arch/x86/include/generated/uapi/asm/unistd_64.h`（含 `#define __NR_my_add 470`）
- `arch/x86/include/generated/asm/syscalls_64.h`（含 `__SYSCALL(470, sys_my_add)`）

```bash
sudo make modules_install
sudo make install
sudo update-grub
```

#### 3.1 验证自动生成的头文件

编译完成后，验证系统调用号已经自动生成：

```bash
grep "my_add" /usr/src/linux-6.18.15/arch/x86/include/generated/uapi/asm/unistd_64.h
# 应输出: #define __NR_my_add 470

grep "my_add" /usr/src/linux-6.18.15/arch/x86/include/generated/asm/syscalls_64.h
# 应输出: __SYSCALL(470, sys_my_add)
```

📸 **截图4**：编译完成（无错误）
📸 **截图5**：`make install` 和 `update-grub` 完成

---

### 4. 重启进新内核并验证 🖼️

```bash
sudo reboot
```

启动后在 GRUB 菜单选择新内核（6.18.15），进入系统后验证：

```bash
uname -r
# 应输出: 6.18.15
```

📸 **截图6**：`uname -r` 输出

---

### 5. 编写并运行用户态测试程序 🖼️

#### 5.1 测试程序源码

文件：`exp2_test_mycall.c`（已写好，放在本目录下）

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>

#define __NR_my_add 470

int main(void)
{
    int a = 10, b = 20;
    long result;

    printf("=== 实验2：自定义系统调用测试 ===\n");
    printf("调用 sys_my_add(%d, %d)\n", a, b);

    result = syscall(__NR_my_add, a, b);

    if (result < 0) {
        fprintf(stderr, "系统调用失败: %s (errno=%d)\n",
            strerror(errno), errno);
        return EXIT_FAILURE;
    }

    printf("返回值: %d + %d = %ld\n", a, b, result);
    printf("测试通过！请执行 dmesg | tail -5 查看内核日志输出。\n");

    return EXIT_SUCCESS;
}
```

📸 **截图7**：测试程序源码

#### 5.2 编译并运行

```bash
gcc exp2_test_mycall.c -o test_mycall
./test_mycall
```

预期输出：
```
=== 实验2：自定义系统调用测试 ===
调用 sys_my_add(10, 20)
返回值: 10 + 20 = 30
测试通过！请执行 dmesg | tail -5 查看内核日志输出。
```

📸 **截图8**：运行测试程序输出

#### 5.3 查看内核日志

```bash
dmesg | tail -5
# 或
sudo dmesg | grep "new syscall"
```

预期输出：
```
This is a new syscall: my_add(10, 20) called
```

📸 **截图9**：`dmesg` 输出显示 "This is a new syscall"

---

## 截图清单（共9张）

| # | 内容 | 对应步骤 |
|---|------|---------|
| 1 | 修改后的 `syscall_64.tbl`（新增行上下文） | 2.2 |
| 2 | 修改后的 `kernel/sys.c`（新增函数） | 2.3 |
| 3 | 修改后的 `unistd.h`（`__NR_syscalls 471`） | 2.4 |
| 4 | `make` 编译完成 | 3 |
| 5 | `make install` + `update-grub` 完成 | 3 |
| 6 | `uname -r` 验证新内核 | 4 |
| 7 | 用户测试程序源码 | 5.1 |
| 8 | `./test_mycall` 运行输出（10 + 20 = 30） | 5.2 |
| 9 | `dmesg` 输出 "This is a new syscall" | 5.3 |

---

## 报告撰写要点

### 实验原理部分（需覆盖）

1. **系统调用完整流程**：触发 → 切换 → 处理 → 返回（参考指导书原文）
2. **关键数据结构与代码位置**（见上表 "系统调用实现涉及的关键文件"）
3. **x86_64 架构特点**：`syscall`/`sysretq` 指令替代 `int 0x80`，寄存器传参（rdi/rsi/rdx 等）

### 实验步骤部分（按指导书框架）

指导书写的是4个步骤（定义函数 → 分配号 → 更新表 → 编译），实际操作按本指南的2~5步。报告中按指导书框架写即可，在对应步骤中说明 Linux 6.x 与旧内核的差异。

---

## 本目录文件

| 文件 | 说明 |
|------|------|
| `exp2_test_mycall.c` | 用户态测试程序（可直接编译运行） |
| `exp2_syscall_ref.c` | 添加到 kernel/sys.c 的参考代码（不可单独编译） |
| `EXP2_GUIDE.md` | 本文件 |

---

## 常见问题

**Q: 编译时提示 `sys_my_add` 未定义？**
检查 `syscall_64.tbl` 表项格式是否正确，注意列之间用 **Tab** 分隔（不是空格）。

**Q: 运行测试程序返回 `-38`（Function not implemented）？**
说明新内核未正确加载。确认 `uname -r` 显示 6.18.15，确认 `syscall_64.tbl` 中已添加表项，确认编译使用了 `sudo make`。

**Q: dmesg 中看不到内核日志？**
使用 `sudo dmesg`；系统日志级别可能过滤了 `KERN_INFO` 级别消息，尝试 `dmesg --level=info`。

**Q: 编译时自动生成的文件在哪里？**
- `arch/x86/include/generated/uapi/asm/unistd_64.h` — `#define __NR_my_add 470`
- `arch/x86/include/generated/asm/syscalls_64.h` — `__SYSCALL(470, sys_my_add)`
用 `grep "my_add" <路径>` 验证即可。
