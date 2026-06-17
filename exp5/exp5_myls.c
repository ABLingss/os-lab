/*
 * 实验5：Linux目录下递归拷贝的多进程实现 — myls.c
 *
 * 功能：遍历目录，每遇到普通文件就fork子进程exec mycp拷贝，
 *       子目录则递归处理。
 *
 * 编译：gcc exp5_myls.c -o myls
 * 用法：./myls <源目录> <目标目录>
 *
 * 依赖：需要同目录下有编译好的 mycp 可执行文件
 *       PATH 或当前目录中需要能找到 mycp
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>

/* 最大同时运行的子进程数 */
#define MAX_CHILDREN 16

static int running_children = 0;

/* 等待一个子进程结束 */
static void wait_one_child(void)
{
	int status;
	pid_t pid = wait(&status);
	if (pid > 0) {
		if (WIFEXITED(status))
			printf("[myls] 子进程 %d 完成 (exit=%d)\n",
			       pid, WEXITSTATUS(status));
		else
			printf("[myls] 子进程 %d 异常退出\n", pid);
		running_children--;
	}
}

/* 等待所有子进程结束 */
static void wait_all_children(void)
{
	while (running_children > 0)
		wait_one_child();
}

/* 递归遍历目录并fork拷贝 */
static int process_directory(const char *src_dir, const char *dst_dir)
{
	DIR *dir;
	struct dirent *entry;
	struct stat st;
	char src_path[PATH_MAX];
	char dst_path[PATH_MAX];

	dir = opendir(src_dir);
	if (dir == NULL) {
		fprintf(stderr, "错误: 无法打开目录 %s: %s\n",
			src_dir, strerror(errno));
		return -1;
	}

	/* 确保目标目录存在 */
	if (mkdir(dst_dir, 0755) == -1 && errno != EEXIST) {
		fprintf(stderr, "错误: 无法创建目录 %s: %s\n",
			dst_dir, strerror(errno));
		closedir(dir);
		return -1;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		snprintf(src_path, sizeof(src_path), "%s/%s",
			 src_dir, entry->d_name);
		snprintf(dst_path, sizeof(dst_path), "%s/%s",
			 dst_dir, entry->d_name);

		if (lstat(src_path, &st) == -1) {
			fprintf(stderr, "警告: 无法获取 %s 状态: %s\n",
				src_path, strerror(errno));
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			/* 子目录：递归处理 */
			printf("[myls] 进入子目录: %s\n", src_path);
			process_directory(src_path, dst_path);

		} else if (S_ISREG(st.st_mode)) {
			/* 普通文件：fork子进程调用mycp */
			pid_t pid = fork();

			if (pid == -1) {
				perror("fork");
				continue;
			}

			if (pid == 0) {
				/* 子进程：exec mycp */
				printf("  [子进程 %d] 拷贝 %s\n",
				       getpid(), src_path);

				/*
				 * 检查目标是否存在，若存在传 -y 自动覆盖
				 * 实际项目中可改为询问，这里为了演示
				 * 自动覆盖模式
				 */
				execlp("./mycp", "mycp",
				       src_path, dst_path, (char *)NULL);

				/* 如果 exec 失败 */
				fprintf(stderr, "  [子进程 %d] exec mycp 失败: %s\n",
					getpid(), strerror(errno));
				_exit(EXIT_FAILURE);
			}

			/* 父进程：记录子进程 */
			printf("[myls] 创建子进程 %d: %s\n", pid, src_path);
			running_children++;

			/* 控制并发数，超过上限时等待 */
			while (running_children >= MAX_CHILDREN)
				wait_one_child();
		}
	}

	closedir(dir);
	return 0;
}

int main(int argc, char *argv[])
{
	struct stat src_st, dst_st;

	if (argc != 3) {
		fprintf(stderr, "用法: %s <源目录> <目标目录>\n", argv[0]);
		fprintf(stderr, "示例: %s ./srcdir ./dstdir\n", argv[0]);
		return EXIT_FAILURE;
	}

	/* 验证源目录 */
	if (stat(argv[1], &src_st) == -1) {
		fprintf(stderr, "错误: 源目录 '%s' 不存在: %s\n",
			argv[1], strerror(errno));
		return EXIT_FAILURE;
	}
	if (!S_ISDIR(src_st.st_mode)) {
		fprintf(stderr, "错误: 源路径 '%s' 不是目录\n", argv[1]);
		return EXIT_FAILURE;
	}

	/* 检查目标 */
	if (stat(argv[2], &dst_st) == 0 && !S_ISDIR(dst_st.st_mode)) {
		fprintf(stderr, "错误: 目标路径 '%s' 已存在但不是目录\n", argv[2]);
		return EXIT_FAILURE;
	}

	printf("========================================\n");
	printf("实验5：多进程目录递归拷贝\n");
	printf("源目录: %s\n", argv[1]);
	printf("目标目录: %s\n", argv[2]);
	printf("最大并发: %d\n", MAX_CHILDREN);
	printf("========================================\n\n");

	process_directory(argv[1], argv[2]);
	wait_all_children();

	printf("\n多进程目录拷贝完成！\n");
	printf("可使用 diff -r %s %s 验证拷贝结果\n", argv[1], argv[2]);

	return EXIT_SUCCESS;
}
