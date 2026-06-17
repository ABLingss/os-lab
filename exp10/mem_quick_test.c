#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define N 50000   /* 50000 × 4KB = ~200MB */

int main(int argc, char **argv) {
    int touch = (argc > 1 && argv[1][0] == '1');
    void **p = calloc(N, sizeof(void*));
    
    printf("PID=%d mode=%s\n", getpid(), touch ? "分配+访问" : "只分配");
    printf("分配前: "); fflush(stdout);
    { char cmd[64]; snprintf(cmd,64,"ps -o vsize,rss -p %d --no-headers",getpid()); system(cmd); }
    
    for (int i = 0; i < N; i++) {
        p[i] = malloc(4096);
        if (touch) memset(p[i], 0xAA, 4096);
    }
    
    printf("分配后: "); fflush(stdout);
    { char cmd[64]; snprintf(cmd,64,"ps -o vsize,rss -p %d --no-headers",getpid()); system(cmd); }
    printf("按Enter退出...\n"); getchar();
    
    for (int i = 0; i < N; i++) free(p[i]);
    free(p);
    return 0;
}
