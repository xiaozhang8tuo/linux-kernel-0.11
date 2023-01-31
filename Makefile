#
# if you want the ram-disk device, define this to be the
# size in blocks.
#
RAMDISK = #-DRAMDISK=512

AS86	=as86 -0 -a # 8086 汇编编译器和连接器，见列表后的介绍。后带的参数含义分别
LD86	=ld86 -0    # -0生成8086目标程序：-a生成与gas和g1d部分兼容的代码。

AS	=gas
LD	=gld
LDFLAGS	=-s -x -M   # -s输出文件中省略所有的符号信息 -x 删除所有局部符号 -M 表示需要在标准输出设备(显示器)上打印连接映象(link map)，是指由连接程序产生的一种内存地址映象，其中列出了程序段装入到内存中的位置信息。
CC	=gcc $(RAMDISK)
CFLAGS	=-Wall -O -fstrength-reduce -fomit-frame-pointer -fcombine-regs -mstring-insns # -f指定与机器无关的编译标志-fstrength-reduce用于优化循环语句，-fomit--frame--pointer指明对于无需帧指针(Frame pointer)的函数不要把帧指针保留在寄存器中。这样在函数中可以避免对帧指针的操作和维护。
# -fcombine-regs用于指明编译器在组合编译阶段把复制一个寄存器到另一个寄存器的指令组合在一起。-mstring-insns是Linus在学习gcc编译器时为gcc增加的选项，用于gcc-1.40在复制结构等操作时使用386CPU的字符串指令，可以去掉。
CPP	=cpp -nostdinc -Iinclude  # cpp - The C Preprocessor 前处理器:include文件，宏替换，条件编译处理 # -nostdinc -Iinclude'含义是不要搜索标准头文件目录中的文件，即不用系统usr/include/目录下的头文件，而是使用'-选项指定的目录或者是在当前目录里搜索头文件。

#
# ROOT_DEV specifies the default root-device when making the image. # ROOT_DEV 指定创建内核镜像文件时使用的默认设备，可以是硬盘/软盘
# This can be either FLOPPY, /dev/xxxx or empty, in which case the
# default of /dev/hd6 is used by 'build'.
#
ROOT_DEV=/dev/hd6

ARCHIVES=kernel/kernel.o mm/mm.o fs/fs.o # 下面是kernel目录、mm目录和fs目录所产生的目标代码文件。为了方便引用在这里将它们用ARCHIVES(归档文件)标识符表示。
DRIVERS =kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a # 块和字符设备库文件。.a表示该文件是个归档文件，也即包含有许多可执行二进制代码子程序集合的库文件，通常是用GN的ar程序生成。ar是GU的二进制文件处理程序，用于创建、修改以及从归档文件中抽取文件。
MATH	=kernel/math/math.a # 数学运算库文件
LIBS	=lib/lib.a          # 由lib/目录中的文件所编译生成的通用库文件
# $@ 表示目标文件 $^ 表示所有的依赖文件 $< 表示第一个依赖文件 $* 这个变量表示目标模式中“%”及其之前的部分。如果目标是“dir/a.foo.b”，并且目标的模式是“a.%.b”，那么，“$*”的值就是“dir/a.foo”
.c.s: # make 老式的隐式后缀规则 *.s:*.c 把所有的.c编译成.s  
	$(CC) $(CFLAGS) \
	-nostdinc -Iinclude -S -o $*.s $<
.s.o: # *.o:*.s 把所有的.s编译成.o  -c 只汇编不链接
	$(AS) -c -o $*.o $<  
.c.o: # *.o:*.c 把所有的.c编译成.o  
	$(CC) $(CFLAGS) \
	-nostdinc -Iinclude -c -o $*.o $<

all:	Image
# Image 有四个依赖目标 bootsect/setup/system/build
Image: boot/bootsect boot/setup tools/system tools/build
	tools/build boot/bootsect boot/setup tools/system $(ROOT_DEV) > Image  # build负责写镜像
	sync                      # 同步命令,使缓冲块数据立即写盘更行超级块
# dd:复制一个文件，根据选项进行转化和格式化 bs=表示一次读/写的字节数
disk: Image
	dd bs=8192 if=Image of=/dev/PS0

tools/build: tools/build.c
	$(CC) $(CFLAGS) \
	-o tools/build tools/build.c

boot/head.o: boot/head.s  # 利用上面的 .s.o规则生成head.o目标文件

tools/system:	boot/head.o init/main.o \ # system 的依赖项, 把依赖项链接在一起 > 将链接映像重定向到System.map中
		$(ARCHIVES) $(DRIVERS) $(MATH) $(LIBS)
	$(LD) $(LDFLAGS) boot/head.o init/main.o \
	$(ARCHIVES) \
	$(DRIVERS) \
	$(MATH) \
	$(LIBS) \
	-o tools/system > System.map
# 数学协处理函数库
kernel/math/math.a:
	(cd kernel/math; make)
# 块设备库文件，其中含有可重定位目标文件
kernel/blk_drv/blk_drv.a:
	(cd kernel/blk_drv; make)
# 字符设备库文件
kernel/chr_drv/chr_drv.a:
	(cd kernel/chr_drv; make)
# 内核目标文件生成规则
kernel/kernel.o:
	(cd kernel; make)
# 内存管理模块生成规则
mm/mm.o:
	(cd mm; make)
# 文件系统目标模块
fs/fs.o:
	(cd fs; make)
# 库函数
lib/lib.a:
	(cd lib; make)

boot/setup: boot/setup.s # 这里开始的三行是使用8086汇编和连接器,对setup.s 文件进行编译生成setup文件。-s选项表示要去除目标文件中的符号信息。
	$(AS86) -o boot/setup.o boot/setup.s
	$(LD86) -s -o boot/setup boot/setup.o

boot/bootsect:	boot/bootsect.s # bootsect 引导磁盘块
	$(AS86) -o boot/bootsect.o boot/bootsect.s
	$(LD86) -s -o boot/bootsect boot/bootsect.o
# 下面92--95行的作用是在bootsect.s文本程序开始处添加一行有关system模块文件长度信息，在把system模块加载到内存期间用于指明系统模块的长度。添加该行信息的方法是首先生成只含有“SYSSIZE=system文件实际长度”一行信息的tmp.s文件，然后将bootsect.s文件添加在其后。
tmp.s:	boot/bootsect.s tools/system
	(echo -n "SYSSIZE = (";ls -l tools/system | grep system \        # 取得system长度的方法是:首先利用命令ls对编译生成的system模块文件进行长列表显示,用grep命令取得列表行上文件字节数字段信息,并定向保存在tmp.s临时文件中。cut命令用于剪切字符串,tr用于去除行尾的回车符。
		| cut -c25-31 | tr '\012' ' '; echo "+ 15 ) / 16") > tmp.s   # 其中:(实际长度+15)/16用于获得用'节'表示的长度信息,1节=16字节。
	cat boot/bootsect.s >> tmp.s                                     # 注意:这是Linux0.11之前的内核版本(0.01-0.10)获取system模块长度并添加到bootsect.s程序中使用的方法。从0.11版内核开始已不使用这个方法,而是直接在bootsect.s程序开始处给出了system模块的一个最大默认长度值。因此这个规则现在已经不起作用。

clean:
	rm -f Image System.map tmp_make core boot/bootsect boot/setup
	rm -f init/*.o tools/system tools/build boot/*.o
	(cd mm;make clean)
	(cd fs;make clean)
	(cd kernel;make clean)
	(cd lib;make clean)

backup: clean # 首先执行clean, 然后对linux目录进行压缩，生成'backup.Z压缩文件。
	(cd .. ; tar cf - linux | compress - > backup.Z) # 通过管道将归档后的文件传递给compress做参数
	sync
# 依赖文件的生成 1 剔除出原来Makefile [#*3 Dependencies:]后面的部分生成tmp_make 2 将init/*.c 中文件的依赖项追加写入tmp_make (依赖项gcc(cpp) -M 可以获得) 3 tmp覆盖
dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make                    # $$i 即 $($i)
	(for i in init/*.c;do echo -n "init/";$(CPP) -M $$i;done) >> tmp_make # 相关性的规则,并且这些规则符合make语法。对于每一个源文件,预处理程序会输出一个规则,其结果形式就是相应源程序文件的目标文件名加上其依赖关系,即该源文件中包含的所有头文件列表.
	cp tmp_make Makefile
	# (cd fs; make dep)
	# (cd kernel; make dep)
	# (cd mm; make dep)

#*3 Dependencies:
init/main.o : init/main.c include/unistd.h include/sys/stat.h \
  include/sys/types.h include/sys/times.h include/sys/utsname.h \
  include/utime.h include/time.h include/linux/tty.h include/termios.h \
  include/linux/sched.h include/linux/head.h include/linux/fs.h \
  include/linux/mm.h include/signal.h include/asm/system.h include/asm/io.h \
  include/stddef.h include/stdarg.h include/fcntl.h 

# 这个Makefile文件的主要作用是指示make程序最终使用独立编译连接成的tools/目录中的build执行程序将所有内核编译代码连接和合并成一个可运行的内核映像文件image。
# 1 对boot/中的bootsect.s、setup.s使用8086汇编器进行编译，分别生成各自的执行模块。
# 2 对源代码中的其他所有程序使用GNU的编译器gcc/gas进行编译，并链接模块system。
# 3 用build工具将这三块组合成一个内核映像文件image。
# build是由tools/build.c源程序编译而成的一个独立的执行程序，它本身并没有被编译链接到内核代码中。
### Dependencies: