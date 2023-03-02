/*
 *  linux/kernel/tty_io.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an orthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 * tty_io.c给tty终端一种非相关的感觉，不管它们是控制台还是串行终端。
 * 该程序同样实现了回显、规范（熟）模式等。
 *
 * Kill-line thanks to John T Kohl.
 */
#include <ctype.h>
#include <errno.h>		// 
#include <signal.h>		// 信号头文件,定义信号符号常量,信号结构及其操作函数原型

#define ALRMMASK (1<<(SIGALRM-1))		// 警告(alarm)信号屏蔽位
#define KILLMASK (1<<(SIGKILL-1))		// 终止(kill)信号屏蔽位
#define INTMASK (1<<(SIGINT-1))			// 键盘中断(int)信号屏蔽位
#define QUITMASK (1<<(SIGQUIT-1))		// 键盘退出(quit)信号屏蔽位
#define TSTPMASK (1<<(SIGTSTP-1))		// tty发出的停止进程(tty stop)信号屏蔽位

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/segment.h>
#include <asm/system.h>

// 获取termios结构中三个模式标志集之一，或者用于判断一个标志集是否有置位标志
#define _L_FLAG(tty,f)	((tty)->termios.c_lflag & f)// 本地模式标志
#define _I_FLAG(tty,f)	((tty)->termios.c_iflag & f)// 输入模式标志
#define _O_FLAG(tty,f)	((tty)->termios.c_oflag & f)// 输出模式标志

// 取termios结构终端特殊（本地）模式标志集中的一个标志
#define L_CANON(tty)	_L_FLAG((tty),ICANON)	// 取规范模式标志
#define L_ISIG(tty)	_L_FLAG((tty),ISIG)			// 取信号标志
#define L_ECHO(tty)	_L_FLAG((tty),ECHO)			// 取回显字符标志
#define L_ECHOE(tty)	_L_FLAG((tty),ECHOE)	// 规范模式时取回显擦除标志
#define L_ECHOK(tty)	_L_FLAG((tty),ECHOK)	// 规范模式时取kill擦除当前标志
#define L_ECHOCTL(tty)	_L_FLAG((tty),ECHOCTL)	// 取回显控制字符标志
#define L_ECHOKE(tty)	_L_FLAG((tty),ECHOKE)	// 规范模式时取kill擦除行并回显标志

// 取termios结构输入模式标志集中的一个标志
#define I_UCLC(tty)	_I_FLAG((tty),IUCLC)		// 取大写到小写转换标志
#define I_NLCR(tty)	_I_FLAG((tty),INLCR)		// 取换行符NL转回车符CR标志
#define I_CRNL(tty)	_I_FLAG((tty),ICRNL)		// 取回车符CR转换行符NL标志
#define I_NOCR(tty)	_I_FLAG((tty),IGNCR)		// 取忽略回车符CR标志

// 取termios结构输出模式标志集中的一个标志
#define O_POST(tty)	_O_FLAG((tty),OPOST)		// 取执行输出处理标志
#define O_NLCR(tty)	_O_FLAG((tty),ONLCR)		// 取换行符NL转回车换行符CR-NL标志
#define O_CRNL(tty)	_O_FLAG((tty),OCRNL)		// 取回车符CR转换行符NL标志
#define O_NLRET(tty)	_O_FLAG((tty),ONLRET)	// 取换行符L执行回车功能的标志
#define O_LCUC(tty)	_O_FLAG((tty),OLCUC)		// 取小写转大写字符标志


// tty数据结构
// struct tty_struct {
// 	struct termios termios;					//终端io属性和控制字符数据结构
// 	int pgrp;								//所属进程组
// 	int stopped;							//停止标志
// 	void (*write)(struct tty_struct * tty);	//tty写函数指针
// 	struct tty_queue read_q;				//tty读队列
// 	struct tty_queue write_q;				//tty写队列
// 	struct tty_queue secondary;				//tty辅助队列(存放规范模式字符序列)// 规范模式队列
// };

// struct termios {
// 	unsigned long c_iflag;		/* input mode flags */
// 	unsigned long c_oflag;		/* output mode flags */
// 	unsigned long c_cflag;		/* control mode flags */
// 	unsigned long c_lflag;		/* local mode flags */
// 	unsigned char c_line;		/* line discipline */
// 	unsigned char c_cc[NCCS];	/* control characters */
// };

struct tty_struct tty_table[] = {
	{
		{ICRNL,		/* change incoming CR to NL */
		OPOST|ONLCR,	/* change outgoing NL to CRNL */
		0,
		ISIG | ICANON | ECHO | ECHOCTL | ECHOKE,
		0,		/* console termio */
		INIT_C_CC},
		0,			/* initial pgrp */
		0,			/* initial stopped */
		con_write,
		{0,0,0,0,""},		/* console read-queue */
		{0,0,0,0,""},		/* console write-queue */
		{0,0,0,0,""}		/* console secondary queue */
	},{
		{0, /* no translation */
		0,  /* no translation */
		B2400 | CS8,
		0,
		0,
		INIT_C_CC},
		0,
		0,
		rs_write,
		{0x3f8,0,0,0,""},		/* rs 1 */
		{0x3f8,0,0,0,""},
		{0,0,0,0,""}
	},{
		{0, /* no translation */
		0,  /* no translation */
		B2400 | CS8,
		0,
		0,
		INIT_C_CC},
		0,
		0,
		rs_write,
		{0x2f8,0,0,0,""},		/* rs 2 */
		{0x2f8,0,0,0,""},
		{0,0,0,0,""}
	}
};

/*
 * these are the tables used by the machine code handlers.
 * you can implement pseudo-tty's or something by changing
 * them. Currently not done.
 */
// tty读写缓冲队列结构地址表。供rs_io.s汇编程序使用，用于取得读写缓冲队列地址
struct tty_queue * table_list[]={
	&tty_table[0].read_q, &tty_table[0].write_q,
	&tty_table[1].read_q, &tty_table[1].write_q,
	&tty_table[2].read_q, &tty_table[2].write_q
	};
// tty中断初始化函数
// 初始化串口终端和控制台终端
void tty_init(void)
{
	rs_init();	// serial.c
	con_init();	// console.c
}
// tty键盘中断(^C)处理函数
// 向tty结构中指明的（前台）进程组中所有进程发送指定信号mask,通常该信号是SIGINT
// 参数：tty--指定终瑞的tty结构指针：mask-信号屏蔽位。
void tty_intr(struct tty_struct * tty, int mask)
{
	int i;

	// 首先检查终端进程组号。如果tty所属进程组号小于等于0，则退出。因为当pgrp=0时，
	// 表明进程是初始进程init,它没有控制终端，因此不应该会发出中断字符。
	if (tty->pgrp <= 0)
		return;
	for (i=0;i<NR_TASKS;i++)
		if (task[i] && task[i]->pgrp==tty->pgrp)
			task[i]->signal |= mask;
}

// 如果队列缓冲区空则让进程进入可中断睡眠状态。
// 参数：queue--指定队列的指针。
// 进程在取队列缓冲区中字符之前需要调用此函数加以验证
static void sleep_if_empty(struct tty_queue * queue)
{
	// 若当前进程没有信号要处理，并且指定的队列缓冲区空，则让进程进入可中断睡眠状态，并
	// 让队列的进程等待指针指向该进程。
	cli();
	while (!current->signal && EMPTY(*queue))
		interruptible_sleep_on(&queue->proc_list);
	sti();
}

// 若队列缓冲区满则让进程进入可中断的睡眠状态
// 参数：queue--指定队列的指针
// 进程在往队列缓冲区中写入字符之前需要调用此函数判断队列情况
static void sleep_if_full(struct tty_queue * queue)
{
	// 如果队列缓冲区不满则返回退出。否则若进程没有信号需要处理，并且队列缓冲区中空闲剩
	// 余区长度<128，则让进程进入可中断睡眠状态，并让该队列的进程等待指针指向该进程。
	if (!FULL(*queue))
		return;
	cli();
	while (!current->signal && LEFT(*queue)<128)
		interruptible_sleep_on(&queue->proc_list);
	sti();
}

// 等待按键
// 如果控制台读队列缓冲区空，则让进程进入可中断睡眠状态
void wait_for_keypress(void)
{
	sleep_if_empty(&tty_table[0].secondary);
}

// 复制成规范模式字符序列
// 根据终端termios结构中设置的各种标志，将指定tty终端队列缓冲区中的字符复制转换成
// 规范模式（熟模式）字符并存放在辅助队列（规范模式队列）中。
// 参数：tty-指定终端的tty结构指针。
void copy_to_cooked(struct tty_struct * tty)
{
	signed char c;

	// 如果tty的读队列缓冲区不空并且辅助队列缓冲区不满，则循环读取读队列缓冲区中的字符
	// 转换成规范摸式后放入辅助队列(secondary)缓冲区中，直到读队列缀冲区空或者辅助队
	// 列满为止。在循环体内，程序首先从读队列缓冲区尾指针处取一字符，并把尾指针前移一个
	// 字符位置。然后根据终端termios中输入模式标志集中设置的标志对字符进行处理
	while (!EMPTY(tty->read_q) && !FULL(tty->secondary)) {
		GETCH(tty->read_q,c);
		// 如果该字符是回车符CR(13),那么若回车转换行标志CRNL置位，则将字符转换为换行符
		// NL(10)。否则如果忽略回车标志NOCR置位，则忽咯该字符，维续处理其他字符。如果字
		// 符是换行符NL(10),并且换行转回车标志NLCR置位，则将其转换为回车符CR(13)
		if (c==13)
			if (I_CRNL(tty))
				c=10;
			else if (I_NOCR(tty))
				continue;
			else ;
		else if (c==10 && I_NLCR(tty))
			c=13;
		// 如果大写转小写标志CLC置位，则将该字符转换为小写字符
		if (I_UCLC(tty))
			c=tolower(c);
		// 如果本地摸式标志集中规范模式标志CANON已置位，则对读取的字符进行以下处理。首先：
		// 如果该字符是键盘终止控制字符KILL(^U),则对已输入的当前行执行删除处理。删除一行字
		// 符的循环过程如下：如果y辅助队列不空，并且取出的辅助队列中最后一个字符不是换行
		// 符NL(10),并且该字符不是文件结束字符(^D),则循环执行下列代码：
		// 如果本地回显标志ECHO置位，那么：若字符是控制字符（值<32），则往tty写队列中放
		// 入擦除控制字符ERASE(^H)。然后再放入一个擦除字符ERASE,并且调用该tLy写函数，把
		// 写队列中的所有字符输出到终端屏幕上。另外，因为控制字符在放入写队列时需要用2个字
		// 节表示（例如^V),因此要求特别对控制字符多放入一个ERASE。最后将tty辅助队列头指针后退1字节。
		if (L_CANON(tty)) {
			if (c==KILL_CHAR(tty)) {
				/* deal with killing the input line */
				while(!(EMPTY(tty->secondary) ||
				        (c=LAST(tty->secondary))==10 ||//删除一行即删除到只剩换行符
				        c==EOF_CHAR(tty))) {
					if (L_ECHO(tty)) {
						if (c<32)
							PUTCH(127,tty->write_q);//控制字符要删除两个字节
						PUTCH(127,tty->write_q);
						tty->write(tty);
					}
					DEC(tty->secondary.head);
				}
				continue;
			}
			if (c==ERASE_CHAR(tty)) {
				if (EMPTY(tty->secondary) ||
				   (c=LAST(tty->secondary))==10 ||
				   c==EOF_CHAR(tty))
					continue;
				if (L_ECHO(tty)) {
					if (c<32)
						PUTCH(127,tty->write_q);
					PUTCH(127,tty->write_q);
					tty->write(tty);
				}
				DEC(tty->secondary.head);
				continue;
			}
			if (c==STOP_CHAR(tty)) {
				tty->stopped=1;
				continue;
			}
			if (c==START_CHAR(tty)) {
				tty->stopped=0;
				continue;
			}
		}
		if (L_ISIG(tty)) {
			if (c==INTR_CHAR(tty)) {
				tty_intr(tty,INTMASK);
				continue;
			}
			if (c==QUIT_CHAR(tty)) {
				tty_intr(tty,QUITMASK);
				continue;
			}
		}
		if (c==10 || c==EOF_CHAR(tty))
			tty->secondary.data++;
		if (L_ECHO(tty)) {
			if (c==10) {
				PUTCH(10,tty->write_q);
				PUTCH(13,tty->write_q);
			} else if (c<32) {
				if (L_ECHOCTL(tty)) {
					PUTCH('^',tty->write_q);
					PUTCH(c+64,tty->write_q);
				}
			} else
				PUTCH(c,tty->write_q);
			tty->write(tty);
		}
		PUTCH(c,tty->secondary);
	}
	wake_up(&tty->secondary.proc_list);
}

int tty_read(unsigned channel, char * buf, int nr)
{
	struct tty_struct * tty;
	char c, * b=buf;
	int minimum,time,flag=0;
	long oldalarm;

	if (channel>2 || nr<0) return -1;
	tty = &tty_table[channel];
	oldalarm = current->alarm;
	time = 10L*tty->termios.c_cc[VTIME];
	minimum = tty->termios.c_cc[VMIN];
	if (time && !minimum) {
		minimum=1;
		if (flag=(!oldalarm || time+jiffies<oldalarm))
			current->alarm = time+jiffies;
	}
	if (minimum>nr)
		minimum=nr;
	while (nr>0) {
		if (flag && (current->signal & ALRMMASK)) {
			current->signal &= ~ALRMMASK;
			break;
		}
		if (current->signal)
			break;
		if (EMPTY(tty->secondary) || (L_CANON(tty) &&
		!tty->secondary.data && LEFT(tty->secondary)>20)) {
			sleep_if_empty(&tty->secondary);
			continue;
		}
		do {
			GETCH(tty->secondary,c);
			if (c==EOF_CHAR(tty) || c==10)
				tty->secondary.data--;
			if (c==EOF_CHAR(tty) && L_CANON(tty))
				return (b-buf);
			else {
				put_fs_byte(c,b++);
				if (!--nr)
					break;
			}
		} while (nr>0 && !EMPTY(tty->secondary));
		if (time && !L_CANON(tty))
			if (flag=(!oldalarm || time+jiffies<oldalarm))
				current->alarm = time+jiffies;
			else
				current->alarm = oldalarm;
		if (L_CANON(tty)) {
			if (b-buf)
				break;
		} else if (b-buf >= minimum)
			break;
	}
	current->alarm = oldalarm;
	if (current->signal && !(b-buf))
		return -EINTR;
	return (b-buf);
}

int tty_write(unsigned channel, char * buf, int nr)
{
	static cr_flag=0;
	struct tty_struct * tty;
	char c, *b=buf;

	if (channel>2 || nr<0) return -1;
	tty = channel + tty_table;
	while (nr>0) {
		sleep_if_full(&tty->write_q);
		if (current->signal)
			break;
		while (nr>0 && !FULL(tty->write_q)) {
			c=get_fs_byte(b);
			if (O_POST(tty)) {
				if (c=='\r' && O_CRNL(tty))
					c='\n';
				else if (c=='\n' && O_NLRET(tty))
					c='\r';
				if (c=='\n' && !cr_flag && O_NLCR(tty)) {
					cr_flag = 1;
					PUTCH(13,tty->write_q);
					continue;
				}
				if (O_LCUC(tty))
					c=toupper(c);
			}
			b++; nr--;
			cr_flag = 0;
			PUTCH(c,tty->write_q);
		}
		tty->write(tty);
		if (nr>0)
			schedule();
	}
	return (b-buf);
}

/*
 * Jeh, sometimes I really like the 386.
 * This routine is called from an interrupt,
 * and there should be absolutely no problem
 * with sleeping even in an interrupt (I hope).
 * Of course, if somebody proves me wrong, I'll
 * hate intel for all time :-). We'll have to
 * be careful and see to reinstating the interrupt
 * chips before calling this, though.
 *
 * I don't think we sleep here under normal circumstances
 * anyway, which is good, as the task sleeping might be
 * totally innocent.
 */
void do_tty_interrupt(int tty)
{
	copy_to_cooked(tty_table+tty);
}

void chr_dev_init(void)
{
}
