/*
 * 实验5：Linux目录下递归拷贝的单进程实现 — mycp.c
 *
 * 功能：文件和目录的递归拷贝
 *   - 文件 → 文件拷贝
 *   - 文件 → 目录拷贝（文件放入目标目录）
 *   - 目录 → 目录拷贝（递归复制所有内容）
 *
 * 编译：gcc exp5_mycp.c -o mycp
 * 用法：./mycp <源路径> <目标路径>
 *
 * 交互提示：
 *   - 目标文件已存在 → 提示是否覆盖 (y/n)
 *   - 目标目录已存在 → 提示是否合并 (y/n)
 *
 * 错误处理：
 *   - 源路径不存在 → 报错退出
 *   - 目标路径为文件时尝试放入文件 → 报错
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <libgen.h>
#include <limits.h>

#define BUFFER_SIZE 8192

/* 询问用户是否覆盖/合并 */
static int ask_yesno(const char *prompt)
{
	char answer[16];
	printf("%s (y/n): ", prompt);
	fflush(stdout);
	if (fgets(answer, sizeof(answer), stdin) == NULL)
		return 0;
	return (answer[0] == 'y' || answer[0] == 'Y');
}

/* 拷贝单个文件内容 */
static int copy_file(const char *src_path, const char *dst_path)
{
	int fd_src, fd_dst;
	ssize_t nread, nwritten;
	char buf[BUFFER_SIZE];
	struct stat st;

	fd_src = open(src_path, O_RDONLY);
	if (fd_src == -1) {
		fprintf(stderr, "错误: 无法打开源文件 %s: %s\n",
			src_path, strerror(errno));
		return -1;
	}

	/* 获取源文件权限 */
	if (fstat(fd_src, &st) == -1) {
		fprintf(stderr, "错误: 无法获取源文件状态 %s: %s\n",
			src_path, strerror(errno));
		close(fd_src);
		return -1;
	}

	/* 打开目标文件（创建或截断） */
	fd_dst = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
	if (fd_dst == -1) {
		fprintf(stderr, "错误: 无法创建目标文件 %s: %s\n",
			dst_path, strerror(errno));
		close(fd_src);
		return -1;
	}

	/* 循环读写 */
	while ((nread = read(fd_src, buf, sizeof(buf))) > 0) {
		char *ptr = buf;
		ssize_t remaining = nread;
		while (remaining > 0) {
			nwritten = write(fd_dst, ptr, remaining);
			if (nwritten == -1) {
				fprintf(stderr, "错误: 写入目标文件失败: %s\n",
					strerror(errno));
				close(fd_src);
				close(fd_dst);
				return -1;
			}
			ptr += nwritten;
			remaining -= nwritten;
		}
	}

	if (nread == -1) {
		fprintf(stderr, "错误: 读取源文件失败: %s\n", strerror(errno));
		close(fd_src);
		close(fd_dst);
		return -1;
	}

	close(fd_src);
	close(fd_dst);

	/* 保留源文件的时间戳 */
	struct timespec times[2];
	times[0] = st.st_atim;
	times[1] = st.st_mtim;
	utimensat(AT_FDCWD, dst_path, times, 0);

	return 0;
}

/* 递归拷贝目录 */
static int copy_directory(const char *src_dir, const char *dst_dir)
{
	DIR *dir;
	struct dirent *entry;
	struct stat st;
	char src_path[PATH_MAX];
	char dst_path[PATH_MAX];

	dir = opendir(src_dir);
	if (dir == NULL) {
		fprintf(stderr, "错误: 无法打开源目录 %s: %s\n",
			src_dir, strerror(errno));
		return -1;
	}

	/* 确保目标目录存在 */
	if (mkdir(dst_dir, 0755) == -1 && errno != EEXIST) {
		fprintf(stderr, "错误: 无法创建目标目录 %s: %s\n",
			dst_dir, strerror(errno));
		closedir(dir);
		return -1;
	}

	/* 遍历目录中的所有条目 */
	while ((entry = readdir(dir)) != NULL) {
		/* 跳过 . 和 .. */
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		snprintf(src_path, sizeof(src_path), "%s/%s",
			 src_dir, entry->d_name);
		snprintf(dst_path, sizeof(dst_path), "%s/%s",
			 dst_dir, entry->d_name);

		if (lstat(src_path, &st) == -1) {
			fprintf(stderr, "警告: 无法获取 %s 状态，跳过: %s\n",
				src_path, strerror(errno));
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			/* 递归处理子目录 */
			printf("  进入目录: %s\n", src_path);
			if (copy_directory(src_path, dst_path) != 0) {
				closedir(dir);
				return -1;
			}
		} else if (S_ISREG(st.st_mode)) {
			/* 拷贝普通文件 */
			printf("  拷贝文件: %s → %s\n", src_path, dst_path);

			/* 检查目标文件是否已存在 */
			struct stat dst_st;
			if (stat(dst_path, &dst_st) == 0) {
				if (!ask_yesno("    目标文件已存在，是否覆盖？"))
					continue;
			}

			if (copy_file(src_path, dst_path) != 0) {
				fprintf(stderr, "警告: 拷贝 %s 失败\n", src_path);
			}
		} else if (S_ISLNK(st.st_mode)) {
			/* 拷贝符号链接 */
			char link_target[PATH_MAX];
			ssize_t len = readlink(src_path, link_target,
					      sizeof(link_target) - 1);
			if (len != -1) {
				link_target[len] = '\0';
				/* 先删除可能存在的旧链接 */
				unlink(dst_path);
				if (symlink(link_target, dst_path) == -1) {
					fprintf(stderr, "警告: 无法创建符号链接 %s: %s\n",
						dst_path, strerror(errno));
				} else {
					printf("  符号链接: %s → %s\n",
					       dst_path, link_target);
				}
			}
		}
	}

	closedir(dir);
	return 0;
}

int main(int argc, char *argv[])
{
	struct stat src_st, dst_st;

	if (argc != 3) {
		fprintf(stderr, "用法: %s <源路径> <目标路径>\n", argv[0]);
		fprintf(stderr, "示例:\n");
		fprintf(stderr, "  %s file.txt dir/        # 文件到目录\n", argv[0]);
		fprintf(stderr, "  %s file.txt file2.txt   # 文件到文件\n", argv[0]);
		fprintf(stderr, "  %s dir1/ dir2/          # 目录递归拷贝\n", argv[0]);
		return EXIT_FAILURE;
	}

	const char *src = argv[1];
	const char *dst = argv[2];

	/* ---- 1. 检查源路径是否存在 ---- */
	if (lstat(src, &src_st) == -1) {
		fprintf(stderr, "错误: 源路径 '%s' 不存在或无法访问: %s\n",
			src, strerror(errno));
		return EXIT_FAILURE;
	}

	/* ---- 2. 根据源类型和目标状态决定拷贝策略 ---- */
	int dst_exists = (lstat(dst, &dst_st) == 0);

	if (S_ISREG(src_st.st_mode)) {
		/* 源是普通文件 */

		if (dst_exists && S_ISDIR(dst_st.st_mode)) {
			/* 目标存在且是目录：文件 → 目录 */
			char dst_file[PATH_MAX];
			char *src_name = basename(strdup(src));
			snprintf(dst_file, sizeof(dst_file), "%s/%s",
				 dst, src_name);
			printf("拷贝文件: %s → %s\n", src, dst_file);
			if (copy_file(src, dst_file) == 0)
				printf("拷贝成功！\n");

		} else if (dst_exists && S_ISREG(dst_st.st_mode)) {
			/* 目标存在且是普通文件：提示覆盖 */
			if (ask_yesno("目标文件已存在，是否覆盖？")) {
				printf("拷贝文件: %s → %s\n", src, dst);
				if (copy_file(src, dst) == 0)
					printf("拷贝成功！\n");
			} else {
				printf("取消拷贝。\n");
			}

		} else if (!dst_exists) {
			/* 目标不存在：文件到文件 */
			/*
			 * 如果目标路径以 / 结尾 → 意图是放入目录，
			 * 但目录不存在 → 报错
			 */
			size_t dst_len = strlen(dst);
			if (dst_len > 0 && dst[dst_len - 1] == '/') {
				fprintf(stderr, "错误: 目标目录 '%s' 不存在\n", dst);
				return EXIT_FAILURE;
			}
			printf("拷贝文件: %s → %s\n", src, dst);
			if (copy_file(src, dst) == 0)
				printf("拷贝成功！\n");
		} else {
			fprintf(stderr, "错误: 不支持的目标类型\n");
			return EXIT_FAILURE;
		}

	} else if (S_ISDIR(src_st.st_mode)) {
		/* 源是目录 */

		if (dst_exists && S_ISREG(dst_st.st_mode)) {
			/* 目标是文件 → 不能把目录拷成文件 */
			fprintf(stderr, "错误: 不能将目录 '%s' 拷贝为文件 '%s'\n",
				src, dst);
			return EXIT_FAILURE;

		} else if (dst_exists && S_ISDIR(dst_st.st_mode)) {
			/* 目标存在且是目录：合并或提示 */
			printf("目标目录 '%s' 已存在。\n", dst);
			if (ask_yesno("是否合并目录内容？")) {
				printf("开始递归拷贝目录: %s → %s\n", src, dst);
				if (copy_directory(src, dst) == 0)
					printf("目录递归拷贝完成！\n");
			} else {
				printf("取消拷贝。\n");
			}

		} else if (!dst_exists) {
			/* 目标不存在：创建目录并递归拷贝 */
			printf("开始递归拷贝目录: %s → %s\n", src, dst);
			if (copy_directory(src, dst) == 0)
				printf("目录递归拷贝完成！\n");
		} else {
			fprintf(stderr, "错误: 不支持的目标类型\n");
			return EXIT_FAILURE;
		}

	} else if (S_ISLNK(src_st.st_mode)) {
		/* 源是符号链接 */
		char link_target[PATH_MAX];
		ssize_t len = readlink(src, link_target,
				      sizeof(link_target) - 1);
		if (len == -1) {
			fprintf(stderr, "错误: 无法读取符号链接: %s\n",
				strerror(errno));
			return EXIT_FAILURE;
		}
		link_target[len] = '\0';

		/* 检查目标是否已存在 */
		if (dst_exists) {
			if (!ask_yesno("目标已存在，是否覆盖？"))
				return EXIT_SUCCESS;
		}

		unlink(dst);
		if (symlink(link_target, dst) == -1) {
			fprintf(stderr, "错误: 无法创建符号链接: %s\n",
				strerror(errno));
			return EXIT_FAILURE;
		}
		printf("符号链接拷贝: %s → %s\n", dst, link_target);

	} else {
		fprintf(stderr, "错误: 不支持的源文件类型\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
