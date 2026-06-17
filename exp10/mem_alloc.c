/*
 * exp10 实验内容二-2: 观察内存分配、访问与物理内存分配的关系
 *
 * 使用 mmap 分配内存（绕过 glibc arena），清楚展示需求分页:
 *   mode 1: 小块(4KB) 只分配不访问 — VIRT↑ RES不变
 *   mode 2: 小块(4KB) 分配并访问   — VIRT↑ RES↑
 *   mode 3: 大块(2MB) 只分配不访问 — VIRT↑ RES不变
 *   mode 4: 大块(2MB) 分配并访问   — VIRT↑ RES↑
 *
 * 编译: gcc -O2 -o mem_alloc mem_alloc.c
 * 使用:
 *   ./mem_alloc 1    # 另开终端: top -p $(pidof mem_alloc) 观察 VIRT/RES
 *   ./mem_alloc 2
 *   ./mem_alloc 3
 *   ./mem_alloc 4
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#define SMALL_BLOCK  (4*1024)        /* 4 KB  — 小分配 */
#define LARGE_BLOCK  (2*1024*1024)   /* 2 MB  — 大分配 */
#define MAX_BLOCKS   20000

static void read_proc(int pid)
{
    char path[64], buf[256];
    FILE *f;
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(buf, sizeof(buf), f)) {
        if (strncmp(buf, "VmSize:", 7) == 0 ||
            strncmp(buf, "VmRSS:", 6) == 0)
            fputs(buf, stdout);
    }
    fclose(f);
}

int main(int argc, char *argv[])
{
    int mode, i;
    size_t blksz, total = 0;
    int do_touch;
    void **blocks;
    int nblocks = 0;

    if (argc < 2) {
        fprintf(stderr, "用法: %s <mode 1-4>\n", argv[0]);
        fprintf(stderr, "  1=小块只分配  2=小块分配+访问\n");
        fprintf(stderr, "  3=大块只分配  4=大块分配+访问\n");
        return 1;
    }
    mode = atoi(argv[1]);
    if (mode < 1 || mode > 4) { fprintf(stderr, "mode 1-4\n"); return 1; }

    blksz    = (mode <= 2) ? SMALL_BLOCK : LARGE_BLOCK;
    do_touch = (mode == 2 || mode == 4);

    printf("=== 实验10-2.2: VIRT/RES 观察 (mmap版) ===\n");
    printf("模式 %d: blk=%lu KB, %s\n",
           mode, (unsigned long)blksz/1024,
           do_touch ? "分配+访问(RES↑)" : "只分配不访问(RES→)");
    printf("PID = %d\n\n", getpid());

    blocks = calloc(MAX_BLOCKS, sizeof(void *));

    printf("--- 分配前 ---\n");
    read_proc(getpid());

    printf("\n正在分配...\n");
    for (i = 0; i < MAX_BLOCKS; i++) {
        blocks[i] = mmap(NULL, blksz, PROT_READ|PROT_WRITE,
                         MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (blocks[i] == MAP_FAILED) {
            printf("mmap失败 at %d, total=%lu MB\n",
                   i, (unsigned long)total/(1024*1024));
            break;
        }
        nblocks = i + 1;
        total += blksz;

        if (do_touch)
            memset(blocks[i], 0xCD, blksz);

        if ((i + 1) % 2000 == 0)
            printf("  %d块 / %lu MB\n",
                   i+1, (unsigned long)total/(1024*1024));
    }

    printf("\n--- 分配后 (总计 %lu MB) ---\n", (unsigned long)total/(1024*1024));
    read_proc(getpid());

    printf("\n观察: %s\n",
           do_touch ? "VIRT≈RSS≈分配量 (每页都有物理帧)"
                    : "VIRT≈分配量, RSS≈不变 (无缺页=无物理页分配)");
    printf("按 Enter 释放所有内存...\n");
    getchar();

    for (i = 0; i < nblocks; i++)
        munmap(blocks[i], blksz);
    free(blocks);
    printf("已释放。\n");
    return 0;
}
