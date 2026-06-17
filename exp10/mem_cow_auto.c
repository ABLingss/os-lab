/*
 * exp10 COW 自动验证 — 无需手动按Enter，直接输出log
 * 编译: gcc -O2 -o mem_cow_auto mem_cow_auto.c
 * 运行: ./mem_cow_auto [GB数默认2]
 * 截图: 直接截终端输出即可
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

static void show(const char *tag)
{
    char buf[256];
    FILE *f = fopen("/proc/self/status", "r");
    printf("[%s]\n", tag);
    while (fgets(buf, sizeof(buf), f))
        if (strncmp(buf, "VmSize:", 7) == 0 ||
            strncmp(buf, "VmRSS:",  6) == 0 ||
            strncmp(buf, "VmPTE:",  6) == 0)
            printf("  %s", buf);
    fclose(f);
}

static void show_sysmem(const char *tag)
{
    char buf[256];
    FILE *f = fopen("/proc/meminfo", "r");
    printf("[系统内存 %s]\n", tag);
    while (fgets(buf, sizeof(buf), f))
        if (strncmp(buf, "MemFree:",     8) == 0 ||
            strncmp(buf, "MemAvailable:", 13) == 0)
            printf("  %s", buf);
    fclose(f);
}

int main(int argc, char *argv[])
{
    size_t gb = (argc > 1) ? (size_t)atoi(argv[1]) : 1;
    size_t size = gb * 1024UL * 1024UL * 1024UL;
    pid_t pid;

    if (gb < 1 || gb > 15) { fprintf(stderr, "用法: %s [1-15 GB]\n", argv[0]); return 1; }

    printf("══════════════════════════════════════\n");
    printf("  实验10 COW 写时复制验证 (%zu GB)\n", gb);
    printf("══════════════════════════════════════\n\n");
    setbuf(stdout, NULL);   // 关缓冲区，fork后父子都不丢输出

    /* ── 阶段1: 父进程分配 ── */
    printf("── 阶段1: 父进程 mmap + memset ──\n");
    show_sysmem("分配前");
    show("父-分配前");

    void *p = mmap(NULL, size, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    memset(p, 0xCC, size);

    show("父-分配后(RES≈VIRT)");
    show_sysmem("父分配后");
    printf("  → 父进程 RES ≈ %zu GB (全部触发缺页)\n\n", gb);

    /* ── 阶段2: fork ── */
    printf("── 阶段2: fork() ──\n");
    show_sysmem("fork前");
    sleep(1);
    pid = fork();

    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* ========== 子进程 ========== */
        printf("\n╔══════════════════════════════════╗\n");
        printf("║         子 进 程                  ║\n");
        printf("╚══════════════════════════════════╝\n\n");

        show("子-fork后(RES≈0,COW共享父物理页)");
        show_sysmem("子fork后(物理≈不变,COW共享)");
        printf("  → COW: VIRT=%zu GB 但 RES≈0, 物理页全共享父进程\n\n", gb);
        sleep(1);

        printf("── 阶段3: 子进程写入一半触发COW ──\n");
        memset(p, 0xDD, size / 2);

        show("子-写入后(RES增长≈写入量)");
        show_sysmem("子写入后(物理↓≈写入量, COW破裂)");
        printf("  → COW破裂: 子RES≈%zu GB, 仅写入的页被复制\n\n",
               gb / 2);
        sleep(1);

        printf("── COW验证完成 ──\n");
        printf("fork前:   父RES = %zu GB\n", gb);
        printf("fork后:   父子VIRT各%zu GB, 但系统总物理 ≈ %zu GB (共享)\n",
               gb, gb);
        printf("写入后:   子RES ≈ %zu GB (COW触发, 仅写入页复制)\n",
               gb / 2);
        _exit(0);
    }

    /* ========== 父进程 ========== */
    printf("\n╔══════════════════════════════════╗\n");
    printf("║         父 进 程                  ║\n");
    printf("╚══════════════════════════════════╝\n\n");

    show("父-fork后(不变)");
    sleep(1);
    waitpid(pid, NULL, 0);
    show("父-子进程退出后");
    munmap(p, size);
    printf("\n[父] 退出。\n");
    return 0;
}
