/*
 *  linux/fs/read_write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/segment.h>

extern int rw_char(int rw,int dev, char * buf, int count, off_t * pos);	// char_dev.c
extern int read_pipe(struct m_inode * inode, char * buf, int count);	// pipe.c
extern int write_pipe(struct m_inode * inode, char * buf, int count);	// pipe.c
extern int block_read(int dev, off_t * pos, char * buf, int count);		// block_dev.c
extern int block_write(int dev, off_t * pos, char * buf, int count);	// block_dev.c
extern int file_read(struct m_inode * inode, struct file * filp,		// file_dev.c
		char * buf, int count);
extern int file_write(struct m_inode * inode, struct file * filp,		// file_dev.c
		char * buf, int count);

// 重定位文件读写指针系统调用。
// 参数fd是文件句柄，offset是新的文件读写指针偏移值，origin是偏移的起始位置，可有
// 三种选择：SEEK_SET(0,从文件开始处)、SEEK_CUR(1,从当前读写位置)、SEEK_END(2,从文件尾处)
int sys_lseek(unsigned int fd,off_t offset, int origin)
{
	struct file * file;
	int tmp;

	// 首先判断函数提供的参数有效性。如果文件句柄值大于程序最多打开文件数NR_OPEN(20)
	// 或者该句柄的文件结构指针为空，或者对应文件结构的i节点字段为空，或者指定设备文件
	// 指针是不可定位(除硬件软盘内存外的设备)的，则返回出错码并退出。如果文件对应的i节点是管道节点，则返回出错
	// 码退出。因为管道头尾指针不可随意移动！
	if (fd >= NR_OPEN || !(file=current->filp[fd]) || !(file->f_inode)
	   || !IS_SEEKABLE(MAJOR(file->f_inode->i_dev)))
		return -EBADF;
	if (file->f_inode->i_pipe)
		return -ESPIPE;
	
	//然后根据设置的定位标志，分别重新定位文件读写指针。
	switch (origin) {
		// origin=SEEK_SET,要求以文件起始处作为原点设置文件读写指针。若偏移值小于零，则出
		// 错返回错误码。否则设置文件读写指针等于offset。
		case 0:
			if (offset<0) return -EINVAL;
			file->f_pos=offset;
			break;
		// origin=SEEK_CUR,要求以文件当前读写指针处作为原点重定位读写指针。如果文件当前指
		// 针加上偏移值小于0，则返回出错码退出。否则在当前读写指针上加上偏移值。
		case 1:
			if (file->f_pos+offset<0) return -EINVAL;
			file->f_pos += offset;
			break;
		// origin=SEEK_END,要求以文件末尾作为原点重定位读写指针。此时若文件大小加上偏移值
		// 小于零则返回出错码退出。否则重定位读写指针为文件长度加上偏移值。
		case 2:
			if ((tmp=file->f_inode->i_size+offset) < 0)
				return -EINVAL;
			file->f_pos = tmp;
			break;
		default:
			return -EINVAL;
	}
	return file->f_pos;	// 最后返回重定位后的文件读写指针
}

// 读文件系统调用。
// 参数fd是文件句柄，buf是缓冲区，count是欲读字节数。
int sys_read(unsigned int fd,char * buf,int count)
{
	struct file * file;
	struct m_inode * inode;

	// 函数首先对参数有效性进行判断。如果文件句柄值大于程序最多打开文件数NR_OPEN,或者
	// 需要读取的字节计数值小于0，或者该句柄的文件结构指针为空，则返回出错码并退出。若
	// 需读取的字节数count等于0，则返回0退出
	if (fd>=NR_OPEN || count<0 || !(file=current->filp[fd]))
		return -EINVAL;
	if (!count)
		return 0;
	
	// 然后验证存放数据的缓冲区内存限制。并取文件的i节点。用于根据该i节点的属性，分别
	// 调用相应的读操作函数。若是管道文件，并且是读管道文件模式，则进行读管道操作，若成
	// 功则返回读取的字节数，否则返回出错码，退出。如果是字符型文件，则进行读字符设备操
	// 作，并返回读取的字符数。如果是块设备文件，则执行块设备读操作，并返回读取的字节数。
	verify_area(buf,count);
	inode = file->f_inode;
	if (inode->i_pipe)
		return (file->f_mode&1)?read_pipe(inode,buf,count):-EIO;
	if (S_ISCHR(inode->i_mode))
		return rw_char(READ,inode->i_zone[0],buf,count,&file->f_pos);
	if (S_ISBLK(inode->i_mode))
		return block_read(inode->i_zone[0],&file->f_pos,buf,count);
	// 如果是目录文件或者是常规文件，则首先验证读取字节数count的有效性并进行调整（若读
	// 取字节数加上文件当前读写指针值大于文件长度，则重新设置读取字节数为文件长度-当前
	// 读写指针值，若读取数等于0，则返回0退出)，然后执行文件读操作，返回读取的字节数并退出。
	if (S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode)) {
		if (count+file->f_pos > inode->i_size)
			count = inode->i_size - file->f_pos;
		if (count<=0)
			return 0;
		return file_read(inode,file,buf,count);
	}
	printk("(Read)inode->i_mode=%06o\n\r",inode->i_mode);
	return -EINVAL;
}

// 写文件系统调用。
// 参数fd是文件句柄，buf是用户缓冲区，count是欲写字节数。
int sys_write(unsigned int fd,char * buf,int count)
{
	struct file * file;
	struct m_inode * inode;
	
	// 同样地，我们首先判断函数参数的有效性。如果进程文件句柄值大于程序最多打开文件数
	// NR_OPEN,或者需要写入的字节计数小于0，或者该句柄的文件结构指针为空，则返回出错
	// 码并退出。如果需读取的字节数count等于0，则返回0退出
	if (fd>=NR_OPEN || count <0 || !(file=current->filp[fd]))
		return -EINVAL;
	if (!count)
		return 0;
	// 然后验证存放数据的缓冲区内存限制。并取文件的i节点。用于根据该i节点的属性，分别
	// 调用相应的读操作函数。若是管道文件，并且是写管道文件模式，则进行写管道操作，若成
	// 功则返回写入的字节数，否则返回出错码退出。如果是字符设备文件，则进行写字符设备操
	// 作，返回写入的字符数退出。如果是块设备文件，则进行块设备写操作，并返回写入的字节
	// 数退出。若是常规文件，则执行文件写操作，并返回写入的字节数，退出。
	inode=file->f_inode;
	if (inode->i_pipe)
		return (file->f_mode&2)?write_pipe(inode,buf,count):-EIO;
	if (S_ISCHR(inode->i_mode))
		return rw_char(WRITE,inode->i_zone[0],buf,count,&file->f_pos);
	if (S_ISBLK(inode->i_mode))
		return block_write(inode->i_zone[0],&file->f_pos,buf,count);
	if (S_ISREG(inode->i_mode))
		return file_write(inode,file,buf,count);
	printk("(Write)inode->i_mode=%06o\n\r",inode->i_mode);
	return -EINVAL;
}
