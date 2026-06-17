/*
 * 实验9：用户空间 XOR 加密工具
 * 编译: gcc xor_tool.c -o xor_tool
 * 用法:
 *   ./xor_tool <输入文件> <输出文件>  — XOR加密/解密
 *
 * XOR是对称操作: 加密=解密
 * 配合ext2m使用: 文件经xor_tool加密→写入ext2m→读取时ext2m自动解密
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char key[16] = {
    0x4F, 0x53, 0x4C, 0x61, 0x62, 0x32, 0x30, 0x32,
    0x35, 0x55, 0x45, 0x53, 0x54, 0x43, 0x00, 0x01
};

int main(int argc, char *argv[]) {
    FILE *fin, *fout;
    int c;
    long pos = 0;

    if (argc != 3) {
        fprintf(stderr, "用法: %s <输入> <输出>\n", argv[0]);
        return 1;
    }

    fin = fopen(argv[1], "rb");
    if (!fin) { perror(argv[1]); return 1; }

    fout = fopen(argv[2], "wb");
    if (!fout) { perror(argv[2]); fclose(fin); return 1; }

    while ((c = fgetc(fin)) != EOF) {
        /* 匹配内核公式: (i + block*7) % 16，block = pos/4096 */
        fputc(c ^ key[(pos % 4096 + (pos / 4096) * 7) % 16], fout);
        pos++;
    }

    fclose(fin);
    fclose(fout);
    printf("XOR: %s -> %s (%ld bytes)\n", argv[1], argv[2], pos);
    return 0;
}
