/*
 * exp10 实验内容二-1: 观察 malloc/free 底层系统调用 (brk/mmap)
 *
 * 分配不同大小内存(1B, 256B, 1K, 64K, 128K, 1M)，观察C库何时使用
 * brk(堆扩展) vs mmap(匿名映射)。
 *
 * 编译: gcc -o mem_observe mem_observe.c
 * 使用: strace -e trace=brk,mmap,munmap,mprotect ./mem_observe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N_SIZES 6

int main(void)
{
    size_t sizes[N_SIZES] = { 1, 256, 1024, 64*1024, 128*1024, 1024*1024 };
    void *ptrs[N_SIZES];
    int i;

    printf("=== 实验10-2.1: malloc/free 系统调用观察 ===\n");
    printf("观察 brk (小内存) 和 mmap (大内存>128KB) 的使用\n\n");

    /* Phase 1: 分配不同大小的内存 */
    printf("--- Phase 1: 分配 ---\n");
    for (i = 0; i < N_SIZES; i++) {
        if (sizes[i] >= 1024*1024)
            printf("malloc(%lu) = %lu MB\n",
                   (unsigned long)sizes[i],
                   (unsigned long)sizes[i] / (1024*1024));
        else if (sizes[i] >= 1024)
            printf("malloc(%lu) = %lu KB\n",
                   (unsigned long)sizes[i],
                   (unsigned long)sizes[i] / 1024);
        else
            printf("malloc(%lu) = %lu B\n",
                   (unsigned long)sizes[i],
                   (unsigned long)sizes[i]);

        ptrs[i] = malloc(sizes[i]);
        if (!ptrs[i]) {
            perror("malloc");
            return 1;
        }
        /* 写入数据，确保物理页真正分配 */
        memset(ptrs[i], 0xAB, sizes[i]);
    }

    printf("\n--- Phase 2: 释放 (乱序) ---\n");
    /* 乱序释放: 先释放大的，再释放小的 */
    for (i = N_SIZES - 1; i >= 0; i--) {
        printf("free(%p)  size=%lu\n", ptrs[i], (unsigned long)sizes[i]);
        free(ptrs[i]);
    }

    printf("\n=== 完成 ===\n");
    printf("提示: 用 strace -e trace=brk,mmap,munmap ./mem_observe 查看系统调用\n");
    return 0;
}
