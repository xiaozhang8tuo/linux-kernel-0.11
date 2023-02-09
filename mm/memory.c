/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
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
	"movl $1024,%%ecx\n\t"				// 寄存器ecx置10224
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
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 */
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;
	unsigned long * dir, nr;

	if (from & 0x3fffff)
		panic("free_page_tables called with wrong alignment");
	if (!from)
		panic("Trying to free up swapper memory space");
	size = (size + 0x3fffff) >> 22;
	dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	for ( ; size-->0 ; dir++) {
		if (!(1 & *dir))
			continue;
		pg_table = (unsigned long *) (0xfffff000 & *dir);
		for (nr=0 ; nr<1024 ; nr++) {
			if (1 & *pg_table)
				free_page(0xfffff000 & *pg_table);
			*pg_table = 0;
			pg_table++;
		}
		free_page(0xfffff000 & *dir);
		*dir = 0;
	}
	invalidate();
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
	for( ; size-->0 ; from_dir++,to_dir++) {
		if (1 & *to_dir)
			panic("copy_page_tables: already exist");
		if (!(1 & *from_dir))
			continue;
		from_page_table = (unsigned long *) (0xfffff000 & *from_dir);
		if (!(to_page_table = (unsigned long *) get_free_page()))
			return -1;	/* Out of memory, see freeing */
		*to_dir = ((unsigned long) to_page_table) | 7;
		nr = (from==0)?0xA0:1024;
		for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
			this_page = *from_page_table;
			if (!(1 & this_page))
				continue;
			this_page &= ~2;
			*to_page_table = this_page;
			if (this_page > LOW_MEM) {
				*from_page_table = this_page;
				this_page -= LOW_MEM;
				this_page >>= 12;
				mem_map[this_page]++;
			}
		}
	}
	invalidate();
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
unsigned long put_page(unsigned long page,unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n",page,address);
	if (mem_map[(page-LOW_MEM)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp|7;
		page_table = (unsigned long *) tmp;
	}
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
// 复制被写页面内容，以供写进程单独使用。共享被取消。本函数供下面do_wp_page()调用。
// [ un_wp_page : Un-Write Protect Page]
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
 */
void do_wp_page(unsigned long error_code,unsigned long address)
{
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */
	if (CODE_SPACE(address))
		do_exit(SIGSEGV);
#endif
	// 调用上面函数un_wp_page()来处理取消页面保护。但首先需要为其准备好参数。参数是
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
		(((address>>10) & 0xffc) + (0xfffff000 &
		*((unsigned long *) ((address>>20) &0xffc)))));

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
	// 常而去执行do_no_page,并为这个地方使用put_page()函数映射一个物理页面。
	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
		return;
	
	// 接着程序从目录项中取页表地址，加上指定页面在页表中的页表项偏移值，得对应地址的页
	// 表项指针。在该表项中包含着给定线性地址对应的物理页面。
	page &= 0xfffff000;
	page += ((address>>10) & 0xffc);

	// 然后判断该页表项中的位1(R/W)、位0(P)标志。如果该页面不可写(R/W=0)且存在，
	// 那么就执行共享检验和复制页面操作（写时复制）。否则什么也不做，直接退出。
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *) page);
	return;
}

void get_empty_page(unsigned long address)
{
	unsigned long tmp;

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
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	from_page = to_page = ((address>>20) & 0xffc);
	from_page += ((p->start_code>>20) & 0xffc);
	to_page += ((current->start_code>>20) & 0xffc);
/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;
	from_page = from + ((address>>10) & 0xffc);
	phys_addr = *(unsigned long *) from_page;
/* is the page clean and present? */
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	phys_addr &= 0xfffff000;
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;
	to = *(unsigned long *) to_page;
	if (!(to & 1))
		if (to = get_free_page())
			*(unsigned long *) to_page = to | 7;
		else
			oom();
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	invalidate();
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	mem_map[phys_addr]++;
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
static int share_page(unsigned long address)
{
	struct task_struct ** p;

	if (!current->executable)
		return 0;
	if (current->executable->i_count < 2)
		return 0;
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (current == *p)
			continue;
		if ((*p)->executable != current->executable)
			continue;
		if (try_to_share(address,*p))
			return 1;
	}
	return 0;
}

void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;

	address &= 0xfffff000;
	tmp = address - current->start_code;
	if (!current->executable || tmp >= current->end_data) {
		get_empty_page(address);
		return;
	}
	if (share_page(tmp))
		return;
	if (!(page = get_free_page()))
		oom();
/* remember that 1 block is used for header */
	block = 1 + tmp/BLOCK_SIZE;
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(current->executable,block);
	bread_page(page,current->executable->i_dev,nr);
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
	if (put_page(page,address))
		return;
	free_page(page);
	oom();
}

void mem_init(long start_mem, long end_mem)
{
	int i;

	HIGH_MEMORY = end_mem;
	for (i=0 ; i<PAGING_PAGES ; i++)
		mem_map[i] = USED;
	i = MAP_NR(start_mem);
	end_mem -= start_mem;
	end_mem >>= 12;
	while (end_mem-->0)
		mem_map[i++]=0;
}

void calc_mem(void)
{
	int i,j,k,free=0;
	long * pg_tbl;

	for(i=0 ; i<PAGING_PAGES ; i++)
		if (!mem_map[i]) free++;
	printk("%d pages free (of %d)\n\r",free,PAGING_PAGES);
	for(i=2 ; i<1024 ; i++) {
		if (1&pg_dir[i]) {
			pg_tbl=(long *) (0xfffff000 & pg_dir[i]);
			for(j=k=0 ; j<1024 ; j++)
				if (pg_tbl[j]&1)
					k++;
			printk("Pg-dir[%d] uses %d pages\n",i,k);
		}
	}
}
