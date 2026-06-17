#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define SIZE (200UL * 1024 * 1024)  /* 200MB (不用10G, 验证原理即可) */

int main(void) {
    printf("=== COW验证: 父进程分配%luMB → fork → 子进程写入 ===\n\n", SIZE/(1024*1024));

    /* 父进程: mmap + memset (VIRT=SIZE, RSS≈SIZE) */
    void *p = mmap(NULL, SIZE, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    memset(p, 0xCC, SIZE);

    printf("[父] PID=%d, mmap+memset 完成\n", getpid());
    fflush(stdout);
    { char c[128]; snprintf(c,128,"echo '父进程: '; ps -o pid,vsize,rss -p %d --no-headers",getpid()); system(c); }

    printf("\n按Enter fork...\n"); getchar();

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* === 子进程 === */
        printf("\n[子] PID=%d fork后(写前):\n", getpid());
        fflush(stdout);
        { char c[128]; snprintf(c,128,"ps -o pid,vsize,rss -p %d,%d --no-headers",getppid(),getpid()); system(c); }

        printf("\n[子] 按Enter写入100MB (触发COW)...\n"); getchar();

        memset(p, 0xDD, SIZE/2);  /* 只写一半, 触发一半COW */

        printf("\n[子] 写入后:\n");
        fflush(stdout);
        { char c[128]; snprintf(c,128,"ps -o pid,vsize,rss -p %d,%d --no-headers",getppid(),getpid()); system(c); }

        printf("\n[子] 按Enter退出...\n"); getchar();
        _exit(0);
    } else {
        waitpid(pid, NULL, 0);
    }

    munmap(p, SIZE);
    return 0;
}
