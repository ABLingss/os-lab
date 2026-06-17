# 实验7：Linux设备文件与驱动程序 — 内核模块 Makefile
#
# 编译:  make -C /lib/modules/$(uname -r)/build M=$PWD modules
# 清理:  make -C /lib/modules/$(uname -r)/build M=$PWD clean
#
# 加载:  sudo insmod hello_module.ko
#       sudo insmod char_driver.ko
# 卸载:  sudo rmmod char_driver
#       sudo rmmod hello_module
# 测试:  gcc exp7_test_driver.c -o test_driver && sudo ./test_driver

obj-m += hello_module.o
obj-m += char_driver.o

all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f test_driver
