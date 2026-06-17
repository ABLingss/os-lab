# 实验1：Linux内核裁剪和编译 — 操作指导

> 严格按指导书「实验一」内容编写 | 内核版本：linux-6.18.15

---

## 当前状态（已完成）

| 项目 | 状态 |
|------|------|
| 系统 | Ubuntu 22.04.5 LTS |
| 磁盘 | 57G 总量，46G 空闲 |
| 内核源码 | `/usr/src/linux-6.18.15`，软链接 `/usr/src/linux-new` |
| 编译工具 | gcc, make, binutils, flex, bison, libncurses-dev, libelf-dev, libssl-dev, bc ✅ |
| 基础配置 | `.config` 已生成，`olddefconfig` 已执行 |

---

## 步骤

### 1. 实验前准备（已完成）

指导书要求的工具安装：
```bash
sudo apt update
sudo apt install -y gcc make binutils flex bison libncurses5-dev libelf-dev openssl libssl-dev dkms
```

获取内核源码（已完成）：
```bash
sudo tar -xvf /usr/src/linux-6.18.15.tar.xz -C /usr/src/
sudo ln -s /usr/src/linux-6.18.15 /usr/src/linux-new
```

---

### 2. 内核裁剪（核心步骤 🖼️ 需要截图）

指导书原文：
> 内核裁剪的核心是通过配置工具筛选功能模块，原则是"保留必需功能，删除冗余模块"

#### 2.1 生成基础配置文件（已完成）

```bash
cd /usr/src/linux-new
sudo cp /boot/config-$(uname -r) .config   # 已完成
sudo make olddefconfig                       # 已完成
```

#### 2.2 启动菜单配置工具

```bash
cd /usr/src/linux-6.18.15
sudo make menuconfig
```

> 📸 **截图1**：menuconfig 主界面

#### 2.3 关键模块裁剪建议

> 以下内容来自指导书原文。你需要自己在 menuconfig 里操作并截图。

**1. 通用设置（General setup）**
- 取消 "Local version - append to kernel release"：避免内核版本后缀过长（可选）
- 取消 "Support for paging of anonymous memory (swap)"：若系统无 swap 分区，可关闭（需确认当前系统无 swap）

> 📸 **截图2**：General Setup 菜单裁剪后

**2. 处理器类型（Processor type and features）**
- 仅保留当前 CPU 架构，如 "x86-64"，取消其他架构（如 "32-bit"）
- 取消 "Multi-core scheduler support"：若实验机为单核（极少情况，需确认）

> 📸 **截图3**：Processor type and features 菜单裁剪后

**3. 文件系统（Filesystems）**
- 仅保留当前系统使用的文件系统：如 "Ext3/Ext4"，取消 "Btrfs""XFS""NTFS" 等不使用的文件系统
- 取消 "CD-ROM/DVD Filesystems"：若实验机无光驱

> 📸 **截图4**：Filesystems 菜单裁剪后

**4. 网络支持（Networking support）**
- 取消 "Bluetooth subsystem support""Wi-Fi"：若实验机仅用有线网络
- 取消 "ATM""Token Ring" 等老旧网络协议

> 📸 **截图5**：Networking 菜单裁剪后

**5. 设备驱动（Device Drivers）**
- 显卡驱动：仅保留当前显卡型号的驱动，取消 NVIDIA/AMD 驱动
- 存储驱动：仅保留当前硬盘接口驱动（如 "SATA"，取消 "SCSI""IDE" 等不使用的接口）
- 声卡/USB：若实验无需音频或 USB，可关闭（谨慎，关闭 USB 后可能无法使用鼠标键盘）

> 📸 **截图6**：Device Drivers 菜单裁剪后

#### 2.4 保存配置并退出

裁剪完成后：
1. 按 `Esc` 返回主界面
2. 选择 "Save" 保存配置（默认保存为 `.config`）
3. 选择 "Exit" 退出配置工具

> 📸 **截图7**：保存配置界面

---

### 3. 内核编译与安装 🖼️

#### 3.1 内核编译

指导书原文：
> 执行编译命令，建议使用多线程加速（-j后接线程数，通常为 "CPU 核心数 + 1"）

```bash
cd /usr/src/linux-6.18.15
sudo make -j$(nproc)
```

编译产物：`arch/x86/boot/bzImage`（内核镜像），各目录下 `.ko` 文件（模块）

> 📸 **截图8**：编译开始
> 📸 **截图9**：编译完成

#### 3.2 安装内核模块

```bash
sudo make modules_install
```

验证：查看 `/lib/modules/` 目录是否生成以 `6.18.15` 命名的文件夹

> 📸 **截图10**：`make modules_install` 完成

#### 3.3 安装内核镜像与更新 GRUB

```bash
sudo make install
sudo update-grub
```

> 📸 **截图11**：`make install` 完成
> 📸 **截图12**：`update-grub` 完成

---

### 4. 实验验证与结果分析 🖼️

#### 4.1 验证新内核启动

```bash
sudo reboot
```

重启后在 GRUB 菜单中（若未显示 GRUB，开机时按 Shift 键），选择 "Advanced options" → 选择新编译的内核。

系统启动后验证：
```bash
uname -r
# 应输出 6.18.15
```

> 📸 **截图13**：GRUB 启动菜单
> 📸 **截图14**：`uname -r` 输出

#### 4.2 结果对比分析

> 指导书要求对比裁剪前后指标：

| 指标 | 裁剪前（原内核） | 裁剪后（新内核） | 变化说明 |
|------|-----------------|-----------------|---------|
| 内核镜像体积 `ls -lh /boot/vmlinuz-*` | | | 冗余模块删除导致体积减小 |
| 启动时间 `systemd-analyze` | | | 驱动/服务减少，启动加速 |
| 内存占用 `free -h` | | | 内核模块减少，内存节省 |

> 📸 **截图15**：裁剪前后镜像体积对比
> 📸 **截图16**：系统内存占用

---

## 截图清单（共16张）

| # | 内容 | 对应步骤 |
|---|------|---------|
| 1 | menuconfig 主界面 | 2.2 |
| 2 | General Setup 裁剪后 | 2.3.1 |
| 3 | Processor type and features 裁剪后 | 2.3.2 |
| 4 | Filesystems 裁剪后 | 2.3.3 |
| 5 | Networking 裁剪后 | 2.3.4 |
| 6 | Device Drivers 裁剪后 | 2.3.5 |
| 7 | 保存配置 | 2.4 |
| 8 | make 编译开始 | 3.1 |
| 9 | make 编译完成 | 3.1 |
| 10 | modules_install 完成 | 3.2 |
| 11 | make install 完成 | 3.3 |
| 12 | update-grub 完成 | 3.3 |
| 13 | GRUB 启动菜单（新内核选项） | 4.1 |
| 14 | uname -r 输出 6.18.15 | 4.1 |
| 15 | 裁剪前后镜像体积对比 | 4.2 |
| 16 | 系统内存占用 free -h | 4.2 |

---

## 下一步

你现在可以执行：
```bash
cd /usr/src/linux-6.18.15 && sudo make menuconfig
```

进行内核裁剪并截图。裁剪完成后告诉我，我帮你编译安装。
