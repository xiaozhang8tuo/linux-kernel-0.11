/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h> 		// 内核配置头文件。定义键盘语言和硬盘类型(HD_TYPE)可选项。
#include <linux/sched.h>		// 调度程序头文件，定义了任务结构task_struct、任务0的数据，还有一些有关描述符参数设置和获取的嵌入式汇编函数宏语句。
#include <linux/kernel.h>		// 内核头文件。含有一些内核常用函数的原形定义。
#include <asm/system.h>			// 系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏。

#include <errno.h>				// 错误号头文件。包含系统中各种出错号。
#include <sys/stat.h>			// 文件状态头文件。含有文件或文件系统状态结构stat和常量。

// 对指定设备执行高速缓冲与设备上数据的同步操作(fs/buffer.c)
int sync_dev(int dev);
// 等待击建(kernel/chr_tty/tty_io.c)
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
// 测试指定位偏移处比特位的值，并返回该原比特位值（应该取名为test_bit()更妥帖）。
// 嵌入式汇编宏。参数bitnr是比特位偏移值，addr是测试比特位操作的起始地址。
// %0-ax(res)  %1-0   %2 -bitnr   %3 addr
// 定义了一个局部寄存器变量。该变量将被保存在eax寄存器中，以便于高效访问和
// 操作。第24行上指令bt用于对比特位进行测试(Bit Test)。它会把地址addr(%3)和
// 比特位偏移量bitnr(%2)指定的比特位的值放入进位标志CF中。指令setb用于根据进
// 位标志CF设置操作数%al。如果CF=1则%al=1,否则%al=0。
#define set_bit(bitnr,addr) ({ \
register int __res __asm__("ax"); \
__asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
__res; })

struct super_block super_block[NR_SUPER];	// 超级块结构表数组(NR_SUPER=8)
/* this is initialized in init/main.c */
// ROOT_DEV在init/main.c 中初始化
int ROOT_DEV = 0;							// 根文件系统设备号

// 以下3个函数lock_super、free_super和wait_on_super的作用与inode.c文
// 件中头3个函数的作用雷同，只是这里操作的对象换成了超级块。
// 锁定超级块。
// 如果超级块已被锁定，则将当前任务置为不可中断的等待状态，并添加到该超级块等待队
// 列s_wait中。直到该超级块解锁并明确地唤醒本任务。然后对其上锁。
static void lock_super(struct super_block * sb)
{
	cli();										// 关中断
	while (sb->s_lock)							// 如果该超级块已经上锁,则睡眠等待
		sleep_on(&(sb->s_wait));				// kernel/sched.c
	sb->s_lock = 1;								// 给该超级块加锁
	sti();										// 开中断
}

// 对指定超级块解锁。
// 复位超级块的锁定标志，并明确地唤醒等待在此超级块等待队列s_wait上的所有进程。
// 如果使用ulock_super这个名称则可能更妥帖。
static void free_super(struct super_block * sb)
{
	cli();
	sb->s_lock = 0;								// 解锁
	wake_up(&(sb->s_wait));						// 唤醒等待的
	sti();
}

// 睡眠等待超级块解锁。
// 如果超级块已被锁定，则将当前任务置为不可中断的等待状态，并添加到该超级块的等待队
// 列s_wait中。直到该超级块解锁并明确地唤醒本任务。	//和lock_super的区别在于等到后是否上锁
static void wait_on_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sti();
}

// 取指定设备的超级块，
// 在超级块表（数组）中搜索指定设备dev的超级块结构信息。若找到则返回超级块的指针，
// 否则返回空指针。
struct super_block * get_super(int dev)
{
	struct super_block * s;			// s是超级块数据结构指针

	// 首先判断参数给出设备的有效性。若设备号为0则返回空指针。然后让s指向超级块数组
	// 起始处，开始搜索整个超级块数组，以寻找指定设备dev的超级块。
	if (!dev)
		return NULL;
	s = 0+super_block;
	while (s < NR_SUPER+super_block)
		// 如果当前搜索项是指定设备的超级块，即该超级块的设备号字段值与函数参数指定的相同，
		// 则先等待该超级块解锁（若已被其他进程上锁的话）。在等待期间，该超级块项有可能被
		// 其他设备使用，因此等待返回之后需再判断一次是否是指定设备的超级块，如果是则返回
		// 该超级块的指针。否则就重新对超级块数组再搜索一遍，因此此时s需重又指向超级块数
		// 组开始处。
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)	//double check
				return s;
			s = 0+super_block;
		} else
			s++;
	return NULL;
}

// 释放（放回）指定设备的超级块。
// 释放设备所使用的超级块数组项（置sdev=0),并释放该设备i节点位图和逻辑块位图所
// 占用的高速缓冲块。如果超级块对应的文件系统是根文件系统，或者其某个i节点上已经安
// 装有其他的文件系统，则不能释放该超级块。
void put_super(int dev)
{
	struct super_block * sb;
	struct m_inode * inode;
	int i;

	// 首先判断参数的有效性和合法性。如果指定设备是根文件系统设备，则显示警告信息“根系
	// 统盘改变了，准备生死决战吧”，并返回。然后在超级块表中寻找指定设备号的文件系统超
	// 级块。如果找不到指定设备的超级块，则返回。另外，如果该超级块指明该文件系统所安装
	// 到的i节点还没有被处理过，则显示警告信息并返回。在文件系统卸载(umount)操作中，
	// s_imount会先被置成Nul1以后才会调用本函数，参见第192行。
	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
	if (!(sb = get_super(dev)))
		return;
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}

	// 然后在找到指定设备的超级块之后，我们先锁定该超级块，再置该超级块对应的设备号字段
	// s_dev为0，也即释放该设备上的文件系统超级块。然后释放该超级块占用的其他内核资源，
	// 即释放该设备上文件系统i节点位图和逻辑块位图在缓冲区中所占用的缓冲块。下面常数符
	// 号I_MAP_SLOTS和Z_MAP_SLOTS均等于8，用于分别指明i节点位图和逻辑块位图占用的磁
	// 盘逻辑块数。注意，若这些缓冲块内容被修改过，则需要作同步操作才能把缓冲块中的数据
	// 写入设备中。函数最后对该超级块解锁，并返回。
	lock_super(sb);
	sb->s_dev = 0;
	for(i=0;i<I_MAP_SLOTS;i++)
		brelse(sb->s_imap[i]);
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
	free_super(sb);
	return;
}

// 读取指定设备的超级块。
// 如果指定设备dev上的文件系统超级块已经在超级块表中，则直接返回该超级块项的指针。
// 否则就从设备dev上读取超级块到缓冲块中，并复制到超级块表中。并返回超级块指针。
static struct super_block * read_super(int dev)
{
	struct super_block * s;
	struct buffer_head * bh;
	int i,block;

	// 首先判断参数的有效性。如果没有指明设备，则返回空指针。然后检查该设备是否可更换
	// 过盘片（也即是否是软盘设备）。如果更换过盘，则高速缓冲区有关该设备的所有缓冲块
	// 均失效，需要进行失效处理，即释放原来加载的文件系统。
	if (!dev)
		return NULL;
	check_disk_change(dev);

	// 如果该设备的超级块已经在超级块表中，则直接返回该超级块的指针。否则，首先在超级
	// 块数组中找出一个空项（也即字段s_dev=0的项）。如果数组已经占满则返回空指针。
	if (s = get_super(dev))
		return s;
	for (s = 0+super_block ;; s++) {
		if (s >= NR_SUPER+super_block)
			return NULL;
		if (!s->s_dev)
			break;
	}

	// 找到空项后，就对该超级块项用于指定设备dev上的文件系统。于是对该超级块结构中的内存字段进行部分初始化处理
	s->s_dev = dev;
	s->s_isup = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_rd_only = 0;
	s->s_dirt = 0;

	// 然后锁定该超级块，并从设备上读取超级块信息到bh指向的缓冲块中。超级块位于块设备
	// 的第2个逻辑块(1号块)中，（第1个是引导盘块）。如果读超级块操作失败，则释放上
	// 面选定的超级块数组中的项（即置s_dev=0),并解锁该项，返回空指针退出。
	lock_super(s);
	if (!(bh = bread(dev,1))) {						// 读1号磁盘块
		s->s_dev=0;
		free_super(s);
		return NULL;
	}

	// 否则就将设备上读取的超级块信息从缓冲块数据区复制到超级块数组相应项结构中。并释放存放读取信息的高速缓冲块。
	*((struct d_super_block *) s) =
		*((struct d_super_block *) bh->b_data);
	brelse(bh);

	// 现在我们从设备dev上得到了文件系统的超级块，于是开始检查这个超级块的有效性并从设
	// 备上读取i节点位图和逻辑块位图等信息。如果所读取的超级块的文件系统魔数字段不对，
	// 说明设备上不是正确的文件系统，因此同上面一样，释放上面选定的超级块数组中的项，并
	// 解锁该项，返回空指针退出。对于该版Linux内核，只支特MINIX文件系统1.0版本，其魔数是0x137f.
	if (s->s_magic != SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}

	// 下面开始读取设备上i节点位图和逻辑块位图数据。首先初始化内存超级块结构中位图空间
	// 然后从设备上读取i节点位图和逻辑块位图信息，并存放在超级块对应字段中。i节点位图
	// 保存在设备上2号块开始的逻辑块中，共占用s_imap_blocks个块。逻辑块位图在i节点位图所在块的后续块中，共占用s_zmap_blocks个块。
	for (i=0;i<I_MAP_SLOTS;i++)
		s->s_imap[i] = NULL;
	for (i=0;i<Z_MAP_SLOTS;i++)
		s->s_zmap[i] = NULL;
	block=2;
	for (i=0 ; i < s->s_imap_blocks ; i++)
		if (s->s_imap[i]=bread(dev,block))		// 读取设备中的i节点位图
			block++;
		else
			break;
	for (i=0 ; i < s->s_zmap_blocks ; i++)		// 读取设备中的逻辑块位图
		if (s->s_zmap[i]=bread(dev,block))
			block++;
		else
			break;

	// 如果读出的位图块数不等于位图应该占有的逻辑块数，说明文件系统位图信息有问题，超级
	// 块初始化失败。因此只能释放前面申请并占用的所有资源，即释放节点位图和逻辑块位图
	// 占用的高速缓冲块、释放上面选定的超级块数组项、解锁该超级块项，并返回空指针退出。
	if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
		for(i=0;i<I_MAP_SLOTS;i++)
			brelse(s->s_imap[i]);
		for(i=0;i<Z_MAP_SLOTS;i++)
			brelse(s->s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		return NULL;
	}

	// 否则一切成功。另外，由于对于申请空闲i节点的函数来讲，如果设备上所有的i节点已经
	// 全被使用，则查找函数会返回0值。因此0号i节点是不能用的，所以这里将位图中第1块
	// 的最低比特位设置为1，以防止文件系统分配0号i节点。同样的道理，也将逻辑块位图的
	// 最低位设置为1。最后函数解锁该超级块，并返回超级块指针。
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
	free_super(s);
	return s;
}

// 卸载文件系统（系统调用）
// 参数dev_name是文件系统所在设备的设备文件名。
// 该函数首先根据参数给出的块设备文件名获得设备号，然后复位文件系统超级块中的相应字
// 段，释放超级块和位图占用的缓冲块，最后对该设备执行高速缓冲与设备上数据的同步操作。
// 若卸载操作成功则返回0，否则返回出错码。
int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;

	// 首先根据设备文件名找到对应的节点，并取其中的设备号。设备文件所定义设备的设备号
	// 是保存在其i节点的i_zone[0]中的。参见后面namei.c程序中系统调用sys_mknod()的代
	// 码。另外，由于文件系统需要存放在块设备上，因此如果不是块设备文件，则放回
	// 刚申请的i节点dev_i,返回出错码.
	if (!(inode=namei(dev_name)))
		return -ENOENT;
	dev = inode->i_zone[0];
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);
		return -ENOTBLK;
	}

	// OK,现在上面为了得到设备号而取得的i节点已完成了它的使命，因此这里放回该设备文件
	// 的i节点。接着我们来检查一下卸载该文件系统的条件是否满足。如果设备上是根文件系统，
	// 则不能被卸截，返回忙出错号。
	iput(inode);
	if (dev==ROOT_DEV)
		return -EBUSY;
	// 如果在超级块表中没有找到该设备上文件系统的超级块，或者已找到但是该设备上文件系统
	// 没有安装过，则返回出错码。如果超级块所指明的被安装到的i节点并没有置位其安装标志
	// i_mout,则显示警告信息。然后查找一下i节点表，看看是否有进程在使用该设备上的文
	// 件，如果有则返回忙出错码。
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)	//已经不再使用该设备上的文件
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;
	
	// 现在该设备上文件系统的卸载条件均得到满足，因此我们可以开始实施卸截操作了。
	// 首先复位被安装到的i节点的安装标志，释放该ⅰ节点。然后置超级块中被安装i节点字段
	// 为空，并放回设备文件系统的根i节点，接着置超级块中被安装系统根i节点指针为空。
	sb->s_imount->i_mount=0;
	iput(sb->s_imount);
	sb->s_imount = NULL;
	iput(sb->s_isup);
	sb->s_isup = NULL;
	// 最后我们释放该设备上的超级块以及位图占用的高速缓冲块，并对该设备执行高速缓冲与设
	// 备上数据的同步操作。然后返回0（卸截成功）。
	put_super(dev);
	sync_dev(dev);
	return 0;
}

// 安装文件系统（系统调用）。
// 参数dev_name是设备文件名，dir_name是安装到的目录名，rw_flag被安装文件系统的可
// 读写标志。将被加载的地方必须是一个目录名，并且对应的节点没有被其他程序占用。
// 若操作成功则返回0，否则返回出错号。mount -o ro /dev/hda1 /mnt
int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;

	// 首先根据设备文件名找到对应的i节点，以取得其中的设备号。对于块特殊设备文件，设备
	// 号在其i节点的i_zone[0]中。另外，由于文件系统必须在块设备中，因此如果不是块设备
	// 文件，则放回刚取得的i节点dev_i,返回出错码。
	if (!(dev_i=namei(dev_name)))
		return -ENOENT;
	dev = dev_i->i_zone[0];
	if (!S_ISBLK(dev_i->i_mode)) {
		iput(dev_i);
		return -EPERM;
	}

	// OK,现在上面为了得到设备号而取得的i节点dev_i已完成了它的使命，因此这里放回该设
	// 备文件的i节点。接着我们来检查一下文件系统安装到的目录名是否有效。于是根据给定的
	// 目录文件名找到对应的i节点dir_i。如果该节点的引用计数不为1（仅在这里引用），
	// 或者该i节点的节点号是根文件系统的节点号1，则放回该i节点返回出错码。另外，如果
	// 该节点不是一个目录文件节点，则也放回该1节点，返回出错码。因为文件系统只能安装在
	// 一个目录名上。
	iput(dev_i);
	if (!(dir_i=namei(dir_name)))
		return -ENOENT;
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -EPERM;
	}

	// 现在安装点也检查完毕，我们开始读取要安装文件系统的超级块信息。如果读超级块操作失
	// 败，则放回该安装点i节点dir_i并返回出错码。一个文件系统的超级块会首先从超级块表
	// 中进行搜索，如果不在超级块表中就从设备上读取。
	if (!(sb=read_super(dev))) {
		iput(dir_i);
		return -EBUSY;
	}
	// 在得到了文件系统超级块之后，我们对它先进行检测一番。如果将要被安装的文件系统已经
	// 安装在其他地方，则放回该i节点，返回出错码。如果将要安装到的i节点已经安装了文件
	// 系统（安装标志已经置位），则放回该i节点，也返回出错码。
	if (sb->s_imount) {
		iput(dir_i);
		return -EBUSY;
	}
	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}
	
	// 最后设置被安装文件系统超级块的“被安装到i节点”字段指向安装到的目录名的i节点。
	// 并设置安装位置i节点的安装标志和节点已修改标志。然后返回0（安装成功）。
	sb->s_imount=dir_i;
	dir_i->i_mount=1;
	dir_i->i_dirt=1;		/* NOTE! we don't iput(dir_i) */
	return 0;			/* we do that in umount */
}


// 安装根文件系统
// 该函数属于系统初始化操作的一部分。函数首先初始化文件表数组file_table和超级块表super_block
// (数组)，然后读取根文件系统超级块，并取得文件系统根i节点。最后统计并显示出根文
// 件系统上的可用资源(空闲块数和空闲i节点数)。该函数会在系统开机进行初始化设置时sys_setup被调用blk_drv/hd.c
void mount_root(void)
{
	int i,free;
	struct super_block * p;
	struct m_inode * mi;

	// 若磁盘i节点结构不是32字节，则出错停机。该判断用于防止修改代码时出现不一致情况
	if (32 != sizeof (struct d_inode))
		panic("bad i-node size");
	// 首先初始化文件表数组（共64项，即系统同时只能打开64个文件）和超级块表。这里将所
	// 有文件结构中的引用计数设置为0（表示空闲），并把超级块表中各项结构的设备字段初始
	// 化为0（也表示空闲）。如果根文件系统所在设备是软盘的话，就提示“插入根文件系统盘，
	// 并按回车键”，并等待按键。
	for(i=0;i<NR_FILE;i++)
		file_table[i].f_count=0;
	if (MAJOR(ROOT_DEV) == 2) {
		printk("Insert root floppy and press ENTER");
		wait_for_keypress();
	}
	for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
		p->s_dev = 0;									// 初始化超级块表
		p->s_lock = 0;
		p->s_wait = NULL;
	}

	// 做好以上“份外”的初始化工作之后，我们开始安装根文件系统。于是从根设备上读取文件
	// 系统超级块，并取得文件系统的根i节点(1号节点)在内存i节点表中的指针。如果读根
	// 设备上超级块失败或取根节点失败，则都显示信息并停机。
	if (!(p=read_super(ROOT_DEV)))
		panic("Unable to mount root");
	if (!(mi=iget(ROOT_DEV, ROOT_INO)))
		panic("Unable to read root i-node");

	// 现在我们对超级块和根i节点进行设置。把根i节点引用次数递增3次。因为下面415行上
	// 也引用了该i节点。另外，iget()函数中i节点引用计数已被设置为1。然后置该超级块的
	// 被安装文件系统i节点和被安装到i节点字段为该i节点。再设置当前进程的当前工作目录
	// 和根目录i节点。此时当前进程是1号进程(init进程)。
	mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1   因为iget()函数中i节点引用计数已被设置为1,所以设置成4,只要+3*/
	p->s_isup = p->s_imount = mi;	// 2
	current->pwd = mi;				// 1	共4
	current->root = mi;				// 1

	// 然后我们对根文件系统上的资源作统计工作。统计该设备上空闲块数和空闲i节点数。首先
	// 令i等于超级块中表明的设备逻辑块总数。然后根据逻辑块位图中相应比特位的占用情况统
	// 计出空闲块数。这里宏函数set_bit只是在测试比特位，而非设置比特位。"i&8191”用于
	// 取得i节点号在当前位图块中对应的比特位偏移值。”>13”是将i除以8192，也即除一个
	// 磁盘块包含的比特位数。
	free=0;
	i=p->s_nzones;
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_zmap[i>>13]->b_data))			// i>>13得到逻辑块位图的indx, i&8191是对应逻辑块位图的第几位
			free++;
	// 在显示过设备上空闲逻辑块数/逻辑块总数之后。我们再统计设备上空闲i节点数。首先令
	// 等于超级块中表明的设备上i节点总数+1。加1是将0节点也统计进去。然后根据i节点位
	// 图中相应比特位的占用情况计算出空闲1节点数。最后再显示设备上可用空闲i节点数和
	// 节点总数。
	printk("%d/%d free blocks\n\r",free,p->s_nzones);
	free=0;
	i=p->s_ninodes+1;
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
			free++;
	printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}
