#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#define SIZE (100UL * 1024 * 1024)  /* 100MB */

int main(int argc, char **argv) {
    int touch = (argc > 1 && argv[1][0] == '1');

    printf("PID=%d mode=%s\n", getpid(), touch ? "MAP_POPULATE+写" : "只mmap不访问");
    printf("mmap前: "); fflush(stdout);
    { char c[64]; snprintf(c,64,"ps -o vsize,rss -p %d --no-headers",getpid()); system(c); }

    void *p = mmap(NULL, SIZE, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS|(touch ? MAP_POPULATE : MAP_NORESERVE),
                   -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }

    printf("mmap后: "); fflush(stdout);
    { char c[64]; snprintf(c,64,"ps -o vsize,rss -p %d --no-headers",getpid()); system(c); }

    if (touch) memset(p, 0xBB, SIZE);

    printf("最终:   "); fflush(stdout);
    { char c[64]; snprintf(c,64,"ps -o vsize,rss -p %d --no-headers",getpid()); system(c); }

    printf("按Enter退出...\n"); getchar();
    munmap(p, SIZE);
    return 0;
}
