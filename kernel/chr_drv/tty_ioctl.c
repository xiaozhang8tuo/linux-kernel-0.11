/*
 *  linux/kernel/chr_drv/tty_ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <termios.h>		// 终端输入输出函数头文件。主要定义控制异步通信口的终端接口

#include <linux/sched.h>	
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>

// 这是波特率因子数组(或称为除数数组),波特率与波特率因子的对应关系参见列表后说明
// 例如波特率是2400bps时，对应的因子是48(0x30)；9600bps的因子是12(0x1e)
static unsigned short quotient[] = {
	0, 2304, 1536, 1047, 857,
	768, 576, 384, 192, 96,
	64, 48, 24, 12, 6, 3
};
// 修改传输波特率。
// 参数：tty-终端对应的tty数据结构。
// 在除数锁存标志DLAB置位情况下，通过端口0x38和0x39向UART分别写入波特率因子低
// 字节和高字节。写完后再复位DLAB位。对于串口2，这两个带口分别是0x28和0x29。
static void change_speed(struct tty_struct * tty)
{
	unsigned short port,quot;

	// 函数首先检查参数y指定的终端是否是串行终端，若不是则退出。对于串口终端的tty结
	// 构，其读缓冲队列data字段存放着串行端口基址(0x3f8或0x2f8),而一般控制台终端的
	// tty结构的read_q.data字段值为0。然后从终端termios结构的控制模式标志集中取得已设
	// 置的波特率索引号，并据此从波特率因子数组quotient中取得对应的波特率因子值quot.
	// CBUD是控制摸式标志集中波特率位屏被码
	if (!(port = tty->read_q.data))
		return;
	quot = quotient[tty->termios.c_cflag & CBAUD];
	// 接着把波特率因子quot写入串行瑞口对应UART芯片的波特率因子锁存器中。在写之前我
	// 先要把线路控制寄存器LCR的除数锁存访问比特位DLAB(位7)置1。然后把16位的波特
	// 率因子低高字节分别写入带口0x3f8、0x3f9(分别对应波特率因子低、高字节锁存器)。
	// 最后再复位LCR的DLAB标志位。
	cli();
	outb_p(0x80,port+3);		/* set DLAB */		// 首先设置除数锁定标志DLAB
	outb_p(quot & 0xff,port);	/* LS of divisor */	// 输出因子的低字节
	outb_p(quot >> 8,port+1);	/* MS of divisor */
	outb(0x03,port+3);		/* reset DLAB */
	sti();
}

// 刷新tty缓冲队列
// 参数：queue 指定的缓冲队列指针
// 令缓冲队列的头指针等于尾指针，从而达到清空缓冲区的目的
static void flush(struct tty_queue * queue)
{
	cli();
	queue->head = queue->tail;
	sti();
}

// 等待字符发送
static void wait_until_sent(struct tty_struct * tty)
{
	/* do nothing - not implemented */
}
// 发送break控制符
static void send_break(struct tty_struct * tty)
{
	/* do nothing - not implemented */
}
// 取终端termios结构信息
// 参数：tty-指定终端的tty结构指针：termios-存放termios结构的用户缓冲区
static int get_termios(struct tty_struct * tty, struct termios * termios/*传入传出结构*/)
{
	int i;

	verify_area(termios, sizeof (*termios));
	for (i=0 ; i< (sizeof (*termios)) ; i++)
		put_fs_byte( ((char *)&tty->termios)[i] , i+(char *)termios );
	return 0;
}
// 设置终端termios结构信息。
// 参数：tty-指定终端的tty结构指针：termios~用户数据区termios结构指针。
static int set_termios(struct tty_struct * tty, struct termios * termios/* const */)
{
	int i;

	for (i=0 ; i< (sizeof (*termios)) ; i++)
		((char *)&tty->termios)[i]=get_fs_byte(i+(char *)termios);
	change_speed(tty);
	return 0;
}

static int get_termio(struct tty_struct * tty, struct termio * termio)
{
	int i;
	struct termio tmp_termio;

	verify_area(termio, sizeof (*termio));
	tmp_termio.c_iflag = tty->termios.c_iflag;
	tmp_termio.c_oflag = tty->termios.c_oflag;
	tmp_termio.c_cflag = tty->termios.c_cflag;
	tmp_termio.c_lflag = tty->termios.c_lflag;
	tmp_termio.c_line = tty->termios.c_line;
	for(i=0 ; i < NCC ; i++)
		tmp_termio.c_cc[i] = tty->termios.c_cc[i];
	for (i=0 ; i< (sizeof (*termio)) ; i++)
		put_fs_byte( ((char *)&tmp_termio)[i] , i+(char *)termio );
	return 0;
}

/*
 * This only works as the 386 is low-byt-first
 */
static int set_termio(struct tty_struct * tty, struct termio * termio)
{
	int i;
	struct termio tmp_termio;

	for (i=0 ; i< (sizeof (*termio)) ; i++)
		((char *)&tmp_termio)[i]=get_fs_byte(i+(char *)termio);
	*(unsigned short *)&tty->termios.c_iflag = tmp_termio.c_iflag;
	*(unsigned short *)&tty->termios.c_oflag = tmp_termio.c_oflag;
	*(unsigned short *)&tty->termios.c_cflag = tmp_termio.c_cflag;
	*(unsigned short *)&tty->termios.c_lflag = tmp_termio.c_lflag;
	tty->termios.c_line = tmp_termio.c_line;
	for(i=0 ; i < NCC ; i++)
		tty->termios.c_cc[i] = tmp_termio.c_cc[i];
	change_speed(tty);
	return 0;
}

// tty终端设备输入输出控制函数
// 参数：dev-设备号：cmd一ioctl命令：arg-操作参数指针
// 该函数首先根据参数给出的设备号找出对应终端的tty结构，然后根据控制命令cmd分别进行处理
int tty_ioctl(int dev, int cmd, int arg)
{
	struct tty_struct * tty;
	// 首先根据设备号取得tty子设备号，从而取得终端的tty结构。若主设备号是5（控制终端）
	// 则进程的tty字段即是tty子设备号。此时如果进程的tty子设备号是负数，表明该进程没有
	// 控制终端，即不能发出该iocl调用，于是显示出错信息并停机。如果主设备号不是5而是4
	// 我们就可以从设备号中取出子设备号。子设备号可以是0（控制台终端）、1（串口1终端）、2(串口2终端)。
	if (MAJOR(dev) == 5) {
		dev=current->tty;
		if (dev<0)
			panic("tty_ioctl: dev<0");
	} else
		dev=MINOR(dev);
	// 然后根据子设备号和tty表，我们可取得对应终端的tty结构。于是让tty指向对应子设备
	// 号的tty结构。然后再根据参数提供的ioctl命令cmd进行分别处理
	tty = dev + tty_table;
	switch (cmd) {
		case TCGETS:
			return get_termios(tty,(struct termios *) arg);	// 
		case TCSETSF:
			flush(&tty->read_q); /* fallthrough */
		case TCSETSW:
			wait_until_sent(tty); /* fallthrough */
		case TCSETS:
			return set_termios(tty,(struct termios *) arg);
		case TCGETA:
			return get_termio(tty,(struct termio *) arg);
		case TCSETAF:
			flush(&tty->read_q); /* fallthrough */
		case TCSETAW:
			wait_until_sent(tty); /* fallthrough */
		case TCSETA:
			return set_termio(tty,(struct termio *) arg);
		case TCSBRK:
			if (!arg) {
				wait_until_sent(tty);
				send_break(tty);
			}
			return 0;
		case TCXONC:
			return -EINVAL; /* not implemented */
		case TCFLSH:
			if (arg==0)
				flush(&tty->read_q);
			else if (arg==1)
				flush(&tty->write_q);
			else if (arg==2) {
				flush(&tty->read_q);
				flush(&tty->write_q);
			} else
				return -EINVAL;
			return 0;
		case TIOCEXCL:
			return -EINVAL; /* not implemented */
		case TIOCNXCL:
			return -EINVAL; /* not implemented */
		case TIOCSCTTY:
			return -EINVAL; /* set controlling term NI */
		case TIOCGPGRP:
			verify_area((void *) arg,4);
			put_fs_long(tty->pgrp,(unsigned long *) arg);
			return 0;
		case TIOCSPGRP:
			tty->pgrp=get_fs_long((unsigned long *) arg);
			return 0;
		case TIOCOUTQ:
			verify_area((void *) arg,4);
			put_fs_long(CHARS(tty->write_q),(unsigned long *) arg);
			return 0;
		case TIOCINQ:
			verify_area((void *) arg,4);
			put_fs_long(CHARS(tty->secondary),
				(unsigned long *) arg);
			return 0;
		case TIOCSTI:
			return -EINVAL; /* not implemented */
		case TIOCGWINSZ:
			return -EINVAL; /* not implemented */
		case TIOCSWINSZ:
			return -EINVAL; /* not implemented */
		case TIOCMGET:
			return -EINVAL; /* not implemented */
		case TIOCMBIS:
			return -EINVAL; /* not implemented */
		case TIOCMBIC:
			return -EINVAL; /* not implemented */
		case TIOCMSET:
			return -EINVAL; /* not implemented */
		case TIOCGSOFTCAR:
			return -EINVAL; /* not implemented */
		case TIOCSSOFTCAR:
			return -EINVAL; /* not implemented */
		default:
			return -EINVAL;
	}
}
