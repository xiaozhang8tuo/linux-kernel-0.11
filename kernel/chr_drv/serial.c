/*
 *  linux/kernel/serial.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	serial.c
 *
 * This module implements the rs232 io functions
 *	void rs_write(struct tty_struct * queue);
 *	void rs_init(void);
 * and all interrupts pertaining to serial IO.
 */

#include <linux/tty.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>

#define WAKEUP_CHARS (TTY_BUF_SIZE/4)

extern void rs1_interrupt(void);
extern void rs2_interrupt(void);

// 初始化串行端口
// 设置指定串行端口的传输波特率(2400bps)并允许除了写保特寄存器空以外的所有中断源，
// 另外，在输出2字节的波特率因子时，须首先设置线路控制寄存器的DLAB位（位7）。
// 参数：port是串行端口基地址，串口1-0x3F8:串口2-0x2F8。
static void init(int port)
{
	outb_p(0x80,port+3);	/* set DLAB of line control reg */	// 设置线路控制寄存器的DLAB位(位7)
	outb_p(0x30,port);	/* LS of divisor (48 -> 2400 bps */		// 发送波特率因子低字节,0x30->2400bps
	outb_p(0x00,port+1);	/* MS of divisor */					// 发送波特率银子高字节
	outb_p(0x03,port+3);	/* reset DLAB */					// 复位DLAB,数据为8位
	outb_p(0x0b,port+4);	/* set DTR,RTS, OUT_2 */			// 设置DTR,RTS辅助用户输出2
	outb_p(0x0d,port+1);	/* enable all intrs but writes */	// 除了写(写保持空)以外,允许所有中断源中断
	(void)inb(port);	/* read data port to reset things (?) */// 读数据口,以进行复位操作
}

// 初始化串行中断程序和串行接口
// 
void rs_init(void)
{
	// 下面两句用于设置两个串行口的中断门描述符。rs1_interrupt是串口1的中断处理过程指针
	// 串口1使用的中断是int0x24,串口2的是int0x23。
	set_intr_gate(0x24,rs1_interrupt);
	set_intr_gate(0x23,rs2_interrupt);
	init(tty_table[1].read_q.data);//初始化串行口1
	init(tty_table[2].read_q.data);
	outb(inb_p(0x21)&0xE7,0x21);	//允许主8259A相应IRQ3,IRQ4中断请求
}

/*
 * This routine gets called when tty_write has put something into
 * the write_queue. It must check wheter the queue is empty, and
 * set the interrupt register accordingly
 *
 *	void _rs_write(struct tty_struct * tty);
 在tty_write已将数据放入输出（写）队列时会调用下面的子程序。在该
子程序中必须首先检查写队列是否为空，然后设置相应中断寄存器。
 */
// 串行数据发送输出。
// 该函数实际上只是开启发送保持寄存器已空中断标志。此后当发送保特寄存器空时，UART就会
// 产生中断请求。而在该串行中断处理过程中，程序会取出写队列尾指针处的字符，并输出到发
// 送保持寄存器中。一旦UART把该字符发送了出去，发送保持寄存器又会变空而引发中断请求。
// 于是只要写队列中还有字符，系统就会重复这个处理过程，把字符一个一个地发送出去。当写
// 队列中所有字符都发送了出去，写队列变空了，中断处理程序就会把中断允许寄存器中的发送
// 保持寄存器中断允许标志复位掉，从而再次禁止发送保持寄存器空引发中断请求。此次“循环”
// 发送操作也随之结束。
void rs_write(struct tty_struct * tty)
{
	cli();
	// 如果写队列不空，则首先从0x39(或0x29)读取中断允许寄存器内容，添上发送保持寄存器
	// 中断允许标志（位1）后，再写回该寄存器。这样，当发送保持寄存器空时UART就能够因期望
	// 获得欲发送的字符而引发中断。write_q.data中是串行端口基地址。
	if (!EMPTY(tty->write_q))
		outb(inb_p(tty->write_q.data+1)|0x02,tty->write_q.data+1);
	sti();
}
