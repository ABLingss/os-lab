# OS Lab — Operating System Course Experiments

Linux kernel programming lab covering kernel compilation, system calls, scheduling, IPC, file systems, memory management, and device drivers.

## Environment

- **Kernel**: Linux 6.18.15 (self-compiled, x86_64)
- **Platform**: VMware VM / QEMU
- **Language**: C (user-space + kernel-space)

## Experiments

| # | Topic | Type | Key Files |
|---|-------|------|-----------|
| 1 | Linux Kernel Trimming & Compilation | Kernel config + build | `exp1/` |
| 2 | System Call Analysis & Adding a Syscall | Kernel programming | `exp2/` |
| 3 | Dining Philosophers — Multi-threaded | User-space C/pthread | `exp3/` |
| 4 | Producer-Consumer — Multi-process | User-space C/XSI IPC | `exp4/` |
| 5 | Recursive Directory Copy — Single/Multi-process | User-space C/file I/O/fork | `exp5/` |
| 6 | x86 Boot Process Analysis & Tracing | Kernel analysis + GDB | `exp6/` |
| 7 | Device Files & Character Driver | Kernel module | `exp7/` |
| 8 | IPC Analysis & Semaphore Deadlock Detection | Kernel programming (IPC) | `exp8/` |
| 9 | File System Analysis & Encrypted ext2 | Kernel programming (FS) | `exp9/` |
| 10 | Memory Management Analysis & Verification | Kernel analysis + module + app | `exp10/` |
| 11 | Process Scheduling — Multi-level Feedback Queue | Kernel programming (sched) | `exp11/` |

## Kernel Modules (exp7/exp10)

- `char_driver` — simple character device driver
- `hello_module` — minimal kernel module template
- `page_stats` — page frame statistics by type

## Build & Run

```bash
# User-space programs
gcc -O2 -o prog prog.c -lpthread

# Kernel modules
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# Kernel image (bzImage)
cd /usr/src/linux-6.18.15
make -j$(nproc) bzImage
```

## QEMU Testing

```bash
qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -initrd /tmp/initramfs.img \
  -append "console=ttyS0 nokaslr" -nographic -m 512
```

## Docs

- `CLAUDE.md` — project reference & environment details
- `PROJECT_NOTE.md` — detailed notes, bug history & lessons learned
- `WORKLOAD_ANALYSIS.md` — workload breakdown per experiment
- `report-reading/` — Linux kernel source code analysis report (LaTeX)
