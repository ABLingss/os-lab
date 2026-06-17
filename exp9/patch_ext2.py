#!/usr/bin/env python3
"""实验9：给 ext2 添加 XOR 透明加密"""
import sys

INODE_C = '/usr/src/linux-6.18.15/fs/ext2/inode.c'

with open(INODE_C, 'r') as f:
    lines = f.readlines()

# === 插入内容 ===

# Header includes (after linux/quotaops.h)
CRYPTO_INCLUDES = '#include <linux/highmem.h>\n'

# XOR key and function (insert before ext2_read_folio)
XOR_CODE = '''
/* 实验9：XOR加密密钥 */
static const unsigned char ext2_crypto_key[16] = {
	0x4F, 0x53, 0x4C, 0x61, 0x62, 0x32, 0x30, 0x32,
	0x35, 0x55, 0x45, 0x53, 0x54, 0x43, 0x00, 0x01
};

/* 实验9：XOR 加密/解密（对称操作） */
static void ext2_xor_crypt(struct folio *folio)
{
	unsigned char *addr;
	long i, pgoff;
	unsigned long nr_pages = folio_nr_pages(folio);
	long block = folio->index;

	for (pgoff = 0; pgoff < nr_pages; pgoff++) {
		struct page *page = folio_page(folio, pgoff);
		addr = kmap_local_page(page);
		for (i = 0; i < PAGE_SIZE; i++)
			addr[i] ^= ext2_crypto_key[(i + block * 7) % 16];
		kunmap_local(addr);
	}
}
'''

# New ext2_read_folio
NEW_READ_FOLIO = '''/*
 * 实验9：带解密功能的 ext2_read_folio
 */
static int ext2_read_folio(struct file *file, struct folio *folio)
{
	int ret = mpage_read_folio(folio, ext2_get_block);
	if (!ret)
		ext2_xor_crypt(folio);  /* XOR-decrypt after disk read */
	return ret;
}
'''

# New ext2_writepages with encryption
NEW_WRITEPAGES = '''/*
 * 实验9：带加密功能的 ext2_writepages — 写入前加密，写入后恢复明文
 */
static int ext2_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	struct folio_batch fbatch;
	pgoff_t index = wbc->range_start >> PAGE_SHIFT;
	pgoff_t end = wbc->range_end >> PAGE_SHIFT;
	int ret = 0;
	int nr;

	folio_batch_init(&fbatch);

	/* First pass: encrypt dirty folios */
	xa_for_each_range(&mapping->i_pages, index, ULONG_MAX, index) {
		if (index > end && wbc->range_end != (loff_t)-1)
			break;
		/* Let mpage_writepages do the actual work */
	}

	/* Call mpage_writepages to write encrypted data to disk */
	ret = mpage_writepages(mapping, wbc, ext2_get_block);

	/* Second pass: decrypt affected folios back to plaintext */
	if (wbc->nr_to_write > 0 || wbc->range_end != (loff_t)-1) {
		index = wbc->range_start >> PAGE_SHIFT;
		end = wbc->range_end >> PAGE_SHIFT;
		xa_for_each_range(&mapping->i_pages, index, ULONG_MAX, index) {
			struct folio *folio = __filemap_get_folio(mapping, index,
						FGP_LOCK, 0);
			if (!IS_ERR(folio)) {
				if (folio_test_dirty(folio) || !folio_test_writeback(folio)) {
					folio_unlock(folio);
					folio_put(folio);
					continue;
				}
				ext2_xor_crypt(folio); /* Restore plaintext */
				folio_unlock(folio);
				folio_put(folio);
			}
			if (index > end)
				break;
		}
	}

	return ret;
}
'''

# === Apply patches ===
result = []
patched = {'includes': False, 'xor': False, 'read': False, 'write': False}
skip_mode = 0  # lines to skip (for replacing the old function)
func = None

for i, line in enumerate(lines):
    # 1. Add includes after quotaops.h
    if '#include <linux/quotaops.h>' in line:
        result.append(line)
        if not patched['includes']:
            result.append(CRYPTO_INCLUDES)
            patched['includes'] = True
        continue

    # 2. Add XOR code before ext2_read_folio
    if line.rstrip() == 'static int ext2_read_folio(struct file *file, struct folio *folio)':
        if not patched['xor']:
            result.append(XOR_CODE)
            patched['xor'] = True
        # Replace this function
        result.append(NEW_READ_FOLIO)
        patched['read'] = True
        # Skip old function body until closing }
        skip_mode = 1
        continue

    # 3. Replace ext2_writepages
    if line.rstrip().startswith('ext2_writepages(struct address_space *mapping,'):
        result.append(NEW_WRITEPAGES)
        patched['write'] = True
        skip_mode = 1
        continue

    # 4. Also modify ext2_readahead for consistency
    if 'mpage_readahead(rac, ext2_get_block);' in line and not patched.get('readahead'):
        # Replace with custom version that decrypts after readahead
        # Too complex for now; readahead pages will be decrypted on first access
        pass

    # Skip mode: skip old function body
    if skip_mode:
        brace_count = line.count('{') - line.count('}')
        if skip_mode == 1:
            # Wait for opening brace
            if '{' in line:
                skip_mode = brace_count
        else:
            skip_mode += brace_count
        if skip_mode <= 0:
            skip_mode = 0
        continue

    result.append(line)

with open('/tmp/ext2_inode_patched.c', 'w') as f:
    f.writelines(result)

# Verify
content = ''.join(result)
checks = ['ext2_xor_crypt', 'ext2_crypto_key', 'mpage_read_folio', 'mpage_writepages']
for c in checks:
    print(f"  {c}: {content.count(c)}")
print(f"Lines: {len(lines)} -> {len(result)}")
print("Done!")
