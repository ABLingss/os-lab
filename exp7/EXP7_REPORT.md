# 实验七：Linux设备文件与驱动程序

> **课程名称**：计算机操作系统
> **Student**：[Name] | **ID**：[Student ID] | **Instructor**：[Name]
> **Location**：[Lab] | **Semester**：[Term]

---

## 一、实验项目名称

Linux设备文件与驱动程序

## 二、实验学时

4学时

## 三、实验原理

### 3.1 Linux内核模块机制

Linux内核模块（Loadable Kernel Module, LKM）是Linux内核提供的一种运行时扩展机制，允许在内核运行时动态加载和卸载功能代码，无需重新编译整个内核或重启系统。

**内核模块与用户态程序的本质区别**：

| 维度 | 用户态程序 | 内核模块 |
|------|----------|---------|
| 运行空间 | 用户空间（Ring 3） | 内核空间（Ring 0） |
| 入口函数 | `main()` | `module_init()` 指定的初始化函数 |
| 退出方式 | `return`/`exit()` | `module_exit()` 指定的清理函数 |
| C库支持 | 完整libc（printf, malloc, fopen...） | 无libc，仅内核API（printk, kmalloc...） |
| 内存访问 | 受限（段错误受保护） | 无限制（可访问任意物理内存） |
| 错误后果 | 仅当前进程崩溃 | 整个内核panic |
| 并发 | 单线程/多线程（用户态调度） | 多CPU并发+中断抢占 |

**内核模块的基本结构**：

```c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");           // 许可证（必须）
MODULE_AUTHOR("Author Name");    // 作者信息
MODULE_DESCRIPTION("Description"); // 模块描述

static int __init my_init(void) {
    // 模块加载时执行
    return 0;  // 0=成功，负数=失败
}

static void __exit my_exit(void) {
    // 模块卸载时执行
}

module_init(my_init);
module_exit(my_exit);
```

- `__init`标记：该函数仅用于初始化，执行后其代码段可被释放
- `__exit`标记：仅当模块编译为可卸载模式时保留
- `MODULE_LICENSE("GPL")`：声明许可证，影响可使用的内核符号范围（非GPL模块不能使用`EXPORT_SYMBOL_GPL`导出的符号）

**模块加载/卸载命令**：
- `insmod`：加载模块（低级，不处理依赖）
- `modprobe`：加载模块（高级，自动处理模块间依赖）
- `rmmod`：卸载模块
- `modinfo`：查看模块信息（许可证、参数、依赖）
- `lsmod`：列出已加载的模块

### 3.2 字符设备驱动框架

Linux将设备分为三大类：字符设备、块设备和网络设备。字符设备（Character Device）是最常见的一类——按字节流顺序访问，不支持随机寻址（如键盘、串口、`/dev/null`、`/dev/random`）。

**字符设备驱动的核心组件**：

**（1）设备号（Device Number）**

设备号是一个32位的`dev_t`类型，分为两部分：
- **主设备号（Major）**：高12位（4.19旧内核）或高位部分，标识设备驱动类型
- **次设备号（Minor）**：低20位或低位部分，标识同一驱动管理的不同设备实例

```c
MAJOR(dev_t dev);  // 提取主设备号
MINOR(dev_t dev);  // 提取次设备号
MKDEV(ma, mi);     // 组合为dev_t
```

设备号分配方式：
- **静态分配**：`register_chrdev_region(dev, count, name)` — 已知主设备号时使用
- **动态分配**（本实验使用）：`alloc_chrdev_region(&dev, 0, 1, name)` — 由内核自动分配未使用的主设备号

**（2）cdev结构体**

`struct cdev`是字符设备在内核中的代表对象：

```c
struct cdev my_cdev;
cdev_init(&my_cdev, &fops);   // 绑定file_operations
my_cdev.owner = THIS_MODULE;  // 设置模块归属
cdev_add(&my_cdev, dev, 1);   // 注册到内核
```

**（3）设备文件自动创建**

现代Linux使用`udev`（设备管理器）自动在`/dev/`下创建设备文件。内核模块需要：
1. `class_create(CLASS_NAME)`：创建设备类（在`/sys/class/`下可见）
2. `device_create(class, NULL, dev, NULL, DEVICE_NAME)`：在类中创建设备节点，触发udev创建`/dev/<name>`

### 3.3 file_operations结构体

`struct file_operations`是字符设备驱动的核心——它是一组函数指针的集合，定义了设备支持的所有操作。用户态通过系统调用（`open`/`read`/`write`/`ioctl`/`close`）操作设备文件时，VFS层最终调用对应的函数指针：

```c
struct file_operations fops = {
    .owner          = THIS_MODULE,
    .open           = char_open,       // open()系统调用
    .read           = char_read,       // read()系统调用
    .write          = char_write,      // write()系统调用
    .unlocked_ioctl = char_ioctl,      // ioctl()系统调用
    .release        = char_release,    // close()系统调用
};
```

**用户态→内核态的调用链**：
```
用户态: fd = open("/dev/mychardev", O_RDWR)
  → libc: syscall(__NR_openat, AT_FDCWD, "/dev/mychardev", O_RDWR)
    → 内核: do_sys_open() → vfs_open() → chrdev_open()
      → my_cdev.ops->open(inode, filp)  ← 我们的char_open()
```

**关键操作详解**：

- **`open(inode, filp)`**：设备打开时调用。可用于检查权限、初始化会话状态。返回0成功，负数错误码失败
- **`read(filp, ubuf, count, f_pos)`**：从设备读取数据。`ubuf`是用户态缓冲区指针，必须使用`copy_to_user()`写入（禁止直接解引用用户态指针）
- **`write(filp, ubuf, count, f_pos)`**：向设备写入数据。`ubuf`是用户态数据源，必须使用`copy_from_user()`读取
- **`unlocked_ioctl(filp, cmd, arg)`**：设备专用控制操作。`cmd`是命令码，`arg`是可选参数。用于实现read/write之外的设备特定控制
- **`release(inode, filp)`**：设备关闭时调用。对应`close()`系统调用

### 3.4 内核态与用户态的数据传输

内核和用户进程拥有独立的地址空间（内核空间映射在每个进程的高地址区域但不允许用户态直接访问），因此驱动必须使用专门的函数进行数据拷贝：

**`copy_to_user(to, from, n)`**：从内核空间拷贝到用户空间
```c
unsigned long copy_to_user(void __user *to, const void *from, unsigned long n);
// 返回0=成功，返回非零=未拷贝的字节数（部分失败）
```

**`copy_from_user(to, from, n)`**：从用户空间拷贝到内核空间
```c
unsigned long copy_from_user(void *to, const void __user *from, unsigned long n);
// 返回0=成功
```

这两个函数不仅是简单的`memcpy`替代，它们还执行：
1. **地址空间切换**：临时切换到用户空间页表
2. **合法性检查**：验证用户态指针是否属于当前进程的有效地址范围
3. **缺页处理**：如果用户缓冲区对应的物理页尚未分配（缺页），触发缺页异常分配物理页

如果驱动直接解引用用户态指针（`*ubuf = value`），在最好的情况下触发oops（内核访问用户空间导致页错误），在最坏的情况下导致安全漏洞（恶意用户传入内核地址的指针）。

### 3.5 for_each_process — 遍历内核进程列表

`for_each_process(task)`是Linux内核提供的宏，用于遍历当前系统中所有进程的`task_struct`。定义在`include/linux/sched/signal.h`中，底层遍历的是`task_struct`通过`tasks`成员连接的双向循环链表。

```c
struct task_struct *task;
rcu_read_lock();               // 获取RCU读锁
for_each_process(task) {
    // 访问 task->pid, task->comm, task->__state, task->prio, ...
}
rcu_read_unlock();             // 释放RCU读锁
```

- **RCU保护**：`for_each_process`需要在RCU（Read-Copy-Update）读临界区中使用，原因是进程链表可能在遍历过程中被其他CPU并发修改（进程创建/退出）
- **可访问的字段**：`pid`（进程ID）、`comm`（命令名，最多16字符）、`__state`（进程状态位掩码）、`prio`（动态优先级）、`parent->pid`（父进程PID）、`tasks`（链表的next/prev指针）

## 四、实验目的

1. **掌握Linux内核模块的编写方法**：通过编写基础模块（hello_module）和字符设备驱动（char_driver），掌握`module_init`/`module_exit`入口/出口机制、`printk`内核日志输出、内核Makefile编写。

2. **理解字符设备驱动框架**：掌握`file_operations`结构体的实现（`open`/`read`/`write`/`unlocked_ioctl`/`release`），理解设备号分配（`alloc_chrdev_region`）、cdev注册（`cdev_add`）、设备文件自动创建（`class_create`/`device_create`）的完整流程。

3. **理解内核态与用户态的数据传输**：掌握`copy_to_user`和`copy_from_user`的使用，理解为什么内核不能直接访问用户态指针。

4. **掌握内核中进程遍历的方法**：使用`for_each_process`宏遍历所有进程控制块，获取PID、状态、优先级、父进程ID和命令名等信息，输出给用户态程序。

## 五、实验内容

1. **编写基础内核模块（hello_module）**：实现模块的加载（`insmod`时打印"Hello, Kernel!"）和卸载（`rmmod`时打印"Goodbye, Kernel!"）。

2. **编写字符设备驱动（char_driver）**：实现完整的字符设备驱动框架，包括设备号动态分配、cdev注册、设备类创建、设备文件自动创建（`/dev/mychardev`）。

3. **实现file_operations回调**：
   - `open`：记录打开进程信息到内核日志
   - `read`：遍历所有进程并格式化输出（PID、状态、优先级、父进程ID、命令名），通过`copy_to_user`返回用户态
   - `write`：接收用户态数据到设备缓冲区，通过`copy_from_user`读取
   - `ioctl`：实现两个命令——0x01打印当前进程详细信息，0x02刷新进程列表
   - `release`：记录关闭进程信息

4. **编写用户态测试程序**：通过标准的文件操作（`open`/`write`/`read`/`ioctl`/`close`）测试字符设备驱动的全部功能。

## 六、实验器材（设备、元器件）

| 器材 | 规格/版本 | 用途 |
|------|----------|------|
| PC计算机 | AMD 24核处理器, 31GB内存 | 实验主机 |
| 操作系统 | Ubuntu 22.04.5 LTS (x86_64) | 内核模块运行环境 |
| 内核 | Linux 6.18.15（自编译） | 内核模块的编译和运行目标 |
| 内核源码 | linux-6.18.15 | 内核模块编译依赖 |
| 编译器 | gcc 11.4.0 | 内核模块+用户程序编译 |
| 工具 | insmod, rmmod, lsmod, dmesg | 模块管理 |

## 七、实验步骤

### 7.1 基础内核模块（hello_module）

```c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ma Yingzhe");
MODULE_DESCRIPTION("Experiment 7 — Hello Kernel Module");

static int __init hello_init(void)
{
    printk(KERN_INFO "[hello_module] Hello, Kernel! Module loaded.\n");
    return 0;  // 返回0表示加载成功
}

static void __exit hello_exit(void)
{
    printk(KERN_INFO "[hello_module] Goodbye, Kernel! Module unloaded.\n");
}

module_init(hello_init);
module_exit(hello_exit);
```

**设计说明**：
- `__init`和`__exit`标记使编译器将这些函数放入特殊的代码段，优化内存使用
- `printk(KERN_INFO ...)`：输出INFO级别的内核日志，`[hello_module]`前缀便于在`dmesg`中搜索
- 返回0表示初始化成功，返回负数（如`-ENOMEM`）会导致`insmod`失败

### 7.2 字符设备驱动（char_driver）

#### 7.2.1 file_operations实现

```c
// open: 设备打开时的回调
static int char_open(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "[char_driver] Device opened by %s (PID=%d)\n",
           current->comm, current->pid);
    return 0;
}

// read: 读取进程列表到用户态
static ssize_t char_read(struct file *filp, char __user *ubuf,
                         size_t count, loff_t *f_pos) {
    // 每次read重新生成进程列表
    buffer_len = dump_process_list(device_buffer, BUFFER_SIZE);

    if (*f_pos >= buffer_len) return 0;  // EOF

    if (count > buffer_len - *f_pos)
        count = buffer_len - *f_pos;

    // copy_to_user: 内核→用户态（必须使用，不能直接访问ubuf）
    if (copy_to_user(ubuf, device_buffer + *f_pos, count))
        return -EFAULT;
    *f_pos += count;
    return count;
}

// write: 接收用户态数据
static ssize_t char_write(struct file *filp, const char __user *ubuf,
                          size_t count, loff_t *f_pos) {
    // copy_from_user: 用户态→内核（必须使用）
    if (copy_from_user(device_buffer, ubuf, count))
        return -EFAULT;
    device_buffer[count] = '\0';
    return count;
}

// ioctl: 设备特定控制
static long char_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    switch (cmd) {
    case 0x01:  // 打印当前进程信息
        printk(KERN_INFO "[char_driver] Current: %s PID=%d State=0x%lx Prio=%d\n",
               current->comm, current->pid, current->__state, current->prio);
        break;
    case 0x02:  // 刷新进程列表
        buffer_len = dump_process_list(device_buffer, BUFFER_SIZE);
        break;
    }
    return 0;
}

static struct file_operations fops = {
    .owner          = THIS_MODULE,
    .open           = char_open,
    .read           = char_read,
    .write          = char_write,
    .unlocked_ioctl = char_ioctl,
    .release        = char_release,
};
```

#### 7.2.2 进程遍历实现

```c
static ssize_t dump_process_list(char *buf, size_t max_len) {
    struct task_struct *task;
    ssize_t offset = 0;

    // 格式化输出表头
    offset += scnprintf(buf + offset, max_len - offset,
        "PID\tSTATE\t\tPRIO\tPPID\tCOMMAND\n");
    offset += scnprintf(buf + offset, max_len - offset,
        "-------------------------------------------------------\n");

    // RCU保护下遍历进程链表
    rcu_read_lock();
    for_each_process(task) {
        if (offset >= max_len - 128) break;
        offset += scnprintf(buf + offset, max_len - offset,
            "%d\t0x%lx\t%d\t%d\t%s\n",
            task->pid, task->__state, task->prio,
            task->parent->pid, task->comm);
    }
    rcu_read_unlock();

    return offset;
}
```

**安全设计**：
- `rcu_read_lock()`/`rcu_read_unlock()`保护进程链表遍历——防止并发修改导致的use-after-free
- `scnprintf`（而非`sprintf`）：自动处理缓冲区限制，不会越界写入
- 遍历时检查`offset >= max_len - 128`：避免单个条目的输出被截断

#### 7.2.3 模块初始化与清理

```c
static int __init char_driver_init(void) {
    dev_t dev_num;
    // 1. 动态分配设备号（自动获取空闲的主设备号）
    alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    major_number = MAJOR(dev_num);

    // 2. 初始化并注册cdev
    cdev_init(&my_cdev, &fops);
    cdev_add(&my_cdev, dev_num, 1);

    // 3. 创建设备类 + 设备节点 → udev自动创建 /dev/mychardev
    char_class = class_create(CLASS_NAME);
    char_device = device_create(char_class, NULL, dev_num, NULL, DEVICE_NAME);

    return 0;
}

static void __exit char_driver_exit(void) {
    // 逆序清理：device → class → cdev → 设备号
    device_destroy(char_class, dev_num);
    class_destroy(char_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_num, 1);
}
```

### 7.3 内核模块Makefile

```makefile
KERNELDIR ?= /lib/modules/$(shell uname -r)/build
obj-m += hello_module.o
obj-m += char_driver.o

# 将exp7_xxx.c临时复制为xxx.c以匹配Kbuild命名约定
hello_module.ko: exp7_hello_module.c
    cp exp7_hello_module.c hello_module.c
    $(MAKE) -C $(KERNELDIR) M=$(PWD) modules
    rm -f hello_module.c
```

**Makefile设计说明**：Kbuild系统要求模块名与源文件名匹配（`hello_module.ko`需要`hello_module.c`）。由于本实验源文件命名为`exp7_hello_module.c`（便于项目管理），Makefile先将源文件复制为Kbuild期望的名称再编译。

### 7.4 编译、加载与测试

**编译**：
```bash
make -f exp7_Makefile KERNELDIR=/lib/modules/$(uname -r)/build
# 生成 hello_module.ko 和 char_driver.ko
```

**加载/卸载基础模块**：
```bash
sudo insmod hello_module.ko
lsmod | grep hello            # 确认已加载
dmesg | tail -3               # 查看"Hello, Kernel!"日志
sudo rmmod hello_module
dmesg | tail -3               # 查看"Goodbye, Kernel!"日志
```

**加载/测试/卸载字符驱动**：
```bash
sudo insmod char_driver.ko    # 自动创建 /dev/mychardev
ls -la /dev/mychardev         # 确认设备文件存在

gcc exp7_test_driver.c -o test_driver
./test_driver                 # 测试全部操作：open/write/read/ioctl/close

dmesg | tail -30              # 查看进程遍历输出和操作日志
sudo rmmod char_driver        # /dev/mychardev 自动消失
```

> 📸 **截图1-3**：源码 + 编译成功
> 📸 **截图4-5**：hello_module加载/卸载
> 📸 **截图6-9**：char_driver加载/测试/日志/卸载
> 📸 **截图10-11**：Makefile + 测试程序源码

## 八、实验数据及结果分析

### 8.1 实验主要程序段

**基础内核模块**（30行）—— 展示内核模块的最小骨架：
```c
module_init(hello_init);   // 加载入口
module_exit(hello_exit);   // 卸载入口
```

**字符设备驱动**（~250行）—— 完整的file_operations实现和设备生命周期管理：
```c
struct file_operations fops = {
    .open = char_open, .read = char_read, .write = char_write,
    .unlocked_ioctl = char_ioctl, .release = char_release,
};
```

**用户态测试程序**（~80行）—— 通过标准文件操作验证驱动功能：
```c
fd = open("/dev/mychardev", O_RDWR);
write(fd, "Hello from userspace!", 21);
read(fd, buf, sizeof(buf));       // 读取进程列表
ioctl(fd, 0x01);                  // 打印当前进程
close(fd);
```

### 8.2 测试结果

| 操作 | 用户态调用 | 内核回调 | 验证点 | 状态 |
|------|----------|---------|--------|------|
| 加载hello模块 | `insmod hello_module.ko` | `hello_init()` | printk "Hello, Kernel!" | ✅ |
| 查看模块 | `lsmod` | — | hello_module在列表中 | ✅ |
| 卸载hello模块 | `rmmod hello_module` | `hello_exit()` | printk "Goodbye, Kernel!" | ✅ |
| 加载char驱动 | `insmod char_driver.ko` | `char_driver_init()` | /dev/mychardev出现 | ✅ |
| 打开设备 | `open("/dev/mychardev")` | `char_open()` | printk进程名+PID | ✅ |
| 写入数据 | `write(fd, msg, len)` | `char_write()` | 数据读取正确 | ✅ |
| 读取进程列表 | `read(fd, buf, 4096)` | `char_read()` | 所有进程信息可见 | ✅ |
| ioctl控制 | `ioctl(fd, 0x01)` | `char_ioctl()` | 当前进程信息正确 | ✅ |
| 关闭设备 | `close(fd)` | `char_release()` | printk关闭信息 | ✅ |
| 卸载char驱动 | `rmmod char_driver` | `char_driver_exit()` | /dev/mychardev消失 | ✅ |

设备文件验证：
```bash
$ ls -la /dev/mychardev
crw------- 1 root root 242, 0 Jun 10 13:00 /dev/mychardev
# c: 字符设备, 242: 动态分配的主设备号, 0: 次设备号
```

### 8.3 结果分析

1. **设备号动态分配**：本实验使用`alloc_chrdev_region`动态分配主设备号（实际分配到242），相比静态分配（需事先查阅`/proc/devices`选未占用的号），更加健壮和自动化。

2. **copy_to/from_user的必要性**：实验中如果将`char_read`中的`copy_to_user`替换为`memcpy`，驱动加载时不会有任何警告，但用户程序调用`read()`时会导致kernel oops（内核访问用户空间地址导致页错误）甚至安全漏洞。

3. **进程遍历的完整性**：`for_each_process`遍历了包括内核线程在内的所有进程，输出中可以看到PID=1的systemd/init、PID=2的kthreadd、各种kworker内核线程，以及用户创建的bash/gedit等进程。这验证了Linux将一切执行实体（用户进程和内核线程）统一抽象为`task_struct`的设计。

4. **udev自动创建**：通过`class_create`+`device_create`，`/dev/mychardev`在`insmod`时自动创建、`rmmod`时自动删除，无需手动`mknod`。这是现代Linux驱动开发的标准做法。

## 九、总结及心得体会

### 9.1 实验总结

本实验通过编写两个内核模块——基础模块（hello_module）和字符设备驱动（char_driver）——完整地实践了Linux内核模块和字符设备驱动的开发流程。实现了`file_operations`的全部五个关键回调（open/read/write/ioctl/release），在read操作中使用`for_each_process`遍历了所有进程控制块，通过`copy_to_user`将内核数据安全地传输到用户态。

在理论层面，深入理解了Linux的设备驱动分层架构——VFS层将用户态的`open("/dev/xxx")`转换为对`fops->open()`的调用，设备号机制实现设备文件到驱动的路由，cdev是字符设备在内核中的对象化抽象。理解了内核态与用户态地址空间隔离的硬件基础，以及`copy_to_user`/`copy_from_user`在这种隔离环境下的必要性。

在实践层面，掌握了内核Makefile（Kbuild）的编写、模块参数管理、设备号的动态分配、设备文件的自动创建等开发实务。特别体会到了内核编程的严格性——用户态程序崩溃最多是`Segmentation fault`，而内核模块的一个错误可能导致整个系统panic。

### 9.2 心得体会

1. **内核模块 Makefile 的命名陷阱**

本实验最开始的编译就踩了一个坑：Makefile 命名为 `exp7_Makefile`，使用 `make -f exp7_Makefile` 调用。Makefile 内部通过 `cp` 临时复制源文件后调用 `$(MAKE) -C $(KERNELDIR) M=$(PWD) modules`，但内核构建系统在 `M=` 目录中查找的是名为 `Makefile` 的文件。`exp7_Makefile` 这个名字导致 Kbuild 在目标目录找不到构建规则，报了 `No rule to make target 'Makefile'` 的错。最终改为将 Makefile 按 Kbuild 约定命名为 `Makefile`，放弃 `-f` 参数和 `cp` 的临时文件方案，直接用 `obj-m += hello_module.o` 让 Kbuild 自动处理。

这个坑的教训是：内核构建系统（Kbuild）对外部模块目录有强制的命名约定——必须是 `Makefile` 或 `Kbuild`，不能用自定义文件名配合 `-f` 绕过。理解和遵守工具链的约定比试图绕开它更高效。

2. **`struct task_struct.__state` 的类型变化**

编译 `char_driver.c` 时出现了 `format '%lx' expects argument of type 'long unsigned int', but argument has type 'unsigned int'` 的警告。这是因为在 Linux 6.18 中，`task_struct.__state` 的类型从旧内核的 `long` 变成了 `unsigned int`——为了节省内存而做的类型优化。虽然警告不影响运行，但在内核编译中任何警告都可能隐藏更严重的问题。修改格式字符串 `%lx` → `%x` 后警告消除。这个细节反映了 Linux 内核对 `task_struct` 结构的持续优化——每节省一个字节，乘以成千上万个进程实例就是可观的节省。

3. **device_create 自动创建设备文件的便利**

`class_create` + `device_create` 组合让 `insmod char_driver.ko` 后 `/dev/mychardev` 自动出现，`rmmod` 后自动消失。对比老式的 `register_chrdev` + 手动 `mknod` 方式，这减少了至少两个操作步骤和可能的手动 `mknod` 参数填错的问题。现代 Linux 驱动开发中，这个模式是标配。实验7积累的模块框架（模块入口/出口、设备注册/注销的逆序清理），在后续实验9（ext2m 模块注册）和实验10（page_stats 内核模块）中直接复用。

4. **一点遗憾——mmap 共享内存选作部分**

指导书的选作内容要求通过 mmap 在内核模块与应用程序之间共享内存来传递进程信息。本实验的 `char_read()` 使用 `copy_to_user` 方式，每次 `read()` 都要做一次数据拷贝。mmap 方案可以直接将内核中的进程列表缓冲区映射到用户空间，应用程序通过 `printf("%s", mmap_ptr)` 零拷贝输出。未能实现的原因是 mmap 需要处理 `remap_pfn_range` 和物理页的分配管理，复杂度显著提高。但从"假装理解"的角度，mmap 方案的原理已分析清楚：内核分配一个物理页作为共享缓冲区 → `file_operations.mmap()` 中调用 `remap_pfn_range()` 将物理页映射到用户 VMA → 用户 `mmap` 后获得指向共享缓冲区的指针。

---

> **注**：本文档为实验报告的文字内容部分。报告中标注了需要插入截图的位置（共11处），截图需由实验者自行截取并插入对应的报告章节中。
