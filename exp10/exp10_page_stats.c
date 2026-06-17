/*
 * 实验10：Linux内存管理分析与验证 — 内核模块
 *         物理页面分类统计
 *
 * 功能：遍历物理页面，统计空闲页、锁定页、slab页、保留页等分类
 *
 * 编译：同实验7 Makefile（作为 obj-m 添加）
 * 加载：sudo insmod page_stats.ko
 * 查看：dmesg | tail -30
 * 卸载：sudo rmmod page_stats
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/mm_types.h>
#include <linux/slab.h>
#include <linux/vmstat.h>
#include <linux/swap.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ma Yingzhe");
MODULE_DESCRIPTION("Experiment 10 — Physical Page Statistics");

/*
 * 页面分类统计结构
 */
struct page_stats {
	unsigned long total_pages;       /* 总物理页面 */
	unsigned long free_pages;        /* 空闲页面 */
	unsigned long locked_pages;      /* 锁定页面 (不可换出) */
	unsigned long slab_pages;        /* Slab 分配器使用的页面 */
	unsigned long reserved_pages;    /* 保留页面 */
	unsigned long file_pages;        /* 文件缓存页面 */
	unsigned long anon_pages;        /* 匿名页面 */
	unsigned long dirty_pages;       /* 脏页 */
	unsigned long mapped_pages;      /* 映射到用户空间的页面 */
	unsigned long kernel_stack_pages;/* 内核栈页面 */
	unsigned long page_table_pages;  /* 页表页面 */
};

/*
 * 遍历所有内存区域，统计页面分类
 */
static void count_pages(struct page_stats *stats)
{
	struct zone *zone;
	unsigned long flags;

	memset(stats, 0, sizeof(*stats));

	/* 使用内核提供的全局统计信息 */
	stats->total_pages   = totalram_pages();
	stats->free_pages    = nr_free_pages();

	/* 遍历所有 zone */
	for_each_populated_zone(zone) {
		unsigned long zone_start = zone->zone_start_pfn;
		unsigned long zone_end   = zone_start + zone->spanned_pages;
		unsigned long pfn;

		spin_lock_irqsave(&zone->lock, flags);

		/* 统计该 zone 的页面状态 */
		for (pfn = zone_start; pfn < zone_end; pfn++) {
			struct page *page;

			if (!pfn_valid(pfn))
				continue;

			page = pfn_to_page(pfn);

			/*
			 * 使用 PageXXX() 宏分类页面
			 */
			if (PageReserved(page))
				stats->reserved_pages++;

			if (PageSlab(page))
				stats->slab_pages++;

			if (PageLocked(page))
				stats->locked_pages++;

			if (PageDirty(page))
				stats->dirty_pages++;

			if (PageAnon(page))
				stats->anon_pages++;

			if (page_mapped(page))
				stats->mapped_pages++;
		}

		spin_unlock_irqrestore(&zone->lock, flags);
	}

	/* 文件缓存页统计（从 NR_FILE_PAGES 获取） */
	stats->file_pages = global_node_page_state(NR_FILE_PAGES);
	stats->page_table_pages = global_node_page_state(NR_PAGETABLE);
	stats->kernel_stack_pages = global_node_page_state(NR_KERNEL_STACK_KB) / 4;
}

static int __init page_stats_init(void)
{
	struct page_stats stats;

	printk(KERN_INFO "==========================================\n");
	printk(KERN_INFO "[page_stats] 物理页面统计模块加载\n");
	printk(KERN_INFO "==========================================\n");

	count_pages(&stats);

	printk(KERN_INFO "[page_stats] 物理页面统计结果:\n");
	printk(KERN_INFO "  总物理页面:       %lu (%.2f GB)\n",
	       stats.total_pages,
	       (double)stats.total_pages * PAGE_SIZE / (1024.0 * 1024 * 1024));
	printk(KERN_INFO "  空闲页面:         %lu (%.2f GB)\n",
	       stats.free_pages,
	       (double)stats.free_pages * PAGE_SIZE / (1024.0 * 1024 * 1024));
	printk(KERN_INFO "  锁定页面:         %lu\n", stats.locked_pages);
	printk(KERN_INFO "  Slab页面:         %lu (%.2f MB)\n",
	       stats.slab_pages,
	       (double)stats.slab_pages * PAGE_SIZE / (1024.0 * 1024));
	printk(KERN_INFO "  保留页面:         %lu\n", stats.reserved_pages);
	printk(KERN_INFO "  文件缓存页面:     %lu (%.2f MB)\n",
	       stats.file_pages,
	       (double)stats.file_pages * PAGE_SIZE / (1024.0 * 1024));
	printk(KERN_INFO "  匿名页面:         %lu\n", stats.anon_pages);
	printk(KERN_INFO "  脏页:             %lu\n", stats.dirty_pages);
	printk(KERN_INFO "  映射页面:         %lu\n", stats.mapped_pages);
	printk(KERN_INFO "  页表页面:         %lu\n", stats.page_table_pages);
	printk(KERN_INFO "  内核栈页面:       %lu (%.2f MB)\n",
	       stats.kernel_stack_pages,
	       (double)stats.kernel_stack_pages * PAGE_SIZE / (1024.0 * 1024));
	printk(KERN_INFO "  已使用页面:       %lu (%.2f GB)\n",
	       stats.total_pages - stats.free_pages,
	       (double)(stats.total_pages - stats.free_pages) * PAGE_SIZE /
	       (1024.0 * 1024 * 1024));

	/* 系统内存信息 */
	printk(KERN_INFO "------------------------------------------\n");
	printk(KERN_INFO "[page_stats] 系统内存信息 (/proc/meminfo):\n");

	{
		struct sysinfo si;
		si_meminfo(&si);
		printk(KERN_INFO "  总内存:    %lu KB\n", si.totalram * 4);
		printk(KERN_INFO "  空闲内存:  %lu KB\n", si.freeram * 4);
		printk(KERN_INFO "  共享内存:  %lu KB\n", si.sharedram * 4);
		printk(KERN_INFO "  缓冲内存:  %lu KB\n", si.bufferram * 4);
		printk(KERN_INFO "  总交换:    %lu KB\n", si.totalswap * 4);
		printk(KERN_INFO "  空闲交换:  %lu KB\n", si.freeswap * 4);
	}

	printk(KERN_INFO "==========================================\n");
	printk(KERN_INFO "[page_stats] 统计完成。可与 /proc/meminfo 对比。\n");
	printk(KERN_INFO "==========================================\n");

	return 0;
}

static void __exit page_stats_exit(void)
{
	printk(KERN_INFO "[page_stats] 模块卸载\n");
}

module_init(page_stats_init);
module_exit(page_stats_exit);
