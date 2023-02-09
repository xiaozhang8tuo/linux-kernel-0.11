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
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	p = (struct task_struct *) get_free_page();
	if (!p)
		return -EAGAIN;
	task[nr] = p;
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */
	p->state = TASK_UNINTERRUPTIBLE;
	p->pid = last_pid;
	p->father = current->pid;
	p->counter = p->priority;
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p;
	p->tss.ss0 = 0x10;
	p->tss.eip = eip;
	p->tss.eflags = eflags;
	p->tss.eax = 0;
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);
	p->tss.trace_bitmap = 0x80000000;
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	if (copy_mem(nr,p)) {
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}
	for (i=0; i<NR_OPEN;i++)
		if (f=p->filp[i])
			f->f_count++;
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->state = TASK_RUNNING;	/* do this last, just in case */
	return last_pid;
}

int find_empty_process(void)
{
	int i;

	repeat:
		if ((++last_pid)<0) last_pid=1;
		for(i=0 ; i<NR_TASKS ; i++)
			if (task[i] && task[i]->pid == last_pid) goto repeat;
	for(i=1 ; i<NR_TASKS ; i++)
		if (!task[i])
			return i;
	return -EAGAIN;
}
