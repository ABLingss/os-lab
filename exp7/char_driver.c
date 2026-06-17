/*
 * 实验7：Linux设备文件与驱动程序 — 字符设备驱动
 *
 * 使用 file_operations 实现 open/read/write/unlocked_ioctl
 * 在操作函数中遍历进程控制块链表（for_each_process），
 * 通过 printk 输出 PID、状态、优先级、父进程ID等信息。
 *
 * 编译：见同目录 Makefile（make char_driver）
 * 加载：sudo insmod char_driver.ko
 * 设备文件：/dev/mychardev（自动创建）
 * 测试：gcc exp7_test_driver.c -o test_driver && ./test_driver
 * 卸载：sudo rmmod char_driver
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/pid.h>
#include <linux/init_task.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ma Yingzhe");
MODULE_DESCRIPTION("Experiment 7 — Character Device Driver with Process Traversal");

#define DEVICE_NAME "mychardev"
#define CLASS_NAME  "mychar"
#define BUFFER_SIZE 4096

/* 设备号 */
static int major_number;
static struct class  *char_class  = NULL;
static struct device *char_device = NULL;
static struct cdev my_cdev;

/* 设备缓冲区 */
static char device_buffer[BUFFER_SIZE];
static size_t buffer_len = 0;

/*
 * 遍历所有进程并格式化输出到 buf
 * 返回写入的字节数
 */
static ssize_t dump_process_list(char *buf, size_t max_len)
{
	struct task_struct *p;
	ssize_t offset = 0;

	offset += scnprintf(buf + offset, max_len - offset,
		"PID\tSTATE\t\tPRIO\tPPID\tCOMMAND\n");
	offset += scnprintf(buf + offset, max_len - offset,
		"-------------------------------------------------------\n");

	rcu_read_lock();
	p = &init_task;
	for_each_process(p) {
		if (offset >= max_len - 128)
			break;
		offset += scnprintf(buf + offset, max_len - offset,
			"%d\t0x%x\t%d\t%d\t%s\n",
			p->pid, p->__state,
			p->prio, p->parent->pid,
			p->comm);
	}
	rcu_read_unlock();

	offset += scnprintf(buf + offset, max_len - offset,
		"-------------------------------------------------------\n");
	offset += scnprintf(buf + offset, max_len - offset,
		"[char_driver] Total processes listed above.\n");

	return offset;
}

/* ========== file_operations 实现 ========== */

static int char_open(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "[char_driver] Device opened by %s (PID=%d)\n",
	       current->comm, current->pid);
	return 0;
}

static ssize_t char_read(struct file *filp, char __user *ubuf,
			 size_t count, loff_t *f_pos)
{
	ssize_t ret;

	/* 每次 read 都重新生成进程列表 */
	buffer_len = dump_process_list(device_buffer, BUFFER_SIZE);

	if (*f_pos >= buffer_len)
		return 0;  /* EOF */

	if (count > buffer_len - *f_pos)
		count = buffer_len - *f_pos;

	ret = copy_to_user(ubuf, device_buffer + *f_pos, count);
	if (ret) {
		printk(KERN_ERR "[char_driver] copy_to_user failed: %zd bytes\n", ret);
		return -EFAULT;
	}

	*f_pos += count;
	printk(KERN_INFO "[char_driver] Read %zu bytes by %s (PID=%d)\n",
	       count, current->comm, current->pid);
	return count;
}

static ssize_t char_write(struct file *filp, const char __user *ubuf,
			  size_t count, loff_t *f_pos)
{
	ssize_t ret;
	size_t to_copy = count;

	if (to_copy > BUFFER_SIZE - 1)
		to_copy = BUFFER_SIZE - 1;

	ret = copy_from_user(device_buffer, ubuf, to_copy);
	if (ret) {
		printk(KERN_ERR "[char_driver] copy_from_user failed: %zd bytes\n", ret);
		return -EFAULT;
	}

	device_buffer[to_copy] = '\0';
	buffer_len = to_copy;

	printk(KERN_INFO "[char_driver] Write %zu bytes by %s (PID=%d): \"%s\"\n",
	       to_copy, current->comm, current->pid, device_buffer);
	return to_copy;
}

static long char_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	printk(KERN_INFO "[char_driver] ioctl called by %s (PID=%d): cmd=%u, arg=0x%lx\n",
	       current->comm, current->pid, cmd, arg);

	switch (cmd) {
	case 0x01:  /* 打印当前进程信息 */
		printk(KERN_INFO "[char_driver] Current process: %s PID=%d "
		       "State=0x%x Prio=%d Parent=%s(%d)\n",
		       current->comm, current->pid, current->__state,
		       current->prio,
		       current->parent->comm, current->parent->pid);
		break;
	case 0x02:  /* 刷新进程列表到设备缓冲区 */
		buffer_len = dump_process_list(device_buffer, BUFFER_SIZE);
		printk(KERN_INFO "[char_driver] Process list refreshed, %zu bytes\n",
		       buffer_len);
		break;
	default:
		printk(KERN_WARNING "[char_driver] Unknown ioctl cmd=%u\n", cmd);
		return -EINVAL;
	}
	return 0;
}

static int char_release(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "[char_driver] Device closed by %s (PID=%d)\n",
	       current->comm, current->pid);
	return 0;
}

static struct file_operations fops = {
	.owner          = THIS_MODULE,
	.open           = char_open,
	.read           = char_read,
	.write          = char_write,
	.unlocked_ioctl = char_ioctl,
	.release        = char_release,
};

/* ========== 模块初始化和退出 ========== */

static int __init char_driver_init(void)
{
	dev_t dev_num;
	int ret;

	printk(KERN_INFO "[char_driver] Initializing character device driver...\n");

	/* 1. 动态分配设备号 */
	ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		printk(KERN_ERR "[char_driver] alloc_chrdev_region failed: %d\n", ret);
		return ret;
	}
	major_number = MAJOR(dev_num);
	printk(KERN_INFO "[char_driver] Major number: %d\n", major_number);

	/* 2. 初始化并注册 cdev */
	cdev_init(&my_cdev, &fops);
	my_cdev.owner = THIS_MODULE;
	ret = cdev_add(&my_cdev, dev_num, 1);
	if (ret) {
		printk(KERN_ERR "[char_driver] cdev_add failed: %d\n", ret);
		goto err_chrdev;
	}

	/* 3. 创建设备类（udev自动创建 /dev/mychardev） */
	char_class = class_create(CLASS_NAME);
	if (IS_ERR(char_class)) {
		ret = PTR_ERR(char_class);
		printk(KERN_ERR "[char_driver] class_create failed: %d\n", ret);
		goto err_cdev;
	}

	char_device = device_create(char_class, NULL, dev_num,
				    NULL, DEVICE_NAME);
	if (IS_ERR(char_device)) {
		ret = PTR_ERR(char_device);
		printk(KERN_ERR "[char_driver] device_create failed: %d\n", ret);
		goto err_class;
	}

	printk(KERN_INFO "[char_driver] Character device /dev/%s created successfully!\n",
	       DEVICE_NAME);
	return 0;

err_class:
	class_destroy(char_class);
err_cdev:
	cdev_del(&my_cdev);
err_chrdev:
	unregister_chrdev_region(dev_num, 1);
	return ret;
}

static void __exit char_driver_exit(void)
{
	dev_t dev_num = MKDEV(major_number, 0);

	device_destroy(char_class, dev_num);
	class_destroy(char_class);
	cdev_del(&my_cdev);
	unregister_chrdev_region(dev_num, 1);

	printk(KERN_INFO "[char_driver] Character device driver removed.\n");
}

module_init(char_driver_init);
module_exit(char_driver_exit);
