#
# Makefile for the FREAX-kernel.
#
# 注意！依赖关系是由'make dep'自动进行的，它也会自动去除原来的依赖信息。不要把你自己的
# 依赖关系信息放在这里，除非是特别文件的（也即不是一个.c文件的信息）。

#

AR	=gar
AS	=gas
LD	=gld
LDFLAGS	=-s -x
CC	=gcc
CFLAGS	=-Wall -O -fstrength-reduce -fomit-frame-pointer -fcombine-regs \
	-finline-functions -mstring-insns -nostdinc -I../include
CPP	=gcc -E -nostdinc -I../include

.c.s:
	$(CC) $(CFLAGS) \
	-S -o $*.s $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) \
	-c -o $*.o $<

OBJS  = sched.o system_call.o traps.o asm.o fork.o \
	panic.o printk.o vsprintf.o sys.o exit.o \
	signal.o mktime.o

# 在有了先决条件OBJS后使用下面的命令连接成目标kernel.o。
# 选项’-r’用于指示生成可重定位的输出，即产生可以作为链接器ld输入的目标文件。
kernel.o: $(OBJS)
	$(LD) -r -o kernel.o $(OBJS)
	sync

clean:
	rm -f core *.o *.a tmp_make keyboard.s
	for i in *.c;do rm -f `basename $$i .c`.s;done
	(cd chr_drv; make clean)
	(cd blk_drv; make clean)
	(cd math; make clean)
# 下面的目标或规则用于检查各文件之间的依赖关系。方法如下：
# 使用字符串编辑程序sed对Makefile文件（这里即是自己）进行处理，输出为删除Makefile
# 文件中'##Dependencies'行后面的所有行（下面从51开始的行），并生成tmp_make
# 临时文件(43行的作用)。然后对kernel./目录下的每一个C文件执行gcc预处理操作
# -M标志告诉预处理程序输出描述每个目标文件相关性的规则，并且这些规则符合make语法。
# 对于每一个源文件，预处理程序输出一个make规则，其结果形式是相应源程序文件的目标
# 文件名加上其依赖关系-一该源文件中包含的所有头文件列表。把预处理结果都添加到临时
# 文件tmp_make中，然后将该临时文件复制成新的Makefile文件。
dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in *.c;do echo -n `echo $$i | sed 's,\.c,\.s,'`" "; \
		$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile
	(cd chr_drv; make dep)
	(cd blk_drv; make dep)

### Dependencies:
exit.s exit.o : exit.c ../include/errno.h ../include/signal.h \
  ../include/sys/types.h ../include/sys/wait.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/linux/mm.h \
  ../include/linux/kernel.h ../include/linux/tty.h ../include/termios.h \
  ../include/asm/segment.h 
fork.s fork.o : fork.c ../include/errno.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/segment.h ../include/asm/system.h 
mktime.s mktime.o : mktime.c ../include/time.h 
panic.s panic.o : panic.c ../include/linux/kernel.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h 
printk.s printk.o : printk.c ../include/stdarg.h ../include/stddef.h \
  ../include/linux/kernel.h 
sched.s sched.o : sched.c ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
  ../include/signal.h ../include/linux/kernel.h ../include/linux/sys.h \
  ../include/linux/fdreg.h ../include/asm/system.h ../include/asm/io.h \
  ../include/asm/segment.h 
signal.s signal.o : signal.c ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
  ../include/signal.h ../include/linux/kernel.h ../include/asm/segment.h 
sys.s sys.o : sys.c ../include/errno.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/tty.h \
  ../include/termios.h ../include/linux/kernel.h ../include/asm/segment.h \
  ../include/sys/times.h ../include/sys/utsname.h 
traps.s traps.o : traps.c ../include/string.h ../include/linux/head.h \
  ../include/linux/sched.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/system.h ../include/asm/segment.h ../include/asm/io.h 
vsprintf.s vsprintf.o : vsprintf.c ../include/stdarg.h ../include/string.h 
