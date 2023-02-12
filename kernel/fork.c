/*
 *  linux/kernel/fork.c
 * fork系统调用用于创建子进程.Liux中所有进程都是进程0（任务0）的子进程.该程序是sys_fork
(在kernel/system_call.s中从208行开始)系统调用的辅助处理函数集，给出了sys_fork()系统调用中使
用的两个C语言函数：find_empty_process和copy_process。还包括进程内存区域验证与内存分配函数
verify_area和copy_mem
 *  (C) 1991  Linus Torvalds
 */

/*
fork.c中含有系统调用fork的辅助子程序（参见system_call.s),以及一些
其他函数verify_area.一旦你了解了fork,就会发现它是非常简单的，但
内存管理却有些难度。参见mm/memory.c中的copy_page_tables函数。
 */
#include <errno.h>			//错误号头文件。包含系统中各种出错号。(Linus从minix中引进)。

#include <linux/sched.h>	//调度程序头文件，定义了任务结构task struct、任务0的数据，还有一些有关描述符参数设置和获取的嵌入式汇编函数宏语句。
#include <linux/kernel.h>	//内核头文件。含有一些内核常用函数的原形定义。
#include <asm/segment.h>	//段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数。
#include <asm/system.h>		//系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏。

//写页面验证。若页面不可写，则复制页面。定义在mm/memory.c
extern void write_verify(unsigned long address);

long last_pid=0;	//最新进程号，其值会由get_empty_process生成

// 进程空间区域写前验证函数。
// 对于80386CPU,在执行特权级0代码时不会理会用户空间中的页面是否是页保护的，因此
// 在执行内核代码时用户空间中数据页面保护标志起不了作用，写时复制机制也就失去了作用
// verify_area(函数就用于此目的。但对于80486或后来的CPU,其控制寄存器CR0中有一个
// 写保护标志WP(位16)，内核可以通过设置该标志来禁止特权级0的代码向用户空间只读
// 页面执行写数据，否则将导致发生写保护异常。从而486以上CPU可以通过设置该标志来达
// 到本函数的目的。
// 该函数对当前进程逻辑地址从addr到addr+size这一段范围以页为单位执行写操作前
// 的检测操作。由于检测判断是以页面为单位进行操作，因此程序首先需要找出addr所在页
// 面开始地址start,然后start加上进程数据段基址，使这个start变换成CPU 4G线性空
// 间中的地址。最后循环调用write_verify对指定大小的内存空间进行写前验证。若页面
// 是只读的，则执行共享检验和复制页面操作(写时复制)。
void verify_area(void * addr,int size)
{
	unsigned long start;

	// 首先将起始地址start调整为其所在页的左边界开始位置,同时相应地调整验证区域大小。
	// 下句中的 start & 0xfff 用来获得指定起始位置addr(也即start)在所在页面中的偏移
	// 值，原验证范围size加上这个偏移值即扩展成以addr所在页面起始位置开始的范围值。
	// 因此在50行上也需要把验证开始位置start调整成页面边界值。参见前面的图 "内存验
	// 证范围的调整"。
	//	|-----------------------------------------------|
	//			           start       |
	//  |<--  start&0xfff  --><--size-->
	//  |start&0xfffff000
	//  |<--          size           -->
	start = (unsigned long) addr;
	size += start & 0xfff; // start & 0xfff 
	start &= 0xfffff000;
	// 下面把start加上进程数据段在线性地址空间中的起始基址，变成系统整个线性空间中的地
	// 址位置。对于Liux0.11内核，其数据段和代码段在线性地址空间中的基址和限长均相同。
	// 然后循环进行写页面验证。若页面不可写，则复制页面。(mm/memory.c,261行)
	start += get_base(current->ldt[2]);  //基地址+偏移量=线性地址
	while (size>0) {
		size -= 4096;
		write_verify(start);
		start += 4096;
	}
}

// 复制内存页表。
// 参数nr是新任务号：p是新任务数据结构指针。该函数为新任务在线性地址空间中设置代码
// 段和数据段基址、限长，并复制页表。由于Linux系统采用了写时复制(copy on write)
// 技术，因此这里仅为新进程设置自己的页目录表项和页表项，而没有实际为新进程分配物理
// 内存页面。此时新进程与其父进程共享所有内存页面。操作成功返回0，否则返回出错号。
int copy_mem(int nr,struct task_struct * p)
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;


	// 首先取当前进程局部描述符表中代码段描述符和数据段描述符项中的段限长（字节数）。
	// 0x0F是代码段选择符：0x17是数据段选择符。然后取当前进程代码段和数据段在线性地址
	// 空间中的基地址。由于Liux0.11内核还不支持代码和数据段分立的情况，因此这里需要
	// 检查代码段和数据段基址和限长是否都分别相同。否则内核显示出错信息，并停止运行。
	// get_limit 和 get_base 定义在include/1inux/sched.h
	code_limit=get_limit(0x0f);
	data_limit=get_limit(0x17);
	old_code_base = get_base(current->ldt[1]);
	old_data_base = get_base(current->ldt[2]);
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");
	if (data_limit < code_limit)
		panic("Bad data_limit");
	
	// 然后设置创建中的新进程在线性地址空间中的基地址等于(64MB*其任务号)，并用该值
	// 设置新进程局部描述符表中段描述符中的基地址。接着设置新进程的页目录表项和页表项，
	// 即复制当前进程（父进程）的页目录表项和页表项。此时子进程共享父进程的内存页面。
	// 正常情况下copy_page_tables返回0，否则表示出错，则释放刚申请的页表项。
	new_data_base = new_code_base = nr * 0x4000000;//64MB * nr 为线性空间的基地址
	p->start_code = new_code_base;
	set_base(p->ldt[1],new_code_base);
	set_base(p->ldt[2],new_data_base);
	if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
		//复制页表失败要free
		free_page_tables(new_data_base,data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
	下面是主要的fork子程序。它复制系统进程信息(task[n])
并且设置必要的寄存器。还整个的复制制数据段
 */
// 复制进程。
// 该函数的参数是进入系统调用中断处理过程system_call.s开始，直到调用本系统调用处理
// 过程system call.s第322行和调用本函数前时system call.s第317行逐步压入栈的
// 各寄存器的值。这些在system_call.s程序中逐步压入栈的值（参数）包括：
// CPU执行中断指令压入的用户栈地址ss和esp、标志寄存器eflags和返回地址cs和eip:
// 在刚进入system_cal1时压入栈的段寄存器ds、es、fs和edx、ecx、ebx;
// 调用sys_call_table中sys_fork函数时压入栈的返回地址（用参数none表示）；
// 在调用copy_process()之前压入栈的gs、esi、edi、ebp和eax(nr)值。
// 其中参数nr是调用find_empty_process分配的任务数组项号。
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	// 首先为新任务数据结构分配内存。如果内存分配出错，则返回出错码并退出。然后将新任务
	// 结构指针放入任务数组的nr项中。其中nr为任务号，由前面find_empty_process返回。
	// 接着把当前进程任务结构内容复制到刚申请到的内存页面p开始处。
	p = (struct task_struct *) get_free_page();
	if (!p)
		return -EAGAIN;
	task[nr] = p;
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack  注意！这样做不会复削超级用户堆栈（只复制进程结构）*/

	// 随后对复制来的进程结构内容进行一些修改，作为新进程的任务结构。先将新进程的状态
	// 置为不可中断等待状态，以防止内核调度其执行。然后设置新进程的进程号pid和父进程
	// 号father,并初始化进程运行时间片值等于其priority值(一般为15个嘀嗒)。接着
	// 复位新进程的信号位图、报警定时值、会话(session)领导标志leader、进程及其子
	// 进程在内核和用户态运行时间统计值，还设置进程开始运行的系统时间start_time。
	p->state = TASK_UNINTERRUPTIBLE;		// 不可中断等待状态
	p->pid = last_pid;						// 新进程号,由find_empty_process得到
	p->father = current->pid;				// 设置父进程号
	p->counter = p->priority;				// 运行时间片值
	p->signal = 0;							// 信号位置0
	p->alarm = 0;							// 报警定时器清零
	p->leader = 0;		/* process leadership doesn't inherit 进程的领导权不继承*/
	p->utime = p->stime = 0;				// 用户态和内核态运行时间
	p->cutime = p->cstime = 0;				// 子进程用户态和内核态运行时间
	p->start_time = jiffies;				// 进程开始运行时间(当前时间滴答数)
	
	// 再修改任务状态段TSS数据（参见列表后说明）。由于系统给任务结构p分配了1页新
	// 内存，所以(PAGE_SIZE+(long)p)让esp0正好指向该页顶端。ss0:esp0用作程序
	// 在内核态执行时的栈。另外，在第3章中我们已经知道，每个任务在GDT表中都有两个
	// 段描述符，一个是任务的TSS段描述符，另一个是任务的LDT表段描述符。下面_LDT(nr)
	// 语句就是把GDT中本任务LDT段描述符的选择符保存在本任务的TSS段中。当CPU执行
	// 切换任务时，会自动从TSS中把LDT段描述符的选择符加载到ldtr寄存器中。
	p->tss.back_link = 0;					
	p->tss.esp0 = PAGE_SIZE + (long) p;		// 内核态栈指针
	p->tss.ss0 = 0x10;						// 内核态栈的段选择符(与内核数据段一致)
	p->tss.eip = eip;						// 指令代码指针
	p->tss.eflags = eflags;					// 标志寄存器
	p->tss.eax = 0;							// fork返回时新进程返回0的原因
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;				// 段寄存器仅16位有效
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);					// 任务局部表描述符的选择符(LDT描述符在GDT中)
	p->tss.trace_bitmap = 0x80000000;		// 高16位有效

	// 如果当前任务使用了协处理器，就保存其上下文。汇编指令clts用于清除控制寄存器CRO
	// 中的任务已交换(TS)标志。每当发生任务切换，CPU都会设置该标志。该标志用于管理
	// 数学协处理器：如果该标志置位，那么每个ESC指令都会被捕获（异常7）。如果协处理
	// 器存在标志MP也同时置位的话，那么WAIT指令也会捕获。因此，如果任务切换发生在一
	// 个ESC指令开始执行之后，则协处理器中的内容就可能需要在执行新的ESC指令之前保存
	// 起来。捕获处理句柄会保存协处理器的内容并复位TS标志。指令fnsave用于把协处理器
	// 的所有状态保存到目的操作数指定的内存区域中(tss.1387)·
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));

	//接下来复制进程页表。即在线性地址空间中设置新任务代码段和数据段描述符中的基址
	//和限长，并复制页表。如果出错（返回值不是0），则复位任务数组中相应项并释放为
	//该新任务分配的用于任务结构的内存页。
	if (copy_mem(nr,p)) {
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}

	// 如果父进程中有文件是打开的，则将对应文件的打开次数增1。因为这里创建的子进程
	// 会与父进程共享这些打开的文件。将当前进程（父进程）的pwd,root和executable
	// 引用次数均增1。与上面同样的道理，子进程也引用了这些i节点。
	for (i=0; i<NR_OPEN;i++)
		if (f=p->filp[i])
			f->f_count++;
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;

	// 随后在GDT表中设置新任务TSS段和LDT段描述符项。这两个段的限长均被设置成104
	// 字节。set_tss_desc和set_ldt_desc的定义参见include/asm/system.h文件
	// “gdt+(nr<<1)+FIRST_TSS_ENTRY”是任务nr的TSS描述符项在全局
	// 表中的地址。因为每个任务占用GDT表中2项，因此上式中要包括(nr<<1)。
	// 程序然后把新进程设置成就绪态。另外在任务切换时，任务寄存器tr由CPU自动加载。
	// 最后返回新进程号。
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->state = TASK_RUNNING;	/* do this last, just in case */
	return last_pid;
}

// 为新进程取得不重复的进程号last_pid。函数返回在任务数组中的任务号(数组项)。
int find_empty_process(void)
{
	int i;

	// 首先获取新的进程号。如果last_pid增1后超出进程号的正数表示范围，则重新从1开始
	// 使用pid号。然后在任务数组中搜索刚设置的pid号是否已经被任何任务使用。如果是则
	// 跳转到函数开始处重新获得一个pid号。
	repeat:
		if ((++last_pid)<0) last_pid=1;
		for(i=0 ; i<NR_TASKS ; i++)
			if (task[i] && task[i]->pid == last_pid) goto repeat;
	// 接着在任务数组中为新任务寻找一个空闲项，并返回项号。last_pid是一个全局变量，不用返回。
	// 如果此时任务数组中64个项已经被全部占用，则返回出错码。
	for(i=1 ; i<NR_TASKS ; i++)
		if (!task[i])
			return i;
	return -EAGAIN;
}
