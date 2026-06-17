#!/usr/bin/env python3
"""实验9：创建 ext2m 加密文件系统 — ext2 的加密版本"""
import os, re, sys

FSDIR = '/usr/src/linux-6.18.15/fs/ext2m'
os.chdir(FSDIR)

# ==== Step 1: Rename ext2 → ext2m in all files ====
for root, dirs, files in os.walk('.'):
    for fn in files:
        if fn.endswith(('.c', '.h', 'Kconfig', 'Makefile')):
            path = os.path.join(root, fn)
            with open(path, 'r') as f:
                content = f.read()
            new = content.replace('ext2', 'ext2m').replace('EXT2', 'EXT2M')
            with open(path, 'w') as f:
                f.write(new)

# Fix: ext2m_get_block etc. — the function names got renamed but external refs may need fixing
# Actually the simple replace should handle most cases

# ==== Step 2: Add XOR encryption to inode.c ====
with open('inode.c', 'r') as f:
    lines = f.readlines()

# Find insertion points
xor_inserted = False
read_folio_fixed = False
writepages_fixed = False
result = []

# XOR key and function
XOR_CODE = '''
/* 实验9：XOR透明加密密钥 */
static const unsigned char ext2m_crypto_key[16] = {
	0x4F, 0x53, 0x4C, 0x61, 0x62, 0x32, 0x30, 0x32,
	0x35, 0x55, 0x45, 0x53, 0x54, 0x43, 0x00, 0x01
};

/* XOR加密/解密一个folio的所有页面 */
static void ext2m_xor_crypt(struct folio *folio)
{
	unsigned char *addr;
	long i, p;
	unsigned long nr = folio_nr_pages(folio);
	unsigned long block = folio->index;

	for (p = 0; p < nr; p++) {
		struct page *page = folio_page(folio, p);
		addr = kmap_local_page(page);
		for (i = 0; i < PAGE_SIZE; i++)
			addr[i] ^= ext2m_crypto_key[(i + (block + p) * 7) % 16];
		kunmap_local(addr);
	}
}
'''

i = 0
skip = 0
while i < len(lines):
    line = lines[i]

    # Add XOR code before ext2m_read_folio
    if 'static int ext2m_read_folio' in line and not xor_inserted:
        result.append(XOR_CODE)
        xor_inserted = True

    # Replace ext2m_read_folio: add decrypt after mpage_read_folio
    if line.strip().startswith('static int ext2m_read_folio'):
        result.append('/* 实验9：带解密功能的 read_folio */\n')
        result.append('static int ext2m_read_folio(struct file *file, struct folio *folio)\n')
        result.append('{\n')
        result.append('\tint ret = mpage_read_folio(folio, ext2m_get_block);\n')
        result.append('\tif (!ret)\n')
        result.append('\t\text2m_xor_crypt(folio);\n')
        result.append('\treturn ret;\n')
        result.append('}\n')
        # Skip original function body
        i += 1
        brace_depth = 0
        while i < len(lines):
            brace_depth += lines[i].count('{') - lines[i].count('}')
            i += 1
            if brace_depth <= 0:
                break
        read_folio_fixed = True
        continue

    # Replace ext2m_writepages: encrypt before writing
    if 'ext2m_writepages(struct address_space *mapping' in line and 'mpage_writepages' not in line:
        result.append('/* 实验9：加密后写盘，写后恢复明文 */\n')
        result.append('static int\n')
        result.append('ext2m_writepages(struct address_space *mapping,\n')
        result.append('\t\tstruct writeback_control *wbc)\n')
        result.append('{\n')
        result.append('\tint ret;\n')
        result.append('\tstruct folio *folio;\n')
        result.append('\tpgoff_t idx;\n')
        result.append('\n')
        result.append('\t/* 1. 加密脏页面 */\n')
        result.append('\txa_for_each(&mapping->i_pages, idx, folio) {\n')
        result.append('\t\tif (idx < wbc->range_start >> PAGE_SHIFT)\n')
        result.append('\t\t\tcontinue;\n')
        result.append('\t\tif (wbc->range_end != (loff_t)-1 && idx > wbc->range_end >> PAGE_SHIFT)\n')
        result.append('\t\t\tbreak;\n')
        result.append('\t\tfolio_lock(folio);\n')
        result.append('\t\tif (folio_test_dirty(folio))\n')
        result.append('\t\t\text2m_xor_crypt(folio);\n')
        result.append('\t\tfolio_unlock(folio);\n')
        result.append('\t}\n')
        result.append('\n')
        result.append('\t/* 2. 写入磁盘 */\n')
        result.append('\tret = mpage_writepages(mapping, wbc, ext2m_get_block);\n')
        result.append('\n')
        result.append('\t/* 3. 恢复明文 */\n')
        result.append('\txa_for_each(&mapping->i_pages, idx, folio) {\n')
        result.append('\t\tfolio_lock(folio);\n')
        result.append('\t\tif (folio_test_dirty(folio) || folio_test_writeback(folio)) {\n')
        result.append('\t\t\tfolio_unlock(folio);\n')
        result.append('\t\t\tcontinue;\n')
        result.append('\t\t}\n')
        result.append('\t\text2m_xor_crypt(folio);\n')
        result.append('\t\tfolio_unlock(folio);\n')
        result.append('\t}\n')
        result.append('\treturn ret;\n')
        result.append('}\n')
        # Skip original
        i += 1
        brace_depth = 0
        while i < len(lines):
            brace_depth += lines[i].count('{') - lines[i].count('}')
            i += 1
            if brace_depth <= 0:
                break
        writepages_fixed = True
        continue

    result.append(line)
    i += 1

with open('inode.c', 'w') as f:
    f.writelines(result)

# Verify
content = ''.join(result)
for tag in ['ext2m_xor_crypt', 'ext2m_crypto_key', 'mpage_read_folio', 'ext2m_get_block']:
    count = content.count(tag)
    print(f'  {tag}: {count}')
print(f'  read_folio: {read_folio_fixed}')
print(f'  writepages: {writepages_fixed}')
print(f'  xor_inserted: {xor_inserted}')

# ==== Step 3: Register in fs/Makefile and fs/Kconfig ====
print('\n=== Step 3: Register ext2m ===')
# Already done: ext2m/ has its own Makefile and Kconfig (inherited from ext2)
# Need to add ext2m to fs/Makefile and fs/Kconfig

with open('/usr/src/linux-6.18.15/fs/Kconfig', 'r') as f:
    content = f.read()

if 'source "fs/ext2m/Kconfig"' not in content:
    content = content.replace('source "fs/ext2/Kconfig"',
                              'source "fs/ext2/Kconfig"\nsource "fs/ext2m/Kconfig"')
    with open('/usr/src/linux-6.18.15/fs/Kconfig', 'w') as f:
        f.write(content)
    print('  Added ext2m to fs/Kconfig')

with open('/usr/src/linux-6.18.15/fs/Makefile', 'r') as f:
    content = f.read()

if 'CONFIG_EXT2M_FS' not in content:
    content = content.replace('obj-$(CONFIG_EXT2_FS)',
                              'obj-$(CONFIG_EXT2M_FS)\t+= ext2m/\nobj-$(CONFIG_EXT2_FS)')
    with open('/usr/src/linux-6.18.15/fs/Makefile', 'w') as f:
        f.write(content)
    print('  Added ext2m to fs/Makefile')

# ==== Step 4: Enable CONFIG_EXT2M_FS in kernel config ====
with open('/usr/src/linux-6.18.15/.config', 'r') as f:
    content = f.read()

if 'CONFIG_EXT2M_FS' not in content:
    with open('/usr/src/linux-6.18.15/.config', 'a') as f:
        f.write('\nCONFIG_EXT2M_FS=y\n')
    print('  Enabled CONFIG_EXT2M_FS=y')

print('\nDone! ext2m filesystem ready.')
