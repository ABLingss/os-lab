/*
 * exp10 实验内容二-3: COW (Copy-On-Write) 父子进程共享内存观察
 *
 * 父进程 mmap + memset N GB → fork → 子进程写入一半触发COW
 *
 * 编译: gcc -O2 -o mem_cow mem_cow.c
 * 使用: ./mem_cow [N]     # N=GB数, 默认2
 *
 * 观察要点:
 *   1. fork后: 父子VIRT各≈N GB, 但总物理内存≈N GB (共享COW页)
 *   2. 子进程写入后: COW触发, 系统总物理内存增加
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

static void show_status(const char *tag)
{
    char buf[256];
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return;
    printf("  [%s]\n", tag);
    while (fgets(buf, sizeof(buf), f)) {
        if (strncmp(buf, "VmSize:", 7) == 0 ||
            strncmp(buf, "VmRSS:", 6) == 0 ||
            strncmp(buf, "VmPTE:", 6) == 0)
            printf("    %s", buf);
    }
    fclose(f);
}

static void show_meminfo(const char *tag)
{
    char buf[256];
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    printf("  [%s] 系统内存:\n", tag);
    while (fgets(buf, sizeof(buf), f)) {
        if (strncmp(buf, "MemFree:", 8) == 0 ||
            strncmp(buf, "MemAvailable:", 13) == 0)
            printf("    %s", buf);
    }
    fclose(f);
}

int main(int argc, char *argv[])
{
    size_t gb = (argc > 1) ? (size_t)atoi(argv[1]) : 2;
    size_t size = gb * 1024UL * 1024UL * 1024UL;
    pid_t pid;

    if (gb < 1 || gb > 20) { fprintf(stderr, "GB 1-20\n"); return 1; }

    printf("=== 实验10-2.3: COW 验证 (%zu GB) ===\n\n", gb);

    show_meminfo("初始");
    show_status("父进程初始");

    /* 父进程: mmap + MAP_POPULATE + 写入 (确保所有页有物理帧) */
    printf("\n[父] mmap %zu GB + memset...\n", gb);
    void *p = mmap(NULL, size, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    memset(p, 0xCC, size);

    show_meminfo("父分配后");
    show_status("父进程分配后");

    printf("\n按Enter fork...\n"); getchar();

    pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* ===== 子进程 ===== */
        show_status("子进程 fork后(写前)");
        show_meminfo("子进程 fork后(写前)");

        printf("\n[子] 注意: 子VmRSS≈父, 但系统MemFree减少不大(共享COW页)\n");
        printf("[子] 按Enter写入一半内存触发COW...\n"); getchar();

        memset(p, 0xDD, size / 2);  /* 写一半, 触发 50% COW */

        show_status("子进程 写入后");
        show_meminfo("子进程 写入后");

        printf("\n[子] COW效果: 写前父子共享物理页, 写后子进程获取私有副本\n");
        printf("[子] 按Enter退出...\n"); getchar();
        _exit(0);
    } else {
        waitpid(pid, NULL, 0);
    }

    munmap(p, size);
    printf("[父] 退出。\n");
    return 0;
}
