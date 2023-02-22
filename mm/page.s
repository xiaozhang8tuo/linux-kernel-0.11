/*
 *  linux/mm/page.s
 * 该文件包括页异常中断处理程序（中断14），主要分两种情况处理。一是由于缺页引起的页异常中
 * 断，通过调用do_no_page(error_code, address)来处理：二是由页写保护引起的页异常，此时调用页写保
 * 护处理函数do_wp_page(error_code, address)进行处理。其中的出错码(error_code)是由CPU自动产生并压
 * 入堆栈的，出现异常时访问的线性地址是从控制寄存器C2中取得的。CR2是专门用来存放页出错时的线性地址。
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * page.s contains the low-level page-exception code.
 * the real work is done in mm.c
 */

.globl _page_fault		// 声明为全局变量,在traps.c中用于设置页异常描述符

_page_fault:
	xchgl %eax,(%esp)	// 取出错码到eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%edx		// 设置内核数据段选择符
	mov %dx,%ds			
	mov %dx,%es
	mov %dx,%fs
	movl %cr2,%edx		// 取引起页面异常的线性地址
	pushl %edx			// 将该线性地址和出错码压入栈中,作为将调用函数的参数
	pushl %eax
	testl $1,%eax		// 测试页存在标志P(位0), 如果不是缺页引起的异常则跳转
	jne 1f
	call _do_no_page	// 调用缺页处理函数(mm/memory.c)
	jmp 2f
1:	call _do_wp_page	// 调用写保护处理函数
2:	addl $8,%esp		// 丢弃压入栈的两个参数,弹出栈中寄存器并退出终端
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret
