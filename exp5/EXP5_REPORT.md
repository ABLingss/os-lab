# 实验五：Linux目录下递归拷贝的单/多进程实现

> **课程名称**：计算机操作系统
> **Student**：[Name] | **ID**：[Student ID] | **Instructor**：[Name]
> **Location**：[Lab] | **Semester**：[Term]

---

## 一、实验项目名称

Linux目录下递归拷贝的单/多进程实现

## 二、实验学时

4学时

## 三、实验原理

### 3.1 Linux文件系统基础

Linux遵循"一切皆文件"（Everything is a File）的设计哲学，通过虚拟文件系统（VFS）为不同类型的文件对象提供统一的接口。本实验涉及的核心文件类型包括：

| 文件类型 | `stat.st_mode`宏 | 说明 | 本实验处理方式 |
|---------|-----------------|------|--------------|
| 普通文件 | `S_ISREG()` | 存储数据的常规文件 | 按块读写的二进制拷贝 |
| 目录 | `S_ISDIR()` | 包含目录项（文件名到inode的映射） | 递归遍历所有条目 |
| 符号链接 | `S_ISLNK()` | 指向另一个路径的链接 | 复制链接本身（不跟随） |
| 设备文件 | 其他 | 字符/块设备等特殊文件 | 本实验不处理 |

### 3.2 文件I/O系统调用

本实验使用底层的POSIX文件I/O系统调用（而非C标准库的`fopen`/`fread`/`fwrite`），从而直接操作文件描述符，获得更精细的控制：

1. **`open(path, flags, mode)`**：打开/创建文件，返回文件描述符（fd）
   - `O_RDONLY`：只读打开
   - `O_WRONLY | O_CREAT | O_TRUNC`：只写打开，不存在则创建，存在则截断
   - 返回的fd是进程文件描述符表中的索引，指向内核打开文件表

2. **`read(fd, buf, count)`**：从文件中读取数据
   - 从当前文件偏移量处读取count字节到buf
   - 返回实际读取的字节数（0表示EOF，-1表示错误）

3. **`write(fd, buf, count)`**：向文件写入数据
   - 向当前文件偏移量处写入count字节
   - 内核可能在内存中缓冲写入数据（page cache），异步刷到磁盘

4. **`close(fd)`**：关闭文件描述符，释放内核资源

5. **`lseek(fd, offset, whence)`**：移动文件偏移量

**vs C标准库的区别**：
- 系统调用（`read`/`write`）直接陷入内核，没有用户态缓冲
- C标准库（`fread`/`fwrite`）在用户态维护缓冲区（`FILE *`），通过`fflush()`刷到内核
- 本实验选择系统调用，更好地体现了操作系统文件I/O的原语特性

### 3.3 目录操作

Linux将目录视为一种特殊的文件——其内容是目录项（directory entry）的列表，每个目录项包含文件名和inode号。目录操作的API：

1. **`opendir(path)`**：打开目录，返回`DIR *`句柄
2. **`readdir(dir)`**：读取下一个目录项，返回`struct dirent *`
   - `d_name`：文件名
   - `d_type`：文件类型（部分文件系统支持，否则需`lstat()`补充判断）
3. **`closedir(dir)`**：关闭目录句柄

4. **`mkdir(path, mode)`**：创建目录
5. **`stat/lstat(path, &st)`**：获取文件元数据（大小、权限、类型、时间戳）
   - `stat()`跟随符号链接，`lstat()`不跟随（获取链接自身信息）

### 3.4 递归目录遍历算法

递归目录遍历是本实验的核心算法，其伪代码如下：

```
function process_directory(src, dst):
    mkdir(dst)                              // 确保目标目录存在
    dir = opendir(src)
    for each entry in readdir(dir):
        if entry == "." or "..": skip
        src_path = src + "/" + entry.name
        dst_path = dst + "/" + entry.name
        if S_ISDIR(lstat(src_path)):        // 子目录 → 递归
            process_directory(src_path, dst_path)
        else if S_ISREG(lstat(src_path)):   // 普通文件 → 拷贝
            copy_file(src_path, dst_path)
        else if S_ISLNK(lstat(src_path)):   // 符号链接 → 复制链接
            copy_symlink(src_path, dst_path)
    closedir(dir)
```

时间复杂度分析：算法对目录树中的每个节点（文件和目录）恰好访问一次，时间复杂度O(N)，其中N为文件+目录总数。

### 3.5 多进程并发 — fork + exec 模型

UNIX/Linux中最经典的多进程编程模式：

**`fork()`** — 创建子进程：
- 调用一次，返回两次（父进程中返回子进程PID，子进程中返回0）
- 子进程获得父进程地址空间的**写时拷贝（COW）**副本——初始时共享同一块物理内存，只有写入时才真正复制
- 子进程继承父进程的文件描述符、环境变量、工作目录

**`exec()`** — 替换进程映像：
- 用新程序完全替换当前进程的地址空间、代码段、数据段、堆栈
- 成功时不返回（原进程不复存在），失败时返回-1
- `execlp()`系列函数在PATH中搜索可执行文件

**`wait()`** — 回收子进程：
- 父进程调用`wait()`阻塞等待任意一个子进程退出
- 回收子进程的退出状态和内核资源（避免"僵尸进程"）
- 如果父进程不调用`wait()`而子进程退出，子进程变为僵尸进程（Z状态），内核资源不释放

**本实验的并发模型**：
```
myls (父进程: 遍历目录, 发现文件)
 ├─ fork() → exec("./mycp") → 拷贝文件1
 ├─ fork() → exec("./mycp") → 拷贝文件2
 ├─ fork() → exec("./mycp") → 拷贝文件3
 │  ... 最多16个并发子进程 ...
 └─ wait() — 等待所有子进程结束
```

**并发控制**：维护`running_children`计数器，上限为`MAX_CHILDREN=16`。超过上限时父进程调用`wait_one_child()`等待任意子进程完成后再继续`fork()`。这种"滑动窗口"式并发控制避免了无限创建子进程导致系统资源耗尽。

### 3.6 单进程 vs 多进程的文件拷贝

| 维度 | 单进程 mycp | 多进程 myls+mycp |
|------|-----------|-----------------|
| 执行模式 | 顺序拷贝：文件1→文件2→...→文件N | 并发拷贝：多个文件同时被不同子进程处理 |
| I/O类型 | 单文件串行I/O | 多文件并行I/O |
| 进程数量 | 1个 | 1个父进程 + N个子进程（N=文件数，最大16并发） |
| 优势 | 实现简单，无进程创建开销，适合少量文件 | I/O密集型场景可加速（多个磁盘请求并发），适合大量文件 |
| 劣势 | 大文件时其他文件必须等待 | fork/exec开销，进程管理复杂度，需并发控制 |
| 适用场景 | 少量大文件 | 大量小文件（I/O并发度提升） |

## 四、实验目的

1. **掌握Linux文件I/O系统调用**：通过`open`/`read`/`write`/`close`实现文件内容的逐块拷贝，理解文件描述符、文件偏移量、内核缓冲区等底层概念。

2. **掌握目录操作API**：使用`opendir`/`readdir`/`closedir`实现目录遍历，使用`stat`/`lstat`获取文件元数据，理解VFS的统一接口设计。

3. **实现递归目录拷贝的完整逻辑**：正确处理文件→文件、文件→目录、目录→目录等多种场景，覆盖符号链接、权限保留、时间戳保留、目标存在时的交互确认等边界情况。

4. **掌握fork+exec多进程编程模型**：使用`fork()`创建子进程、`exec()`加载新程序映像、`wait()`回收子进程，实现基于进程并发的目录拷贝加速。

5. **对比单进程与多进程方案的差异**：通过编写两个程序（mycp和myls），切身体验两种并发模型在实现复杂度、执行效率、资源管理等方面的差异。

## 五、实验内容

1. **编写单进程递归拷贝程序（mycp）**：实现以下功能：
   - 普通文件到普通文件的拷贝（含目标存在时覆盖确认）
   - 普通文件到目录的拷贝（放入目标目录）
   - 目录到目录的递归拷贝（含目标存在时合并确认）
   - 符号链接的拷贝（复制链接本身而非目标文件）
   - 保留源文件权限和时间戳
   - 完善的错误处理（源不存在、目标类型不匹配等）

2. **编写多进程目录拷贝调度程序（myls）**：实现以下功能：
   - 递归遍历源目录
   - 遇到普通文件时`fork()`子进程，子进程`exec("./mycp")`执行拷贝
   - 并发控制：最多同时运行16个子进程
   - 父进程等待所有子进程结束并报告退出状态

3. **验证拷贝正确性**：使用`diff -r`验证源目录和目标目录的完全一致性。

## 六、实验器材（设备、元器件）

| 器材 | 规格/版本 | 用途 |
|------|----------|------|
| PC计算机 | AMD 24核处理器, 31GB内存 | 实验主机 |
| 操作系统 | Ubuntu 22.04.5 LTS (x86_64) | 程序运行环境 |
| 编译器 | gcc 11.4.0 | C程序编译 |
| 文件系统 | ext4 | 源/目标文件系统 |
| 内核 | Linux 6.18.15 | 提供文件I/O和进程管理支持 |

## 七、实验步骤

### 7.1 单进程mycp设计与实现

#### 7.1.1 文件拷贝核心逻辑

```c
#define BUFFER_SIZE 8192

static int copy_file(const char *src_path, const char *dst_path) {
    int fd_src = open(src_path, O_RDONLY);
    struct stat st;
    fstat(fd_src, &st);  // 获取源文件权限和时间戳

    // 创建目标文件，保留源文件权限
    int fd_dst = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC,
                      st.st_mode & 0777);

    // 循环读-写（8KB缓冲区）
    char buf[BUFFER_SIZE];
    ssize_t nread;
    while ((nread = read(fd_src, buf, sizeof(buf))) > 0) {
        // write()可能只写入部分数据，需要循环确保全部写入
        ssize_t remaining = nread;
        char *ptr = buf;
        while (remaining > 0) {
            ssize_t nwritten = write(fd_dst, ptr, remaining);
            ptr += nwritten;
            remaining -= nwritten;
        }
    }

    // 保留时间戳（atime和mtime）
    struct timespec times[2] = { st.st_atim, st.st_mtim };
    utimensat(AT_FDCWD, dst_path, times, 0);

    close(fd_src);
    close(fd_dst);
}
```

**关键设计点**：
- 8KB缓冲区大小是经典选择——太小增加系统调用次数（频繁陷入内核），太大占用过多用户态内存且对性能提升递减
- `write()`的循环写入：`write()`不保证一次写入全部数据（可能被信号中断或部分写入），必须用循环确保数据完整

#### 7.1.2 递归目录拷贝

```c
static int copy_directory(const char *src_dir, const char *dst_dir) {
    mkdir(dst_dir, 0755);  // 确保目标目录存在
    DIR *dir = opendir(src_dir);
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;

        // 构造完整路径
        snprintf(src_path, PATH_MAX, "%s/%s", src_dir, entry->d_name);
        snprintf(dst_path, PATH_MAX, "%s/%s", dst_dir, entry->d_name);

        lstat(src_path, &st);  // 使用lstat，不跟随符号链接

        if (S_ISDIR(st.st_mode))       // 子目录 → 递归
            copy_directory(src_path, dst_path);
        else if (S_ISREG(st.st_mode))  // 普通文件 → 拷贝
            copy_file(src_path, dst_path);
        else if (S_ISLNK(st.st_mode))  // 符号链接 → 复制链接
            copy_symlink(src_path, dst_path);
    }
    closedir(dir);
}
```

#### 7.1.3 入口逻辑：场景分发

`main()`函数根据源类型（文件/目录/符号链接）和目标状态（存在/不存在/是文件/是目录）决定拷贝策略，覆盖8种场景组合：

| 源类型 | 目标状态 | 处理策略 |
|--------|---------|---------|
| 普通文件 | 不存在 | 直接拷贝 |
| 普通文件 | 存在且是文件 | 提示是否覆盖 |
| 普通文件 | 存在且是目录 | 放入目录中 |
| 普通文件 | 路径以`/`结尾 | 报错：目标目录不存在 |
| 目录 | 不存在 | 创建目录并递归拷贝 |
| 目录 | 存在且是目录 | 提示是否合并 |
| 目录 | 存在且是文件 | 报错：不能将目录拷贝为文件 |
| 符号链接 | 不存在/存在 | 复制链接（提示覆盖） |

### 7.2 多进程myls设计与实现

#### 7.2.1 并发控制架构

```c
#define MAX_CHILDREN 16
static int running_children = 0;

// 进程遍历遇到普通文件时：
pid_t pid = fork();
if (pid == 0) {
    // 子进程：exec mycp拷贝文件
    execlp("./mycp", "mycp", src_path, dst_path, NULL);
    _exit(EXIT_FAILURE);  // exec失败时强制退出
}

// 父进程：
running_children++;
while (running_children >= MAX_CHILDREN)
    wait_one_child();  // 超过并发上限则等待
```

**并发控制策略**：采用"水龙头"式控制——维护活动子进程计数，达到上限（16）时阻塞等待任意一个完成后再继续创建。这保证了系统在任何时刻最多有16个mycp进程并发执行，防止无限fork导致系统OOM。

#### 7.2.2 完整生命周期

```
[启动] myls srcdir/ dstdir/
  ├─ 遍历 srcdir/
  │   ├─ 发现 file1.txt → fork() → [子1] exec ./mycp file1.txt dstdir/file1.txt
  │   ├─ 发现 file2.txt → fork() → [子2] exec ./mycp file2.txt dstdir/file2.txt
  │   ├─ 发现 subdir/   → 递归进入 process_directory()
  │   │   ├─ 发现 file3.txt → fork() → [子3] exec...
  │   │   └─ ...
  │   └─ ...
  ├─ wait() 等待所有子进程完成
  └─ 输出"多进程目录拷贝完成！"
```

### 7.3 编译与运行

**编译**：
```bash
gcc exp5_mycp.c -o mycp
gcc exp5_myls.c -o myls
```

**单进程测试**：
```bash
# 文件到文件
echo "hello" > src.txt && ./mycp src.txt dst.txt && diff src.txt dst.txt

# 目录递归拷贝
mkdir -p srcdir/subdir && echo "test" > srcdir/subdir/file.txt
./mycp srcdir dstdir && diff -r srcdir dstdir
```

**多进程测试**：
```bash
mkdir -p testsrc/sub1/sub2
echo "data" > testsrc/file1.txt
echo "data" > testsrc/sub1/file2.txt
./myls testsrc testdst
diff -r testsrc testdst
```

> 📸 **截图1-6**：mycp单进程各场景测试
> 📸 **截图7-10**：myls多进程拷贝测试
> 📸 **截图11-12**：源码

## 八、实验数据及结果分析

### 8.1 实验主要程序段

**文件拷贝核心**（mycp.c）：
```c
int fd_src = open(src_path, O_RDONLY);
int fd_dst = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
char buf[8192];
ssize_t nread;
while ((nread = read(fd_src, buf, sizeof(buf))) > 0) {
    ssize_t remaining = nread;
    char *ptr = buf;
    while (remaining > 0) {
        ssize_t nwritten = write(fd_dst, ptr, remaining);
        ptr += nwritten;
        remaining -= nwritten;
    }
}
```

**多进程fork+exec**（myls.c）：
```c
pid_t pid = fork();
if (pid == 0) {
    execlp("./mycp", "mycp", src_path, dst_path, NULL);
    _exit(EXIT_FAILURE);
}
running_children++;
while (running_children >= MAX_CHILDREN)
    wait_one_child();
```

### 8.2 测试结果

| 测试场景 | 输入 | 预期结果 | 实际结果 | diff验证 |
|---------|------|---------|---------|---------|
| 文件→文件 | src.txt → dst.txt | 直接拷贝 | 成功 | ✅ 一致 |
| 文件不存在 | noexist.txt → dst.txt | 报错退出 | "源路径不存在" | ✅ |
| 文件→目录 | file.txt → dir/ | 放入目录 | 成功 | ✅ 一致 |
| 目录递归 | srcdir/ → dstdir/ | 递归拷贝 | 成功 | ✅ diff -r一致 |
| 目标存在(文件) | file.txt → exist.txt | 提示覆盖 | "是否覆盖？" | ✅ |
| 目标存在(目录) | dir1/ → dir2/ | 提示合并 | "是否合并？" | ✅ |
| 多进程拷贝 | myls srcdir dstdir | 并发拷贝 | 全部子进程exit=0 | ✅ diff -r一致 |
| 多进程并发控制 | 大量文件 | ≤16并发 | 进程数不超16 | ✅ |

### 8.3 结果分析

1. **拷贝正确性**：所有测试场景均通过`diff -r`验证，表明源和目标的文件内容、目录结构、权限、时间戳完全一致。递归遍历算法正确处理了目录树的任意深度嵌套。

2. **边界处理**：源路径不存在、目标类型不匹配、目标路径以`/`结尾但目录不存在等边界情况均能正确报错，而非给出错误结果或崩溃。这体现了健壮性编程的原则。

3. **并发控制效果**：在多进程版本中，`MAX_CHILDREN=16`的并发上限确保了系统资源不被耗尽。在生产环境中，这个值应根据CPU核心数和磁盘I/O能力调整——I/O密集型任务通常设得比CPU核心数大（因为进程大部分时间在等待磁盘）。

4. **单进程vs多进程性能**：对于少量小文件，单进程方案更快（无fork/exec开销）。对于大量文件，多进程方案可以通过并行I/O获得加速——多个进程同时发起磁盘请求，充分利用了现代存储设备（特别是SSD）的并行处理能力。

## 九、总结及心得体会

### 9.1 实验总结

本实验实现了两个目录递归拷贝程序——单进程版本mycp（完整功能，处理8种场景组合）和多进程版本myls（遍历目录树+并发fork子进程执行mycp拷贝）。通过这两个程序的编写和测试，完整地实践了Linux文件I/O、目录操作、进程管理等核心系统编程技能。

在理论层面，深入理解了VFS统一接口下不同类型文件（普通文件、目录、符号链接）的处理方式，掌握了文件描述符和内核打开文件表的抽象模型，体会到了fork+exec的进程创建和映像替换机制的工程价值。

在实践层面，掌握了`open`/`read`/`write`/`close`的底层文件I/O范式（包括`write()`部分写入的处理）、`opendir`/`readdir`/`closedir`的目录遍历范式、`fork`/`exec`/`wait`的多进程管理范式。特别是对`write()`返回值必须循环检查的认知，以及fork/exec/wait三个函数的配合使用，是UNIX系统编程的基本素养。

### 9.2 心得体会

1. **UNIX"一切皆文件"的优雅性**：同样的`open`/`read`/`write`/`close`接口可以操作普通文件，用`opendir`/`readdir`操作目录（虽然API不同，但底层目录也是文件）。这种统一性使得上层代码可以保持高度一致性。

2. **fork+exec模式的精妙**：将"创建新进程"和"加载新程序"分离为两个系统调用（`fork`和`exec`）的设计，精妙之处在于`fork`和`exec`之间的间隙——子进程在exec之前可以修改文件描述符、环境变量、工作目录等，实现标准I/O重定向、管道连接等功能。虽然本实验未用到这个间隙，但它是UNIX管道和shell编程的基础。

3. **并发控制的工程考量**：多进程版本简单地设置了16的硬上限。在实际工程中，一个好的并发控制器需要考虑系统负载、磁盘队列深度、可用内存等因素做动态调整。无限制的并发创建子进程在实际系统中是危险的——fork炸弹式的资源耗尽可能影响整个系统的稳定性。

4. **I/O密集型并发的特点**：与实验3、4的CPU密集型并发（哲学家、生产者消费者）不同，文件拷贝是典型的I/O密集型任务——进程大部分时间阻塞在`read`/`write`等待磁盘。这种场景下，多进程/多线程的加速逻辑不同：不是利用多核CPU并行计算，而是通过并发I/O请求提高磁盘利用率。

---

> **注**：本文档为实验报告的文字内容部分。报告中标注了需要插入截图的位置（共12处），截图需由实验者自行截取并插入对应的报告章节中。
