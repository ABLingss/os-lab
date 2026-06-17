/*
 * 实验9：Linux文件系统分析及加密文件系统实现 — 内核参考代码
 *
 * 方案A（简单）：在 ext2 的 file_operations 的 read/write 路径中添加加解密
 * 方案B（完整）：使用内核 Crypto API (AES) 实现透明加密
 *
 * 说明：本文件是修改 fs/ext2/inode.c（或创建 fs/ext2m/ 目录）的参考代码，
 *       不可单独编译。
 *
 * 参考位置：/usr/src/linux-6.18.15/fs/ext2/
 */

// ================================================================
// 第一部分：加密/解密辅助函数（添加到 ext2 相关文件中）
// ================================================================

/*
 * 简单 XOR 加密（方案A，最简实现）
 * 用于快速演示加密文件系统概念
 *
 * 密钥：固定 16 字节，存储在超级块中或硬编码
 * 实际项目应使用内核 Crypto API
 */
static const unsigned char ext2_encrypt_key[16] = {
	0x4F, 0x53, 0x4C, 0x61, 0x62, 0x32, 0x30, 0x32,
	0x35, 0x55, 0x45, 0x53, 0x54, 0x43, 0x00, 0x01
};

/* XOR 加密/解密（对称算法，加密和解密操作相同） */
static void ext2_xor_crypt(const unsigned char *key, size_t key_len,
			    unsigned char *data, size_t data_len,
			    unsigned long block_num)
{
	size_t i;
	/*
	 * 每个块用不同的密钥偏移量（基于块号），
	 * 确保相同内容在不同块加密结果不同
	 */
	size_t key_offset = (block_num * 7) % key_len;

	for (i = 0; i < data_len; i++) {
		data[i] ^= key[(key_offset + i) % key_len];
	}
}


// ================================================================
// 第二部分：在 ext2 的读写路径中插入加解密
// ================================================================

/*
 * 方案A实现思路 — 修改 fs/ext2/inode.c:
 *
 * 1. ext2_file_read_iter() 或 ext2_readpage()
 *    → 读取原始数据后，调用解密函数
 *    → ext2_xor_crypt(key, 16, page_data, PAGE_SIZE, block_num)
 *
 * 2. ext2_file_write_iter() 或 ext2_write_begin()
 *    → 写入数据前，先调用加密函数
 *    → ext2_xor_crypt(key, 16, page_data, PAGE_SIZE, block_num)
 *
 * 简单做法：在 ext2_file_read_iter 和 ext2_file_write_iter 中
 * 插入加密/解密逻辑。
 */


// ================================================================
// 第三部分：使用内核 Crypto API 的完整实现（方案B）
// ================================================================

#if 0  /* 参考代码 — 需要包含头文件后使用 */

#include <crypto/skcipher.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>

/* 全局加密上下文 */
static struct crypto_skcipher *ext2_cipher;
static unsigned char ext2_key[16];  /* 128-bit AES key */
static bool ext2_encryption_enabled = false;

/* 初始化加密引擎 */
static int ext2_crypto_init(void)
{
	ext2_cipher = crypto_alloc_skcipher("cbc(aes)", 0, 0);
	if (IS_ERR(ext2_cipher)) {
		printk(KERN_ERR "[ext2_crypto] Failed to allocate cipher\n");
		return PTR_ERR(ext2_cipher);
	}

	/* 设置密钥 */
	if (crypto_skcipher_setkey(ext2_cipher, ext2_key, 16)) {
		printk(KERN_ERR "[ext2_crypto] Failed to set key\n");
		crypto_free_skcipher(ext2_cipher);
		return -EINVAL;
	}

	ext2_encryption_enabled = true;
	printk(KERN_INFO "[ext2_crypto] AES-128-CBC encryption enabled\n");
	return 0;
}

/* 加密/解密一个页面 */
static int ext2_crypt_page(struct page *page, unsigned long block_num, bool encrypt)
{
	struct crypto_skcipher *cipher = ext2_cipher;
	struct skcipher_request *req;
	struct scatterlist sg;
	unsigned char iv[16];
	unsigned char *buf;
	int ret;

	if (!ext2_encryption_enabled)
		return 0;

	/* 构造 IV: inode号 + 块号 */
	memset(iv, 0, sizeof(iv));
	*(unsigned long *)iv = block_num;

	buf = kmap_local_page(page);

	req = skcipher_request_alloc(cipher, GFP_KERNEL);
	if (!req) {
		kunmap_local(buf);
		return -ENOMEM;
	}

	sg_init_one(&sg, buf, PAGE_SIZE);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, &sg, &sg, PAGE_SIZE, iv);

	if (encrypt)
		ret = crypto_skcipher_encrypt(req);
	else
		ret = crypto_skcipher_decrypt(req);

	skcipher_request_free(req);
	kunmap_local(buf);

	return ret;
}

#endif /* 0 */
