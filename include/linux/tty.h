/*
 * 'tty.h' defines some structures used by tty_io.c and some defines.
 *
 * NOTE! Don't touch this without checking that nothing in rs_io.s or
 * con_io.s breaks. Some constants are hardwired into the system (mainly
 * offsets into 'tty_queue'
 * 注意！在修改这里的定义时，一定要检查rs_io.s或con_io.s程序中不会出现问题。
 * 在系统中有些常量是直接写在程序中的（主要是一些tty_queue中的偏移值）
 */

#ifndef _TTY_H
#define _TTY_H

#include <termios.h>	//终端输入输出函数头文件,主要定义控制异步通信口的终端接口

#define TTY_BUF_SIZE 1024	//tty缓冲区(缓冲队列)大小

// tty字符缓冲队列数据结构，用于tty_struct 结构中的读，写和辅助(规范)缓冲队列
struct tty_queue {
	unsigned long data;				// 队列缓冲区中含有字符行数值(不是当前字符数), 对于串口终端,则存放串行端口地址
	unsigned long head;				// 缓冲区中数据头指针
	unsigned long tail;				// 缓冲区中数据尾指针
	struct task_struct * proc_list;	// 等待进程列表
	char buf[TTY_BUF_SIZE];			// 队列的缓冲区
};
// 以下定义了tty等待队列中缓冲区操作宏函数。(tail在前,head在后,参见tty_io.c的图)
//  |-----------|-------------------------|----------|
//			  tail(100)		    		head(900):则下一次在900处写数据，写完head走到901

#define INC(a) ((a) = ((a)+1) & (TTY_BUF_SIZE-1))		// a缓冲区指针前移1字节，若已超出缓冲区右侧，则指针循环。
#define DEC(a) ((a) = ((a)-1) & (TTY_BUF_SIZE-1))		// a缓冲区指针后退】字节，并循环。
#define EMPTY(a) ((a).head == (a).tail)					// 清空指定队列的缓冲区
#define LEFT(a) (((a).tail-(a).head-1)&(TTY_BUF_SIZE-1))// 缓冲区还可存放字符的长度（空闲区长度）
#define LAST(a) ((a).buf[(TTY_BUF_SIZE-1)&((a).head-1)])// 缓冲区中最后一个位置
#define FULL(a) (!LEFT(a))								// 缓冲区满(为1的话)
#define CHARS(a) (((a).head-(a).tail)&(TTY_BUF_SIZE-1))	// 缓冲区已经存放字节数
#define GETCH(queue,c) \
(void)({c=(queue).buf[(queue).tail];INC((queue).tail);})// 从queue队列项缓冲区中取一字符（从tail处，并且tail+=1)
#define PUTCH(c,queue) \
(void)({(queue).buf[(queue).head]=(c);INC((queue).head);})// 往queue队列项缓冲区中放置一字符（在head处，并且head+=l)

// 判断终端键盘字符类型
#define INTR_CHAR(tty) ((tty)->termios.c_cc[VINTR])		// 中断符，发送中断信号SIGINT
#define QUIT_CHAR(tty) ((tty)->termios.c_cc[VQUIT])		// 退出符，发送退出信号SIGQUIT
#define ERASE_CHAR(tty) ((tty)->termios.c_cc[VERASE])	// 删除符，擦除一个字符
#define KILL_CHAR(tty) ((tty)->termios.c_cc[VKILL])		// 删除行，删除一行字符
#define EOF_CHAR(tty) ((tty)->termios.c_cc[VEOF])		// 文件结束符
#define START_CHAR(tty) ((tty)->termios.c_cc[VSTART])	// 开始符,恢复输出
#define STOP_CHAR(tty) ((tty)->termios.c_cc[VSTOP])		// 停止符,停止输出
#define SUSPEND_CHAR(tty) ((tty)->termios.c_cc[VSUSP])	// 挂起符,发挂起信号SIGTSTP

// tty数据结构
struct tty_struct {
	struct termios termios;					//终端io属性和控制字符数据结构
	int pgrp;								//所属进程组
	int stopped;							//停止标志
	void (*write)(struct tty_struct * tty);	//tty写函数指针
	struct tty_queue read_q;				//tty读队列
	struct tty_queue write_q;				//tty写队列
	struct tty_queue secondary;				//tty辅助队列(存放规范模式字符序列)// 规范模式队列
};

extern struct tty_struct tty_table[];	//tty结构数组

// 这里给出了终端termios结构中可更改的特珠字符数组c_cc[]的初始值。该termios结构
// 定义在include/termios.h中。如果定义了_POSIX_VDISABLE(\0),那么当某一项值等于
// POSIX_VDISABLE的值时，表示禁止使用相应的特珠字符。[8进制值]
/*	intr=^C		quit=^|		erase=del	kill=^U
	eof=^D		vtime=\0	vmin=\1		sxtc=\0
	start=^Q	stop=^S		susp=^Z		eol=\0 //行结束
	reprint=^R	discard=^U	werase=^W	lnext=^V
	eol2=\0
*/
#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0"

void rs_init(void);		// 异步串行通信初始化
void con_init(void);	// 控制终端初始化
void tty_init(void);	// tty初始化

int tty_read(unsigned c, char * buf, int n);
int tty_write(unsigned c, char * buf, int n);

void rs_write(struct tty_struct * tty);
void con_write(struct tty_struct * tty);

void copy_to_cooked(struct tty_struct * tty);

#endif
