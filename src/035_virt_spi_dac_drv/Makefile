# 1. 使用不同的开发板内核时, 一定要修改KERN_DIR
# 2. KERN_DIR中的内核要事先配置、编译, 为了能编译内核, 要先设置下列环境变量:
# 2.1 ARCH,          比如: export ARCH=arm
# 2.2 CROSS_COMPILE, 比如: export CROSS_COMPILE=arm-buildroot-linux-gnueabihf-
# 2.3 PATH,          比如: export PATH=$PATH:/home/book/100ask_stm32mp157_pro-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin 
# 2.4 CONFIG		 比如: make 100ask_stm32mp157_pro_defconfig
# 2.5 编译dtb		 比如: make dtbs V=1
# 注意: 不同的开发板不同的编译器上述3个环境变量不一定相同,
#       请参考各开发板的高级用户使用手册


KERN_DIR = /home/book/100ask_stm32mp157_pro-sdk/Linux-5.4

all:
	make -C $(KERN_DIR) M=`pwd` modules 


clean:
	make -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order

obj-m	+= virt_spi_dac_drv.o
