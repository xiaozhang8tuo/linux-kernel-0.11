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

//释放所有二次间接块。
//参数dev是文件系统所在设备的设备号：block是逻辑块号。
static void free_dind(int dev,int block)
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;

	if (!block)
		return;
	// 读取二次间接块的一级块，并释放其上表明使用的所有逻辑块，然后释放该一级块的缓冲区。
	if (bh=bread(dev,block)) {
		p = (unsigned short *) bh->b_data;	//指向缓冲块数据区
		for (i=0;i<512;i++,p++)				//每个逻辑块上可有512个一级块号
			if (*p)
				free_ind(dev,*p);			//释放所有一次间接块
		brelse(bh);							//释放二次间接块占用的缓冲块
	}
	free_block(dev,block);					//释放设备上的二次间接块
}

// 截断文件数据函数。
// 将节点对应的文件长度截为0，并释放占用的设备空间。
void truncate(struct m_inode * inode)
{
	int i;

	// 首先判断指定i节点有效性。如果不是常规文件或者是目录文件，则返回。
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))
		return;
	
	// 然后释放i节点的7个直接逻辑块，并将这7个逻辑块项置0.  free block 用于释放设备上指定逻辑号的磁盘块
	for (i=0;i<7;i++)
		if (inode->i_zone[i]) {
			free_block(inode->i_dev,inode->i_zone[i]);
			inode->i_zone[i]=0;
		}
	free_ind(inode->i_dev,inode->i_zone[7]);		//释放一次间接块
	free_dind(inode->i_dev,inode->i_zone[8]);		//释放二次间接块
	inode->i_zone[7] = inode->i_zone[8] = 0;		//逻辑块78置0
	inode->i_size = 0;								//文件大小置0
	inode->i_dirt = 1;								//文件已修改
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;	//最后重置文件修改时间和i节点改变时间为当前时间。
}

