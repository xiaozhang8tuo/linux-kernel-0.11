/*
 *  linux/kernel/rs_io.s
 *
 *  (C) 1991  Linus Torvalds
该汇编程序实现rs232串行通信中断处理过程。在进行字符的传输和存储过程中，该中断过程主要
对终端的读、写缓冲队列进行操作。它把从串行线路上接收到的字符存入串行终端的读缓冲队列read_q
中，或把写缓冲队列write_q中需要发送出去的字符通过串行线路发送给远端的串行终端设备。
 */

/*
 *	rs_io.s
 *
 * This module implements the rs232 io interrupts.
 */

.text
.globl _rs1_interrupt,_rs2_interrupt

size	= 1024				/* must be power of two !
					   and must match the value
					   in tty_io.c!!! */

/* these are the offsets into the read/write buffer structures 读写缓冲队列结构中的偏移量*/
rs_addr = 0
head = 4
tail = 8
proc_list = 12
buf = 16

// 当一个写缓冲队列满后，内核就会把要往写队列填字符的进程设置为等待状态。当写缓冲队列
// 中还利余最多256个字符时，中断处理程序就可以唤醒这些等待进程继续往写队列中放字符。
startup	= 256		/* chars left in write queue when we restart it */

/*
 * These are the actual interrupt routines. They look where
 * the interrupt is coming from, and take appropriate action.
 * 这些是实际的中断处理程序。程序首先检查中断的来源，然后执行
 * 相应的处理。
 */
.align 2
//串行端口1中断处理程序入口点。
; 初始化时rsl_interrupt地址被放入中断描述符0x24中，对应8259A的中断请求IRQ4引脚。
; 这里首先把tty表中串行终端1（串口1）读写缓冲队列指针的地址入栈(tty_io.c,99),
; 然后跳转到rs_int继续处理。这样做可以让串口1和串口2的处理代码公用。
_rs1_interrupt:
	pushl $_table_list+8
	jmp rs_int
.align 2
_rs2_interrupt:
	pushl $_table_list+16
rs_int:
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	push %es
	push %ds		/* as this is an interrupt, we cannot */
	pushl $0x10		/* know that bs is ok. Load it  ds/es指向内核数据段 */
	pop %ds
	pushl $0x10
	pop %es
	movl 24(%esp),%edx	// 取上面入栈的相应串口缓冲队列指针地址
	movl (%edx),%edx	// 获得[读缓冲队列]结构指针->edx
	movl rs_addr(%edx),%edx		// 取端口及地址->edx
	addl $2,%edx		/* interrupt ident. reg */	// 中断标识寄存器端口地址是0x3fa
rep_int:
	xorl %eax,%eax
	inb %dx,%al			//取中断标识字节,判断中断来源
	testb $1,%al		//首先判断有无待处理中断（位0=0有中断）
	jne end				//若无待处理中断，则跳转至退出处理处end
	cmpb $6,%al		/* this shouldn't happen, but ... */
	ja end				//al值大于6，则跳转至end(没有这种状态)
	movl 24(%esp),%ecx	//调用子程序之前把缓冲队列指针地址放入ecx
	pushl %edx			//临时保存中断标识寄存器端口地址
	subl $2,%edx		//edx中灰复串口基地址值0x3f8(0x2f8)
	call jmp_table(,%eax,2)		/* NOTE! not *4, bit0 is 0 already */
	; 上面语句是指，当有待处理中断时，al中位0=0，位2、位1是中断类型，因此相当于已经将
	; 中断类型乘了2，这里再乘2，获得跳转表（第79行）对应各中断类型地址，并跳转到那里去
	; 作相应处理。中断来源有4种：modem状态发生变化：要写（发送）字符：要读（接收）字符：
	; 线路状态发生变化。允许发送字符中断通过设置发送保持寄存器标志实现。在serial.c程序
	; 中，当写缓冲队列中有数据时，rs_write函数就会修改中断允许寄存器内容，添加上发送保
	; 持寄存器中断允许标志，从而在系统需要发送字符时引起串行中断发生。
	popl %edx		// 恢复中断标识寄存器端口地址0x3fa
	jmp rep_int		// 跳转,继续判断有无待处理中断并作相应处理
end:	movb $0x20,%al		//中断退出处理,向中断控制器发送结束中断指令EOI
	outb %al,$0x20		/* EOI */
	pop %ds
	pop %es
	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	addl $4,%esp		# jump over _table_list entry	
	iret

// 各中断类型处理子程序地址跳转表,共有4种中断源
// modem状态变化中断,写字符中断,读字符中断,线路状态问题中断
jmp_table:
	.long modem_status,write_char,read_char,line_status

// 由于modem状态发生变化而引发此次中断。通过读modem状态寄存器MSR对其进行复位操作
.align 2
modem_status:
	addl $6,%edx		/* clear intr by reading modem status reg */
	inb %dx,%al			
	ret
// 由于线路状态发生变化而引起这次串行中断。通过读线路状态寄存器LSR对其进行复位操作
.align 2
line_status:
	addl $5,%edx		/* clear intr by reading line status reg. */
	inb %dx,%al
	ret

; 由于UART芯片接收到字符而引起这次中断。对接收缓冲寄存器执行读操作可复位该中断源。
; 这个子程序将接收到的字符放到读缓冲队列read_q头指针(head)处，并且让该指针前移一
; 个字符位置。若heed指针已经到达缓冲区末端，则让其折返到缓冲区开始处。最后调用C函
; 数do_tty_interrupt也即copy_to_cooked,把读入的字符经过处理放入规范模式缓
; 冲队列（辅助缓冲队列secondary)中。(从串口设备中读一个字节放入读队列中(生产),执行中断处理程序读取(消费)读队列)
.align 2
read_char:
	inb %dx,%al							// 读取接收缓冲寄存器RBR中字符->al
	movl %ecx,%edx						// 当前串口缓冲队列指针地址->edx
	subl $_table_list,%edx				// 当前串口队列指针地址-缓冲队列指针表首地址->edx
	shrl $3,%edx						// 差值/8 得到串口号 串口1即1,串口2即2
	movl (%ecx),%ecx		# read-queue// 取读缓冲队列结构地址->ecx
	movl head(%ecx),%ebx				// 取读队列中缓冲头指针->ebx
	movb %al,buf(%ecx,%ebx)				// 将字符放在缓冲区中头指针所指位置处
	incl %ebx							// 头指针前移1字节
	andl $size-1,%ebx					// 
	cmpl tail(%ecx),%ebx				// 头尾比较
	je 1f								// 移动后相等即缓冲区已满,不保存头指针,跳转
	movl %ebx,head(%ecx)				// 保存修改过后的头指针
1:	pushl %edx
	call _do_tty_interrupt				
	addl $4,%esp
	ret

; 由于设置了发送保持寄存器允许中断标志而引起此次中断。说明对应串行终端的写字符缓冲
; 列中有字符需要发送。于是计算出写队列中当前所含字符数，若字符数已小于256个，则唤醒
; 等待写操作进程。然后从写缓冲队列尾部取出一个字符发送，并调整和保存尾指针。如果写缓
; 冲队列已空，则跳转到write_buffer_empty处处理写缓冲队列空的情况。 (从尾部取一个字符,写入端口，消费)
.align 2
write_char:
	movl 4(%ecx),%ecx		# write-queue
	movl head(%ecx),%ebx
	subl tail(%ecx),%ebx
	andl $size-1,%ebx		# nr chars in queue
	je write_buffer_empty	// 头尾指针相等队列空
	cmpl $startup,%ebx		// 是否超过256个待写入字符
	ja 1f
	movl proc_list(%ecx),%ebx	# wake up sleeping process		//唤醒等待进程
	testl %ebx,%ebx			# is there any?
	je 1f					// 空的
	movl $0,(%ebx)			// 唤醒等待进程,之后多写一些
1:	movl tail(%ecx),%ebx	// 尾部 
	movb buf(%ecx,%ebx),%al // 读取尾部数据
	outb %al,%dx			// 写入一个字符
	incl %ebx				// 尾指针前移
	andl $size-1,%ebx
	movl %ebx,tail(%ecx)	// 尾指针前移
	cmpl head(%ecx),%ebx
	je write_buffer_empty	// 空了吗
	ret
//处理写缓冲队列write q已空的情况。若有等待写该串行终瑞的进程则唤醒之，然后屏敲发
; 送保持寄存器空中断，不让发送保持寄存器空时产生中断。
; 如果此时写缓冲队列write_q已空，表示当前无字符需要发送。于是我们应该做两件事情。
; 首先看看有没有进程正等待写队列空出来，如果有就唤醒之。另外，因为现在系统已无字符
; 需要发送，所以此时我们要哲时禁止发送保持寄存器T取空时产生中断。当再有字符被放入
; 写缓冲队列中时，serial.c中的rs_write(0函数会再次允许发送保特寄存器空时产生中断，
; 因此UART就又会“自动”地来取写缓冲队列中的字符，并发送出去。
.align 2
write_buffer_empty:
	movl proc_list(%ecx),%ebx	# wake up sleeping process
	testl %ebx,%ebx			# is there any?
	je 1f
	movl $0,(%ebx)			// 唤醒
1:	incl %edx				//指向端口0x3fa
	inb %dx,%al				//读取中断允许寄存器IER
	jmp 1f
1:	jmp 1f
1:	andb $0xd,%al		/* disable transmit interrupt */	// 屏蔽发送保持寄存器空中断(通知设备不要再向cpu(os)发请求，我目前没有可写的东西)
	outb %al,%dx			// 写入
	ret
