#!/bin/bash
# 实验1 — 内核裁剪前后指标对比
# 分别在旧内核(5.15)和新内核(6.18)下运行，收集数据填报告

echo "============================================="
echo "  实验1 内核裁剪指标 — $(uname -r)"
echo "  $(date)"
echo "============================================="
echo ""

echo "=== 1. 内核镜像体积 ==="
echo "当前运行内核: $(uname -r)"
ls -lh /boot/vmlinuz-* 2>/dev/null | awk '{print $5, $NF}'
echo ""

echo "=== 2. 系统启动时间 ==="
systemd-analyze 2>/dev/null || echo "(systemd-analyze 不可用)"
echo ""

echo "=== 2b. 启动详情(可选) ==="
systemd-analyze blame 2>/dev/null | head -5 || true
echo ""

echo "=== 3. 内存占用 ==="
free -h
echo ""

echo "=== 3b. 内核模块占用(可选) ==="
du -sh /lib/modules/$(uname -r)/ 2>/dev/null
echo ""

echo "============================================="
echo "  数据收集完毕 — 截此终端窗口即截图"
echo "============================================="
