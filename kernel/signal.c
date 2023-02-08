/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>	// 调度程序头文件，定义了任务结构task_struct、初始任务0的数据，还有一些有关描述符参数设置和获取的嵌入式汇编函数宏语句。
#include <linux/kernel.h> 	// 内核头文件。含有一些内核常用函数的原形定义。
#include <asm/segment.h>	// 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数。

#include <signal.h>			// 信号头文件.定义信号符号常量，信号结构以及信号操作函数原型。


// 下面函数名前的关键字volati1e用于告诉编译器gcc该函数不会返回。这样可让gcc产生更好
// 些的代码，更重要的是使用这个关键字可以避免产生某些（未初始化变量的）假警告信息。
// 等同于现在gcc的函数属性说明：void do_exit(int error_code) __attribute__ ((noreturn))
volatile void do_exit(int error_code);

// 获取当前任务信号屏蔽位图（屏蔽码或阻塞码）。sgetmask可分解为signal-get-mask。以下类似。
int sys_sgetmask()
{
	return current->blocked;
}

// 设置新的信号屏蔽位图。SIGKILL不能被屏蔽。返回值是原信号屏蔽位图。
int sys_ssetmask(int newmask)
{
	int old=current->blocked;

	current->blocked = newmask & ~(1<<(SIGKILL-1));
	return old;
}

// 复制sigaction数据到fs数据段to处。即从内核空间复制到用户（任务）数据段中。
static inline void save_old(char * from,char * to)
{
	int i;

	// 首先验证to处的内存空间是否足够大。然后把一个sigaction结构信息复制到fs段（用户）
	// 空间中。宏函数put_fs_byte()在include/asm/segment.h中实现。
	verify_area(to, sizeof(struct sigaction));			//fork.c中定义
	for (i=0 ; i< sizeof(struct sigaction) ; i++) {
		put_fs_byte(*from,to);
		from++;
		to++;
	}
}

// 把sigaction数据从fs数据段from位置复制到to处。即从用户数据空间复制到内核数据段中。
static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
		*(to++) = get_fs_byte(from++);
}

// signal系统调用。类似于sigaction。为指定的信号安装新的信号句柄（信号处理程序）。
// 信号句柄可以是用户指定的函数，也可以是SIG_DFL(默认句柄)或SIG_IGN(忽略)。
// 参数
// signum-指定的信号：handler-指定的句柄：
// restorer-恢复函数指针，该函数由Libc库提供。用于在信号处理程序结束后恢复系统调用返回时几个寄存器的原有值以及系统
// 调用的返回值，就好象系统调用没有执行过信号处理程序而直接返回到用户程序一样。
// 函数返回原信号句柄。
int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;

	// 首先验证信号值在有效范围(1-32)内，并且不得是信号SIGKILL(和SIGSTOP)。因为这
	// 两个信号不能被进程捕获。
	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
	
	// 然后根据提供的参数组建sigaction结构内容。sa_handler是指定的信号处理句柄（函数）。
	// sa_mask是执行信号处理句柄时的信号屏蔽码。saf1ags是执行时的一些标志组合。这里设定
	// 该信号处理句柄只使用1次后就恢复到默认值，并允许信号在自己的处理句柄中收到。
	tmp.sa_handler = (void (*)(int)) handler;
	tmp.sa_mask = 0;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
	tmp.sa_restorer = (void (*)(void)) restorer;
	handler = (long) current->sigaction[signum-1].sa_handler;
	current->sigaction[signum-1] = tmp;
	return handler;
}

// sigaction系统调用。改变进程在收到一个信号时的操作。signum是除了SIGKILL以外的
// 任何信号。[如果新操作(action)不为空]则新操作被安装。如果oldaction指针不为空，
// 则原操作被保留到oldaction.。成功则返回0，否则为-1。
int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;

	tmp = current->sigaction[signum-1];
	// 在信号的sigaction结构中设置新的操作（动作）。如果oldaction指针不为空的话，则将
	// 原操作指针保存到oldaction所指的位置。
	get_new((char *) action,
		(char *) (signum-1+current->sigaction));
	if (oldaction)
		save_old((char *) &tmp,(char *) oldaction);
	
	// 如果允许信号在自己的信号句柄中收到，则令屏蔽码为0，否则设置屏蔽本信号。
	if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
		current->sigaction[signum-1].sa_mask = 0;
	else
		current->sigaction[signum-1].sa_mask |= (1<<(signum-1));
	return 0;
}


// 系统调用的中断处理程序中真正的信号预处理程序（在kernel/system_cal1.s,193行）。
// 该段代码的主要作用是将信号处理句柄插入到用户程序堆栈中，并在本系统调用结束返回
// 后立刻执行信号句柄程序，然后继续执行用户的程序。这个函数处理比较粗咯，尚不能处
// 理进程暂停SIGSTOP等信号。
// 函数的参数是进入系统调用处理程序system_call.s开始，直到调用本函数(system_call.s
// 第193行)前逐步压入堆栈的值。这些值包括（在system_cal1.s中的代码行）：
// CPU执行中断指令压入的用户栈地址ss和esp、标志寄存器eflags和返回地址cs和eip:
// 第111--121行在刚进入system_cal1时压入栈的寄存器ds、es、fs和edx、ecx、ebx:
// 第132行调用sys_call_table后压入栈中的相应系统调用处理函数的返回值(eax)。
// 第192行压入栈中的当前处理的信号值(signr)。
void do_signal(long signr,long eax, long ebx, long ecx, long edx,
	long fs, long es, long ds,
	long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
	unsigned long sa_handler;
	long old_eip=eip;
	struct sigaction * sa = current->sigaction + signr - 1;
	int longs;
	unsigned long * tmp_esp;

	// 如果信号句柄为SIG_IGN (1,默认忽咯句柄) 则不对信号进行处理而直接返回：如果句柄为
	// SIG_DFL(0,默认处理)，则如果信号是SIGCHLD也直接返回，否则终止进程的执行。
	// 句柄SIG_IGN被定义为1，SIG_DFL被定义为0。参见include,/signal.h,第45、46行。
	// 第100行do_exit的参数是返回码和程序提供的退出状态信息。可作为wait或waitpid函数
	// 的状态信息。参见sys/wait.h文件第13-l8行。wait或waitpid利用这些宏就可以取得子
	// 进程的退出状态码或子进程终止的原因（信号）。
	sa_handler = (unsigned long) sa->sa_handler;
	if (sa_handler==1)
		return;
	if (!sa_handler) {
		if (signr==SIGCHLD)
			return;
		else
			do_exit(1<<(signr-1)); //不再返回到这里
	}

	// OK, 以下准备对信号句柄的调用设置。如果该信号句柄只需使用一次，则将该句柄置空。
	// 注意，该信号句柄已经保存在sa_handler指针中。
	// 在系统调用进入内核时，用户程序返回地址(eip、cs)被保存在内核态栈中。下面这段代
	// 码修改内核态堆栈上用户调用系统调用时的代码指针p为指向信号处理句柄，同时也将
	// sa_restorer、signr、进程屏蔽码（如果SA_NOMASK没置位）、eax、ecx、edx作为参数以及
	// 原调用系统调用的程序返回指针及标志寄存器值压入用户堆栈。因此在本次系统调用中断
	// 返回用户程序时会首先执行用户的信号句柄程序，然后再继续执行用户程序。
	if (sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;

	// 将内核态栈上用户调用系统调用下一条代码指令指针eip指向该信号处理句柄。由于C函数
	// 是传值函数，因此给eip赋值时需要使用“"*(&eip)”的形式。另外，如果允许信号自己的
	// 处理句柄收到信号自己，则也需要将进程的阻塞码压入堆栈。
	// 这里请注意，使用如下方式（第172行）对普通C函数参数进行修改是不起作用的。因为当
	// 函数返回时堆浅上的参数将会被调用者丢弃。这里之所以可以使用这种方式，是因为该函数
	// 是从汇编程序中被调用的，并且在函数返回后汇编程序并没有把调用do_signa1时的所有
	// 参数都丢弃。eip等仍然在堆栈中。
	// sigaction结构的sa_mask字段给出了在当前信号句柄（信号描述符）程序执行期间应该被
	// 屏蔽的信号集。同时，引起本信号句柄执行的信号也会被屏蔽。不过若sa_flags中使用了
	// SA_NOMASK标志，那么引起本信号句柄执行的信号将不会被屏蔽掉。如果允许信号自己的处
	// 理句柄程序收到信号自己，则也需要将进程的信号阻塞码压入堆栈。
	*(&eip) = sa_handler;
	longs = (sa->sa_flags & SA_NOMASK)?7:8;

	
	// 将原调用程序的用户堆栈指针向下扩展7（或8）个长字(4字节)（用来存放调用信号句柄的参数等），
	// 并检查内存使用情况（例如如果内存超界则分配新页等）。
	*(&esp) -= longs;
	verify_area(esp,longs*4);

	// 在用户堆栈中从下到上存放sa_restorer、信号signr、屏蔽码blocked(如果SA_NOMASK
	// 置位)、eax、ecx、edx、eflags和用户程序原代码指针。
	tmp_esp=esp;
	put_fs_long((long) sa->sa_restorer,tmp_esp++);
	put_fs_long(signr,tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK))
		put_fs_long(current->blocked,tmp_esp++);
	put_fs_long(eax,tmp_esp++);
	put_fs_long(ecx,tmp_esp++);
	put_fs_long(edx,tmp_esp++);
	put_fs_long(eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	current->blocked |= sa->sa_mask;
}
// 最后说明一下执行的流程。在do_signel()执行完后，system_call.s将会把进程内核态堆栈上eip以下
// 的所有值弹出堆栈。在执行了iret指令之后，CPU将把内核态堆栈上的cs:eip、eflags以及ss:esp弹出
// 恢复到用户态去执行程序。由于eip已经被替换为指向信号句柄，因此，此刻即会立即执行用户自定义
// 的信号处理程序。在该信号处理程序执行完后，通过ret指令，CPU会把控制权移交给sa_restorer所指
// 向的恢复程序去执行。而sa_restorer程序会做一些用户态堆栈的清理工作，也即会跳过堆栈上的信号值
// signr,并把系统调用后的返回值eax和寄存器ecx、edx以及标志寄存器eflags弹出，完全恢复了系统调
// 用后各寄存器和CPU的状态。最后通过sa_restorer的ret指令弹出原用户程序的eip(也即堆栈上的
// old eip),返回去执行用户程序。

// sa_restorer libc2.2.2实现,弹出各种参数,恢复到系统调用之前的状态. 这次其实系统调用偷偷执行了signal处理函数 :-)
// .globl __sig_restore
// .globl __masksig_restore
// # 若没有blocked则使用这个restorer函数
// __sig_restore:
// 		addl $4,%esp   	#丢弃信号值signr
// 		popl %eax 		#恢复系统调用返回值。
// 		popl %ecx		#恢复原用户程序寄存器值。
// 		popl %edx
// 		popfl			#恢复用户程序时的标志寄存器。
// 		ret