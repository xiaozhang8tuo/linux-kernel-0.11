/*
 *  linux/fs/bitmap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */
#include <string.h>					// 字符串头文件。主要定义了一些有关字符串操作的嵌入函数。这里主要使用了其中的memset函数。
#include <linux/sched.h>			// 调度程序头文件，定义任务结构task_struct、任务0数据。
#include <linux/kernel.h>			// 内核头文件。含有一些内核常用函数的原形定义。

// 将指定地址(addr)处的一块1024字节内存清零。
// 输入：eax=0:ecx=以长字为单位的数据块长度(BLOCK_SIZE/4):edi=指定起始地址addr。
#define clear_block(addr) \
__asm__("cld\n\t" \
	"rep\n\t" \
	"stosl" \
	::"a" (0),"c" (BLOCK_SIZE/4),"D" ((long) (addr)):"cx","di")


// 把指定地址开始的第nr个位偏移处的比特位置位(nr可大于32！)。返回原比特位值。
// 输入：%0-eax(返回值)：%l-eax(O):%2-nr,位偏移值：%3-(addr),addr的内容。
// 定义了一个局部寄存器变量res。该变量将被保存在指定的eax寄存器中，以便于
// 高效访问和操作。这种定义变量的方法主要用于内嵌汇编程序中。详细说明参见gcc手册
// “在指定寄存器中的变量”。整个宏是一个语句表达式（即圆括号括住的组合语句），其
// 值是组合语句中最后一条表达式语句res(第23行)的值，
// 指令btsl用于测试并设置比特位(Bit Test and Set)。把基地址(%3)和
// 比特位偏移值(%2)所指定的比特位值先保存到进位标志CF中，然后设置该比特位为1。
// 指令setb用于返回原比特位,根据进位标志CF设置操作数(%al)。如果CF=1则%a1=1,否则%al=0。
#define set_bit(nr,addr) ({\
register int res __asm__("ax"); \
__asm__ __volatile__("btsl %2,%3\n\tsetb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

// 复位指定地址开始的第nr位偏移处的比特位。返回原比特位值的反码。
// 输入：%0-eax(返回值)：%l-eax(0):%2-nr,位偏移值：%3-(addr),addr的内容。
// 指令btrl用于测试并复位比特位(Bit Test and Reset)。其作用与上面的
// btsl类似，但是复位指定比特位。
// 指令setnb用于根据进位标志CF设置操作数(%al)。如果CF=1则%a1=0,否则%a1=1。
#define clear_bit(nr,addr) ({\
register int res __asm__("ax"); \
__asm__ __volatile__("btrl %2,%3\n\tsetnb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

// 从addr开始寻找第1个0值比特位。
// 输入：%0-ecx(返回值)：%l-ecx(0):%2-esi(addr)。
// 在addr指定地址开始的位图中寻找第1个是0的比特位，并将其距离addr的比特位偏移
// 值返回。addr是缓冲块数据区的地址，扫描寻找的范围是1024字节(8192比特位)。
#define find_first_zero(addr) ({ \
int __res; \
__asm__("cld\n" \					// 清方向位
	"1:\tlodsl\n\t" \				// 取[esi]->eax
	"notl %%eax\n\t" \				// eax中每位取反
	"bsfl %%eax,%%edx\n\t" \		// 从位0开始扫描eax中是1的第一个位,其偏移值->EDX
	"je 2f\n\t" \					// 如果eax中全是0，前跳转到2:
	"addl %%edx,%%ecx\n\t" \		// 偏移值加入ecx(ecx是位图首个0值位的偏移值)。
	"jmp 3f\n" \					// 向前跳转到标号3处（结束）。
	"2:\taddl $32,%%ecx\n\t" \		// 未找到0值位,ecx加一个长字(32)的偏移量32 
						\			// cmpl jl : 如果ecx 小于 8192 就跳转
	"cmpl $8192,%%ecx\n\t" \		// 已经扫描完1块数据(8192比特)了吗   ! cmpl %edx(second source operand)，%eax(first source operand)是看eax-edx结果!   SF：符号标志寄存器，当计算结果为负数时会被设为1。 https://www.cnblogs.com/zuoxiaolong/p/computer18.html
	"jl 1b\n" \						// 扫描未结束，后跳到1:				jl:Jump near if less (SF ≠ OF). OF不会溢出为0，即SF=1跳转，即ecx-8192 < 0 跳转
	"3:" \
	:"=c" (__res):"c" (0),"S" (addr):"ax","dx","si"); \
__res;})

// 释放设备dev上数据区中的逻辑块block.
// 复位指定逻辑块block对应的逻辑块位图比特位。
// 参数：dev是设备号，block是逻辑块号（盘块号）。
void free_block(int dev, int block)
{
	struct super_block * sb;
	struct buffer_head * bh;

	// 首先取设备dev上文件系统的超级块信息，根据其中数据区开始逻辑块号和文件系统中逻辑
	// 块总数信息判断参数block的有效性。如果指定设备超级块不存在，则出错停机。若逻辑块
	// 号小于盘上数据区第1个逻辑块的块号或者大于设备上总逻辑块数，也出错停机。
	if (!(sb = get_super(dev)))
		panic("trying to free block on nonexistent device");
	if (block < sb->s_firstdatazone || block >= sb->s_nzones)
		panic("trying to free block not in datazone");
	
	// 然后从hash表中寻找该块数据。若找到了则判断其有效性，并清已修改和更新标志，释放
	// 该数据块。该段代码的主要用途是如果该逻辑块目前存在于高速缓冲区中，就释放对应的缓冲块。
	// 注意下面这段程序有问题，会造成数据块不能释放。因为当b_count>1时，
	// 这段代码会仅打印一段信息而没有执行释放操作。
	bh = get_hash_table(dev,block);
	if (bh) {
		if (bh->b_count != 1) {
			printk("trying to free block (%04x:%d), count=%d\n",
				dev,block,bh->b_count);
			return;
		}
		bh->b_dirt=0;		//复位脏标志位
		bh->b_uptodate=0;	//复位更新标志位
		brelse(bh);
	}

	// 接着我们复位block在逻辑块位图中的比特位（置0）。先计算block在数据区开始算起的
	// 数据逻辑块号（从1开始计数）。然后对逻辑块（区块）位图进行操作，复位对应的比特位。
	// 如果对应比特位原来就是0，则出错停机。由于1个缓冲块有1024字节，即8192比特位，
	// block/8192 可计算出指定块block在逻辑位图中的哪个块上。
	// block&8191 可以得到block在逻辑块位图当前块中的比特偏移位置。
	block -= sb->s_firstdatazone - 1 ;
	if (clear_bit(block&8191,sb->s_zmap[block/8192]->b_data)) {
		printk("block (%04x:%d) ",dev,block+sb->s_firstdatazone-1);
		panic("free_block: bit already cleared");
	}

	// 最后置相应逻辑块位图所在缓冲区已修改标志。
	sb->s_zmap[block/8192]->b_dirt = 1;
}

// 向设备申请一个逻辑块（盘块，区块）。
// 函数首先取得设备的超级块，并在超级块中的逻辑块位图中寻找第一个0值比特位（代表
// 一个空闲逻辑块)。然后置位对应逻辑块在逻辑块位图中的比特位。接着为该逻辑块在缓
// 冲区中取得一块对应缓冲块。最后将该缓冲块清零，并设置其已更新标志和已修改标志。
// 并返回逻辑块号。函数执行成功则返回逻辑块号（盘块号），否则返回0。
int new_block(int dev)
{
	struct buffer_head * bh;
	struct super_block * sb;
	int i,j;

	// 首先获取设备dev的超级块。如果指定设备的超级块不存在，则出错停机。然后扫描文件
	// 系统的8块逻辑块位图，寻找首个0值比特位，以寻找空闲逻辑块，获取放置该逻辑块的
	// 块号。如果全部扫描完8块逻辑块位图的所有比特位(i>=8或j>=8192)还没找到
	// 0值比特位或者位图所在的缓冲块指针无效(bh=NULL)则返回0退出（没有空闲逻辑块）。
	if (!(sb = get_super(dev)))
		panic("trying to get new block from nonexistant device");
	j = 8192;
	for (i=0 ; i<8 ; i++)
		if (bh=sb->s_zmap[i])
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
	if (i>=8 || !bh || j>=8192)
		return 0;
	
	// 接着设置找到的新逻辑块j对应逻辑块位图中的比特位。若对应比特位已经置位，则出错
	// 停机。否则置存放位图的对应的区块已修改标志。因为逻辑块位图仅表示盘上数据区中
	// 逻辑块的占用情况，即逻辑块位图中比特位偏移值表示从数据区开始处算起的块号，因此
	// 这里需要加上数据区第1个逻辑块的块号，把j转换成逻辑块号。此时如果新逻辑块大于
	// 该设备上的总逻辑块数，则说明指定逻辑块在对应设备上不存在。申请失败，返回0退出。
	if (set_bit(j,bh->b_data))
		panic("new_block: bit already set");
	bh->b_dirt = 1;
	j += i*8192 + sb->s_firstdatazone-1;//     				|8192||8192||8192|j|.....|
	if (j >= sb->s_nzones)				//    firstdatazone ↑                        ↑ s_nzones
		return 0;

	// 然后在高速缓冲区中为该设备上指定的逻辑块号取得一个缓冲块，并返回缓冲块头指针
	// 因为刚取得的逻辑块其引用次数一定为1(getblk中会设置)，因此若不为1则停机
	if (!(bh=getblk(dev,j)))
		panic("new_block: cannot get block");
	if (bh->b_count != 1)
		panic("new block: count is != 1");
	
	// 最后将新逻辑块清零，并设置其已更新标志和已修改标志。然后释放对应缓冲块，返回逻辑辑块号
	clear_block(bh->b_data);
	bh->b_uptodate = 1;		// 数据是最新,刚分配是还调用了set_bit修改了位图
	bh->b_dirt = 1;			// 数据确实修改过,是一块新的blk,需要回写
	brelse(bh);
	return j;
}


// 释放指定的i节点。
// 该函数首先判断参数给出的i节点号的有效性和可释放性。若i节点仍然在使用中则不能
// 被释放。然后利用超级块信息对i节点位图进行操作，复位i节点号对应的i节点位图中
// 比特位，并清空i节点结构。
void free_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;

	// 首先判断参数给出的需要释放的i节点有效性或合法性。如果i节点指针NULL,则退出。
	// 如果i节点上的设备号字段为0，说明该节点没有使用。于是用0清空对应i节点所占内存
	// 区并返回。memset定义在include/string.h第395行开始处。这里表示用0填写inode
	// 指针指定处、长度是sizeof(*inode)的内存块。
	if (!inode)
		return;
	if (!inode->i_dev) {
		memset(inode,0,sizeof(*inode));
		return;
	}

	// 如果此i节点还有其他程序引用，则不能释放，说明内核有问题，停机。如果文件连接数
	// 不为0，则表示还有其他文件目录项在使用该节点，因此也不应释放，而应该放回等。
	if (inode->i_count>1) {
		printk("trying to free inode with count=%d\n",inode->i_count);
		panic("free_inode");
	}
	if (inode->i_nlinks)
		panic("trying to free inode with links");

	// 在判断完i节点的合理性之后，我们开始利用其超级块信息对其中的i节点位图进行操作。
	// 首先取i节点所在设备的超级块，测试设备是否存在。然后判断i节点号的范围是否正确
	// 如果i节点号等于0或大于该设备上i节点总数，则出错(0号i节点保留没有使用)
	// 如果该i节点对应的节点位图不存在，则出错。因为一个缓冲块的节点位图有8192比
	// 特位。因此inum>>13(即inum/8192)可以得到当前i节点号所在的s_imap[]项，即所在盘块。
	if (!(sb = get_super(inode->i_dev)))
		panic("trying to free inode on nonexistent device");
	if (inode->i_num < 1 || inode->i_num > sb->s_ninodes)
		panic("trying to free inode 0 or nonexistant inode");
	if (!(bh=sb->s_imap[inode->i_num>>13]))
		panic("nonexistent imap in superblock");
	// 现在我们复位i节点对应的节点位图中的比特位。如果该比特位已经等于0，则显示出错
	// 警告信息。最后置i节点位图所在缓冲区已修改标志，并清空该i节点结构所占内存区。
	if (clear_bit(inode->i_num&8191,bh->b_data))
		printk("free_inode: bit already cleared.\n\r");
	bh->b_dirt = 1;
	memset(inode,0,sizeof(*inode));
}

// 为设备dev建立一个新i节点。初始化并返回该新i节点的指针。
// 在内存i节点表中获取一个空闲i节点表项，并从i节点位图中找一个空闲i节点。
struct m_inode * new_inode(int dev)
{
	struct m_inode * inode;
	struct super_block * sb;
	struct buffer_head * bh;
	int i,j;

	// 首先从内存i节点表(inode table)中获取一个空闲i节点项，并读取指定设备的超级块
	// 结构。然后扫描超级块中8块i节点位图，寻找首个0比特位，寻找空闲节点，获取放置
	// 该i节点的节点号。如果全部扫描完还没找到，或者位图所在的缓冲块无效(bh=NULL),
	// 则放回先前申请的i节点表中的i节点，并返回空指针退出（没有空闲i节点）。
	if (!(inode=get_empty_inode()))
		return NULL;
	if (!(sb = get_super(dev)))
		panic("new_inode with unknown device");
	j = 8192;
	for (i=0 ; i<8 ; i++)
		if (bh=sb->s_imap[i])
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
	if (!bh || j >= 8192 || j+i*8192 > sb->s_ninodes) {
		iput(inode);	// 放回先前申请的i节点
		return NULL;
	}

	// 现在我们已经找到了还未使用的i节点号j。于是置位i节点j对应的i节点位图相应比
	// 特位（如果已经置位，则出错）。然后置i节点位图所在缓冲块已修改标志。最后初始化
	// 该i节点结构(i ctime是i节点内容改变时间)。
	if (set_bit(j,bh->b_data))
		panic("new_inode: bit already set");
	bh->b_dirt = 1;
	inode->i_count=1;						// 引用计数
	inode->i_nlinks=1;						// 文件目录项连接数
	inode->i_dev=dev;						// i节点所在的设备号
	inode->i_uid=current->euid;				// i节点所属的用户id
	inode->i_gid=current->egid;				// 组id
	inode->i_dirt=1;						// 已修改标志置位
	inode->i_num = j + i*8192;				// 对应设备中的i节点号
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;//设置时间
	return inode;							// 返回i节点指针
}
