/*
 * exp10 实验内容三: 物理内存页分类统计内核模块
 *
 * 遍历系统中所有物理页，按页标志位分类统计:
 *   空闲页、锁定页、Slab页、LRU页(活跃/不活跃)、
 *   保留页、脏页、回写页、页表页等
 *
 * 编译: make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
 * 使用: sudo insmod page_stats.ko && dmesg | tail -60
 *       sudo rmmod page_stats
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/pfn.h>
#include <linux/highmem.h>
#include <linux/version.h>
#include <linux/mm_inline.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ma Yingzhe");
MODULE_DESCRIPTION("Physical page statistics (exp10)");

/* 统计计数器 */
struct page_counts {
    unsigned long total_managed;     /* 总可管理页 */
    unsigned long total_present;     /* 总存在页 */
    unsigned long total_spanned;     /* 总跨越页 */
    unsigned long free_pages;        /* 空闲页 (PageBuddy) */
    unsigned long locked_pages;      /* 锁定页 (PG_locked, I/O进行中) */
    unsigned long slab_pages;        /* Slab分配器占用 */
    unsigned long lru_pages;         /* LRU上的页 (用户态页) */
    unsigned long lru_active;        /*   - 活跃 */
    unsigned long lru_inactive;      /*   - 不活跃 */
    unsigned long reserved_pages;    /* 保留页 */
    unsigned long dirty_pages;       /* 脏页 */
    unsigned long writeback_pages;   /* 回写中页 */
    unsigned long table_pages;       /* 页表页 */
    unsigned long swapbacked_pages;  /* 匿名页/共享内存 */
    unsigned long unevictable_pages; /* 不可回收页 */
    unsigned long hwpoison_pages;    /* 硬件损坏页 */
    unsigned long offline_pages;     /* 离线页 */
    unsigned long other_pages;       /* 其他 (未分类) */
    unsigned long refcount_zero;     /* 引用计数为0的非Buddy页 */
};

static void count_pages(struct page_counts *c)
{
    struct pglist_data *pgdat;
    int nid;
    unsigned long total_pages = 0;

    memset(c, 0, sizeof(*c));

    /* 使用 si_meminfo 获取快速汇总 */
    {
        struct sysinfo si;
        si_meminfo(&si);
        pr_info("=== si_meminfo 快速汇总 ===\n");
        pr_info("  totalram:   %lu pages (%lu MB)\n",
                si.totalram, si.totalram * 4 / 1024);
        pr_info("  freeram:    %lu pages (%lu MB)\n",
                si.freeram, si.freeram * 4 / 1024);
        pr_info("  bufferram:  %lu pages (%lu MB)\n",
                si.bufferram, si.bufferram * 4 / 1024);
        pr_info("  totalhigh:  %lu\n", si.totalhigh);
        pr_info("  freehigh:   %lu\n", si.freehigh);
    }

    pr_info("\n=== 逐页遍历统计 ===\n");

    /* 遍历所有在线节点 */
    for_each_online_node(nid) {
        struct zone *zone;
        int zid;

        pgdat = NODE_DATA(nid);

        pr_info("节点 %d: start_pfn=%lu, present=%lu, spanned=%lu\n",
                pgdat->node_id,
                pgdat->node_start_pfn,
                pgdat->node_present_pages,
                pgdat->node_spanned_pages);

        c->total_present += pgdat->node_present_pages;
        c->total_spanned += pgdat->node_spanned_pages;

        /* 遍历节点内的所有zone */
        for (zid = 0; zid < MAX_NR_ZONES; zid++) {
            unsigned long pfn, end_pfn;

            zone = &pgdat->node_zones[zid];
            if (!zone->spanned_pages)
                continue;

            c->total_managed += atomic_long_read(&zone->managed_pages);

            end_pfn = zone->zone_start_pfn + zone->spanned_pages;

            /* 遍历zone内所有页框 */
            for (pfn = zone->zone_start_pfn; pfn < end_pfn; pfn++) {
                struct page *page;

                if (!pfn_valid(pfn))
                    continue;

                page = pfn_to_page(pfn);

                /*
                 * 某些pfn可能alias到同一个page (SPARSEMEM),
                 * 我们只统计zone起始页开始的页
                 */
                if (page_zone(page) != zone)
                    continue;

                total_pages++;

                /*
                 * 按优先级分类: 先检查特殊情况
                 * 一个page可能同时设置多个标志
                 * 优先级: HWPoison > Offline > Reserved > Slab >
                 *         Buddy(Free) > Table > Locked > Writeback >
                 *         Dirty > LRU > SwapBacked > Unevictable > Other
                 */

                if (PageHWPoison(page)) {
                    c->hwpoison_pages++;
                    continue;
                }

                if (PageOffline(page)) {
                    c->offline_pages++;
                    continue;
                }

                if (PageReserved(page)) {
                    c->reserved_pages++;
                    continue;
                }

                /* Slab 分配器页 */
                if (PageSlab(page)) {
                    c->slab_pages++;
                    continue;
                }

                /* 伙伴系统空闲页 */
                if (PageBuddy(page)) {
                    c->free_pages++;
                    continue;
                }

                /* 页表页 */
                if (PageTable(page)) {
                    c->table_pages++;
                    /* 页表页也检查锁/脏/回写 */
                    if (PageLocked(page))
                        c->locked_pages++;
                    continue;
                }

                /*
                 * 以下是 LRU / 文件页 / 匿名页等
                 * 先按 LRU 状态分
                 */
                if (PageLRU(page)) {
                    c->lru_pages++;
                    if (folio_test_active(page_folio(page)))
                        c->lru_active++;
                    else
                        c->lru_inactive++;

                    if (folio_test_swapbacked(page_folio(page)))
                        c->swapbacked_pages++;
                    if (folio_test_unevictable(page_folio(page)))
                        c->unevictable_pages++;
                    if (PageDirty(page))
                        c->dirty_pages++;
                    if (PageWriteback(page))
                        c->writeback_pages++;
                    if (PageLocked(page))
                        c->locked_pages++;
                    continue;
                }

                /* 不在LRU上但可能有其他标志 */
                {
                    int classified = 0;

                    if (PageLocked(page)) {
                        c->locked_pages++;
                        classified = 1;
                    }
                    if (PageDirty(page)) {
                        c->dirty_pages++;
                        classified = 1;
                    }
                    if (PageWriteback(page)) {
                        c->writeback_pages++;
                        classified = 1;
                    }
                    if (folio_test_swapbacked(page_folio(page))) {
                        c->swapbacked_pages++;
                        classified = 1;
                    }
                    if (folio_test_unevictable(page_folio(page))) {
                        c->unevictable_pages++;
                        classified = 1;
                    }

                    if (classified)
                        continue;
                }

                /* 引用计数为0但不在Buddy中 (过渡状态) */
                if (page_count(page) == 0) {
                    c->refcount_zero++;
                    continue;
                }

                c->other_pages++;
            }
        }
    }

    pr_info("遍历总页数: %lu\n", total_pages);
}

static void print_results(struct page_counts *c)
{
    pr_info("\n");
    pr_info("============================================================\n");
    pr_info("      Linux 物理内存页分类统计 (exp10 实验内容三)\n");
    pr_info("============================================================\n");
    pr_info("\n");
    pr_info("--- 总体信息 ---\n");
    pr_info("  总可管理页 (managed):  %12lu  (%lu MB)\n",
            c->total_managed,  c->total_managed  * 4 / 1024);
    pr_info("  总存在页 (present):   %12lu  (%lu MB)\n",
            c->total_present,  c->total_present  * 4 / 1024);
    pr_info("  总跨越页 (spanned):   %12lu  (%lu MB)\n",
            c->total_spanned,  c->total_spanned  * 4 / 1024);
    pr_info("\n");
    pr_info("--- 按页类型分类 ---\n");
    pr_info("  空闲页 (Buddy free):  %12lu  (%lu MB)\n",
            c->free_pages,     c->free_pages     * 4 / 1024);
    pr_info("  Slab分配器页:         %12lu  (%lu MB)\n",
            c->slab_pages,     c->slab_pages     * 4 / 1024);
    pr_info("  LRU页 (用户态):       %12lu  (%lu MB)\n",
            c->lru_pages,      c->lru_pages      * 4 / 1024);
    pr_info("    ├─ 活跃 (Active):   %12lu  (%lu MB)\n",
            c->lru_active,     c->lru_active     * 4 / 1024);
    pr_info("    └─ 不活跃 (Inactive):%12lu  (%lu MB)\n",
            c->lru_inactive,   c->lru_inactive   * 4 / 1024);
    pr_info("  保留页 (Reserved):    %12lu  (%lu MB)\n",
            c->reserved_pages, c->reserved_pages * 4 / 1024);
    pr_info("  页表页 (PageTable):   %12lu  (%lu MB)\n",
            c->table_pages,    c->table_pages    * 4 / 1024);
    pr_info("\n");
    pr_info("--- 页状态标志统计 (各标志独立计数, 有重叠) ---\n");
    pr_info("  锁定页 (Locked):      %12lu\n", c->locked_pages);
    pr_info("  脏页   (Dirty):       %12lu\n", c->dirty_pages);
    pr_info("  回写中 (Writeback):   %12lu\n", c->writeback_pages);
    pr_info("  匿名页 (SwapBacked):  %12lu\n", c->swapbacked_pages);
    pr_info("  不可回收 (Unevictable):%12lu\n", c->unevictable_pages);
    pr_info("  硬件损坏 (HWPoison):  %12lu\n", c->hwpoison_pages);
    pr_info("  离线页 (Offline):     %12lu\n", c->offline_pages);
    pr_info("  引用计数0 (非Buddy):  %12lu\n", c->refcount_zero);
    pr_info("  其他未分类:           %12lu\n", c->other_pages);
    pr_info("============================================================\n");
}

static int __init page_stats_init(void)
{
    struct page_counts *c;

    pr_info("page_stats: 开始统计物理内存页...\n");

    c = kmalloc(sizeof(*c), GFP_KERNEL);
    if (!c) {
        pr_err("page_stats: kmalloc 失败\n");
        return -ENOMEM;
    }

    count_pages(c);
    print_results(c);

    kfree(c);
    pr_info("page_stats: 统计完成, 模块已加载\n");
    return 0;
}

static void __exit page_stats_exit(void)
{
    pr_info("page_stats: 模块已卸载\n");
}

module_init(page_stats_init);
module_exit(page_stats_exit);
