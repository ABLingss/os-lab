/*
 * 实验7：Linux设备文件与驱动程序 — 用户态测试程序
 *
 * 编译：gcc exp7_test_driver.c -o test_driver
 * 运行：./test_driver
 *
 * 前置：sudo insmod char_driver.ko
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#define DEVICE_PATH "/dev/mychardev"
#define IOCTL_PRINT_CURRENT 0x01
#define IOCTL_REFRESH_LIST  0x02

int main(void)
{
	int fd;
	char buf[4096];
	ssize_t n;

	printf("=== 实验7：字符设备驱动测试 ===\n\n");

	/* 1. 打开设备 */
	fd = open(DEVICE_PATH, O_RDWR);
	if (fd == -1) {
		fprintf(stderr, "错误: 无法打开 %s: %s\n",
			DEVICE_PATH, strerror(errno));
		fprintf(stderr, "请确保已加载驱动: sudo insmod char_driver.ko\n");
		return EXIT_FAILURE;
	}
	printf("[1] 设备 %s 打开成功 (fd=%d)\n", DEVICE_PATH, fd);

	/* 2. 写入数据 */
	const char *msg = "Hello from userspace!";
	n = write(fd, msg, strlen(msg));
	printf("[2] 写入 %zd 字节: \"%s\"\n", n, msg);

	/* 3. 读取进程列表 */
	printf("[3] 读取进程列表:\n");
	printf("----------------------------------------\n");
	n = read(fd, buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		printf("%s", buf);
	}
	printf("----------------------------------------\n");

	/* 4. ioctl — 打印当前进程信息 */
	printf("[4] ioctl(IOCTL_PRINT_CURRENT) — 查看 dmesg\n");
	ioctl(fd, IOCTL_PRINT_CURRENT);

	/* 5. ioctl — 刷新进程列表 */
	printf("[5] ioctl(IOCTL_REFRESH_LIST)\n");
	ioctl(fd, IOCTL_REFRESH_LIST);

	/* 6. 再次读取 */
	printf("[6] 再次读取进程列表:\n");
	printf("----------------------------------------\n");
	lseek(fd, 0, SEEK_SET);
	n = read(fd, buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		printf("%s", buf);
	}
	printf("----------------------------------------\n");

	/* 7. 关闭设备 */
	close(fd);
	printf("[7] 设备已关闭\n");
	printf("\n请执行 dmesg | tail -20 查看内核日志输出。\n");

	return EXIT_SUCCESS;
}
