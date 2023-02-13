AR	=gar      #GNU的二进制文件处理程序，用于创建、修改以及从归档文件中抽取文件。
AS	=gas      #GNU的汇编程序。
CC	=gcc      #GNUC语言编译器。
LD	=gld      #GNWU的连接程序。

#C编译程序选项。-W11显示所有的警告信息：-0优化选项，优化代码长度和执行时间：
#-fstrength-reduce优化循环执行代码，排除重复变量：-fomit-frame--pointer省略保存不必要
#的框架指针：-fcombine-regs合并寄存器，减少寄存器类的使用：-mstring-insns Linus自己
#填加的优化选项，以后不再使用：-nostdinc-I./include不使用默认路径中的包含文件，而使
#用这里指定目录中的(../include)。
CFLAGS	=-Wall -O -fstrength-reduce -fcombine-regs -fomit-frame-pointer \
	-mstring-insns -nostdinc -I../include

#C前处理选项。-E只运行C前处理，对所有指定的C程序进行预处理并将处理结果输出到标准输
#出设备或指定的输出文件中：-nostdinc-I./include同前。
CPP	=gcc -E -nostdinc -I../include

#下面的规则指示make利用下面的命令将所有的.c文件编译生成.s编程序。该规则的命令
#指使gcc采用CFLAGS所指定的选项对C代码编译后不进行汇编就停止(-S),从而产生与
#输入的各个C文件对应的汇编代码文件。默认情况下所产生的汇编程序文件名是原C文件名
#去掉.c而加上.s后缀。-o表示其后是输出文件的名称。其中$*.s(或$@)是自动目标变量
#$<代表第一个先决条件，这里即是符合条件*.c的文件。
#下面这3个不同规则分别用于不同的操作要求。若目标是.s文件，而源文件是.c文件则会使
#用第一个规则：若目录是.o，而原文件是.s,则使用第2个规则：若目标是.o文件而原文件
#是c文件，则可直接使用第3个规则。
.c.s:
	$(CC) $(CFLAGS) \
	-S -o $*.s $<
#将所有*.c文件编译成*.o目标文件。不进行链接。
.c.o:
	$(CC) $(CFLAGS) \
	-c -o $*.o $<
#下面规则表示将所有.s汇编程序文件编译成.o目标文件。
.s.o:
	$(AS) -o $*.o $<

OBJS=	open.o read_write.o inode.o file_table.o buffer.o super.o \
	block_dev.o char_dev.o file_dev.o stat.o exec.o pipe.o namei.o \
	bitmap.o fcntl.o ioctl.o truncate.o

fs.o: $(OBJS)
	$(LD) -r -o fs.o $(OBJS)

clean:
	rm -f core *.o *.a tmp_make
	for i in *.c;do rm -f `basename $$i .c`.s;done

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in *.c;do $(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile

### Dependencies:
bitmap.o : bitmap.c ../include/string.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h 
block_dev.o : block_dev.c ../include/errno.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/segment.h ../include/asm/system.h 
buffer.o : buffer.c ../include/stdarg.h ../include/linux/config.h \
  ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
  ../include/sys/types.h ../include/linux/mm.h ../include/signal.h \
  ../include/linux/kernel.h ../include/asm/system.h ../include/asm/io.h 
char_dev.o : char_dev.c ../include/errno.h ../include/sys/types.h \
  ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/segment.h ../include/asm/io.h 
exec.o : exec.c ../include/errno.h ../include/string.h \
  ../include/sys/stat.h ../include/sys/types.h ../include/a.out.h \
  ../include/linux/fs.h ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/segment.h 
fcntl.o : fcntl.c ../include/string.h ../include/errno.h \
  ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
  ../include/sys/types.h ../include/linux/mm.h ../include/signal.h \
  ../include/linux/kernel.h ../include/asm/segment.h ../include/fcntl.h \
  ../include/sys/stat.h 
file_dev.o : file_dev.c ../include/errno.h ../include/fcntl.h \
  ../include/sys/types.h ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/linux/mm.h ../include/signal.h \
  ../include/linux/kernel.h ../include/asm/segment.h 
file_table.o : file_table.c ../include/linux/fs.h ../include/sys/types.h 
inode.o : inode.c ../include/string.h ../include/sys/stat.h \
  ../include/sys/types.h ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/linux/mm.h ../include/signal.h \
  ../include/linux/kernel.h ../include/asm/system.h 
ioctl.o : ioctl.c ../include/string.h ../include/errno.h \
  ../include/sys/stat.h ../include/sys/types.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/linux/mm.h \
  ../include/signal.h 
namei.o : namei.c ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
  ../include/signal.h ../include/linux/kernel.h ../include/asm/segment.h \
  ../include/string.h ../include/fcntl.h ../include/errno.h \
  ../include/const.h ../include/sys/stat.h 
open.o : open.c ../include/string.h ../include/errno.h ../include/fcntl.h \
  ../include/sys/types.h ../include/utime.h ../include/sys/stat.h \
  ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/tty.h \
  ../include/termios.h ../include/linux/kernel.h ../include/asm/segment.h 
pipe.o : pipe.c ../include/signal.h ../include/sys/types.h \
  ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
  ../include/linux/mm.h ../include/asm/segment.h 
read_write.o : read_write.c ../include/sys/stat.h ../include/sys/types.h \
  ../include/errno.h ../include/linux/kernel.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/linux/mm.h \
  ../include/signal.h ../include/asm/segment.h 
stat.o : stat.c ../include/errno.h ../include/sys/stat.h \
  ../include/sys/types.h ../include/linux/fs.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/mm.h ../include/signal.h \
  ../include/linux/kernel.h ../include/asm/segment.h 
super.o : super.c ../include/linux/config.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/system.h ../include/errno.h ../include/sys/stat.h 
truncate.o : truncate.c ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
  ../include/signal.h ../include/sys/stat.h 
