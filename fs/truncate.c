/*
 *  linux/fs/truncate.c
 *	用来释放指定i节点在设备上占用的所有逻辑块，包括直接块，一次间接块和二次间接块。
 *  从而将文件的节点对应的文件的长度截为0,并释放占用的设备空间。
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h> // 调度程序头文件，定义了任务结构task_struct、任务0数据等。

#include <sys/stat.h>	// 文件状态头文件。含有文件或文件系统状态结构stat和常量。

// 释放所有一次间接块
// dev是文件系统所在设备的设备号，block是逻辑块号
static void free_ind(int dev,int block)
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;

	// 首先判断参数的有效性。如果逻辑块号为0，则返回。
	if (!block)
		return;

	// 然后读取一次间接块，并释放其上表明使用的所有逻辑块，然后释放该一次间接块的缓冲块。
	// 函数free_block用于释放设备上指定逻辑块号的磁盘块(fs/bitmap.c第47行)。
	if (bh=bread(dev,block)) {
		p = (unsigned short *) bh->b_data;	// 指向缓冲块数据
		for (i=0;i<512;i++,p++)				// 每个逻辑块上可有512个块号
			if (*p)
				free_block(dev,*p);			// 释放指定的设备逻辑块
		brelse(bh);// 释放间接块占用的缓冲块
	}
	free_block(dev,block);	// 最后释放设备上的一次间接块
}

static void free_dind(int dev,int block)
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;

	if (!block)
		return;
	if (bh=bread(dev,block)) {
		p = (unsigned short *) bh->b_data;
		for (i=0;i<512;i++,p++)
			if (*p)
				free_ind(dev,*p);
		brelse(bh);
	}
	free_block(dev,block);
}

void truncate(struct m_inode * inode)
{
	int i;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))
		return;
	for (i=0;i<7;i++)
		if (inode->i_zone[i]) {
			free_block(inode->i_dev,inode->i_zone[i]);
			inode->i_zone[i]=0;
		}
	free_ind(inode->i_dev,inode->i_zone[7]);
	free_dind(inode->i_dev,inode->i_zone[8]);
	inode->i_zone[7] = inode->i_zone[8] = 0;
	inode->i_size = 0;
	inode->i_dirt = 1;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
}

