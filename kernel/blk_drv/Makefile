#
# Makefile for the FREAX-kernel block device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

AR	=gar
AS	=gas
LD	=gld
LDFLAGS	=-s -x
CC	=gcc
CFLAGS	=-Wall -O -fstrength-reduce -fomit-frame-pointer -fcombine-regs \
	-finline-functions -mstring-insns -nostdinc -I../../include
CPP	=gcc -E -nostdinc -I../../include

.c.s:
	$(CC) $(CFLAGS) \
	-S -o $*.s $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) \
	-c -o $*.o $<

OBJS  = ll_rw_blk.o floppy.o hd.o ramdisk.o


# 在有了先决条件OBJS后使用下面的命令连接成目标blk_drv.a库文件。
# 命令行中的rcs是操作码和修饰标志（前面可加上-），放置次序任意。其中r是操作码，
# 说明需要执行的操作。r表示要把命令行末列出的目标文件插入（替换replacement)归档文件
# blk_drv.a中。cs是两个修饰标志，用于修饰具体操作行为。c表示当归档文件blk_drv.a不
# 存在时就创建这个文件。s表示写进或更新归档文件中的目标文件索引。对一个归档文件单独
# 使用命令ar s等同于对一个归档文件执行命令ranlib。
blk_drv.a: $(OBJS)
	$(AR) rcs blk_drv.a $(OBJS)
	sync

clean:
	rm -f core *.o *.a tmp_make
	for i in *.c;do rm -f `basename $$i .c`.s;done

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in *.c;do echo -n `echo $$i | sed 's,\.c,\.s,'`" "; \
		$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile

### Dependencies:
floppy.s floppy.o : floppy.c ../../include/linux/sched.h ../../include/linux/head.h \
  ../../include/linux/fs.h ../../include/sys/types.h ../../include/linux/mm.h \
  ../../include/signal.h ../../include/linux/kernel.h \
  ../../include/linux/fdreg.h ../../include/asm/system.h \
  ../../include/asm/io.h ../../include/asm/segment.h blk.h 
hd.s hd.o : hd.c ../../include/linux/config.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/linux/mm.h ../../include/signal.h \
  ../../include/linux/kernel.h ../../include/linux/hdreg.h \
  ../../include/asm/system.h ../../include/asm/io.h \
  ../../include/asm/segment.h blk.h 
ll_rw_blk.s ll_rw_blk.o : ll_rw_blk.c ../../include/errno.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/linux/mm.h ../../include/signal.h \
  ../../include/linux/kernel.h ../../include/asm/system.h blk.h 
