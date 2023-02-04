/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
	asm.s程序中包括大部分的硬件故障（或出错）处理的底层次代码。页异常由内存管理程序
mm处理，所以不在这里。此程序还处理（希望是这样）由于TS-位而造成的u异常，因为
fpu必须正确地进行保存/恢复处理，这些还没有测试过。
 */
; 本代码文件主要涉及对Intel保留中断int0--int16的处理(int17-int31留作今后使用)。
; 以下是一些全局函数名的声明，其原形在traps.c中说明。
.globl _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.globl _double_fault,_coprocessor_segment_overrun
.globl _invalid_TSS,_segment_not_present,_stack_segment
.globl _general_protection,_coprocessor_error,_irq13,_reserved

; 下面这段程序处理无出错号的情况。参见图8-4(a)。
; int0-处理被零除出错的情况。类型：错误(Fault):错误号：无。
; 在执行DIV或DIV指令时，若除数是0,CPU就会产生这个异常。当EAX(或AX、AL)容纳
; 不了一个合法除操作的结果时也会产生这个异常。标号'_do_divide_error'实际上是
; C语言函数_do_divide_error()编译后所生成模块中对应的名称。函数'do_divide_error'在
; traps.c中实现（第97行开始）。
_divide_error:
	pushl $_do_divide_error      # 首先把将要调用的函数地址入栈
no_error_code:                   # 这里是无出错号处理的入口处
	xchgl %eax,(%esp)            # _do_divide_error的地址放入eax, eax被交换入栈
	pushl %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl $0		# "error code"  # 数值0作为出错码入栈
	lea 44(%esp),%edx            # 取堆栈中 原调用返回地址 处堆栈指针位置,并压入堆栈
	pushl %edx
	movl $0x10,%edx              # 初始化段寄存器ds,cs和fs,加载内核数据段选择符
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs
; 下行上的'*'号表示调用操作数指定地址处的函数，称为间接调用。这句的含义是调用引起本次
; 异常的C处理函数，例如do_divide_error等。第50行是将堆栈指针加8相当于执行两次pop
; 操作，弹出（丢弃）最后入堆栈的两个C函数参数(pushl $0和pushl %edx)，让堆栈指针重新
; 指向寄存器fs入栈处。
	call *%eax
	addl $8,%esp
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret
; #int1-debug调试中断入口点。处理过程同上。类型：错误/陷阱(Fault/Trap);错误号：无。
; 当EFLAGS中TF标志置位时而引发的中断。当发现硬件断点（数据：陷阱，代码：错误）：或者
; 开启了指令跟踪陷阱或任务交换陷阱，或者调试寄存器访问无效（错误），CPU就会产生该异常。
_debug:
	pushl $_do_int3		# _do_debug
	jmp no_error_code

; int2--非屏蔽中断调用入口点。类型：陷阱：无错误号。
; 这是仅有的被赋予固定中断向量的硬件中断。每当接收到一个NMI信号，CPU内部就会产生中断
; 向量2，并执行标准中断应答周期，因此很节省时间。NMI通常保留为极为重要的硬件事件使用。
; 当CPU收到一个NMI信号并且开始执行其中断处理过程时，随后所有的硬件中断都将被忽略。
_nmi:
	pushl $_do_nmi
	jmp no_error_code

; int3-断点指令引起中断的入口点。类型：陷阱：无错误号。
; 由int3指令引发的中断，与硬件中断无关。该指令通常由调式器插入被调式程序的代码中。
; 处理过程同_debug。
_int3:
	pushl $_do_int3
	jmp no_error_code

; int4-溢出出错处理中断入口点。类型：陷阱：无错误号。
; EFLAGS中OF标志置位时CPU执行INTO指令就会引发该中断。通常用于编译器跟踪算术计算溢出。
_overflow:
	pushl $_do_overflow
	jmp no_error_code

; int5-边界检查出错中断入口点。类型：错误：无错误号。
; 当操作数在有效范围以外时引发的中断。当BOUND指令测试失败就会产生该中断。BOUND指令有
; 3个操作数，如果第1个不在另外两个之间，就产生异常5。
_bounds:
	pushl $_do_bounds
	jmp no_error_code

; int6-无效操作指令出错中断入口点。
; 类型：错误：无错误号。
; CPU执行机构检测到一个无效的操作码而引起的中断。
_invalid_op:
	pushl $_do_invalid_op
	jmp no_error_code

; int9-协处理器段超出出错中断入口点。类型：放弃：无错误号。
; 该异常基本上等同于协处理器出错保护。因为在浮点指令操作数太大时，我们就有这个机会来
; 加载或者保存超出数据段的浮点值
_coprocessor_segment_overrun:
	pushl $_do_coprocessor_segment_overrun
	jmp no_error_code

; int15-其他Intel保留中断的入口点。
_reserved:
	pushl $_do_reserved
	jmp no_error_code

; int45-(=0x20+13)Linux设置的数学协处理器硬件中断。
; 当协处理器执行完一个操作时就会发出IRQ13中断信号，以通知CPU操作完成。80387在执行
; 计算时，CPU会等待其操作完成。下面88行上OxF0是协处理端口，用于清忙锁存器。通过写
; 该端口，本中断将消除CPU的BSY延续信号，并重新微活80387的处理器扩展请求引脚PEREQ.
; 该操作主要是为了确保在继续执行80387的任何指令之前，CPU响应本中断。
_irq13:
	pushl %eax
	xorb %al,%al
	outb %al,$0xF0
	movb $0x20,%al
	outb %al,$0x20              # 向8259主中断控削芯片发送EOI(中断结束)信号。
	jmp 1f						# 延时
1:	jmp 1f
1:	outb %al,$0xA0              # 再向8259从中断控制芯片发送EOI(中断结束)信号
	popl %eax
	jmp _coprocessor_error      # _coprocessor_error原本再本程序中,现在已经放入system_call.s中



; 以下中断在调用时CPU会在中断返回地址之后将出错号压入堆栈，因此返回时也需要将出错号
; 弹出（参见图8.4(b))。
_double_fault:
	pushl $_do_double_fault
error_code:
	xchgl %eax,4(%esp)		# error code <-> %eax
	xchgl %ebx,(%esp)		# &function <-> %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl %eax			# error code
	lea 44(%esp),%eax		# offset
	pushl %eax
	movl $0x10,%eax     # 初始化段寄存器ds,cs和fs,加载内核数据段选择符
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	call *%ebx
	addl $8,%esp
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret

; int10--无效的任务状态段(TSS)。类型：错误：有出错码。
; CPU企图切换到一个进程，而该进程的TSS无效。根据TSS中哪一部分起了异常，当由于TSS
; 长度超过104字节时，这个异常在当前任务中产生，因而切换被终止。其他问题则会导致在切换
; 后的新任务中产生本异常。
_invalid_TSS:
	pushl $_do_invalid_TSS
	jmp error_code

; int11--段不存在。类型：错误：有出错码。
; 被引用的段不在内存中。段描述符中标志着段不在内存中。
_segment_not_present:
	pushl $_do_segment_not_present
	jmp error_code

; int12--堆栈段错误。类型：错误：有出错码。
; 指令操作试图超出堆栈段范围，或者堆栈段不在内存中。这是异常11和13的特例。有些操作
; 系统可以利用这个异常来确定什么时候应该为程序分配更多的栈空间。
_stack_segment:
	pushl $_do_stack_segment
	jmp error_code

; int13--一般保护性出错。类型：错误：有出错码。
; 表明是不属于任何其他类的错误。若一个异常产生时没有对应的处理向量(0-一16)，通常就
; 会归到此类。
_general_protection:
	pushl $_do_general_protection
	jmp error_code

#int7-设备不存在(_device_not_available)在kernel/system_call.s。
#intl4-页错误(_page_fault)在mm/page.s,l4行。
#int16-协处理器错误(_coprocessor_error)在kernel/system_call.s。
#时钟中断int0x20(_timer_interrupt)在kernel/,system_.call.s。
#系统调用int0x80(_system_call)在kernel/system_call.s。