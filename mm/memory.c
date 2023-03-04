/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 * 需求加如截是从01.12.91开始编写的-在程序编制表中似乎是最重要的程序，并且应该是很容易编制的
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

#include <signal.h>

#include <asm/system.h>

#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

// 函数名前的关键字volatile用于告诉编译器gcc该函数不会返回。这样可让gcc产生更好一
// 些的代码，更重要的是使用这个关键字可以避免产生某些（未初始化变量的）假警告信息。
volatile void do_exit(long code);

static inline volatile void oom(void)
{
	printk("out of memory\n\r");
	do_exit(SIGSEGV);
}

// 刷新页变换高速缓冲宏函数。
// 为了提高地址转换的效率，CPU将最近使用的页表数据存放在芯片中高速缓冲中。在修改过
// 页表信息之后，就需要刷新该缓冲区。这里使用重新加截页目录基址寄存器c3的方法来
// 进行刷新。下面eax=0,是页目录的基址。
#define invalidate() \
__asm__("movl %%eax,%%cr3"::"a" (0))

/* these are not to be changed without changing head.s etc */
//一下定义若有改动,则需要与head.s等文件中的相关信息一起改变
//linux0.11内核默认支持的最大内存容量是16MB,可以修改信息以适合更多的内存
#define LOW_MEM 0x100000						//内存低端(1MB)
#define PAGING_MEMORY (15*1024*1024)			//分页内存15MB.主内存区最多15M
#define PAGING_PAGES (PAGING_MEMORY>>12)		//分页后的物理内存页面数(3840)
#define MAP_NR(addr) (((addr)-LOW_MEM)>>12)		//指定内存地址映射为页号
#define USED 100								//页面被占用标志

#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

static long HIGH_MEMORY = 0;

// 从from处复制1页内存到to处(4k字节)
#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024):"cx","di","si")

static unsigned char mem_map [ PAGING_PAGES ] = {0,};

/*
 * 
 * 获取物理地址的首个(实际上是最后1个)空闲页面，并标记为已使用。如果没有空闲页面，就返回0
 */
// 在主内存区中取空闲物理页面(从后往前找)。如果没有可用物理内存页面,则返回0
// 输入: %1(ax=0) 
//		 %2(LOW_MEM) 内存字节位图管理的起始位置
//		 %3(cx=PAGING_PAGES)
//		 %4(edi = mem_map+PAGING_PAGES-1)
// 输出：返回%0(ax=物理页面起始地址)
// 上面%4寄存器实际指向mem_map[]内存字节位图的最后一个字节。本函数从位图末端开始向
// 前扫描所有页面标志（页面总数为 PAGING_PAGES ),若有页面空闲(内存位图字节为0)则
// 返回页面地址。注意！本函数只是指出在主内存区的一页空闲物理页面，但并没有映射到某
// 个进程的地址空间中去。后面的put_page函数即用于把指定页面映射到某个进程的地址
// 空间中。当然对于内核使用本函数并不需要再使用put_page进行映射，因为内核代码和
// 数据空间(16MB)已经对等地映射到物理地址空间。
// 第87行定义了一个局部寄存器变量。该变量将被保存在eax寄存器中，以便于高效访问和
// 操作。这种定义变量的方法主要用于内嵌汇编程序中。详细说明参见gcc手册“在指定寄存
// 器中的变量”
// CLD指令功能：将标志寄存器Flag的方向标志位DF清零。在字串操作中使变址寄存器SI或DI的地址指针自动增加，字串处理由前往后。
// 虽然有16M物理地址,但是前0-1M已经被操作系统使用(0-640K已经被内核占用),内存起始地址位1M之后
unsigned long get_free_page(void)
{
register unsigned long __res asm("ax");

__asm__("std ; repne ; scasb\n\t"		// 置方向位,al(0)与对应每个页面的(di)内容比较  循环比较，找出mem_map[i]==0的页. 每比较一次ecx--
	"jne 1f\n\t"						// 没有找到mem_map[i]==0的页跳出，返回0
	"movb $1,1(%%edi)\n\t"				// 找到了,但是此时已经edi指针已经自动减少为下一次比较做准备了 1(%%edi)即回到mem_map[i]=0的位置。这里把它置为1 mem_map[i]=1
	"sall $12,%%ecx\n\t"				// 左移12位, 页面数*4K = 相对页面起始地址
	"addl %2,%%ecx\n\t"					// 再加上低端内存地址,得到页面实际物理起始地址
	"movl %%ecx,%%edx\n\t"				// 页面的实际起始地址->edx
	"movl $1024,%%ecx\n\t"				// 寄存器ecx置1024
	"leal 4092(%%edx),%%edi\n\t"		// 将4092+edx的位置->edi
	"rep ; stosl\n\t"					// stosl store EAX at address ES:(E)DI ,页面内存清零
	"movl %%edx,%%eax\n"				// 返回页面的起始地址->eax
	"1:"
	:"=a" (__res)
	:"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
	"D" (mem_map+PAGING_PAGES-1)
	:"di","cx","dx");
return __res;
}

/*
 * 释放物理地址'addr'处的一页内存。用于函数free_page_tables
 * 
 */
// 释放物理地址addr开始的1页面内存
// 物理地址1MB以下的内存空间用于内核程序和缓冲，不作为分配页面的内存空间。因此参数addr需要大于1MB
void free_page(unsigned long addr)
{
	if (addr < LOW_MEM) return;
	if (addr >= HIGH_MEMORY)
		panic("trying to free nonexistent page");
	
	// 如果对参数addr验证通过，那么就根据这个物理地址换算出从内存低端开始计起的内存
	// 页面号。页面号=(addr-LOW_MEM)/4096。可见页面号从0号开始计起。此时addr
	// 中存放着页面号。如果该页面号对应的页面映射字节不等于0，则减1返回。此时该映射
	// 字节值应该为0，表示页面已释放。如果对应页面字节原本就是0，表示该物理页面本来
	// 就是空闲的，说明内核代码出问题。于是显示出错信息并停机。
	addr -= LOW_MEM;
	addr >>= 12;
	if (mem_map[addr]--) return;
	mem_map[addr]=0;
	panic("trying to free free page");
}

/*
下面函数释放页表连续的内存块，'exit'需要该函数。与copy_page_tables()
类似，该函数仅处理4MB长度的内存块。
 */
// 根据指定的线性地址和限长(页表个数)，释放对应内存页表指定的内存块并置表项空闲。
// 页目录位于物理地址0开始处，共1024项，每项4字节，共占4K字节。每个目录项指定一
// 个页表。内核页表从物理地址0x1000处开始（紧接着目录空间），共4个页表。每个页表有
// 1024项，每项4字节。因此也占4K(1页)内存。各进程（除了在内核代码中的进程0和1）
// 的页表所占据的页面在进程被创建时由内核为其在主内存区申请得到。每个页表项对应1页
// 物理内存，因此一个页表最多可映射4MB的物理内存。
// 参数：from-起始线性基地址：size-释放的字节长度。
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;
	unsigned long * dir, nr;

	// 首先检测参数from给出的线性基地址是否在4MB(0x3fffff)的边界处。因为该函数只能处理这种情况。
	// 若from=0,则出错。说明试图释放内核和缓冲所占空间。
	if (from & 0x3fffff)
		panic("free_page_tables called with wrong alignment");
	if (!from)
		panic("Trying to free up swapper memory space");

	// 然后计算参数size给出的长度所占的页目录项数(4MB的进位整数倍)，也即所占页表数。
	// 因为1个页表可管理4MB物理内存，所以这里用右移22位的方式把需要复制的内存长度值
	// 除以4MB。其中加上0x3fffff(即4MB-1)用于得到进位整数倍结果，即除操作若有余数
	// 则进1。例如，如果原size=4.01MB,那么可得到结果size=2。
	size = (size + 0x3fffff) >> 22;
	
	// 接着计算给出的线性基地址对应的起始目录项。对应的目录项号from>>22。因为每项占4字节，并且由于
	// 页目录表从物理地址0开始存放，因此实际目录项指针=目录项号<<2，也即(from>>20)
	// 与上0xfc确保目录项指针范围有效，即用于屏蔽目录项指针最后2位。
	// 因为只移动了20位，因此最后2位是页表项索引的内容，应屏蔽掉。
	dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */  //指向页目录项

	// 此时size是释放的页表个数，即页目录项数，而dir是起始目录项指针。现在开始循环
	// 操作页目录项，依次释放每个页表中的页表项。如果当前目录项无效(P位=0)，表示该
	// 目录项没有使用（对应的页表不存在），则继续处理下一个目录项。否则从目录项中取出
	// 页表地址pg_table,并对该页表中的1024个表项进行处理，释放有效页表项(P位=l)
	// 对应的物理内存页面。然后把该页表项清零，并继续处理下一页表项。当一个页表所有
	// 表项都处理完毕就释放该页表自身占据的内存页面，并继续处理下一页目录项。最后刷新
	// 页变换高速缓冲，并返回0。
	for ( ; size-->0 ; dir++) {
		if (!(1 & *dir))
			continue;
		pg_table = (unsigned long *) (0xfffff000 & *dir);//页目录项高20位即页表项指针
		for (nr=0 ; nr<1024 ; nr++) {
			if (1 & *pg_table)
				free_page(0xfffff000 & *pg_table);
			*pg_table = 0;//该页表项置空,不再指向某一具体的物理地址
			pg_table++;	  //处理下一个页表项
		}
		free_page(0xfffff000 & *dir);//页目录项所指的页表页面释放
		*dir = 0;					 //页目录项置空,处理下一个页目录项
	}
	invalidate();//刷新高速缓存
	return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */
// 下面是内存管理mm中最为复杂的程序之一。它通过只复制内存页面
// 来拷贝一定范围内线性地址中的内容。
// 注意！ 我们并不复制任何内存块---内存块的地址需要是4MB的倍数（正好
// 一个页目录项对应的内存长度)，因为这样处理可使函数很简单。不管怎
// 样，它仅被fork使用。
// 
// 注意2! 当from=0时，说明是在为第一次fork()调用复制内核空间。
// 此时我们就不想复制整个页目录项对应的内存，因为这样做会导致内存严
// 重浪费-我们只须复制开头160个页面-对应640kB。即使是复制这些
// 页面也已经超出我们的需求，但这不会占用更多的内存一在低1Mb内存
// 范围内我们不执行写时复制操作，所以这些页面可以与内核共享。因此这
// 是nr=xxxx的特殊情况(nr在程序中指页面数)。

// 复制页目录表项和页表项。
// 复制指定线性地址和长度内存对应的页目录项和页表项，从而被复制的页目录和页表对应
// 的原物理内存页面区被两套页表映射而共享使用。复制时，需申请新页面来存放新页表，
// 原物理内存区将被共享。此后两个进程（父进程和其子进程）将共享内存区，直到有一个
// 进程执行写操作时，内核才会为写操作进程分配新的内存页（写时复制机制）
// 参数from、to是线性地址，size是需要复制（共享）的内存长度，单位是字节。
int copy_page_tables(unsigned long from,unsigned long to,long size)
{
	unsigned long * from_page_table;
	unsigned long * to_page_table;
	unsigned long this_page;
	unsigned long * from_dir, * to_dir;
	unsigned long nr;

	// 首先检测参数给出的源地址from和目的地址to的有效性。源地址和目的地址都需要在4MB
	// 内存边界地址上。否则出错死机。作这样的要求是因为一个页表的1024项可管理4MB内存。
	if ((from&0x3fffff) || (to&0x3fffff))
		panic("copy_page_tables called with wrong alignment");

	// 源地址from和目的地址to只有满足这个要求才能保证从一个页表的第1项开始复制页表
	// 项，并且新页表的最初所有项都是有效的。然后取得源地址和目的地址的起始目录项指针
	// (from_dir和to_dir)。再根据参数给出的长度size计算要复制的内存块占用的页表数
	// (即目录项数)。参见前面对114、115行的解释。
	from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	to_dir = (unsigned long *) ((to>>20) & 0xffc); 	//页目录项指针
	size = ((unsigned) (size+0x3fffff)) >> 22;		//size=页表数(目录项数)

	// 在得到了源起始目录项指针from dir和目的起始目录项指针to dir以及需要复制的页表
	// 个数size后，下面开始对每个页目录项依次申请1页内存来保存对应的页表，并且开始
	// 页表项复制操作。
	for( ; size-->0 ; from_dir++,to_dir++) {
		// 如果目的目录项指定的页表已经存在(P=1),则出错死机。
		if (1 & *to_dir)
			panic("copy_page_tables: already exist");
		//如果源目录项无效，即指定的页表不存在(P=0)，则继续循环处理下一个页目录项。
		if (!(1 & *from_dir))
			continue;

		// 在验证了当前源目录项和目的项正常之后，我们取源目录项中页表地址from_page_table。
		// 为了保存目的目录项对应的页表，需要在主内存区中申请1页空闲内存页。如果取空闲页面
		// 函数get_free_page返回0，则说明没有申请到空闲内存页面，可能是内存不够。于是返
		// 回-1值退出。
		from_page_table = (unsigned long *) (0xfffff000 & *from_dir);
		if (!(to_page_table = (unsigned long *) get_free_page()))
			return -1;	/* Out of memory, see freeing */

		// 否则我们设置目的目录项信息，把最后3位置位，即当前目的目录项“或”上7，表示对应
		// 页表映射的内存页面是用户级的，并且可读写、存在(Usr,R/W,Present)。(如果U/S
		// 位是0，则RW就没有作用。如果U/S是1，而R/W是0，那么运行在用户层的代码就只能
		// 读页面。如果U/S和R/W都置位，则就有读写的权限)。
		*to_dir = ((unsigned long) to_page_table) | 7;

		// 然后针对当前处理的页目录项对应的页表，设置需要复制的页面项数。
		// 如果是在内核空间，则仅需复制头160页对应的页表项(nr=160),对应于开始640KB物理内存。
		// 否则需要复制一个页表中的所有1024个页表项(nr=1024),可映射4MB物理内存。
		nr = (from==0)?0xA0:1024;

		// 此时对于当前页表，开始循环复制指定的nr个内存页面表项。先取出源页表项内容，如果
		// 当前源页面没有使用，则不用复制该表项，继续处理下一项。否则复位页表项中RW标志
		// (位1置0)，即让页表项对应的内存页面只读。然后将该页表项复制到目的页表中。
		for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
			this_page = *from_page_table;
			if (!(1 & this_page))
				continue;
			this_page &= ~2;//当前页表(页目录项)设置为只读 把第二位设为0
			*to_page_table = this_page;

			// 如果该页表项所指物理页面的地址在1MB以上，则需要设置内存页面映射数组mem_map[],
			// 于是计算页面号，并以它为索引在页面映射数组相应项中增加引用次数。而对于位于B
			// 以下的页面，说明是内核页面，因此不需要对mem_map进行设置。因为mem_map仅用
			// 于管理主内存区中的页面使用情况。因此对于内核移动到任务0中并且调用fork()创建
			// 任务1时,用于运行init(),由于此时复制的页面还仍然都在内核代码区域，因此以下
			// 判断中的语句不会执行，任务0的页面仍然可以随时读写。只有当调用fork的父进程
			// 代码处于主内存区（页面位置大于1MB)时才会执行。这种情况需要在进程调用execve(),
			// 并装载执行了新程序代码时才会出现。
			if (this_page > LOW_MEM) {
				// 令源页表项所指内存页也为只读。因为现在开始有两个进程共用内存区了。
				// 若其中1个进程需要进行写操作，则可以通过页异常写保护处理为执行写操作的进程分配
				// 1页新空闲页面，也即进行写时复制(copy on write)操作。
				*from_page_table = this_page;//令源页表项也只读
				this_page -= LOW_MEM;	//就是MAP_NR 根据页表起始地址算页号
				this_page >>= 12;
				mem_map[this_page]++;//共享计数
			}
		}
	}
	invalidate();  //刷新页变换高速缓冲
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
// 下面函数将一内存页面放置（映射）到指定线性地址处。它返回页面
// 的物理地址，如果内存不够（在访问页表或页面时），则返回0。
// 把一物理内存页面映射到线性地址空间指定处。
// 或者说是把线性地址空间中指定地址address处的页面映射到主内存区页面page上。主要
// 工作是在相关页目录项和页表项中设置指定页面的信息。若成功则返回物理页面地址。在
// 处理缺页异常的C函数do_no_page中会调用此函数。对于缺页引起的异常，由于任何缺
// 页缘故而对页表作修改时，并不需要刷新CPU的页变换缓冲（或称Translation Lookaside
// Buffer-TLB),即使页表项中标志P被从0修改成1。因为无效页项不会被缓冲，因此当
// 修改了一个无效的页表项时不需要刷新。在此就表现为不用调用Invalidate()函数。
// 参数page是分配的主内存区中某一页面（页帧，页框）的指针；address是线性地址。
unsigned long put_page(unsigned long page,unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!!!!!!!!     This uses the fact that _pg_dir=0 */

	// 首先判断参数给定物理内存页面page的有效性。如果该页面位置低于L0WME(1MB)或
	// 超出系统实际含有内存高端HIGH_MEMORY,则发出警告。LOW_MEM是主内存区可能有的最
	// 小起始位置。当系统物理内存小于或等于6MB时，主内存区起始于LOW_MEM处。再查看一
	// 下该page页面是否是已经申请的页面，即判断其在内存页面映射字节图mem_map中相
	// 应字节是否已经置位。若没有则需发出警告。
	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n",page,address);
	if (mem_map[(page-LOW_MEM)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);

	// 然后根据参数指定的线性地址address计算其在页目录表中对应的目录项指针，并从中取得
	// 二级页表地址。如果该目录项有效(P=1),即指定的页表在内存中，则从中取得指定页表
	// 地址放到page_table变量中。否则就申请一空闲页面给页表使用，并在对应目录项中置相
	// 应标志(7-User、U/S、R/W)。然后将该页表地址放到page_table变量中。
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);//此时page_table指向二级页表
	else {
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp|7;// 页目录项 中 填入该页表地址
		page_table = (unsigned long *) tmp;// page_table指向刚分配的页表
	}
	// 最后在找到的页表page_table中设置相关页表项内容，即把物理页面page的地址填入表
	// 项同时置位3个标志(U/S、WR、P)。该页表项在页表中的索引值等于线性地址位21-
	// 位12组成的10比特的值。每个页表共可有1024项(0-0x3ff)。
	page_table[(address>>12) & 0x3ff] = page | 7;
/* no need for invalidate */
	return page;
}


// 取消写保护页面函数。用于页异常中断过程中写保护异常的处理（写时复制）。
// 输入参数为页表项指针，是物理地址。传入传出参数 table_entry指向的内容(无人共享)或者指向(有人共享，指向新页)都可能变化
// 在内核创建进程时，新进程与父进程被设置成共享代码和数据内存页面，并且所有这些页面
// 均被设置成只读页面。而当新进程或原进程需要向内存页面写数据时，CPU就会检测到这个
// 情况并产生页面写保护异常。于是在这个函数中内核就会首先判断要写的页面是否被共享。
// 若没有则把页面设置成可写然后退出。若页面是出于共享状态，则需要重新申请一新页面并
// 复制被写页面内容，以供写进程单独使用。共享被取消。本函数供下面do_wp_page调用。
// [ un_wp_page : Un-Write Protect Page]
// 输入参数页表项指针，是物理地址
void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page,new_page;

	// 首先取参数指定的页表项中物理页面位置（地址）并判断该页面是否是共享页面。如果原
	// 页面地址大于内存低瑞LOW_MEM(表示在主内存区中)，并且其在页面映射字节图数组中
	// 值为1（表示页面仅被引用1次，页面没有被共享），则在该页面的页表项中置RW标志
	// (可写)，并刷新页变换高速缓冲，然后返回。即如果该内存页面此时只被一个进程使用，
	// 并且不是内核中的进程，就直接把属性改为可写即可，不用再重新申请一个新页面。
	old_page = 0xfffff000 & *table_entry;
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {	//非共享
		*table_entry |= 2;										//设置为可写
		invalidate();											
		return;
	}

	// 否则就需要在主内存区内申请一页空闲页面给执行写操作的进程单独使用，取消页面共享。
	// 如果原页面大于内存低端（则意味着mem_map[]>1，页面是共享的），则将原页面的页
	// 面映射字节数组值递减1。然后将指定页表项内容更新为新页面地址，并置可读写等标志
	// (U/S、R/W、P)。在刷新页变换高速缓冲之后，最后将原页面内容复削到新页面上。
	if (!(new_page=get_free_page()))
		oom();
	if (old_page >= LOW_MEM)
		mem_map[MAP_NR(old_page)]--;//共享数-1
	*table_entry = new_page | 7;	//可读写,存在
	invalidate();
	copy_page(old_page,new_page);
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 * 当用户试图往一共享页面上写时，该函数处理已存在的内存页面（写时复制）
 * 它是通过将页面复制到一个新地址上并且递减原页面的共享计数值实现的。
 * 如果它在代码空间，我们就显示段出错信总并退出。
 */
// 执行写保护页面处理。
// 是写共享页面处理函数。是页异常中断处理过程中调用的C函数。在page.s程序中被调用。
// 参数error_code是进程在写写保护页面时由CPU自动产生，address是页面线性地址。
// 写共享页面时，需复制页面（写时复制）。
void do_wp_page(unsigned long error_code,unsigned long address)
{
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */
	if (CODE_SPACE(address))
		do_exit(SIGSEGV);
#endif
	// 调用上面函数un_wp_page来处理取消页面保护。但首先需要为其准备好参数。参数是
	// 线性地址address指定页面在页表中的页表项指针，其计算方法是：
	// 1：(address>l0)&0xffc: 计算指定线性地址中页表项在页表中的偏移地址：因为
	// 根据线性地址结构，(address>12)就是页表项中的索引，但每项占4个字节，因此乘
	// 4后：(address>12)<2=(address>>l0) & 0xffc就可得到页表项在表中的偏移地址，
	// 与操作&0xffc用于限制地址范围在一个页面内。又因为只移动了10位，因此最后2位
	// 是线性地址低12位中的最高2位，也应屏被掉。
	// 因此求线性地址中页表项在页表中偏移地址直观一些的表示方法是(((address>>12)&0x3ff)<2).
	// address>>12 = 线性地址高20位(目录项索引31-22 : 页表项中的索引21-12) 
	// & 0x3ff(取21-12位) = 页表项中的索引
	// <2 即*4 获得页表项在表中的偏移地址

	// 2：(0xfffff000 & *((address>>20) & 0xffc) ):用于取目录项中页表的地址值：其中，
	// ((address>>20)&0xffc)用于取线性地址中的目录索引项在目录表中的偏移位置。因为
	// address>>22是目录项索引值，但每项4个字节，因此乘以4后：(address>>22)<<2
	// =(address>20)就是指定项在目录表中的偏移地址。&0xffc用于屏蔽目录项索引值
	// 中最后2位。因为只移动了20位，因此最后2位是页表索引的内容，应该屏蔽掉。而
	// *((address>>20)&0xffc) 则是取指定目录表项内容中 对应页表的物理地址(页目录项)。最后与上
	// 0xfffff000 用于屏蔽掉页目录项内容中的一些标志位（目录项低12位）。直观表示为
	// (0xfffff000 &((unsigned long *(((address>>22)&0x3ff)<<2))).

	// 3: 由1中页表项在页表中偏移地址 加上 2中目录表项内容中对应页表的物理地址即可
	// 得到页表项的指针（物理地址）。这里对共享的页面进行复制。
	un_wp_page((unsigned long *)
		(
			((address>>10) & 0xffc) + 	/*页表项在表中的偏移地址   */								// 1
			(0xfffff000 & *((unsigned long *) ((address>>20) &0xffc)))	/*二级页表(起始地址)  */	// a   此时相加a+1, 若取地址即 un_wp_page中的 *table_entry = *(a+1) = a[1] = 页表项
		));

}

// 写页面验证
// 参数address是指定页面的线性地址
// 在fork.c中第34行被内存验证通用函数verify_area调用.
// 如果页面不存在直接返回,要写的时候缺页中断会分配的
// 如果页面存在且无人共享，则设置可写
// 如果页面存在且和人共享，则旧页面共享数-1，分配新页面，拷贝旧页面的内容
void write_verify(unsigned long address)
{
	unsigned long page;
	// 首先取指定线性地址对应的页目录项，根据目录项中的存在位(P)判断目录项对应的页表
	// 是否存在（存在位P=1?),若不存在(P=0)则返回。这样处理是因为对于不存在的页面没
	// 有共享和写时复制可言，并且若程序对此不存在的页面执行写操作时，系统就会因为缺页异
	// 常而去执行do_no_page,并为这个地方使用put_page函数映射一个物理页面。  (page此时是页表的物理地址)
	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
		return;
	
	// 接着程序从目录项中取页表地址，加上指定页面在页表中的页表项偏移值，得对应地址的页
	// 表项指针。在该表项中包含着给定线性地址对应的物理页面。
	page &= 0xfffff000;
	page += ((address>>10) & 0xffc);		//此时page(是页表项)是指针(指向某一物理页的起始地址),后面可以通过un_wp_page,改变其指向让其指向其他(新)页的起始地址

	// 然后判断该页表项中的位1(R/W)、位0(P)标志。如果该页面不可写(R/W=0)且存在，
	// 那么就执行共享检验和复制页面操作（写时复制）。否则什么也不做，直接退出。
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *) page);	// 在这里page的指向可能就变了(传入传出)  *page = 新页的起始地址，page
	return;
}

// 取得一页空闲内存页并映射到指定线性地址处。
// get_free_page仅是申请取得了主内存区的一页物理内存。而本函数则不仅是获取到一页
// 物理内存页面，还进一步调用put_page,将物理页面映射到指定的线性地址处。
// 参数address是指定页面的线性地址.
void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	// 若不能取得一空闲页面，或者不能将所取页面放置到指定地址处，则显示内存不够的信息。
	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		free_page(tmp);		/* 0 is ok - ignored */
		oom();
	}
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable.
 * try_to_share在任务p中检查位于地址address处的页面，看页面是否存在，是否干净。
 * 如果是干净的话，就与当前任务共享。
 * 注意,这里我们已假定p!=当前任务，并且它们共享同一个执行程序。
 */
// 尝试对当前进程指定地址处的页面进行共享处理。
// 当前进程与进程p是同一执行代码，也可以认为   [当前进程是由p进程执行fork操作产生的]
// 进程，因此它们的代码内容一样。如果未对数据段内容作过修改那么数据段内容也应一样。
// 
// 参数address是进程中的逻辑地址，即是当前进程欲与p进程共享页面的逻辑页面地址，
// 进程p是将被共享页面的进程。如果p进程address处的页面存在并且没有被修改过的话，
// 就让当前进程与p进程共享之。同时还需要险证指定的地址处是否已经申请了页面，若是
// 则出错，死机。返回：1页面共享处理成功：0失败。
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	// 首先分别求得指定进程p中和当前进程中逻辑地址address对应的页目录项。为了计算方便
	// 先求出指定逻辑地址address处的'逻辑'页目录项号，即以进程空间(0~64MB)算出的页
	// 目录项号。该'逻辑'页目录项号加上进程p在CPU 4G线性空间中起始地址对应的页目录项,
	// 即得到进程p中地址address处页面所对应的4G线性空间中的实际页目录项from_page
	// 而'逻辑'页目录项号加上当前进程CPU4G线性空间中起始地址对应的页目录项，即可最后
	// 得到当前进程中地址address处页面所对应的4G线性空间中的实际页目录项to_page
	from_page = to_page = ((address>>20) & 0xffc);
	from_page += ((p->start_code>>20) & 0xffc);			//p进程目录项
	to_page += ((current->start_code>>20) & 0xffc);		//当前进程目录项     逻辑地址和线性地址就是这么转化的

	// 在得到p进程和当前进程address对应的目录项后，下面分别对进程p和当前进程进行处理。
	// 下面首先对p进程的表项进行操作。目标是取得p进程中address对应的物理内存页面地址，
	// 并且该物理页面存在，而且干净（没有被修改过，不脏）。
	/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;	// 取目录项内容。如果该目录项无效(P=0),表示目录项对应的二级页表不存在，于是返回
	if (!(from & 1))
		return 0;

	from &= 0xfffff000;						 	// 否则取该目录项对应页表地址from,从而计算出逻辑地址address对应的页表项指针，并取出该页表项内容临时保存在phys_addr中。
	from_page = from + ((address>>10) & 0xffc); // 页表地址+页表项偏移量
	phys_addr = *(unsigned long *) from_page;	// 页表项(放着页对应的物理地址)

	/* is the page clean and present? */ //物理页面存在且干净吗
	// 接着看看页表项映射的物理页面是否存在并且干净。0x41对应页表项中的D(Dirty)和
	// P(Present)标志。如果页面不干净或无效则返回。然后我们从该表项中取出物理页面地址
	// 再保存在phys_addr中。最后我们再检查一下这个物理页面地址的有效性，即它不应该超过
	// 机器最大物理地址值，也不应该小于内存低端(1MB)。
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	phys_addr &= 0xfffff000;
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;

	// 下面首先对当前进程的表项进行操作。目标是取得当前进程中address对应的页表项地址，
	// 并且该页表项还没有映射物理页面，即其P=0。
	// 首先取当前进程页目录项内容to。如果该目录项无效(P=0),即目录项对应的二级页表
	// 不存在，则申请一空闲页面来存放页表，并更新目录项to_page内容，让其指向该内存页面。
	to = *(unsigned long *) to_page;
	if (!(to & 1))
		if (to = get_free_page())
			*(unsigned long *) to_page = to | 7;
		else
			oom();
	
	// 否则取目录项中的页表地址-->to，加上页表项索值<<2，即页表项在表中偏移地址，得到
	// 页表项地址to_page。针对该页表项，如果此时我们检查出其对应的物理页面已经存在，
	// 即页表项的存在位P=1, 则说明原本我们想共享进程p中对应的物理页面，但现在我们自己
	// 已经占有了（映射有）物理页面。于是说明内核出错，死机。
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)		// 自己已经有了就不能再和别人共享了
		panic("try_to_share: to_page already exists");
	
	// 在找到了进程p中逻辑地址address处对应的干净且存在的物理页面，而且也确定了当前
	// 进程中逻辑地址ddress所对应的二级页表项地址之后，我们现在对他们进行共享处理。
	// 方法很简单，就是首先对p进程的页表项进行修改，设置其写保护(R/=0,只读)标志，
	// 然后让当前进程复制p进程的这个页表项。此时当前进程逻辑地址address处页面即被
	// 映射到p进程逻辑地址address处页面映射的物理页面上。
	/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;//只读(写保护)
	*(unsigned long *) to_page = *(unsigned long *) from_page;//共享
	invalidate();
	// 随后刷新页变换高速缓冲。计算所操作物理页面的页面号，并将对应页面映射字节数组项中
	// 的引用递增1。最后返回1，表示共享处理成功。
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	mem_map[phys_addr]++;//从LOW_MEM之后的页作为起始页
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 * share_page试图找到一个进程，它可以与当前进程共享页面。参数address是当前进程数据空间中期望共享的某页面地址。
 * 首先我们通过检测executable->i_count来查证是否可行。如果有其他任务已共享该inode,则它应该大于1。
 */

// 共享页面处理。
// 在发生缺页异常时，首先看看能否与运行同一个执行文件的其他进程进行页面共享处理。
// 该函数首先判断系统中是否有另一个进程也在运行当前进程一样的执行文件。若有，则在
// 系统当前所有任务中寻找这样的任务。若找到了这样的任务就尝试与其共享指定地址处的
// 页面。若系统中没有其他任务正在运行与当前进程相同的执行文件，那么共享页面操作的
// 前提条件不存在，因此函数立刻退出。判断系统中是否有另一个进程也在执行同一个执行
// 文件的方法是利用进程任务数据结构中的executable字段。该字段指向进程正在执行程
// 序在内存中的i节点。根据该i节点的引用次数icou我们可以进行这种判断。若
// executable->i_count值大于1，则表明系统中可能有两个进程在运行同一个执行文件，
// 于是可以再对任务结构数组中所有任务比较是否有相同的executable字段来最后确定多
// 个进程运行着相同执行文件的情况。
// 参数address是进程中的逻辑地址，即是当前进程欲与p进程共享页面的逻辑页面地址。
// 返回1共享操作成功，0失败
static int share_page(unsigned long address)
{
	struct task_struct ** p;

	// 首先检查一下当前进程的executable字段是否指向某执行文件的i节点，以判断本进程
	// 是否有对应的执行文件。如果没有，则返回0。如果executable的确指向某个i节点，
	// 则检查该i节点引用计数值。如果当前进程运行的执行文件的内存i节点引用计数等于
	// 1(executable->i_count=1),表示当前系统中只有1个进程（即当前进程）在运行该
	// 执行文件。因此无共享可言，直接退出函数。
	if (!current->executable)
		return 0;
	if (current->executable->i_count < 2)
		return 0;
	
	// 否则搜索任务数组中所有任务。寻找与当前进程可共享页面的进程，即运行相同执行文件
	// 的另一个进程，并尝试对指定地址的页面进行共享。如果找到某个进程p其executable
	// 字段值与当前进程的相同，则调用try_to_share尝试页面共享。若共享操作成功，则
	// 函数返回1。否则返回0，表示共享页面操作失收。
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)									
			continue;
		if (current == *p)
			continue;
		if ((*p)->executable != current->executable)
			continue;
		if (try_to_share(address,*p))
			return 1;	// 共享成功页面
	}
	return 0;
}


// 执行缺页处理。
// 是访问不存在页面处理函数。页异常中断处理过程中调用的函数。在page.s程序中被调用。
// 函数参数error_code和address是进程在访问页面时由CPU因缺页产生异常而自动生成。
// 该函数首先尝试与已加载的相同文件进行页面共亭，或者只是由于进程动态申请内存页面而
// 只需映射一页物理内存页即可。若共亭操作不成功，那么只能从相应文件中读入所缺的数据
// 页面到指定线性地址处。
// error_code指出出错类型，：address是产生异常的页面线性地址
void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;

	// 首先取线性空间中指定地址address处页面地址。从而可算出指定线性地址在进程空间中
	// 相对于进程基址的偏移长度值p,即对应的逻辑地址。
	address &= 0xfffff000;					//address处缺页页面线性地址
	tmp = address - current->start_code;	//两个线性地址相减得到缺页页面对应的逻辑地址  即start_code记录的也是线性地址   逻辑地址和线性地址就是这么转化的

	// 若当前进程的executable节点指针空，或者指定地址超出（代码+数据）长度，则申请
	// 一页物理内存，并映射到指定的线性地址处。executable是进程正在运行的执行文件的i
	// 节点结构。由于任务0和任务1的代码在内核中，因此任务0、任务1以及任务1派生的
	// 没有调用过execve的所有任务的executable都为0。若该值为0，或者参数指定的线性
	// 地址超出代码加数据长度，则表明进程在申请新的内存页面存放堆或栈中数据。因此直接
	// 调用取空闲页面函数get_empty_page为进程申请一页物理内存并映射到指定线性地址
	// 处。进程任务结构字段start_code是线性地址空间中进程代码段地址，字段end_data
	// 是代码加数据长度。对于Linux0.11内核，它的代码段和数据段起始基址相同。
	if (!current->executable || tmp >= current->end_data) { 	//end_code记录的是逻辑地址
		get_empty_page(address);
		return;
	}

	// 否则说明所缺页面在进程执行文件范围内，于是就尝试共享页面操作，若成功则退出，
	// 若不成功就只能申请一页物理内存页面page,然后从设备上读取执行文件中的相应页面并
	// 放置（映射）到进程页面逻辑地址tmp处
	if (share_page(tmp))		// 申请一页物理内存
		return;
	if (!(page = get_free_page()))
		oom();
	/* remember that 1 block is used for header */
	// 记住，（程序）头要使用1个数据块*/
	// 因为块设备上存放的执行文件映像第1块数据是程序头结构，因此在读取该文件时需要跳过
	// 第1块数据。所以需要首先计算缺页所在的数据块号。因为每块数据长度为BLOCK_SIZE=
	// 1KB,因此一页内存可存放4个数据块。进程逻辑地址tmp即除以数据块大小再加1即可得出
	// 缺少的页面在执行映像文件中的起始块号block。根据这个块号和执行文件的i节点，我们
	// 就可以从映射位图中找到对应块设备中对应的设备逻辑块号（保存在nr[]数组中）。利用
	// bread_page即可把这4个逻辑块读入到物理页面page中。
	block = 1 + tmp/BLOCK_SIZE;							// 执行文件中起始数据块号  (加载代码段, 据此可知, 跑多个可执行程序实际用的是一个代码段(只读所以疯狂共享))
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(current->executable,block);		// 设备上对应的逻辑块号
	bread_page(page,current->executable->i_dev,nr);		// 读设备上4个逻辑块
	// 在读设备逻辑块操作时，可能会出现这样一种情况，即在执行文件中的读取页面位置可能离
	// 文件尾不到1个页面的长度。因此就可能读入一些无用的信息。下面的操作就是把这部分超
	// 出执行文件end data以后的部分清零处理。
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
	// 最后把引起缺页异常的一页物理页面映射到指定线性地址address处。若操作成功就返回。
	// 否则就释放内存页，显示内存不够。
	if (put_page(page,address))		//页映射到对应的线性地址,内部操作就是与或对应的标志位，高效
		return;
	free_page(page);
	oom();
}

// 物理内存管理初始化。
// 该函数对1MB以上内存区域以页面为单位进行管理前的初始化设置工作。一个页面长度为
// 4KB字节。该函数把1MB以上所有物理内存划分成一个个页面，并使用一个页面映射字节
// 数组mem_map来管理所有这些页面。对于具有16MB内存容量的机器，该数组共有3840
// 项((16MB·1MB)/4KB),即可管理3840个物理页面。每当一个物理内存页面被占用时就
// 把mem_map[]中对应的的字节值增1：若释放一个物理页面，就把对应字节值减1。若字
// 节值为0，则表示对应页面空闲：若字节值大于或等于1，则表示对应页面被占用或被不
// 同程序共享占用。
// 在该版本的Liux内核中，最多能管理16MB的物理内存，大于16MB的内存将弃置不用。
// 对于具有16MB内存的PC机系统，在没有设置虚拟盘RAMDISK的情况下start_mem通常
// 是4MB,end_mem是16MB。因此此时主内存区范围是4MB一l6MB,共有3072个物理页面可
// 供分配。而范围0-1MB内存空间用于内核系统（其实内核只使用0一640KB,利下的部
// 分被部分高速缓冲和设备内存占用)。
// 参数start_mem是可用作页面分配的主内存区起始地址（已去除RAMDISK所占内存空间）·
// end_mem是实际物理内存最大地址。而地址范围start_men到end_mem是主内存区。
void mem_init(long start_mem, long end_mem)
{
	int i;

	// 首先将1MB到16MB范围内所有内存页面对应的内存映射字节数组项置为已占用状态，即各
	// 项字节值全部设置成USED(100)。PAGING_PAGES被定义为(PAGING MEMORY>>12),即1MB
	// 以上所有物理内存分页后的内存页面数(15MB/4KB=3840)。
	HIGH_MEMORY = end_mem;
	for (i=0 ; i<PAGING_PAGES ; i++)
		mem_map[i] = USED;
	// 然后计算主内存区起始内存start_mem处页面对应内存映射字节数组中项号i和主内存区
	// 页面数。此时mem_map[]数组的第i项正对应主内存区中第1个页面。最后将主内存区中
	// 页面对应的数组项清零（表示空闲）。对于具有16B物理内存的系统，mem map[们中对应
	// 4b-16Mb主内存区的项被清零。
	i = MAP_NR(start_mem);			// 主内存区起始位置处页面号
	end_mem -= start_mem;
	end_mem >>= 12;					// 主内存区中的总页面数
	while (end_mem-->0)
		mem_map[i++]=0;				// 主内存区页面对应字节值清零
}


// 计算内存空闲页面数并显示
void calc_mem(void)
{
	int i,j,k,free=0;
	long * pg_tbl;

	// 扫描内存页面映射数组mem_map[]，获取空闲页面数并显示。然后扫描所有页目录项(除0，
	// 1项)，如果页目录项有效，则统计对应页表中有效页面数，并显示。页目录项0一3被内核
	// 使用，因此应该从第5个目录项(i=4)开始扫描。
	for(i=0 ; i<PAGING_PAGES ; i++)
		if (!mem_map[i]) free++;					
	printk("%d pages free (of %d)\n\r",free,PAGING_PAGES);	// 空闲的物理页面数量
	for(i=2 ; i<1024 ; i++) {
		if (1&pg_dir[i]) {
			pg_tbl=(long *) (0xfffff000 & pg_dir[i]);
			for(j=k=0 ; j<1024 ; j++)
				if (pg_tbl[j]&1)
					k++;
			printk("Pg-dir[%d] uses %d pages\n",i,k);// 页表i对应的有效页表项k (注意不同k可以映射到同一个物理页)
		}
	}
}
