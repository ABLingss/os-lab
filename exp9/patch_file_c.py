#!/usr/bin/env python3
"""实验9：修改 ext2m/file.c — XOR at read_iter/write_iter level"""
import sys

FILE_C = '/usr/src/linux-6.18.15/fs/ext2m/file.c'

with open(FILE_C, 'r') as f:
    c = f.read()

XOR = '''
/* 实验9：XOR加密密钥 */
static const unsigned char ext2m_key[16] = {
	0x4F, 0x53, 0x4C, 0x61, 0x62, 0x32, 0x30, 0x32,
	0x35, 0x55, 0x45, 0x53, 0x54, 0x43, 0x00, 0x01
};

static void ext2m_xor_buf(char *buf, size_t len, loff_t pos)
{
	size_t i;
	unsigned long block = pos >> PAGE_SHIFT;
	for (i = 0; i < len; i++)
		buf[i] ^= ext2m_key[(i + block * 7) % 16];
}

'''

OLD_FUNCS = '''
/* ext2m_file_read_iter: read page via mapping, decrypt, copy to user */
static ssize_t ext2m_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *filp = iocb->ki_filp;
	struct address_space *mapping = filp->f_mapping;
	loff_t pos = iocb->ki_pos;
	pgoff_t index = pos >> PAGE_SHIFT;
	unsigned offset = pos & (PAGE_SIZE - 1);
	size_t count = iov_iter_count(to);
	struct page *page;
	ssize_t ret;
	void *addr;
	size_t n;
	int err;

#ifdef CONFIG_FS_DAX
	if (IS_DAX(mapping->host))
		return ext2_dax_read_iter(iocb, to);
#endif
	if (iocb->ki_flags & IOCB_DIRECT)
		return ext2_dio_read_iter(iocb, to);

	/* read one page at a time */
	n = min3(count, (size_t)PAGE_SIZE - offset, (size_t)PAGE_SIZE);
	page = read_mapping_page(mapping, index, filp);
	if (IS_ERR(page))
		return PTR_ERR(page);

	addr = kmap_local_page(page);
	ext2m_xor_buf(addr, PAGE_SIZE, pos); /* XOR decrypt */
	memcpy_from_iter(addr + offset, n, to); /* this copies TO our buffer... wait */
	kunmap_local(addr);

	/* Actually: decrypt the page, then copy_to_iter */
	addr = kmap_local_page(page);
	ext2m_xor_buf(addr, PAGE_SIZE, pos);
	ret = copy_to_iter(addr + offset, n, to);
	kunmap_local(addr);

	if (ret != n) {
		put_page(page);
		return -EFAULT;
	}
	put_page(page);
	iocb->ki_pos += n;
	file_accessed(filp);
	return n;
}

/* ext2m_file_write_iter: copy from user, encrypt, write via mapping */
static ssize_t ext2m_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *filp = iocb->ki_filp;
	struct address_space *mapping = filp->f_mapping;
	loff_t pos = iocb->ki_pos;
	size_t count = iov_iter_count(from);
	struct folio *folio;
	pgoff_t index = pos >> PAGE_SHIFT;
	unsigned offset = pos & (PAGE_SIZE - 1);
	char *buf;
	ssize_t ret;
	size_t n;

#ifdef CONFIG_FS_DAX
	if (IS_DAX(mapping->host))
		return ext2_dax_write_iter(iocb, from);
#endif
	if (iocb->ki_flags & IOCB_DIRECT)
		return ext2_dio_write_iter(iocb, from);

	/* write at most one page */
	n = min3(count, (size_t)PAGE_SIZE - offset, (size_t)PAGE_SIZE);
	buf = kmalloc(n, GFP_KERNEL);
	if (!buf) return -ENOMEM;

	if (copy_from_iter(buf, n, from) != n) {
		kfree(buf);
		return -EFAULT;
	}

	ext2m_xor_buf(buf, n, pos); /* XOR encrypt */

	/* write to page cache */
	folio = filemap_grab_folio(mapping, index);
	if (IS_ERR(folio)) {
		kfree(buf);
		return PTR_ERR(folio);
	}

	ret = filemap_write_and_wait_range(mapping, pos, pos + n - 1);
	if (ret) goto out;

	memcpy_to_folio(folio, offset, buf, n);
	folio_mark_dirty(folio);
	folio_unlock(folio);
	folio_put(folio);

	iocb->ki_pos += n;
	file_update_time(filp);
	ret = generic_write_sync(iocb, n);
	kfree(buf);
	return ret;

out:
	folio_unlock(folio);
	folio_put(folio);
	kfree(buf);
	return ret;
}

'''

# Apply changes
c = c.replace('#include "trace.h"\n', '#include "trace.h"\n' + XOR)

# Replace read_iter
# find "static ssize_t ext2_file_read_iter" → "static ssize_t ext2m_file_read_iter"
idx = c.find('#ifdef CONFIG_FS_DAX\n\tif (IS_DAX(iocb->ki_filp->f_mapping->host))\n\t\treturn ext2_dax_read_iter(iocb, to);\n#endif\n\tif (iocb->ki_flags & IOCB_DIRECT)\n\t\treturn ext2_dio_read_iter(iocb, to);\n\n\treturn generic_file_read_iter(iocb, to);')

if idx >= 0:
    c = c.replace(
        '#ifdef CONFIG_FS_DAX\n\tif (IS_DAX(iocb->ki_filp->f_mapping->host))\n\t\treturn ext2_dax_read_iter(iocb, to);\n#endif\n\tif (iocb->ki_flags & IOCB_DIRECT)\n\t\treturn ext2_dio_read_iter(iocb, to);\n\n\treturn generic_file_read_iter(iocb, to);',
        '#ifdef CONFIG_FS_DAX\n\tif (IS_DAX(mapping->host))\n\t\treturn ext2_dax_read_iter(iocb, to);\n#endif\n\tif (iocb->ki_flags & IOCB_DIRECT)\n\t\treturn ext2_dio_read_iter(iocb, to);\n\treturn generic_file_read_iter(iocb, to);'
    )
    print("Read iter base replaced")

# Actually, simpler approach: generic_file_read_iter + xor iov post-processing
# Let me rewrite the whole replacement logic
# The idea: keep calling generic_file_read_iter, then walk the iov to XOR the read data.
# But walking an iov after read is complex.

# SIMPLEST CORRECT approach:
# use generic_file_read_iter normally (reads plaintext from disk),
# then use xor_tool to ENCRYPT on write instead of decrypt on read.
# Actually, just make the ext2m write path encrypt before writing to disk,
# and the read path decrypt after reading from disk.

# But if read path is hard, let's do it differently:
# Only encrypt in write path. Read path stays normal.
# User writes plaintext → ext2m encrypts → disk has ciphertext
# User reads via ext2m → gets ciphertext → uses xor_tool to decrypt
# OR: ext2m read path does generic_file_read_iter + xor_tool

# Actually the simplest is: modify read_iter to use a buffer approach
# WITHOUT calling kernel_read (which recurses).

print("Switching to final clean approach")
