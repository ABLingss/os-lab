#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define GB (1024UL * 1024 * 1024)

static void show_mem(const char *tag, int pid)
{
    char buf[256], path[64];
    printf("=== %s ===\n", tag);

    /* 进程内存 */
    if (pid > 0) {
        snprintf(path, sizeof(path), "/proc/%d/status", pid);
        FILE *f = fopen(path, "r");
        if (f) {
            while (fgets(buf, sizeof(buf), f))
                if (strncmp(buf, "VmSize:",7)==0 || strncmp(buf, "VmRSS:",6)==0)
                    printf("  PID %d: %s", pid, buf);
            fclose(f);
        }
    }

    /* 系统内存 */
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        while (fgets(buf, sizeof(buf), f))
            if (strncmp(buf, "MemFree:",8)==0 || strncmp(buf, "MemAvailable:",13)==0)
                printf("  系统: %s", buf);
        fclose(f);
    }
}

int main(void)
{
    void *p;
    pid_t pid;
    size_t size = 1UL * GB;

    printf("COW验证 (1GB)\n\n");

    /* 初始状态 */
    show_mem("0. 初始状态", getpid());

    /* 父分配 1GB */
    p = mmap(NULL, size, PROT_READ|PROT_WRITE,
             MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    memset(p, 0xCC, size);

    show_mem("1. 父分配1GB后", getpid());
    printf("   → 父VmRSS≈1GB, 系统MemFree减少~1GB\n\n");

    /* Fork */
    pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* ===== 子进程 ===== */
        sleep(1);  /* 等待父进程输出稳定 */

        show_mem("2. Fork后(写前)", getpid());
        printf("   → 父子VmSize各≈1GB, VmRSS各≈1GB(共享物理页)\n");
        printf("   → 系统MemFree未进一步大幅减少(页在COW共享中)\n\n");

        /* 子进程写入一半触发COW */
        memset(p, 0xDD, size / 2);

        show_mem("3. 子进程写入一半后", getpid());
        printf("   → COW已触发: 被写页获得私有副本\n");
        printf("   → 系统MemFree进一步减少~512MB(子进程私有页)\n\n");

        fflush(stdout); fflush(stderr); _exit(0);
    } else {
        /* 父进程等待子进程完成 */
        waitpid(pid, NULL, 0);

        show_mem("4. 子进程退出后", getpid());
        printf("   → 子进程释放私有页, 系统MemFree部分恢复\n");

        munmap(p, size);
    }

    printf("\n验证完成!\n");
    return 0;
}
