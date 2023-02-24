#ifndef _SCHED_H
#define _SCHED_H

#define NR_TASKS 64		//系统最多同时任务数(进程数)
#define HZ 100

#define FIRST_TASK task[0]
#define LAST_TASK task[NR_TASKS-1]

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <signal.h>

#if (NR_OPEN > 32)
#error "Currently the close-on-exec-flags are in one word, max 32 files/proc"
#endif

//定义进程运行可能处的状态
#define TASK_RUNNING			0	// 进程正在运行or已经准备就绪
#define TASK_INTERRUPTIBLE		1	// 进程处于可中断等待状态
#define TASK_UNINTERRUPTIBLE	2	// 进程处于不可中断等待状态,主要用于I/O操作等待			https://segmentfault.com/a/1190000030691687  无法使用kill -9杀死的(kill也是一种信号)，除非重启系统(没错，就是这么头硬)
#define TASK_ZOMBIE				3	// 进程处于僵死状态,已经停止运行,但父进程还没发信号
#define TASK_STOPPED			4	// 进程已经停止

#ifndef NULL
#define NULL ((void *) 0)	
#endif

extern int copy_page_tables(unsigned long from, unsigned long to, long size);
extern int free_page_tables(unsigned long from, unsigned long size);

extern void sched_init(void);
extern void schedule(void);
extern void trap_init(void);
extern void panic(const char * str);
extern int tty_write(unsigned minor,char * buf,int count);

typedef int (*fn_ptr)();

struct i387_struct {
	long	cwd;
	long	swd;
	long	twd;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */
};
// 任务状态段数据结构 表4-34
struct tss_struct {
	long	back_link;	/* 16 high bits zero */
	long	esp0;
	long	ss0;		/* 16 high bits zero */
	long	esp1;
	long	ss1;		/* 16 high bits zero */
	long	esp2;
	long	ss2;		/* 16 high bits zero */
	long	cr3;
	long	eip;
	long	eflags;
	long	eax,ecx,edx,ebx;
	long	esp;
	long	ebp;
	long	esi;
	long	edi;
	long	es;		/* 16 high bits zero */
	long	cs;		/* 16 high bits zero */
	long	ss;		/* 16 high bits zero */
	long	ds;		/* 16 high bits zero */
	long	fs;		/* 16 high bits zero */
	long	gs;		/* 16 high bits zero */
	long	ldt;		/* 16 high bits zero */
	long	trace_bitmap;	/* bits: trace 0, bitmap 16-31 */
	struct i387_struct i387;
};

// 这里是task_struct任务(进程)数据结构，或称为进程描述符 task_struct
// =======================================
// long state 						//任务的运行状态(-1不可运行，0可运行（就绪），>0已停止)。
// long counter 					//任务运行时间计数(递减)(滴答数),运行时间片
// long priority 					//运行优先数,任务开始运行时counter=priority, 越大运行越长
// long signal 						//信号。是位图，每个比特位代表一种信号，信号值=位偏移值+1
// struct sigaction sigaction[32]	//信号执行属性结构，对应信号将要执行的操作和标志信息
// long blocked						//进程信号屏蔽码(对应信号位图)
// ------------------------------------------
// int exit_code					//任务执行停止的退出码，其父进程会取
// unsigned long start_code			//代码段地址
// unsigned long end_code			//代码长度(字节数)
// unsigned long end_data			//代码长度+数据长度(字节数)
// unsigned long brk				//总长度(字节数)
// unsigned long start_stack		//堆栈段地址
// long pid 						//进程标识号(进程号)
// long father						//父进程号
// long pgrp						//进程组号
// long session						//会话号
// long leader 						//会话首领
// unsigned short uid				//用户标识号(用户id)
// unsigned short euid				//有效用户id
// unsigned short suid				//保存的用户id
// unsigned short gid				//组标识号(组id)
// unsigned short egid				//有效组id
// unsigned short sgid				//保存的组id
// long alarm						//报警定时器(tick数)
// long utime						//用户态运行时间(tick数)
// long stime						//系统态运行时间(tick数)
// long cutime						//子进程用户态运行时间
// long cstime						//子进程系统态运行时间
// long start_time					//进程开始运行时刻
// unsigned short used_math			//标志:是否使用了协处理器
// -------------------------------------------------
// int tty							//进程使用tty的子设备号。-1表示没有使用。
// unsigned short umask				//文件创建属性屏蔽位。
// struct m_inode* pwd 				//当前工作目录i节点结构
// struct m_inode* root				//根目录i节点结构
// struct m_inode* executable		//执行文件i节点结构。
// unsigned long close_on_exec		//执行时关闭文件句柄位图标志。（参见include/fcntl.h)
// struct file* filp[NR_OPEN]		//进程使用的文件表结构。
// ----------------------------------------------------
// struct desc_struct ldt[3]		//本人任务的局部描述符 0-空 1-代码段cs 2数据段和堆栈段ds&ss
// ----------------------------------------------------
// struct tss_struct tss			//本进程的任务状态段信息结构

struct task_struct {
/* these are hardcoded - don't touch */
	long state;	/* -1 unrunnable, 0 runnable, >0 stopped */
	long counter;
	long priority;
	long signal;
	struct sigaction sigaction[32];
	long blocked;	/* bitmap of masked signals */
/* various fields */
	int exit_code;
	unsigned long start_code,end_code,end_data,brk,start_stack;
	long pid,father,pgrp,session,leader;
	unsigned short uid,euid,suid;
	unsigned short gid,egid,sgid;
	long alarm;
	long utime,stime,cutime,cstime,start_time;
	unsigned short used_math;
/* file system info */
	int tty;		/* -1 if no tty, so it must be signed */
	unsigned short umask;
	struct m_inode * pwd;
	struct m_inode * root;
	struct m_inode * executable;
	unsigned long close_on_exec;
	struct file * filp[NR_OPEN];
/* ldt for this task 0 - zero 1 - cs 2 - ds&ss */
	struct desc_struct ldt[3];
/* tss for this task */
	struct tss_struct tss;
};

/*
 *  INIT_TASK is used to set up the first task table, touch at //第一个任务task0
 * your own risk!. Base=0, limit=0x9ffff (=640kB)
 */
#define INIT_TASK \
/* state etc */	{ 0,15,15, \                   	// state, counter, priority
/* signals */	0,{{},},0, \					// signal, sigaction[32], blocked
/* ec,brk... */	0,0,0,0,0,0, \					// exit_code, start_code, end_code, end_data, brk, start_stack
/* pid etc.. */	0,-1,0,0,0, \					// pid, father, pgrp, session, leader
/* uid etc */	0,0,0,0,0,0, \					// uid, euid, suid, gid, egid, sgid
/* alarm */	0,0,0,0,0,0, \						// alarm, utime, stime, cutime, cstime, start_time
/* math */	0, \								// used_math
/* fs info */	-1,0022,NULL,NULL,NULL,0, \		// tty, umask, pwd, root, executable, close_on_exec
/* filp */	{NULL,}, \							// filp[20]
	{ \											// ldt[3]
		{0,0}, \
/* ldt */	{0x9f,0xc0fa00}, \					// 代码长640k, 基址0x0,G=1,D=1,DPL=3,P=1 TYPE=0x0a
		{0x9f,0xc0f200}, \						// 数据长640k, 基址0x0,G=1,D=1,DPL=3,P=1 TYPE=0x02 https://blog.csdn.net/jmh1996/article/details/83034195
	}, \
/*tss*/	{0,PAGE_SIZE+(long)&init_task,0x10,0,0,0,0,(long)&pg_dir,\ //tss
	 0,0,0,0,0,0,0,0, \
	 0,0,0x17,0x17,0x17,0x17,0x17,0x17, \
	 _LDT(0),0x80000000, \
		{} \
	}, \
}

extern struct task_struct *task[NR_TASKS];
extern struct task_struct *last_task_used_math;
extern struct task_struct *current;
extern long volatile jiffies;
extern long startup_time;

#define CURRENT_TIME (startup_time+jiffies/HZ)

extern void add_timer(long jiffies, void (*fn)(void));
extern void sleep_on(struct task_struct ** p);
extern void interruptible_sleep_on(struct task_struct ** p);
extern void wake_up(struct task_struct ** p);

/*
 * Entry into gdt where to find first TSS. 0-nul, 1-cs, 2-ds, 3-syscall
 * 4-TSS0, 5-LDT0, 6-TSS1 etc ...
 */
#define FIRST_TSS_ENTRY 4
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1)

// 计算在全局表中第n个任务的TSS段描述符的选择符值（偏移量）。
// 因每个描述符占8字节，因此FIRST_TSS_ENTRY<<3表示该描述符(TSS0)在GDT表中的起始偏移位置。
// 因为每个任务使用1个TSS和1个LDT描述符，共占用16字节(TSS+LDT)，因此需要n<<4来表示对应
// TSS起始位置。该宏得到的值正好也是该TSS的选择符值。---- 错误的描述

// 计算(16位)段选择符，注意不是相对GDT的偏移地址 FIRST_TSS_ENTRY<<3 左移3位是TI和RPL设置0, 之后根据n转化成GDT中的索引号  段选择符:去LDT/GDT的index项中拿段描述符
// 4: 0x0100 --> 0x0100 000   6: 0x1 0000 + 0x0100 000   ---> 0x0110 000 
#define _TSS(n) (  (((unsigned long) n)<<4)  +  (FIRST_TSS_ENTRY<<3)  )

// 计算在全局表中第n个任务的LDT段描述符的选择符值(偏移量)
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3))

// 把第n个任务的TSS段选择符加载到任务寄存器TR中
#define ltr(n) __asm__("ltr %%ax"::"a" (_TSS(n)))

// 把第n个任务的LDT段选择符加载到局部描述符表寄存器LDTR中
#define lldt(n) __asm__("lldt %%ax"::"a" (_LDT(n)))

// 取当前任务的任务号(是任务数组中的索引值,与进程号pid不同)
// 返回:n 当前任务号。用于kernel/traps.c中。   _TSS(n)的逆操作
#define str(n) \
__asm__("str %%ax\n\t" \				//将当前任务寄存器中TSS段的选择符复制到ax中
	"subl %2,%%eax\n\t" \				//eax = eax - FIRST_TSS_ENTRY<<3
	"shrl $4,%%eax" \					//eax = (eax/16) 当前任务号
	:"=a" (n) \
	:"a" (0),"i" (FIRST_TSS_ENTRY<<3))


/*
	switch_to(n)将切换当前任务到任务nr,即n。首先检测任务n不是当前任务，
如果是则什么也不做退出。如果我们切换到的任务最近（上次运行）使用过数学
协处理器的话，则还需复位控制寄存器cr0中的TS标志。
 */
// 跳转到一个任务的TSS段选择符组成的地址处会造成CPU进行任务切换操作。
// 输入	%0: 指向__tmp 				%1:指向__tmp.b,用于存放新的TSS的选择符
// 		dx: 新任务n的TSS段选择符 	ecx: 新任务n的任务结构指针task[n]
// _tmp+0:未定义(long)
// _tmp+4:新任务TSS的选择符(word)
// _tmp+6:未定义(word)
// 其中临时数据结构tmp用于组建241行远跳转(far jump)ljmp指令的操作数。该操作数由4字节偏移
// 地址和2字节的段选择符组成。因此tmp中a的值是32位偏移值，而b的低2字节是新TSS段的
// 选择符（高2字节不用）。跳转到TSS段选择符会造成任务切换到该TSS对应的进程。对于造成任务
// 切换的长跳转,a值无用, 因为是跳到对应的b:tss段(tss段里是cpu的信息,恢复cpu)，a:偏移值并不care。241行上的内存间接跳转指令使用6字节操作数作为跳转目的地的长指针，
// 其格式为：jmp 16位段选择符:32位偏移值。但在内存中操作数的表示顺序与这里正好相反。       
// 任务切换回来之后，在判断原任务上次执行是否使用过协处理器时，是通过将原任务指针与保存在
// last_task_used_math变量中的上次使用过协处理器任务指针进行比较而作出的，参见文件
// kernel/sched.c中有关math_state_restore函数的说明
#define switch_to(n) {\
struct {long a,b;} __tmp; \
__asm__("cmpl %%ecx,_current\n\t" \							//任务n是当前任务吗? (current == task[n]?)
	"je 1f\n\t" \											//是，直接退出
	"movw %%dx,%1\n\t" \									//将新任务TSS的16位选择符存入__tmp.b中
	"xchgl %%ecx,_current\n\t" \							//current = task[n], ecx = 被切换出的任务
	"ljmp %0\n\t" \											//执行长跳转到 *&__tmp.a。 造成任务切换  ljmp __tmp.b : __tmp.a(从未设置过,默认0)   

	\//任务切换回来后才会继续执行下面的语句
	"cmpl %%ecx,_last_task_used_math\n\t" \            		//原任务上次使用过协处理器吗?
	"jne 1f\n\t" \											//没有使用则跳转退出
	"clts\n" \												//原任务上次使用过协处理器,则清理cr0中的任务切换标志TS
	"1:" \													
	::"m" (*&__tmp.a),"m" (*&__tmp.b), \
	"d" (_TSS(n)),"c" ((long) task[n])); \
}

//页面地址对准，获得n所在地址的下一页(向下一个页取整)
#define PAGE_ALIGN(n) (((n)+0xfff)&0xfffff000)


// 设置位于地址addr处描述符中的各基地址字段（基地址是base).
// %0-地址addr偏移2 
// %l-地址addr偏移4 
// %2-地址addr偏移7 
// edx-基地址base。
#define _set_base(addr,base) \
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %%dl,%1\n\t" \
	"movb %%dh,%2" \
	::"m" (*((addr)+2)), \
	  "m" (*((addr)+4)), \
	  "m" (*((addr)+7)), \
	  "d" (base) \
	:"dx")

#define _set_limit(addr,limit) \
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %1,%%dh\n\t" \
	"andb $0xf0,%%dh\n\t" \
	"orb %%dh,%%dl\n\t" \
	"movb %%dl,%1" \
	::"m" (*(addr)), \
	  "m" (*((addr)+6)), \
	  "d" (limit) \
	:"dx")

#define set_base(ldt,base) _set_base( ((char *)&(ldt)) , base )
#define set_limit(ldt,limit) _set_limit( ((char *)&(ldt)) , (limit-1)>>12 )


// 从地址addr处描述符中取段基地址。功能与set_base()正好相反。
// edx-存放基地址(_base); %l-地址addr偏移2  %2-地址addr偏移4  %3-addr偏移7。
#define _get_base(addr) ({\
unsigned long __base; \
__asm__("movb %3,%%dh\n\t" \			// 取[addr+7]处基址高16位的高8位(31-24) ->dh
	"movb %2,%%dl\n\t" \				// 取[addr+4]处基址高16位的低8位(23-16) ->dl
	"shll $16,%%edx\n\t" \				// 基地址高16位移到edx中高16位处(左移16位)
	"movw %1,%%dx" \					// 取[addr+2]处基地址低16位(15-0)		->dx
	:"=d" (__base) \					// 返回
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7))); \
__base;})

// 传入ldt[x], 返回所指段描述符中的基地址
#define get_base(ldt) _get_base( ((char *)&(ldt)) )


// 取段选择符segment指定的描述符中的段限长值
// 指令lsl是Load Segment Limit缩写。它从指定段描述符中取出分散的限长比特位拼成完整的
// 段限长值放入指定寄存器中。所得的段限长是实际字节数减1，因此这里还需要加1后才返回。
// %0-存放段长值（字节数）：%1-段选择符segment
#define get_limit(segment) ({ \
unsigned long __limit; \
__asm__("lsll %1,%0\n\tincl %0":"=r" (__limit):"r" (segment)); \
__limit;})

#endif
