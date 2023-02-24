/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This handles all read/write requests to block devices
 */
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include "blk.h"

/*
 * The request-struct contains all necessary data	请求结构中含有加载nr个扇区数据到内存中去的所有必须的信息
 * to load a nr of sectors into memory
 */
// 请求项数组队列,共有NR_REQUEST = 32个请求项
struct request request[NR_REQUEST];

/*
 * used to wait on when there are no free requests
 */
// 是用于在请求数组没有空闲项时进程的临时等待处
struct task_struct * wait_for_request = NULL;

/* blk_dev_struct is:
 *	do_request-address
 *	next-request
 */
// 块设备数组。该数组使用主设备号作为索引。实际内容将在各块设备驱动程序初始化时填入。
// 例如,硬盘驱动程序初始化时(hd.c,343行)，第一条语句即用于设置blk_dev[3]的内容。
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
	{ NULL, NULL },		/* no_dev */
	{ NULL, NULL },		/* dev mem */
	{ NULL, NULL },		/* dev fd */
	{ NULL, NULL },		/* dev hd */
	{ NULL, NULL },		/* dev ttyx */
	{ NULL, NULL },		/* dev tty */
	{ NULL, NULL }		/* dev lp */
};


// 锁定指定缓冲块。
// 如果指定的缓冲块已经被其他任务锁定，则使自己睡眠（不可中断地等待），直到被执行解
// 锁缓冲块的任务明确地唤醒。
static inline void lock_buffer(struct buffer_head * bh)
{
	cli();						// 清中断许可
	while (bh->b_lock)			// 如果缓冲区已经被锁定则睡眠,直到缓冲区解锁
		sleep_on(&bh->b_wait);	
	bh->b_lock=1;				// 立即锁定该缓冲区
	sti();						// 开中断
}

// 释放（解锁）锁定的缓冲区。
// 该函数与blk.h文件中的同名函数完全一样。
static inline void unlock_buffer(struct buffer_head * bh)
{
	if (!bh->b_lock)
		printk("ll_rw_block.c: buffer not locked\n\r");
	bh->b_lock = 0;
	wake_up(&bh->b_wait);
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.
 * add-request向链表中加入一项请求项。它会关闭中断，这样就能安全地处理请求链表了
 */
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
	struct request * tmp;

	// 首先再进一步对参数提供的请求项的指针和标志作初始设置。置空请求项中的下一请求项指
	// 针，关中断并清除请求项相关缓冲区脏标志。
	req->next = NULL;
	cli();
	if (req->bh)
		req->bh->b_dirt = 0;	//清缓冲区脏标志
	
	// 然后查看指定设备是否有当前请求项，即查看设备是否正忙。如果指定设备dev当前请求项
	// (current_request)子段为空，则表示目前该设备没有请求项，本次是第1个请求项，也是
	// 唯一的一个。因此可将块设备当前请求指针直接指向该请求项，并立刻执行相应设备的请求
	// 函数。
	if (!(tmp = dev->current_request)) {
		dev->current_request = req;
		sti();					//开中断
		(dev->request_fn)();	//执行请求函数,对于硬盘是do_hd_request
		return;
	}

	// 如果目前该设备已经有当前请求项在处理，则首先利用电梯算法搜索最佳插入位置，然后将
	// 当前请求插入到请求链表中。最后开中断并退出函数。电梯算法的作用是让磁盘磁头的移动
	// 距离最小，从而改善（减少）硬盘访问时间。
	// 下面for循环中if语句用于把req所指请求项与请求队列（链表）中已有的请求项作比较，
	// 找出req插入该队列的正确位置顶序。然后中断循环，并把req插入到该队列正确位置处。
	for ( ; tmp->next ; tmp=tmp->next)
		if ((IN_ORDER(tmp,req) ||
		    !IN_ORDER(tmp,tmp->next)) &&
		    IN_ORDER(req,tmp->next))
			break;
	req->next=tmp->next;
	tmp->next=req;
	sti();
}

// 创建请求项并插入请求队列中
// 参数major是主设备号, rw是指定命令, bh是存放数据的缓冲区头指针。
static void make_request(int major,int rw, struct buffer_head * bh)
{
	struct request * req;
	int rw_ahead;

	/* WRITEA/READA is special case - it is not really needed, so if the */
	/* buffer is locked, we just forget about it, else it's a normal read */
	// WRITEA/READA是一种特珠情况---它们并非必要，所以如果缓冲区已经上锁/
	// 我们就不用管它，否则的话它只是一个一般的读操作。
	// 这里'READ'和'WRITE'后面的'A'字符代表英文单词Ahead,表示提前预读/写数据块的意思。
	// 该函数首先对命令READA/WRITEA的情况进行一些处理。对于这两个命令，当指定的缓冲区
	// 正在使用而已被上锁时，就放弃预读/写请求。否则就作为普通的READ/WRITE命令进行操作。
	// 另外，如果参数给出的命令既不是READ也不是WRITE,则表示内核程序有错，显示出错信息
	// 并停机。注意，在修改命令之前这里已为参数是否是预读/写命令设置了标志rw_ahead
	if (rw_ahead = (rw == READA || rw == WRITEA)) {
		if (bh->b_lock)
			return;
		if (rw == READA)
			rw = READ;
		else
			rw = WRITE;
	}
	if (rw!=READ && rw!=WRITE)
		panic("Bad block dev command, must be R/W/RA/WA");
	
	// 对命令rw进行了一番处理之后，现在只有READ或WRITE两种命令。在开始生成和添加相
	// 应读/写数据请求项之前，我们再来看看此次是否有必要添加请求项。在两种情况下可以不
	// 必添加请求项。一是当命令是写(WRITE),但缓冲区中的数据在读入之后并没有被修改过：
	// 二是当命令是读(READ)，但缓冲区中的数据已经是更新过的，即与块设备上的完全一样。
	// 因此这里首先锁定缓冲区对其检查一下。如果此时缓冲区已被上锁，则当前任务就会睡眠，
	// 直到被明确地唤醒。如果确实是属于上述两种情况，那么就可以直接解锁缓冲区，并返回。
	// 这几行代码体现了高速缓冲区的用意，在数据可靠的情况下就无须再执行硬盘操作，而直接
	// 使用内存中的现有数据。缓冲块的b_dirt标志用于表示缓冲块中的数据是否已经被修改过。
	// b_uptodate标志用于表示缓冲块中的数据是与块设备上的同步，即在从块设备上读入缓冲块
	// 后没有修改过。
	lock_buffer(bh);
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
		unlock_buffer(bh);
		return;
	}
repeat:
/* we don't allow the write-requests to fill up the queue completely:
 * we want some room for reads: they take precedence. The last third
 * of the requests are only for reads.
 * 不能让队列中全都是写请求项：我们需要为读请求保留一些空间：读操作
 * 是优先的。请求队列的后三分之一空间仅用于读请求项。
 */

	// 好，现在我们必须为本函数生成并添加读/写请求项了。首先我们需要在请求数组中寻找到
	// 一个空闲项（糟）来存放新请求项。搜索过程从请求数组末端开始。根据上述要求，对于读
	// 命令请求，我们直接从队列求尾开始搜索，而对于写请求就只能从队列2/3处向以列头处搜
	// 索空项填入。于是我们开始从后向前搜索，当请求结构request的设备字段dev值=-1时
	// 表示该项未被占用（空闲）。如果没有一项是空闲的（此时请求项数组指针已经搜索越过头
	// 部)，则查看此次请求是否是提前读/写(REDA或WRITEA),如果是则放弃此次请求操作，
	// 否则让本次请求操作先睡眠（以等待请求队列腾出空项），过一会再来搜索请求队列。
	if (rw == READ)
		req = request+NR_REQUEST;			//对于读请求,将指针指向队列尾部
	else
		req = request+((NR_REQUEST*2)/3);	//对于写请求,指针指向队列2/3处
	
	/* find an empty request */
	while (--req >= request)
		if (req->dev<0)
			break;
	/* if none found, sleep on new requests: check for rw_ahead */
	if (req < request) {
		if (rw_ahead) {				//预读预写直接返回,不等了
			unlock_buffer(bh);
			return;
		}
		sleep_on(&wait_for_request);
		goto repeat;
	}
	/* fill up the request-info, and add it to the queue */
	// 向空闲请求项中填写请求信息，并将其加入队列中
	// OK,程序执行到这里表示已找到一个空闲请求项。于是我们在设置好的新请求项后就调用
	// add_request把它添加到请求队列中，立马退出。请求结构请参见blk_drv/blk.h,23行。
	// req->sector是读写操作的起始扇区号，req->buffer是请求项存放数据的缓冲区。

	// 拼协议, :-)
	req->dev = bh->b_dev;				// 设备号
	req->cmd = rw;						// 命令(READ/WRITE)
	req->errors=0;						// 操作时产生的错误次数
	req->sector = bh->b_blocknr<<1;		// 起始扇区, 块号转换成扇区号(1块=2扇区)
	req->nr_sectors = 2;				// 本请求项需要读写的扇区数
	req->buffer = bh->b_data;			// 请求项缓冲区指针指向需要读写的数据缓冲区
	req->waiting = NULL;				// 任务等待操作执行完成的地方
	req->bh = bh;						// 缓冲块头指针
	req->next = NULL;					// 指向下一请求项
	add_request(major+blk_dev,req);		// 
}

// 低层读写数据块函数(Low Level Read Write Block).
// 该函数是块设备驱动程序与系统其他部分的接口函数。通常在fs/buffer.c程序中被调用。
// 主要功能是创建块设备读写请求项并插入到指定块设备请求队列中。实际的读写操作则是
// 由设备的request_fn函数完成。对于硬盘操作，该函数是do_hd_request,对于软盘
// 操作，该函数是do_fd_request,对于虚拟盘则是do_d_request。另外，在调用该函
// 数之前，调用者需要首先把读/写块设备的信息保存在缓冲块头结构中，如设备号、块号。
// 参数：rw-READ、READA、WRITE或WRITEA是命令：bh-数据缓冲块头指针。
void ll_rw_block(int rw, struct buffer_head * bh)
{
	unsigned int major;// 主设备号(对于硬盘是3)

	// 如果设备主设备号不存在或者该设备的请求操作函数不存在,则显示出错信息，并返回。
	// 否则创建请求项并插入请求队列
	if ((major=MAJOR(bh->b_dev)) >= NR_BLK_DEV ||
	!(blk_dev[major].request_fn)) {
		printk("Trying to read nonexistent block-device\n\r");
		return;
	}
	make_request(major,rw,bh);
}

// 块设备初始化函数，由初始化程序main.c调用(init/main.c)。
// 初始化请求数组，将所有请求项置为空闲项(dev=-1)。有32项(RRE0UEST=32)。
void blk_dev_init(void)
{
	int i;

	for (i=0 ; i<NR_REQUEST ; i++) {
		request[i].dev = -1;
		request[i].next = NULL;
	}
}
