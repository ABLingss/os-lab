# exp10 Makefile
# 用户态程序 + 内核模块

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

# 用户态程序
USER_PROGS := mem_observe mem_alloc mem_cow
CFLAGS     := -Wall -O2

# 内核模块目标
obj-m := page_stats.o

.PHONY: all user kernel clean

all: user kernel

user: $(USER_PROGS)

kernel:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

mem_observe: mem_observe.c
	$(CC) $(CFLAGS) -o $@ $<

mem_alloc: mem_alloc.c
	$(CC) $(CFLAGS) -o $@ $<

mem_cow: mem_cow.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(USER_PROGS)
	$(MAKE) -C $(KDIR) M=$(PWD) clean
