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
// 参数：tty-指定终端的tty结构指针。// cooked 已经处理好的
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
		// 如果本地摸式标志集中规范模式标志CANON已置位，则对读取的字符进行以下处理。

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
			// 如果该字符是删除控制字符ERASE(^H),那么：如果tty的辅助队列为空，或者其最后一个
			// 字符是换行符NL(10),或者是文件结束符，则维续处理其他字符。如果本地回显标志ECHO置
			// 位，那么：若字符是控制字符（值<32），则往tty的写队列中放入擦除字符ERASE。再放入
			// 一个擦除字符ERASE,并且调用该tty的写函数。最后将tty铺助队列头指针后退1字节，继
			// 续处理其他字符。
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
			// 如果字符是停止控制字符(^S),则置tty停止标志，停止tty输出，并继续处理其他字符。
			// 如果字符是开始字符(^Q),则复位tty停止标志，恢复tty输出，并维续处理其他字符。
			if (c==STOP_CHAR(tty)) {
				tty->stopped=1;
				continue;
			}
			if (c==START_CHAR(tty)) {
				tty->stopped=0;
				continue;
			}
		}
		// 若输入模式标志集中ISIG标志置位，表示终端键盘可以产生信号，则在收到控制字符INTR
		// QUIT、SUSP或DSUSP时，需要为进程产生相应的信号。如果该字符是键盘中断符(^C)
		// 则向当前进程之进程组中所有进程发送键盘中断信号，并继续处理下一字符。如果该字符是
		// 退出符(^\)，则向当前进程之进程组中所有进程发送键盘退出信号，并继续处理下一字符。
		if (L_ISIG(tty)) {
			if (c==INTR_CHAR(tty)) {	// ^C,中断信号
				tty_intr(tty,INTMASK);
				continue;
			}
			if (c==QUIT_CHAR(tty)) {	// ^\ 退出信号
				tty_intr(tty,QUITMASK);
				continue;
			}
		}
		// 如果该字符是换行符NL(10),或者是文件结束符EOF(4,^D),表示一行字符已处理完，
		// 则把辅助缓冲队列中当前含有字符行数值secondary.data增1。如果在函数tty_read中取
		// 走一行字符，该值即会被减1
		if (c==10 || c==EOF_CHAR(tty))
			tty->secondary.data++;
		
		// 加果本地摸式标志集中回显标志ECHO在置位状态，那么，如果字符是换行符NL(10),则将
		// 换行符NL(10)和回车符CR(13)放入tty写队列缓冲区中：如果字符是控制字符（值<32）
		// 并且回显控制字符标志ECHOCTL置位，则将字符^和字符c+64放入tty写队列中（也即会
		// 显示^C、^H等)：否则将该字符直接放入tty写缓冲队列中。最后调用该tty写操作函数。
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
		// 每一次循环末将未处理的字符放入辅助队列中
		PUTCH(c,tty->secondary);
	}
	wake_up(&tty->secondary.proc_list);
}

// tty读函数
// 从终端辅助缓冲队列中读取指定数量的字符，放到用户指定的缓冲区中
// channel 子设备号 buf 用户缓冲区 nr 希望读字节数 返回已读字节数
int tty_read(unsigned channel, char * buf, int nr)
{
	struct tty_struct * tty;
	char c, * b=buf;
	int minimum,time,flag=0;
	long oldalarm;

	// 首先判断函数参数有效性，并让tty指针指向参数子设备号对应ttb_table表中的tty结构。
	// 本版本Linux内核终端只有3个子设备，分别是控制台终端(0)、串口终端1(1)和串口终
	// 端2(2)。所以任何大于2的子设备号都是非法的。需要读取的字节数当然也不能小于0。
	if (channel>2 || nr<0) return -1;
	tty = &tty_table[channel];
	// 接着保存进程原定时值，并根据VTIME和VMIN对应的控制字符数组值设置读字符操作超时
	// 定时值time和最少需要读取的字符个数minimum。在非规范模式下，这两个是超时定时值。
	// VMIN表示为了满足读操作而需要读取的最少字符个数。VTIME是一个1/10秒计数计时值。
	oldalarm = current->alarm;				// 保存进程当前报警定时值
	time = 10L*tty->termios.c_cc[VTIME];	// 设置读操作超时定时值
	minimum = tty->termios.c_cc[VMIN];		// 最少需要读取的字符个数

	// 然后根据tine和minimum的数值设置最少需要读取的字符数和等待延时值。如果设置了
	// 读超时定时值time(即不等于0)，但没有设置最少读取个数minimum,即目前是0，那么我
	// 们将设置成在读到至少一个字符或者定时超时后读操作将立刻返回。所以需要置minimum。
	// 如果进程原定时值是0或者time加上当前系统时间值小于进程原定时值的话，则置重新设置
	// 进程定时值为(time+当前系统时间)，并置flag标志。另外，如果这里设置的最少读取字
	// 符数大于欲读取的字符数，则令其等于此次欲读取的字符数
	
	// 注意，这里设置进程alarm值而导致收到的SIGALRM信号并不会导致进程终止退出。当
	// 设置进程的alarm时，同时也会设置一个flag标志。当这个alarm超时时，虽然内核也会向
	// 进程发送SIGALRM信号，但这里会利用flag标志来判断到底是用户还是内核设置的alarm值，
	// 若flag不为0，那么内核代码就会负责复位此时产生的SIGALRM信号。因此
	// 这里设置的SIGALRM信号不会使当前进程“无故”终止退出。当然，这种方式有些烦琐，并且
	// 容易出问题，因此以后内核数据结构中就特地使用了一个timeout变量来专门处理这种问题。
	if (time && !minimum) {
		minimum=1;
		if (flag=(!oldalarm || time+jiffies<oldalarm))
			current->alarm = time+jiffies;
	}
	if (minimum>nr)
		minimum=nr;
	// 现在我们开始从辅助队列中循坏取出字符并放到用户缓冲区buf中。当欲读的字节数大于0，
	// 则执行以下循环操作。在循环过程中，如果flag已设置（即进程原定时值是0或者time+
	// 当前系统时间值小于进程原定时值)，并且进程此时已收到定时报警信号SIGALRM,表明这
	// 里新设置的定时时间已到。于是复位进程的定时信号SIGALRM,并中断循环。
	// 如果flag没有置位（即进程原来设置过定时值并且这里重新设置的定时值大于等于原来设置
	// 的定时值)因而是收到了原定时的报警信号，或者flag已置位但当前进程此时收到了其他信
	// 号，则退出循环，返回0
	while (nr>0) {
		if (flag && (current->signal & ALRMMASK)) {
			current->signal &= ~ALRMMASK;// 内核复位
			break;
		}
		if (current->signal)
			break;// 原定时器报警信号
		// 如果辅助缓冲队列为空，或者设置了规范模式标志并且辅助队列中字符行数为0以及辅助模
		// 式缓冲队列空闲空间>20，则让当前进程进入可中断睡眠状态，返回后继续处理。由于规范
		// 摸式时内核以行为单位为用户提供数据，因此在该模式下辅助队列中必须起码有一行字符可
		// 供取用，即secondary.dala起码是1才行。另外，由这里的LEFT判断可知，即使辅助队
		// 列中还没有放入一行(即应该有一个回车符)，但是如果此时一行字符个数已经超过1024
		// -20=1004个字符，那么内核也会立刻执行读取操作。
		if (EMPTY(tty->secondary) || (L_CANON(tty) &&
		!tty->secondary.data && LEFT(tty->secondary)>20)) {
			sleep_if_empty(&tty->secondary);
			continue;
		}
		// 下面开始正式执行取字符操作。需读字符数nr依次递域，直到=0或者辅助缓冲队列为空
		// 在这个循环过程中，首先取辅助缓冲队列字符c，并且把缓冲队列尾指针tail向右移动一个
		// 字符位置。如果所取字符是文件结束符（^D)或者是换行符NL(10),则把辅助缓冲队列中
		// 含有字符行数值减1。如果该字符是文件结束符(^D)并且规范模式标志成置位状态，则返
		// 回已读字符数，并退出。
		do {
			GETCH(tty->secondary,c);
			if (c==EOF_CHAR(tty) || c==10)
				tty->secondary.data--;
			if (c==EOF_CHAR(tty) && L_CANON(tty))
				return (b-buf);
			// 否则说明现在还没有遇到文件结束符，或者正处于原始（非规范）模式。在这种模式中，用
			// 户以字符流作为读取对象，也不识别其中的控制字符（如文件结束符）。于是将字符直接放
			// 入用户数据缓冲区buf中，并把欲读字符数成1。此时如果欲读字符数已为0，则中断循环。
			// 否则，只要还没有取完欲读字符数并且辅助队列不空，就继续取队列中的字符。
			else {
				put_fs_byte(c,b++);
				if (!--nr)
					break;
			}
		} while (nr>0 && !EMPTY(tty->secondary));
		// 执行到此说明我们已经读取了nr个字符，或者辅助队列已经被取空了。对于已经读取nr个
		// 字符的情况，我们无需做什么，程序会自动退出开始的循环，恢复进程原定时值并作
		// 退出处理。如果由于辅助队列中字符被取空而退出上面o循环，那么我们就需要根据终端
		// termios控制字符数组的tine设置的延时时间来确定是否要等待一段时间。如果超时定时值
		// time不为0并且规范模式标志没有置位（非规范模式），那么：
		// 如果进程原定时值是0或者time+当前系统时间值小于进程原定时值oldalarm,则置重新
		// 设置进程定时值为time+当前系统时间，并置flag标志，为还需要读取的字符做好定时准备
		// 否则表明进程原定时间要比等待字符被读取的定时时间要早，可能没有等到字符到来进程
		// 原定时时间就到了。因此，此时这里需要恢复进程的原定时值oldalarm
		if (time && !L_CANON(tty))
			if (flag=(!oldalarm || time+jiffies<oldalarm))
				current->alarm = time+jiffies;
			else
				current->alarm = oldalarm;
		// 另外，如果设置了规范模式标志，那么若已读到起码一个字符则中断循环。否则若已读取数
		// 大于或等于最少要求读取的字符数，则也中断循环。
		if (L_CANON(tty)) {
			if (b-buf)
				break;
		} else if (b-buf >= minimum)
			break;
	}
	// 此时读取tty字符循环操作结束，因此让进程的定时值恢复原值。最后，如果进程接收到信
	// 号并且没有读取到任何字符，则返回出错号（被中断）。否则返回已读字符数。
	current->alarm = oldalarm;
	if (current->signal && !(b-buf))
		return -EINTR;
	return (b-buf);	//返回已经读取字符数
}

// tty写函数。
// 把用户缓冲区中的字符写入tty写队列缓冲区中。
// 参数：channel-子设备号：buf-缓冲区指针：nr-写字节数。
// 返回已写字节数。
int tty_write(unsigned channel, char * buf, int nr)
{
	static cr_flag=0;
	struct tty_struct * tty;
	char c, *b=buf;

	// 首先判断函数参数有效性，并让tty指针指向参数子设备号对应ttb_table表中的tty结构。
	// 本版本Linux内核终端只有3个子设备，分别是控制台终端(0)、串口终端1(1)和串口终
	// 端2(2)。所以任何大于2的子设备号都是非法的。需要读取的字节数当然也不能小于0。
	if (channel>2 || nr<0) return -1;
	tty = channel + tty_table;
	while (nr>0) {
		sleep_if_full(&tty->write_q);
		if (current->signal)
			break;
		while (nr>0 && !FULL(tty->write_q)) {
			c=get_fs_byte(b);
			if (O_POST(tty)) {
				// 如果该字符是回车符\r(CR,13)并且回车符转换行符标志OCRNL置位，则将该字符换成
				// 换行符\n (NL,10),否则如果该字符是换行符\n (NL,10)并且换行转回车功能标志
				// ONLRET置位的话，则将该字符换成回车符\r(CR,13)。
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
		// 若要求的字符全部写完，或者写队列已满，则程序退出循环。此时会调用对应tty写函数，
		// 把写队列缓冲区中的字符显示在控制台屏幕上，或者通过串行端口发送出去。如果当前处
		// 理的tty是控制台终端，那么tty->write调用的是con_write,如果tty是串行终端，
		// 则tty->write调用的是rs_write函数。若还有字节要写，则等待写队列中字符取走。
		// 所以这里调用调度程序，先去执行其他任务。
		tty->write(tty); 	//一个自己定义的函数指针(函数指针实现多态...)
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
// tty中断处理调用函数-字符规范模式处理
// 参数：tty-指定的tty终瑞号(0,1或2)
// 将指定tty终瑞队列缓冲区中的字符复制或转换成规范（熟）模式字符并存放在辅助队列中
// 该函数会在串口读字符中断(rs_io.s)和键盘中断(kerboard.S)中被调用.
void do_tty_interrupt(int tty)
{
	copy_to_cooked(tty_table+tty);
}

void chr_dev_init(void)
{
}
