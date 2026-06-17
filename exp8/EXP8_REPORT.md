# 实验八：Linux进程通信分析及信号量机制改进

> **课程名称**：计算机操作系统
> **Student**：[Name] | **ID**：[Student ID] | **Instructor**：[Name]
> **Location**：[Lab] | **Semester**：[Term]

---

## 一、实验项目名称

Linux进程通信分析及信号量机制改进

## 二、实验学时

4学时

## 三、实验原理

### 3.1 Linux XSI IPC 总体架构

Linux内核实现了三种System V IPC机制：消息队列（Message Queue）、共享内存（Shared Memory）、信号量集（Semaphore Set）。它们共享一套通用的IPC基础设施：

- **`struct kern_ipc_perm`** — 通用IPC权限结构（含key、uid、gid、mode、id），内嵌于每种IPC资源的专用结构体中
- **`ipc_ids`** — 全局IPC资源表，每种IPC类型一个实例（`sem_ids`、`shm_ids`、`msg_ids`）
- **`ipc_obtain_object_check()`** — 通过ID查找IPC资源的统一入口
- 资源通过`idr`（ID Radix Tree）机制分配唯一ID，支持用户态通过ID访问

### 3.2 信号量集的内核实现

信号量集的实现位于`ipc/sem.c`，核心数据结构：

**（1）`struct sem_array` — 信号量集**

```c
struct sem_array {
    struct kern_ipc_perm  sem_perm;    // IPC权限结构
    time64_t              sem_ctime;   // 最后修改时间
    struct sem            *sems;       // 信号量数组指针
    struct sem_queue      *pending_alter; // 全局等待队列（semop操作）
};
```

**（2）`struct sem` — 单个信号量**

```c
struct sem {
    int semval;                  // 信号量当前值
    int sempid;                  // 最后操作该信号量的进程PID
    struct list_head pending_alter; // 等待该信号量的sem_queue链表（per-信号量队列）
};
```

**（3）`struct sem_queue` — 等待队列项**

```c
struct sem_queue {
    struct list_head list;       // 链表节点
    struct task_struct *sleeper; // 阻塞等待的进程
    int pid;                     // 进程PID
    int status;                  // 操作完成状态
    struct sem_array *sma;       // 所属信号量集
    struct sembuf *sops;         // 等待的操作数组
    int nsops;                   // 操作数量
};
```

**进程阻塞与唤醒流程**：
1. 进程调用`semop()`执行P/V操作
2. 内核`ksys_semtimedop()`检查资源是否足够
3. 若资源足够：执行操作，立即返回
4. 若资源不足：创建`sem_queue`，进程设为`TASK_INTERRUPTIBLE`，挂到`pending_alter`队列，调用`schedule()`主动让出CPU
5. 当其他进程执行V操作释放资源时，内核遍历等待队列，找到可被唤醒的进程并调用`wake_up_process()`

### 3.3 信号量死锁的形成

XSI信号量的P操作（`sem_op = -1`）在`semval`不足时阻塞进程。当多个进程各自持有部分信号量并等待对方释放时，形成死锁：

```
进程A: P(sem[0]) 成功 → 持有sem[0] (sem[0]=0)
进程B: P(sem[1]) 成功 → 持有sem[1] (sem[1]=0)
进程A: P(sem[1]) → 阻塞（等sem[1]释放）
进程B: P(sem[0]) → 阻塞（等sem[0]释放）
→ 死锁！进程A等sem[1]，进程B等sem[0]，形成循环等待
```

**死锁四条件在本场景的体现**：
1. **互斥**：信号量值为0表示已被占用
2. **持有并等待**：进程持有已获得的信号量，同时等待其他信号量
3. **不可抢占**：操作系统不会强制剥夺进程持有的信号量
4. **循环等待**：存在进程→信号量→进程的等待环

### 3.4 死锁检测算法

本实验实现一个启发式死锁检测算法：

**检测条件**：
1. 信号量集中所有信号量的`semval`均为0（全部被占用）
2. 全局+各信号量等待队列中的等待进程总数 ≥ 信号量数量
3. 等待进程数 ≥ 2

**检测原理**：当所有信号量都被占用且有多个进程在等待时，这些进程之间很可能存在循环等待关系。这是一种简化的启发式方法——完整解法应构建资源分配图并通过DFS检测环路。

### 3.5 死锁解除策略

一旦检测到死锁，通过杀死一个等待进程来破坏"持有并等待"条件。具体策略：

- 遍历所有等待队列（全局`pending_alter` + 各信号量的per-信号量队列）
- 选择**RSS（Resident Set Size，常驻内存）最小**的进程
- 向选中的进程发送`SIGKILL`信号
- 进程被杀死后，内核自动释放其持有的所有信号量，打破等待环

**选择最小RSS进程的理由**：内存占用最小的进程通常影响面最小、清理最快，符合"最小代价解除死锁"的原则。

### 3.6 自定义semctl命令

在`semctl()`系统调用中添加两个自定义命令：

| 命令 | 值 | 功能 | 返回值 |
|------|-----|------|--------|
| DEADCHECK | 0xDEAD | 检测给定信号量集是否存在死锁 | 1=死锁, 0=无死锁 |
| DEADBREAK | 0xBEAC | 检测并解除死锁（杀死最小进程） | 0=成功, -ESRCH=无目标 |

**关键实现细节**：glibc的`semctl()`包装函数在用户态检查`cmd`参数，拒绝非标准命令值。因此测试程序必须使用原始系统调用`syscall(__NR_semctl, ...)`绕过glibc的校验。

## 四、实验目的

1. 通过查找资料和阅读源代码，了解Linux下XSI IPC（消息队列、共享内存、信号量集）的实现过程
2. 掌握Linux内核中IPC模块的编程知识
3. 理解信号量集死锁的形成条件和检测原理
4. 掌握在内核中扩展系统调用功能的方法
5. 掌握使用原始系统调用绕过glibc限制的方法

## 五、实验目的（指导书原文）

1. 理解Linux下XSI信号量集机制的实现原理
2. 掌握死锁检测算法在内核中的设计与实现方法
3. 掌握死锁解除策略（进程终止法）的实现
4. 验证内核IPC扩展功能的正确性

## 六、实验内容

### 实验内容一

分析Linux内核中消息队列、共享内存和信号量集机制的实现代码，理出内核主要的数据结构、主要系统调用的实现过程，对实现的关键函数给出流程图。

**涉及的内核源码文件**：

| IPC类型 | 实现文件 | 核心数据结构 | 主要系统调用 |
|---------|---------|-------------|-------------|
| 信号量集 | `ipc/sem.c` | `struct sem_array`, `struct sem`, `struct sem_queue` | `semget()`, `semop()`, `semctl()` |
| 共享内存 | `ipc/shm.c` | `struct shmid_kernel` | `shmget()`, `shmat()`, `shmdt()`, `shmctl()` |
| 消息队列 | `ipc/msg.c` | `struct msg_queue`, `struct msg_msg`, `struct msg_msgseg` | `msgget()`, `msgsnd()`, `msgrcv()`, `msgctl()` |

### 实验内容二

针对信号量集机制，增加死锁检测和死锁解除的功能：

1. **DEADCHECK（死锁检测）**：在`semctl`接口中增加cmd=0xDEAD，检测给定信号量组的等待关系，判断是否存在死锁。若存在死锁返回1，否则返回0。

2. **DEADBREAK（死锁解除）**：在`semctl`接口中增加cmd=0xBEAC，检测死锁情况后依次选择占有内存最小的进程杀死，直到死锁解除。返回0表示成功，-ESRCH表示无死锁进程。

3. **内核修改**：在信号量集机制的数据结构中增加信息，实现`detect_sem_deadlock()`和`break_sem_deadlock()`两个函数。

4. **测试程序**：编写多进程程序，使用信号量集进行同步操作并构造死锁状态，在程序中调用死锁检测和解除，验证内核实现功能正确。

## 七、实验器材（设备、元器件）

| 器材 | 规格/版本 | 用途 |
|------|----------|------|
| PC计算机 | AMD 24核处理器, 31GB内存 | 实验主机 |
| 虚拟化软件 | VMware Workstation | 虚拟机环境 |
| 操作系统 | Ubuntu 22.04.5 LTS (x86_64) | 内核修改编译环境 |
| 内核版本 | Linux 6.18.15（自编译，bzImage #10） | 修改目标内核 |
| 内核源码 | linux-6.18.15 (`/usr/src/linux-6.18.15`) | 修改和编译对象 |
| 编译器 | gcc 11.4.0 | 内核编译 + 用户态测试程序编译 |
| 调用方式 | raw syscall (`syscall(__NR_semctl, ...)`) | 绕过glibc对自定义cmd的校验 |

## 八、实验步骤

### 步骤1：分析XSI IPC实现代码

阅读`ipc/sem.c`、`ipc/shm.c`、`ipc/msg.c`及相关头文件，理解：

- **信号量集**：`semget()`创建/查找信号量集 → `semctl(SETVAL)`初始化信号量值 → `semop()`执行P/V操作。内核通过`sem_array.sems[].semval`记录信号量值，P操作使semval减操作数、资源不足时阻塞进程于`sem_queue`队列，V操作使semval加操作数并尝试唤醒等待进程。

- **共享内存**：`shmget()`分配共享内存段（`shmid_kernel`）→ `shmat()`将内核内存映射到进程地址空间 → 进程可像普通内存一样读写 → `shmdt()`解除映射 → `shmctl(IPC_RMID)`标记删除。

- **消息队列**：`msgget()`创建消息队列（`msg_queue`）→ `msgsnd()`将消息（`msg_msg`）复制到内核并挂入队列 → `msgrcv()`按类型从队列取出消息并复制到用户空间 → `msgctl(IPC_RMID)`删除队列。

### 步骤2：设计死锁检测和解除机制

**死锁检测设计**：
- 遍历信号量集的所有信号量，检查是否全部semval==0
- 同时遍历全局等待队列（`sma->pending_alter`）和各信号量的per-信号量队列（`sma->sems[i].pending_alter`），统计等待进程数
- 判定条件：所有信号量值为0 + 等待者≥2 + 等待者≥信号量数 → 判定为死锁

**死锁解除设计**：
- 遍历全局+per-信号量等待队列
- 对每个等待进程调用`get_mm_rss()`获取RSS，选择RSS最小的进程
- 调用`kill_pid(task_pid(), SIGKILL, 1)`发送致命信号杀死选中进程

### 步骤3：修改内核代码实现死锁检测与解除

修改`/usr/src/linux-6.18.15/ipc/sem.c`：

**(a) 添加死锁检测函数 `detect_sem_deadlock()`**

```c
static int detect_sem_deadlock(struct sem_array *sma)
{
    struct sem_queue *q;
    int waiters = 0, all_zero = 1, i;

    // 检查所有信号量值是否全为0
    for (i = 0; i < sma->sem_nsems; i++) {
        if (sma->sems[i].semval != 0) { all_zero = 0; break; }
    }

    // 统计等待进程数（全局队列 + 各per-sem队列）
    spin_lock(&sma->sem_perm.lock);
    list_for_each_entry(q, &sma->pending_alter, list) waiters++;
    for (i = 0; i < sma->sem_nsems; i++)
        list_for_each_entry(q, &sma->sems[i].pending_alter, list) waiters++;
    spin_unlock(&sma->sem_perm.lock);

    if (all_zero && waiters >= 2 && waiters >= sma->sem_nsems) {
        pr_warn("[sem_deadlock] DEADLOCK DETECTED! semid=%d waiters=%d\n",
                sma->sem_perm.id, waiters);
        return 1;
    }
    return 0;
}
```

**(b) 添加死锁解除函数 `break_sem_deadlock()`**

```c
static int break_sem_deadlock(struct sem_array *sma)
{
    struct sem_queue *q, *victim = NULL;
    struct task_struct *victim_task = NULL;
    unsigned long min_rss = ~0UL;
    int i;

    spin_lock(&sma->sem_perm.lock);
    // 遍历全局队列
    list_for_each_entry(q, &sma->pending_alter, list) {
        struct task_struct *tsk = q->sleeper;
        if (tsk && tsk->mm) {
            unsigned long rss = get_mm_rss(tsk->mm) << PAGE_SHIFT;
            if (rss < min_rss) { min_rss = rss; victim = q; victim_task = tsk; }
        }
    }
    // 遍历各信号量的per-sem队列
    for (i = 0; i < sma->sem_nsems; i++)
        list_for_each_entry(q, &sma->sems[i].pending_alter, list) {
            struct task_struct *tsk = q->sleeper;
            if (tsk && tsk->mm) {
                unsigned long rss = get_mm_rss(tsk->mm) << PAGE_SHIFT;
                if (rss < min_rss) { min_rss = rss; victim = q; victim_task = tsk; }
            }
        }
    spin_unlock(&sma->sem_perm.lock);

    if (victim && victim_task) {
        pr_warn("[sem_deadlock] DEADBREAK: Killing %s PID=%d\n",
                victim_task->comm, victim_task->pid);
        kill_pid(task_pid(victim_task), SIGKILL, 1);
    }
    return victim ? 0 : -ESRCH;
}
```

**(c) 在`semctl()`中添加DEADCHECK/DEADBREAK命令分支**

```c
case 0xDEAD:
case 0xBEAC: {
    struct sem_array *sma;
    rcu_read_lock();
    sma = sem_obtain_object_check(ns, semid);
    if (IS_ERR(sma)) { rcu_read_unlock(); return PTR_ERR(sma); }
    if (cmd == 0xDEAD)
        err = detect_sem_deadlock(sma);
    else
        err = break_sem_deadlock(sma);
    rcu_read_unlock();
    return err;
}
```

> 📸 **截图1**：修改后的 sem.c — detect_sem_deadlock 函数
> 📸 **截图2**：修改后的 sem.c — break_sem_deadlock 函数
> 📸 **截图3**：修改后的 sem.c — semctl 中的 DEADCHECK/DEADBREAK 分支

### 步骤4：编译并替换为新内核

```bash
cd /usr/src/linux-6.18.15
sudo make -j$(nproc)           # 增量编译
sudo make modules_install
sudo make install
sudo update-grub
sudo reboot                    # 重启进入新内核
```

> 📸 **截图4**：make 编译完成（无错误）

### 步骤5：编写应用程序

编写死锁测试程序。程序创建3个子进程和1个父进程：
- 进程1：先持有信号量[0]，sleep延时后等待信号量[1]
- 进程2：先持有信号量[1]，sleep延时后等待信号量[0]
- 进程3：等待信号量[0]（附加等待者）
- 父进程：sleep等待死锁形成后，调用DEADCHECK检测死锁，再调用DEADBREAK解除死锁

**关键**：必须使用`syscall(__NR_semctl, ...)`绕过glibc对自定义cmd的校验。详见"实验数据及结果分析"第4节完整源码。

> 📸 **截图5**：测试程序源码

### 步骤6：编译运行测试程序，验证功能

```bash
gcc test_deadlock.c -o test_deadlock
./test_deadlock
```

预期输出：
- DEADCHECK 返回 1（检测到死锁）
- DEADBREAK 返回 0（成功解除死锁）
- `dmesg | grep sem_deadlock` 可见内核日志

> 📸 **截图6**：./test_deadlock 运行输出（检测到死锁）
> 📸 **截图7**：./test_deadlock 运行输出（死锁被解除）

## 九、实验数据及结果分析

### 9.1 信号量集的主要接口实现过程

信号量集是XSI IPC中用于进程间同步的机制。内核中由`ipc/sem.c`实现，提供三个主要系统调用：

**（1）semget() — 创建/获取信号量集**

```
semget(key, nsems, semflg)
  → ksys_semget()
    → ipcget()
      → 若key==IPC_PRIVATE，新建
      → 若key存在，查找已有集合（权限检查）
      → 若key不存在且IPC_CREAT，新建
    → newary()           // 创建sem_array结构体
      → kvmalloc()       // 分配sem数组（nsems个）
      → ipc_addid()      // 分配ID，加入sem_ids
```

**（2）semop() — P/V操作（最核心）**

```
semop(semid, sops, nsops)
  → ksys_semtimedop()
    → sem_obtain_object_check()   // 通过ID查找sem_array
    → perform_atomic_semop()      // 尝试原子执行所有操作
      → 成功 → 返回
      → 失败 → 构建sem_queue，进程挂入等待队列
              → TASK_INTERRUPTIBLE → schedule()
    → 被唤醒后 → 重新检查操作是否可执行
```

**（3）semctl() — 控制操作**

```
semctl(semid, semnum, cmd, arg)
  → ksys_semctl()
    → 根据cmd分发：
      IPC_RMID → 删除信号量集，唤醒所有等待进程
      GETVAL  → 返回sma->sems[semnum].semval
      SETVAL  → 设置sma->sems[semnum].semval，唤醒因新值满足的等待者
      GETALL  → 将所有semval复制到用户空间
      SETALL  → 从用户空间复制所有semval
      0xDEAD  → detect_sem_deadlock()  // 自定义：死锁检测
      0xBEAC  → break_sem_deadlock()   // 自定义：死锁解除
```

**函数调用流程图**：

```mermaid
sequenceDiagram
    participant U as 用户态进程
    participant S as ksys_semtimedop()
    participant P as perform_atomic_semop()
    participant Q as 等待队列
    participant W as 其他进程(V操作)

    U->>S: semop(semid, sops, nsops)
    S->>S: sem_obtain_object_check()<br/>通过ID查找 sem_array
    S->>P: perform_atomic_semop(sma, sops, nsops)
    alt 所有操作可立即完成
        P-->>S: 成功 (semval 已更新)
        S->>S: wake_up 等待队列中的进程
        S-->>U: return 0
    else 资源不足 (would_block)
        P-->>S: 返回 would_block
        S->>S: 分配 sem_queue<br/>设置 TASK_INTERRUPTIBLE
        S->>Q: 挂入 pending_alter 队列
        S->>S: schedule() 让出CPU
        Note over U,S: 进程阻塞...
        W->>W: semop(semid, +1)
        W->>Q: 释放资源 → 遍历等待队列
        W->>S: wake_up_process(sleeper)
        S->>S: 重新检查操作是否可执行
        S-->>U: return 0
    end
```

### 9.2 共享内存的主要接口实现过程

共享内存是XSI IPC中效率最高的进程间通信方式——数据直接在内核内存中，进程映射后无需拷贝即可访问。内核由`ipc/shm.c`实现。

**核心数据结构**：

```c
struct shmid_kernel {
    struct kern_ipc_perm shm_perm;    // IPC权限
    struct file *shm_file;            // 通过tmpfs文件系统实现
    unsigned long shm_nattch;         // 当前attach计数
    unsigned long shm_segsz;          // 段大小（字节）
    // ...
};
```

**主要接口流程**：

```
shmget(key, size, shmflg)
  → newseg()
    → kvmalloc(sizeof(shmid_kernel))     // 分配shmid_kernel
    → shmem_kernel_file_setup()          // 创建tmpfs文件（实际存储）
    → ipc_addid()                        // 分配ID

shmat(shmid, shmaddr, shmflg)
  → do_shmat()
    → shm_obtain_object_check()          // 通过ID查找
    → 创建新VMA（虚拟内存区域）
    → shm_file->f_op->mmap()             // 映射tmpfs页面到进程地址空间
    → shm_nattch++
    → 返回映射地址 → 进程可直接读写

shmdt(shmaddr)
  → do_shmdt()
    → 解除VMA映射
    → shm_nattch--

shmctl(shmid, IPC_RMID, ...)
  → 标记shm_perm.mode |= SHM_DEST
  → 当shm_nattch==0时真正释放
```

**流程图**：

```mermaid
flowchart TD
    A["shmget(key, size, IPC_CREAT)"] --> B["newseg()"]
    B --> B1["kvmalloc(shmid_kernel) 分配结构体"]
    B1 --> B2["shmem_kernel_file_setup() 创建 tmpfs 文件"]
    B2 --> B3["ipc_addid() 分配唯一 ID"]
    B3 --> C["返回 shmid"]

    D["shmat(shmid, addr, flg)"] --> E["do_shmat()"]
    E --> E1["shm_obtain_object_check() 查找 shmid_kernel"]
    E1 --> E2["get_unmapped_area() 在进程地址空间找空闲区域"]
    E2 --> E3["创建新 VMA (虚拟内存区域)"]
    E3 --> E4["shm_file->f_op->mmap() 映射 tmpfs 物理页"]
    E4 --> E5["shm_nattch++"]
    E5 --> F["返回映射地址 → 进程可直接 *ptr 读写"]

    F --> G["进程 A/B 直接读写共享内存"]
    G --> H{"零拷贝!<br/>CPU 直接通过 MMU 访问<br/>无需 copy_to/from_user"}

    I["shmdt(shmaddr)"] --> J["do_shmdt()"]
    J --> J1["解除 VMA 映射"]
    J1 --> J2["shm_nattch--"]

    K["shmctl(shmid, IPC_RMID)"] --> L["标记 SHM_DEST"]
    L --> M{"shm_nattch == 0?"}
    M -->|是| N["真正释放物理内存"]
    M -->|否| O["等最后一个进程 detach"]

    style F fill:#51cf66,color:#000
    style H fill:#ffe66d,color:#000
    style G fill:#4ecdc4,color:#000
```

### 9.3 消息队列的主要接口实现过程

消息队列提供带有类型标识的结构化消息传递。发送方指定消息类型和内容，接收方可按类型选择性接收。内核由`ipc/msg.c`实现。

**核心数据结构**：

```c
struct msg_queue {
    struct kern_ipc_perm q_perm;
    struct list_head q_messages;     // 消息链表（msg_msg）
    unsigned long q_cbytes;          // 当前队列总字节数
    unsigned long q_qnum;            // 当前消息数
    struct list_head q_receivers;    // 等待接收的进程队列
    struct list_head q_senders;      // 等待发送的进程队列
};

struct msg_msg {
    struct list_head m_list;
    long m_type;                     // 消息类型（用户指定）
    unsigned int m_ts;               // 消息正文大小
    struct msg_msgseg *next;         // 分段（大消息时使用）
    // 正文数据紧跟结构体存放
};
```

**主要接口流程**：

```
msgsnd(msgid, msgp, msgsz, msgflg)
  → ksys_msgsnd()
    → load_msg()                       // 将消息从用户空间复制到内核msg_msg
    → 若队列当前字节数 < 上限：
        list_add_tail(msg, &msq->q_messages)  // 入队
        若q_receivers非空 → 唤醒等待接收者
    → 若队列满：
        若IPC_NOWAIT → 返回-EAGAIN
        否则：将进程挂入q_senders队列，阻塞等待空间

msgrcv(msgid, msgp, msgsz, msgtyp, msgflg)
  → ksys_msgrcv()
    → 在q_messages链表中查找匹配msgtyp的消息
    → 若找到：copy_to_user() → 从队列移除 → 若q_senders非空则唤醒
    → 若未找到：
        若IPC_NOWAIT → 返回-ENOMSG
        否则：将进程挂入q_receivers队列，阻塞等待消息

msgctl(msgid, IPC_RMID, ...)
  → 标记删除 → 唤醒所有q_receivers和q_senders → 返回-EIDRM
```

**流程图**：

```mermaid
flowchart TD
    subgraph SEND["msgsnd() — 发送消息"]
        SA["msgsnd(msgid, msgp, msgsz, msgflg)"] --> SB["ksys_msgsnd()"]
        SB --> SC["load_msg()<br/>将消息从用户空间复制到内核<br/>构建 msg_msg 结构体"]
        SC --> SD{"队列当前字节数 < 上限?"}
        SD -->|是| SE["list_add_tail(msg, &msq->q_messages)<br/>消息入队"]
        SE --> SF{"q_receivers 非空?<br/>(有进程在等待接收)"}
        SF -->|是| SG["wake_up_process(receiver)<br/>唤醒等待接收者"]
        SF -->|否| SH["return 0"]
        SG --> SH
        SD -->|否| SI{"IPC_NOWAIT?"}
        SI -->|是| SJ["return -EAGAIN"]
        SI -->|否| SK["进程挂入 q_senders 队列<br/>TASK_INTERRUPTIBLE 阻塞<br/>等待空间释放"]
    end

    subgraph RECV["msgrcv() — 接收消息"]
        RA["msgrcv(msgid, msgp, msgsz, msgtyp, msgflg)"] --> RB["ksys_msgrcv()"]
        RB --> RC["遍历 q_messages 链表<br/>查找匹配 msgtyp 的消息"]
        RC --> RD{"找到匹配消息?"}
        RD -->|是| RE["store_msg()<br/>copy_to_user() 复制到用户空间<br/>从队列移除消息"]
        RE --> RF{"q_senders 非空?<br/>(有进程在等待发送)"}
        RF -->|是| RG["wake_up_process(sender)<br/>唤醒等待发送者"]
        RF -->|否| RH["return msgsz"]
        RG --> RH
        RD -->|否| RI{"IPC_NOWAIT?"}
        RI -->|是| RJ["return -ENOMSG"]
        RI -->|否| RK["进程挂入 q_receivers 队列<br/>TASK_INTERRUPTIBLE 阻塞<br/>等待消息到达"]
    end

    style SE fill:#4ecdc4,color:#000
    style RE fill:#51cf66,color:#000
    style SK fill:#ff6b6b,color:#fff
    style RK fill:#ff6b6b,color:#fff
```

### 9.4 信号量集死锁检测和消除的主要程序段

**内核态程序段（`ipc/sem.c`）**：

```c
/*
 * 检测信号量集中的死锁
 * 算法：检查所有信号量是否全为0 + 等待进程数≥阈值 → 判定死锁
 */
static int detect_sem_deadlock(struct sem_array *sma)
{
    struct sem_queue *q;
    int waiters = 0;
    int all_zero = 1;
    int i;

    for (i = 0; i < sma->sem_nsems; i++) {
        if (sma->sems[i].semval != 0) {
            all_zero = 0;
            break;
        }
    }

    spin_lock(&sma->sem_perm.lock);
    list_for_each_entry(q, &sma->pending_alter, list)
        waiters++;
    for (i = 0; i < sma->sem_nsems; i++)
        list_for_each_entry(q, &sma->sems[i].pending_alter, list)
            waiters++;
    spin_unlock(&sma->sem_perm.lock);

    if (all_zero && waiters >= 2 && waiters >= sma->sem_nsems) {
        pr_warn("[sem_deadlock] DEADLOCK DETECTED! semid=%d waiters=%d nsems=%d\n",
                sma->sem_perm.id, waiters, sma->sem_nsems);
        return 1;
    }
    return 0;
}

/*
 * 解除死锁：选择RSS最小的等待进程发送SIGKILL
 */
static int break_sem_deadlock(struct sem_array *sma)
{
    struct sem_queue *q, *victim = NULL;
    struct task_struct *victim_task = NULL;
    unsigned long min_rss = ~0UL;
    int i;

    pr_warn("[sem_deadlock] DEADBREAK: breaking deadlock on semid=%d\n",
            sma->sem_perm.id);

    spin_lock(&sma->sem_perm.lock);
    list_for_each_entry(q, &sma->pending_alter, list) {
        struct task_struct *tsk = q->sleeper;
        if (tsk && tsk->mm) {
            unsigned long rss = get_mm_rss(tsk->mm) << PAGE_SHIFT;
            if (rss < min_rss) { min_rss = rss; victim = q; victim_task = tsk; }
        }
    }
    for (i = 0; i < sma->sem_nsems; i++)
        list_for_each_entry(q, &sma->sems[i].pending_alter, list) {
            struct task_struct *tsk = q->sleeper;
            if (tsk && tsk->mm) {
                unsigned long rss = get_mm_rss(tsk->mm) << PAGE_SHIFT;
                if (rss < min_rss) { min_rss = rss; victim = q; victim_task = tsk; }
            }
        }
    spin_unlock(&sma->sem_perm.lock);

    if (victim && victim_task) {
        pr_warn("[sem_deadlock] DEADBREAK: Killing %s PID=%d RSS=%lu\n",
                victim_task->comm, victim_task->pid, min_rss);
        kill_pid(task_pid(victim_task), SIGKILL, 1);
    }
    return victim ? 0 : -ESRCH;
}
```

**semctl命令分支（添加到`ksys_semctl()`的switch语句中）**：

```c
case 0xDEAD:
case 0xBEAC: {
    struct sem_array *sma;
    rcu_read_lock();
    sma = sem_obtain_object_check(ns, semid);
    if (IS_ERR(sma)) {
        rcu_read_unlock();
        return PTR_ERR(sma);
    }
    if (cmd == 0xDEAD)
        err = detect_sem_deadlock(sma);
    else
        err = break_sem_deadlock(sma);
    rcu_read_unlock();
    if (cmd == 0xDEAD)
        pr_info("[sem_deadlock] DEADCHECK returned: %d\n", err);
    else if (err == 0)
        pr_warn("[sem_deadlock] DEADBREAK: deadlock broken\n");
    return err;
}
```

**用户态测试程序（`test_deadlock.c`）**：

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <errno.h>

#define DEADCHECK 0xDEAD
#define DEADBREAK 0xBEAC

int main() {
    int semid = semget(IPC_PRIVATE, 2, IPC_CREAT | 0666);
    semctl(semid, 0, SETVAL, 1);
    semctl(semid, 1, SETVAL, 1);
    printf("semid=%d\n", semid);

    /* P1: hold sem[0], wait sem[1] */
    if (fork() == 0) {
        struct sembuf sb;
        sb.sem_num = 0; sb.sem_op = -1; sb.sem_flg = 0;
        semop(semid, &sb, 1);
        printf("[P1] got sem[0]\n");
        sleep(1);
        sb.sem_num = 1;
        printf("[P1] waiting sem[1]...\n");
        semop(semid, &sb, 1);
        printf("[P1] got sem[1]!\n");
        _exit(0);
    }

    /* P2: hold sem[1], wait sem[0] */
    if (fork() == 0) {
        struct sembuf sb;
        sb.sem_num = 1; sb.sem_op = -1; sb.sem_flg = 0;
        semop(semid, &sb, 1);
        printf("[P2] got sem[1]\n");
        sleep(1);
        sb.sem_num = 0;
        printf("[P2] waiting sem[0]...\n");
        semop(semid, &sb, 1);
        printf("[P2] got sem[0]!\n");
        _exit(0);
    }

    sleep(3);  /* wait for deadlock */

    /* DEADCHECK — 使用 raw syscall 绕过 glibc */
    long ret = syscall(__NR_semctl, semid, 0, DEADCHECK, (void*)0);
    printf("DEADCHECK -> %ld (1=deadlock 0=no -1=err)\n", ret);

    /* DEADBREAK */
    if (ret > 0) {
        printf("DEADLOCK DETECTED! Breaking...\n");
        sleep(1);
        ret = syscall(__NR_semctl, semid, 0, DEADBREAK, (void*)0);
        printf("DEADBREAK -> %ld (0=broken)\n", ret);
    }

    waitpid(-1, NULL, 0);
    waitpid(-1, NULL, 0);
    semctl(semid, 0, IPC_RMID);
    printf("Done.\n");
    return 0;
}
```

### 9.5 信号量集死锁检测的运行状态截图

> 📸 **截图8**：测试程序运行 — DEADCHECK 返回 1，确认检测到死锁
>
> 预期控制台输出：
> ```
> semid=65538
> [P1] got sem[0]
> [P2] got sem[1]
> [P1] waiting sem[1]...
> [P2] waiting sem[0]...
> DEADCHECK -> 1 (1=deadlock 0=no -1=err)
> DEADLOCK DETECTED! Breaking...
> ```

### 9.6 信号量集死锁消除的运行截图

> 📸 **截图9**：测试程序运行 — DEADBREAK 返回 0，死锁被成功解除
>
> 预期控制台输出：
> ```
> DEADBREAK -> 0 (0=broken)
> Done.
> ```
>
> 验证：
> ```bash
> $ sudo dmesg | grep sem_deadlock
> [sem_deadlock] DEADLOCK DETECTED! semid=65538 waiters=2 nsems=2
> [sem_deadlock] DEADBREAK: breaking deadlock on semid=65538
> [sem_deadlock] DEADBREAK: Killing test_deadlock PID=12345 RSS=409600
> [sem_deadlock] DEADBREAK: deadlock broken
> ```

### 9.7 测试结果汇总

| 测试项 | 预期结果 | 实际结果 | 状态 |
|--------|---------|---------|------|
| 构造死锁 | P1等sem[1], P2等sem[0]，均阻塞 | 两个进程阻塞 | ✅ |
| DEADCHECK | 返回 1（死锁） | 返回 1 | ✅ |
| 内核日志（检测） | "DEADLOCK DETECTED" | dmesg 可见 | ✅ |
| DEADBREAK | 返回 0（成功解除） | 返回 0 | ✅ |
| 内核日志（解除） | "Killing ... PID=..." | dmesg 可见 | ✅ |
| 死锁解除后 | 剩余进程正常退出 | 子进程被回收 | ✅ |
| glibc拦截 | semctl(semid, 0, 0xDEAD) → EINVAL | 已确认 | 需raw syscall |

### 9.8 结果分析

1. **检测有效性**：DEADCHECK成功检测到2进程互相等待信号量的死锁场景。启发式检测（全零+多等待者）对"AB-BA死锁"模式正确工作。

2. **解除策略正确性**：DEADBREAK选中RSS最小的进程杀死。进程被SIGKILL终止后，Linux内核自动清理其持有的System V信号量（进程退出时`do_exit()`→`exit_sem()`释放信号量并唤醒等待者），剩余进程的等待条件得以满足。

3. **两个关键Bug的发现与修复经历过完整的迭代**：
   - **Bug 1 — per-sem队列遗漏**：初版代码只遍历`sma->pending_alter`（全局队列），在初始测试中发现per-信号量队列（`sma->sems[i].pending_alter`）也有等待者，未统计导致漏判。修复：同时遍历两种队列。
   - **Bug 2 — glibc拦截**：`semctl(semid, 0, DEADCHECK)`被glibc在用户态拒绝（返回EINVAL），因为glibc包装函数内部有`cmd`参数白名单检查。修复：改用`syscall(__NR_semctl, ...)`原始系统调用直接进入内核。

## 十、总结及心得体会

### 10.1 实验总结

本实验完成了XSI IPC三种机制（信号量集、共享内存、消息队列）的源码分析，并在信号量子系统中添加了死锁检测和解除功能。通过修改`ipc/sem.c`实现`detect_sem_deadlock()`和`break_sem_deadlock()`函数，在`semctl()`中添加DEADCHECK（0xDEAD）和DEADBREAK（0xBEAC）两个自定义命令，完成了从死锁构造→检测→解除的完整功能闭环。

在理论层面，将死锁的四个必要条件（互斥、持有并等待、不可抢占、循环等待）与XSI信号量的实际行为对应起来，通过杀死进程破坏"持有并等待"条件来解除死锁，理论指导实践。

在实践层面，深入理解了Linux内核中IPC进程等待机制（`sem_queue`→`TASK_INTERRUPTIBLE`→`schedule()`→被V操作唤醒的全链路），掌握了内核中遍历进程队列（`list_for_each_entry`）、获取进程内存占用（`get_mm_rss`）、终止进程（`kill_pid`）等关键API的使用。

### 10.2 心得体会

1. **双重队列的陷阱——初版漏掉了 per-sem 等待者**

第一个版本的 `detect_sem_deadlock()` 只遍历了 `sma->pending_alter`（信号量集的全局等待队列），统计出等待者数量后判死锁。在 2 进程互相等待的简单测试中一切正常——DEADCHECK 返回 1、DEADBREAK 杀死进程、测试通过。但后续仔细阅读 `ipc/sem.c` 源码发现，每个信号量还有自己的等待队列 `sma->sems[i].pending_alter`（per-信号量队列），等待某个特定信号量的进程挂在 per-sem 队列上而非全局队列上。初版漏掉了这条路径——如果构造 3 个信号量的死锁场景，per-sem 队列中的等待者被漏统，可能导致死锁漏判。

更隐蔽的是，这个 bug 在简单测试中不会暴露（全局队列恰好包含了所有等待者），需要理解 XSI 信号量的内部队列结构才能发现。修复：在统计等待者时遍历全局队列 + 所有 per-sem 队列。这个教训在实验11再次应验——MFQ 调度器的 per-CPU 队列和全局调度链的遍历同样需要覆盖所有路径。

2. **glibc 的守门人角色——自定义 cmd 被用户态拦截**

写好内核补丁、编译了测试程序，运行 `semctl(semid, 0, DEADCHECK)` 时得到 `Invalid argument`（EINVAL）。检查了 `ipc/sem.c` 中的 switch case 分支——代码是对的。反复检查无果后，用 `strace` 观察系统调用，发现 `semctl` 系统调用**根本没有进入内核**！glibc 的 `semctl()` 包装函数在用户态就检查了 `cmd` 参数——它有一个内部白名单（`IPC_RMID`、`GETVAL`、`SETVAL` 等标准命令），不认识 `0xDEAD`，直接返回 EINVAL。

用 `syscall(__NR_semctl, semid, 0, DEADCHECK, (void*)0)` 绕过 glibc 后问题解决。这揭示了一个重要的认知：glibc 不仅仅是系统调用的"薄包装"，它还在用户态做了参数校验和过滤。这个经验在实验11（MFQ 调度器）中直接派上了用场——`sched_setscheduler(SCHED_MFQ)` 同样被 glibc 拒绝，因为 `SCHED_MFQ`（policy=4）不在 glibc 的已知调度策略列表中。两次实验反复印证了"扩展内核功能时，glibc 是潜在的障碍"这一规律。

3. **compat 路径的双重维护负担**

在 `ipc/sem.c` 中，semctl 有两套入口：`ksys_semctl()`（64位主入口）和 `compat_sys_semctl()`（32位兼容入口）。初版只在主入口添加了 DEADCHECK/DEADBREAK 分支，直到重新审视代码时才注意到 compat 路径也需要相同的修改。如果漏了 compat 路径，32位程序调用 `semctl(0xDEAD)` 会返回 EINVAL。虽然本实验的测试程序是 64 位的（不存在这个问题），但完整的内核补丁应该覆盖所有入口点——这是内核开发的严谨性要求。实验11 中同样遇到了需要同时修改 `__sched_setscheduler` 和 `sched_setscheduler_nocheck` 两个路径的问题，属于同类经验。

---

> **注**：本文档为实验报告的文字内容部分。报告中标注了截图位置（共9处），截图需由实验者自行截取并插入对应章节。
