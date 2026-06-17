#!/usr/bin/env python3
"""实验9 v2: 精确修改 ext2 → ext2m，只改必要部分"""
import os, re

FSDIR = '/usr/src/linux-6.18.15/fs/ext2m'
os.chdir(FSDIR)

# === Step 1: Rename files ===
for f in list(os.listdir('.')):
    if f.startswith('ext2') and f.endswith(('.c','.h')):
        new = 'ext2m' + f[4:]
        os.rename(f, new)
        print(f'  {f} -> {new}')

# === Step 2: Minimal content changes (only what's needed) ===
changes = {
    'ext2.h': [
        # Filesystem name
        ('#define EXT2_NAME "ext2"', '#define EXT2M_NAME "ext2m"'),
        # extern file_system_type
        ('extern struct file_system_type ext2_fs_type;',
         'extern struct file_system_type ext2m_fs_type;'),
    ],
    'super.c': [
        # module init/exit
        ('module_init(ext2_init)', 'module_init(ext2m_init)'),
        ('module_exit(ext2_exit)', 'module_exit(ext2m_exit)'),
        # init/exit functions
        ('static int __init ext2_init(void)', 'static int __init ext2m_init(void)'),
        ('static void __exit ext2_exit(void)', 'static void __exit ext2m_exit(void)'),
        # fs_type struct
        ('struct file_system_type ext2_fs_type = {',
         'struct file_system_type ext2m_fs_type = {'),
        ('.name\t\t= "ext2"', '.name\t\t= "ext2m"'),
        # MODULE_ALIAS
        ('MODULE_ALIAS_FS("ext2");', 'MODULE_ALIAS_FS("ext2m");'),
        # in super.c, ext2_fill_super references
        # Keep ext2_fill_super as-is (it's the function name)
        # But the filesystem type registration needs updating
        ('ret = register_filesystem(&ext2_fs_type);',
         'ret = register_filesystem(&ext2m_fs_type);'),
        ('unregister_filesystem(&ext2_fs_type);',
         'unregister_filesystem(&ext2m_fs_type);'),
        # Description
        ('Ext2', 'Ext2m'),
    ],
    'inode.c': [
        # inode.c references ext2_fs_type for statfs - needs ext2m_fs_type
        ('ext2_fs_type', 'ext2m_fs_type'),  # Only this one reference
        # ext2_aops reference name
        ('const struct address_space_operations ext2_aops',
         'const struct address_space_operations ext2m_aops'),
        ('const struct address_space_operations ext2_dax_aops',
         'const struct address_space_operations ext2m_dax_aops'),
        ('&ext2_aops', '&ext2m_aops'),
        ('&ext2_dax_aops', '&ext2m_dax_aops'),
        # ext2_file_inode_operations / ext2_dir_inode_operations etc
        ('ext2_file_inode_operations', 'ext2m_file_inode_operations'),
        ('ext2_dir_inode_operations', 'ext2m_dir_inode_operations'),
        ('ext2_special_inode_operations', 'ext2m_special_inode_operations'),
        ('ext2_symlink_inode_operations', 'ext2m_symlink_inode_operations'),
        ('ext2_fast_symlink_inode_operations', 'ext2m_fast_symlink_inode_operations'),
    ],
    'file.c': [
        ('ext2_file_operations', 'ext2m_file_operations'),
    ],
    'namei.c': [
        ('ext2_file_inode_operations', 'ext2m_file_inode_operations'),
        ('ext2_dir_inode_operations', 'ext2m_dir_inode_operations'),
        ('ext2_special_inode_operations', 'ext2m_special_inode_operations'),
        ('ext2_symlink_inode_operations', 'ext2m_symlink_inode_operations'),
    ],
    'symlink.c': [
        ('ext2_fast_symlink_inode_operations', 'ext2m_fast_symlink_inode_operations'),
        ('ext2_symlink_inode_operations', 'ext2m_symlink_inode_operations'),
        ('ext2_file_inode_operations', 'ext2m_file_inode_operations'),
    ],
    'dir.c': [
        ('ext2_dir_operations', 'ext2m_dir_operations'),
    ],
    'acl.c': [
        ('ext2_file_inode_operations', 'ext2m_file_inode_operations'),
    ],
    'xattr.c': [
        ('ext2_file_inode_operations', 'ext2m_file_inode_operations'),
        ('ext2_dir_inode_operations', 'ext2m_dir_inode_operations'),
        ('ext2_special_inode_operations', 'ext2m_special_inode_operations'),
        ('ext2_symlink_inode_operations', 'ext2m_symlink_inode_operations'),
    ],
    'ioctl.c': [
        ('ext2_file_operations', 'ext2m_file_operations'),
    ],
    'Kconfig': [
        ('EXT2_FS', 'EXT2M_FS'),
        ('ext2', 'ext2m'),
        ('Ext2', 'Ext2m'),
        ('Second Extended', 'Second Extended (Crypto)'),
    ],
    'Makefile': [
        ('ext2', 'ext2m'),
    ],
}

for fn, replacements in changes.items():
    if not os.path.exists(fn):
        print(f'  WARN: {fn} not found, skipping')
        continue
    with open(fn, 'r') as f:
        content = f.read()
    for old, new in replacements:
        if old in content:
            content = content.replace(old, new)
        else:
            print(f'  WARN: "{old[:40]}" not found in {fn}')
    with open(fn, 'w') as f:
        f.write(content)
    print(f'  OK: {fn} ({len(replacements)} replacements)')

# Also need to fix ext2m_fs_type references in super.c (the struct member)
# The superblock stores s_type->name for error messages
# ext2_fill_super references ext2_fs_type indirectly through EXT2_NAME
# Let's check and fix
with open('super.c', 'r') as f:
    c = f.read()
# Make sure EXT2_NAME references are updated
c = c.replace('EXT2_NAME', 'EXT2M_NAME')
with open('super.c', 'w') as f:
    f.write(c)

# ext2m.h: need to declare ext2m_fs_type and EXT2M_NAME
with open('ext2m.h', 'r') as f:
    c = f.read()
c = c.replace('EXT2_NAME', 'EXT2M_NAME')
c = c.replace('extern struct file_system_type ext2_fs_type;',
              'extern struct file_system_type ext2m_fs_type;')
with open('ext2m.h', 'w') as f:
    f.write(c)

# ext2m.h: update extern declarations for renamed ops structs
with open('ext2m.h', 'r') as f:
    c = f.read()
extern_fixes = [
    ('ext2_file_operations', 'ext2m_file_operations'),
    ('ext2_dir_operations', 'ext2m_dir_operations'),
    ('ext2_aops', 'ext2m_aops'),
    ('ext2_dax_aops', 'ext2m_dax_aops'),
    ('ext2_file_inode_operations', 'ext2m_file_inode_operations'),
    ('ext2_dir_inode_operations', 'ext2m_dir_inode_operations'),
    ('ext2_special_inode_operations', 'ext2m_special_inode_operations'),
    ('ext2_symlink_inode_operations', 'ext2m_symlink_inode_operations'),
    ('ext2_fast_symlink_inode_operations', 'ext2m_fast_symlink_inode_operations'),
]
for old, new in extern_fixes:
    c = c.replace(old, new)
with open('ext2m.h', 'w') as f:
    f.write(c)

print('\n=== ext2m filesystem code ready ===')
