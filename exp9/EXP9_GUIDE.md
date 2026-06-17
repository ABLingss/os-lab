# 实验9：Linux文件系统分析及加密文件系统实现 — 操作指导

> 严格按指导书「实验九」内容编写 | 内核编程（文件系统）

---

## 实验概述

在 Linux ext2 文件系统中增加透明加密功能。在文件读写路径中插入加密/解密逻辑，实现简单的加密文件系统。

- **学时**: 8（含较多代码编写和调试时间）
- **类型**: 内核编程（文件系统）
- **难度**: ⭐⭐⭐⭐⭐

**核心知识点**：
- Linux VFS 与 ext2 文件系统内部结构
- 文件读写路径（`file_operations` → `address_space_operations`）
- 内核 Crypto API（`crypto_alloc_skcipher` 等）

---

## 两种方案

| 方案 | 加密方式 | 复杂度 | 说明 |
|------|---------|------|------|
| **A（简单）** | 简单 XOR 加密 | 低 | 在 ext2 读写路径中直接做 XOR |
| **B（完整）** | AES-128-CBC | 高 | 使用内核 Crypto API |

**推荐先从方案A入手**，验证流程正确后再升级方案B。

---

## 需要修改的内核文件

| # | 文件 | 修改内容 |
|---|------|---------|
| 1 | `fs/ext2/inode.c` | 在读写路径添加加密/解密调用 |
| 2 | `fs/ext2/ext2.h` | 添加加密相关字段到超级块/inode |
| 3 | `fs/ext2/super.c` | 挂载时初始化加密上下文 |

---

## 步骤

### 1. 阅读 ext2 源码，理解读写路径

```bash
cd /usr/src/linux-6.18.15/fs/ext2
ls -la
# 关键文件:
#   inode.c  — inode 操作（含 file_operations 和 address_space_operations）
#   file.c   — ext2_file_read_iter / ext2_file_write_iter
#   super.c  — 超级块操作
#   ext2.h   — 数据结构定义
```

### 2. 实现 XOR 加密（方案A）

参考 `exp9_ext2_crypto_ref.c` 中的代码。

**修改 file.c**：在 `ext2_file_read_iter()` 读取后解密，在 `ext2_file_write_iter()` 写入前加密。

```bash
sudo vim /usr/src/linux-6.18.15/fs/ext2/file.c
```

> 📸 **截图1**：修改后的 ext2 代码片段

### 3. 重新编译内核

```bash
cd /usr/src/linux-6.18.15
sudo make -j$(nproc)
sudo make modules_install && sudo make install && sudo update-grub
sudo reboot
```

> 📸 **截图2**：编译内核成功

### 4. 创建加密 ext2 分区并测试

```bash
# 创建测试镜像
dd if=/dev/zero of=/tmp/encrypted.img bs=1M count=100
mkfs.ext2 /tmp/encrypted.img

# 挂载
sudo mkdir -p /mnt/encrypted
sudo mount -o loop /tmp/encrypted.img /mnt/encrypted

# 创建测试文件
echo "Hello, Encrypted File System!" | sudo tee /mnt/encrypted/test.txt
sudo cat /mnt/encrypted/test.txt
# 应正常显示明文（因为加解密是透明的）
```

> 📸 **截图3**：创建加密分区
> 📸 **截图4**：在加密分区创建/读取文件

### 5. 验证加密效果

```bash
# 用非加密方式直接读取底层块设备
sudo umount /mnt/encrypted
sudo xxd /tmp/encrypted.img | head -30
# 应看到密文（乱码），验证加密生效
```

> 📸 **截图5**：非加密挂载查看（看到的应是密文）

### 6. 重新挂载验证正常读写

```bash
sudo mount -o loop /tmp/encrypted.img /mnt/encrypted
sudo cat /mnt/encrypted/test.txt
# 在新内核下应正常显示明文
```

> 📸 **截图6**：加密分区正常读写验证

---

## 截图清单（共6张）

| # | 内容 | 步骤 |
|---|------|------|
| 1 | 修改后的 ext2 代码 | 2 |
| 2 | 编译内核成功 | 3 |
| 3 | 创建加密 ext2 分区 | 4 |
| 4 | 加密分区创建/读取文件（明文） | 4 |
| 5 | 非加密方式查看（密文） | 5 |
| 6 | 加密分区正常读写 | 6 |

---

## 实验原理要点（供写报告参考）

### 透明加密
文件系统层面的加密对用户透明：用户读写文件时，加密/解密在内核文件系统层自动完成。应用程序无需修改。

### XOR 加密原理
- 加密：`ciphertext = plaintext XOR key`
- 解密：`plaintext = ciphertext XOR key`
- XOR 两次 key 恢复原文（对称操作）
- 每个块用不同密钥偏移（基于块号），防止相同明文产生相同密文

### AES 加密（方案B）
使用内核 Crypto API 的 `cbc(aes)` 算法，IV 由 inode 号 + 块号生成，确保不同文件/不同块的加密结果不同。

---

## 本目录文件

| 文件 | 说明 |
|------|------|
| `exp9_ext2_crypto_ref.c` | ext2 加密修改参考代码 |
| `EXP9_GUIDE.md` | 本文件 |
