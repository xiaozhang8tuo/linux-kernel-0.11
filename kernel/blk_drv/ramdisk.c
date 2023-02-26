/*
 *  linux/kernel/blk_drv/ramdisk.c
 *
 *  Written by Theodore Ts'o, 12/2/91
 */

#include <string.h>

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/memory.h>		// 内存拷贝头文件。含有memcpy嵌入式汇编宏函数。

// 定义RAM盘主设备号符号常数。在驱动程序中主设备号必须在包含blk.h文件之前被定义。
// 因为blk.h文件中要用到这个符号常数值来确定一些列的其他常数符号和宏。
#define MAJOR_NR 1
#include "blk.h"

// 虚拟盘在内存中的起始位置。该位置会在第52行上初始化函数rd_init中确定。参见内核
// 初始化程序init/main.c,第124行。'rd'是'ramdisk'的缩写。
char	*rd_start;		//虚拟盘在内存中的开始地址
int	rd_length = 0;		//虚拟盘所占内存大小(字节)

// 虚拟盘当前请求项操作函数。
// 该函数的程序结构与硬盘的do_hd_request函数类似。在低级块设备
// 接口函数ll_rw_block建立起虚拟盘(rd)的请求项并添加到rd的链表中之后，就会调
// 用该函数对rd当前请求项进行处理。该函数首先计算当前请求项中指定的起始扇区对应虚
// 拟盘所处内存的起始位置addr和要求的扇区数对应的字节长度值len,然后根据请求项中
// 的命令进行操作。若是写命令WRITE,就把请求项所指缓冲区中的数据直接复制到内存位置
// addr处。若是读操作则反之。数据复制完成后即可直接调用end_request对本次请求项
// 作结束处理。然后跳转到函数开始处再去处理下一个请求项。若已没有请求项则退出。
void do_rd_request(void)
{
	int	len;
	char	*addr;

	INIT_REQUEST;
	// 首先检测请求项的合法性，若已没有请求项则退出（参见blk.h）。然后计算请
	// 求项处理的虚拟盘中起始扇区在物理内存中对应的地址addr和占用的内存字节长度值len,
	// 下句用于取得请求项中的起始扇区对应的内存起始位置和内存长度。其中sector<<9表示
	// sector*5l2,换算成字节值。CURRENT被定义为(blk_dev[MAJOR_NR].current_request)。
	addr = rd_start + (CURRENT->sector << 9);
	len = CURRENT->nr_sectors << 9;
	// 如果当前请求项中子设备号不为1或者对应内存起始位置大于虚拟盘末尾，则结束该请求项
	// 并跳转到repeat处去处理下一个虚拟盘请求项。标号repeat定义在宏
	// INIT REQUEST内，位于宏的开始处，参见blk.h
	if ((MINOR(CURRENT->dev) != 1) || (addr+len > rd_start+rd_length)) {
		end_request(0);
		goto repeat;
	}
	// 然后进行实际的读写操作。如果是写命令(WRITE),则将请求项中缓冲区的内容复制到地址
	// addr处，长度为len字节。如果是读命令(READ),则将addr开始的内存内容复制到请求项
	// 缓冲区中，长度为len字节。否则显示命令不存在，死机。
	if (CURRENT-> cmd == WRITE) {
		(void ) memcpy(addr,
			      CURRENT->buffer,
			      len);
	} else if (CURRENT->cmd == READ) {
		(void) memcpy(CURRENT->buffer, 
			      addr,
			      len);
	} else
		panic("unknown ramdisk-command");
	end_request(1);
	goto repeat;
}

/*
 * Returns amount of memory which needs to be reserved.
 */
// 返回内存虚拟盘ramdisk所需的内存量
// 虚拟盘初始化函数。
// 该函数首先设置虚拟盘设备的请求项处理函数指针指向do_rd_request,然后确定虚拟盘
// 在物理内存中的起始地址、占用字节长度值。并对整个虚拟盘区清零。最后返回盘区长度。
// 当linux/Makefile文件中设置过RAMDISK值不为零时，表示系统中会创建RAM虚拟盘设备，
// 在这种情况下的内核初始化过程中，本函数就会被调用(init/main.c)。该函数
// 的第2个参数length会被赋值成RAMDISK*1024,单位为字节。
long rd_init(long mem_start, int length)
{
	int	i;
	char	*cp;

	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	rd_start = (char *) mem_start;	//对于16MB系统该值为4MB
	rd_length = length;				//虚拟盘长度值
	cp = rd_start;
	for (i=0; i < length; i++)		//盘区清零
		*cp++ = '\0';
	return(length);
}

/*
 * If the root device is the ram disk, try to load it.
 * In order to do this, the root device is originally set to the
 * floppy, and we later change it to be ram disk.
 * 如果根文件系统设备(root device)是ramdisk的话，则尝试加载它。
 * root device原先是指向软盘的，我们将它改成指向ramdisk.
 */
// 尝试把根文件系统加载到虚拟盘中。
// 该函数将在内核设置函数setup(hd.c)中被调用。另外，1磁盘块=1024字节。
// 第109行上的变量block=256表示根文件系统映像文件被存储于boot盘第256磁盘块开始处。
void rd_load(void)
{
	struct buffer_head *bh;		// 高速缓冲区头指针
	struct super_block	s;		// 文件超级块结构
	int		block = 256;	/* Start at block 256 */	// 开始于256盘块
	int		i = 1;
	int		nblocks;			// 文件系统盘块总数
	char		*cp;		/* Move pointer */
	
	if (!rd_length)
		return;
	printk("Ram disk: %d bytes, starting at 0x%x\n", rd_length,
		(int) rd_start);
	if (MAJOR(ROOT_DEV) != 2)
		return;
	
	// 然后读根文件系统的基本参数。即读软盘块256+1、256和256+2。这里block+1是指磁盘上
	// 的超级块。breada用于读取指定的数据块，并标出还需要读的块，然后返回含有数据块的
	// 缓冲区指针。如果返回NULL,则表示数据块不可读(fs/bufer.c)。然后把缓冲区中
	// 的磁盘超级块(d_super_block是磁盘超级块结构)复制到s变量中，并释放缓冲区。接着
	// 我们开始对超级块的有效性进行判断。如果超级块中文件系统魔数不对，则说明加载的数据
	// 块不是MINIX文件系统，于是退出。有关MINIX超级块的结构请参见文件系统一章内容 ------ 软盘块中可以写入期望的ramdisk设置,占用内存总长度,块数
	// 虚拟盘可以从软盘中加载一些只读程序用来提高运行速度，好比ls,shell等系统指令
	bh = breada(ROOT_DEV,block+1,block,block+2,-1);
	if (!bh) {
		printk("Disk error while looking for ramdisk!\n");
		return;
	}
	*((struct d_super_block *) &s) = *((struct d_super_block *) bh->b_data);
	brelse(bh);
	if (s.s_magic != SUPER_MAGIC)
		/* No ram disk image present, assume normal floppy boot */	//磁盘中没有ramdisk映像文件，退出去执行通常的软盘引导
		return;
	// 然后我们试图把整个根文件系统读入到内存虚拟盘区中。对于一个文件系统来说，其超级块
	// 结构的s_nzones字段中保存着总逻辑块数（或称为区段数）。一个逻辑块中含有的数据块
	// 数则由字段s_log_zone_size指定。因此文件系统中的数据块总数nblocks就等于（逻辑块
	// 数*2^（每区段块数的次方）)，即nblocks=(s_nzones*2^s_1og_zone_size)。如果遇到
	// 文件系统中数据块总数大于内存虚拟盘所能容纳的块数的情况，则不能执行加载操作，而只
	// 能显示出错信息并返回。
	nblocks = s.s_nzones << s.s_log_zone_size;
	if (nblocks > (rd_length >> BLOCK_SIZE_BITS)) {
		printk("Ram disk image too big!  (%d blocks, %d avail)\n", 
			nblocks, rd_length >> BLOCK_SIZE_BITS);
		return;
	}
	// 否则若虚拟盘能容纳得下文件系统总数据块数，则我们显示加载数据块信息，并让cp指向
	// 内存虚拟盘起始处，然后开始执行循环操作将磁盘上根文件系统映像文件加载到虚拟盘上。
	// 在操作过程中，如果一次需要加载的盘块数大于2块，我们就是用超前顶读函数breada,
	// 否则就使用bread函数进行单块读取。若在读盘过程中出现I/O操作错误，就只能放弃加
	// 载过程返回。所读取的磁盘块会使用memcpy函数从高速缓冲区中复制到内存虚拟盘相应
	// 位置处，同时显示已加载的块数。显示字符串中的八进制数'\010'表示显示一个制表符。
	printk("Loading %d bytes into ram disk... 0000k", 
		nblocks << BLOCK_SIZE_BITS);
	cp = rd_start;
	while (nblocks) {
		if (nblocks > 2) 
			bh = breada(ROOT_DEV, block, block+1, block+2, -1);
		else
			bh = bread(ROOT_DEV, block);
		if (!bh) {
			printk("I/O error on block %d, aborting load\n", 
				block);
			return;
		}
		(void) memcpy(cp, bh->b_data, BLOCK_SIZE);	//复制到cp处
		brelse(bh);
		printk("\010\010\010\010\010%4dk",i);		//打印加载块计数值
		cp += BLOCK_SIZE;							//虚拟盘指针前移
		block++;
		nblocks--;
		i++;
	}
	// 当boot盘中从256盘块开始的整个根文件系统加载完毕后，我们显示“done”,并把目前
	// 根文件设备号修改成虚拟盘的设备号0x0101,最后返回。
	printk("\010\010\010\010\010done \n");
	ROOT_DEV=0x0101;
}
