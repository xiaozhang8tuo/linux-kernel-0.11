/*
 *  linux/fs/open.c
 *	本文件实现了许多与文件操作相关的系统调用。主要有文件的创建、打开和关闭，文件宿主和属性
的修改、文件访问权限的修改、文件操作时间的修改和系统文件系统root的变动等。
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <fcntl.h>			// 文件控削头文件。用于文件及其描述符操作控制常数符号定义。
#include <sys/types.h>
#include <utime.h>			// 用户时间头文件。定义访问和修改时间结构以及utime原型。
#include <sys/stat.h>		// 文件状态头文件。含有文件状态结构stat和符号常量等。

#include <linux/sched.h>
#include <linux/tty.h>		// tty头文件，定义了有关tty_io,串行通信方面的参数、常数。
#include <linux/kernel.h>
#include <asm/segment.h>

// 取文件系统信息。
// 参数dev是含有已安装文件系统的设备号。ubuf是一个ustat结构缓冲区指针，用于存放
// 系统返回的文件系统信息。该系统调用用于返回已安装(mounted)文件系统的统计信息。
// 成功时返回0，并且ubuf指向的ustate结构被添入文件系统总空闲块数和空闲i节点数。
// ustat结构定义在include/sys/types.h中.
int sys_ustat(int dev, struct ustat * ubuf)
{
	return -ENOSYS;
}

// 设置文件访问和修改时间。
// 参数filename是文件名，times是访问和修改时间结构指针。
// 如果times指针不为NULL,则取utimbuf结构中的时间信息来设置文件的访问和修改时间。
// 如果times指针是NULL,则取系统当前时间来设置指定文件的访问和修改时间域。
int sys_utime(char * filename, struct utimbuf * times)
{
	struct m_inode * inode;
	long actime,modtime;

	// 文件的时间信息保存在其i节点钟。因此我们首先根据文件名取得对应节点。如果没有找
	// 到，则返回出错码。
	if (!(inode=namei(filename)))
		return -ENOENT;
	// 如果提供的访问和修改时间结构指针times不为NULL,则从结构中读取用户设置的时间值。
	// 否则就用系统当前时间来设置文件的访问和修改时间。
	if (times) {
		actime = get_fs_long((unsigned long *) &times->actime);
		modtime = get_fs_long((unsigned long *) &times->modtime);
	} else
		actime = modtime = CURRENT_TIME;
	// 然后修改i节点中的访问时间字段和修改时间字段。再设置i节点已修改标志，放回该i节点，并返回0。
	inode->i_atime = actime;
	inode->i_mtime = modtime;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */

// 检查文件的访问权限。
// 参数filename是文件名，mode是检查的访问属性，它由3个有效比特位组成：ROK(4)、
// W_0K(2)、X0K(1)和F_0K(0)组成，分别表示检测文件是否可读、可写、可执行和文件是
// 否存在。如果访问允许的话，则返回0，否则返回出错码。
int sys_access(const char * filename,int mode)
{
	struct m_inode * inode;
	int res, i_mode;

	mode &= 0007;
	if (!(inode=namei(filename)))
		return -EACCES;
	i_mode = res = inode->i_mode & 0777;
	iput(inode);

	// 如果当前进程用户是该文件的宿主，则取文件宿主属性。否则如果当前进程用户与该文件宿
	// 主同属一组，则取文件组属性。否则，此时rs最低3比特是其他人访问该文件的许可属性。
	if (current->uid == inode->i_uid)
		res >>= 6;	// res >>=3;
	else if (current->gid == inode->i_gid)
		res >>= 6;
	// 此时res的最低3比特是根据当前进程用户与文件的关系选择出来的访问属性位。现在我们
	// 来判断这3比特。如果文件属性具有参数所查询的属性位mode,则访问许可，返回0。
	if ((res & 0007 & mode) == mode)
		return 0;
	/*
	 * XXX we are doing this test last because we really should be
	 * swapping the effective with the real user id (temporarily),
	 * and then calling suser() routine.  If we do call the
	 * suser() routine, it needs to be called last. 
	 * XXX我们最后才做下面的测试，因为我们实际上需要交换有效用户ID和
	 * 真实用户ID(临时地)，然后才调用suser0函数。如果我们确实要调用suser函数，则需要最后才被调用。
	 */
	// 如果当前用户ID为0（超级用户）并且屏蔽码执行位是0或者文件可以被任何人执行、搜索，则返回0。否则返回出错码。
	if ((!current->uid) &&
	    (!(mode & 1) || (i_mode & 0111)))
		return 0;
	return -EACCES;
}

// 改变当前工作目录系统调用。
// 参数filename是目录名。
// 操作成功则返回0，否则返回出错码。
int sys_chdir(const char * filename)
{
	struct m_inode * inode;

	if (!(inode = namei(filename)))
		return -ENOENT;					// 出错码,文件或目录不存在
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	// 然后释放进程原工作目录i节点，并使其指向新设置的工作目录i节点。返回0。
	iput(current->pwd);
	current->pwd = inode;
	return (0);
}

// 改变根目录系统调用
// 把指定的目录名设置成为当前进程的根目录'/'。
// 如果操作成功则返回0，否则返回出错码。
int sys_chroot(const char * filename)
{
	struct m_inode * inode;

	// 该调用用于改变当前进程任务结构中的根目录字段root,让其指向参数给定目录名的i节点。
	// 如果目录名对应的i节点不存在，则返回出错码。如果该1节点不是目录i节点，则放回该
	// 1节点，也返回出错码。
	if (!(inode=namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	// 然后释放当前进程的根目录i节点，并重新设置为指定目录名的i节点，返回0。
	iput(current->root);
	current->root = inode;
	return (0);
}


// 修改文件属性系统调用。
// 参数filename是文件名，mode是新的文件属性。
// 若操作成功则返回0，否则返回出错码。
int sys_chmod(const char * filename,int mode)
{
	struct m_inode * inode;

	// 该调用为指定文件设置新的访问属性mod。文件的访问属性在文件名对应的i节点中，因此
	// 我们首先取文件名对应的i节点。如果i节点不存在，则返回出错码（文件或目录不存在）。
	// 如果当前进程的有效用户id与文件i节点的用户id不同，并且也不是超级用户，则放回该
	// 文件i节点，返回出错码（没有访问权限）。
	if (!(inode=namei(filename)))
		return -ENOENT;
	if ((current->euid != inode->i_uid) && !suser()) {
		iput(inode);
		return -EACCES;
	}
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);	// 重置i节点的文件属性   (inode->i_mode & ~07777):不修改的地方保留
	inode->i_dirt = 1;// 设置已修改标志位
	iput(inode);
	return 0;
}

// 修改文件宿主系统调用。
// 参数filename是文件名，uid是用户标识符（用户ID),gid是组ID。
// 若操作成功则返回0，否则返回出错码。
int sys_chown(const char * filename,int uid,int gid)
{
	struct m_inode * inode;

	// 该调用用于设置文件i节点中的用户和组ID,因此首先要取得给定文件名的i节点。如果文
	// 件名的i节点不存在，则返回出错码（文件或目录不存在）。如果当前进程不是超级用户，
	// 则放回该1节点，并返回出错码（没有访问权限）。
	if (!(inode=namei(filename)))
		return -ENOENT;
	if (!suser()) {
		iput(inode);
		return -EACCES;
	}
	inode->i_uid=uid;
	inode->i_gid=gid;
	inode->i_dirt=1;
	iput(inode);
	return 0;
}

// 打开（或创建）文件系统调用。
// 参数filename是文件名，flag是打开文件标志，它可取值：
// O_RDONLY(只读)、
// O_WRONLY(只写)或
// O_RDWR(读写)
// O_CREAT(创建)、
// O_EXCL(被创建文件必须不存在)、
// 0_APPEND(在文件尾添加数据)等其他一些标志的组合。如果本调用创建了一个新文件，则
// mode就用于指定文件的许可属性。这些属性有S_IRWXU(文件宿主具有读、写和执行权限)、
// S_IRUSR(用户具有读文件权限)、S_IRWXG(组成员具有读、写和执行权限)等等。对于新
// 创建的文件，这些属性只应用于将来对文件的访问，创建了只读文件的打开调用也将返回一
// 个可读写的文件句柄。如果调用操作成功，则返回文件句柄(文件描述符)，否则返回出错码。
// 参见sys/stat.h、fcntl.h。
int sys_open(const char * filename,int flag,int mode)
{
	struct m_inode * inode;
	struct file * f;
	int i,fd;

	// 首先对参数进行处理。将用户设置的文件模式和进程模式屏蔽码相与，产生许可的文件模式。
	// 为了为打开文件建立一个文件句柄，需要搜索进程结构中文件结构指针数组，以查找一个空
	// 闲项。空闲项的索引号d即是句柄值。若已经没有空闲项，则返回出错码（参数无效）。
	mode &= 0777 & ~current->umask;
	for(fd=0 ; fd<NR_OPEN ; fd++)
		if (!current->filp[fd])
			break;
	if (fd>=NR_OPEN)
		return -EINVAL;
	// 然后我们设置当前进程的执行时关闭文件句柄(close_on_exec)位图，复位对应的比特位。
	// close_on_exec是一个进程所有文件句柄的位图标志。每个比特位代表一个打开着的文件描
	// 述符，用于确定在调用系统调用execve()时需要关闭的文件句柄。当程序使用fork()函数
	// 创建了一个了子进程时，通常会在该子进程中调用execve()函数加截执行另一个新程序。此时
	// 子进程中开始执行新程序。若一个文件句柄在close_on_exec中的对应比特位被置位，那么
	// 在执行execve时该对应文件句柄将被关闭，否则该文件句柄将始终处于打开状态。当打开
	// 一个文件时，默认情况下文件句柄在子进程中也处于打开状态。因此这里要复位对应比特位。
	current->close_on_exec &= ~(1<<fd);// 子进程执行execve时,不关闭该文件

	// 然后为打开文件在文件表中寻找一个空闲结构项。我们令f指向文件表数组开始处。搜索空
	// 闲文件结构项（引用计数为0的项），若已经没有空闲文件表结构项，则返回出错码。
	f=0+file_table;
	for (i=0 ; i<NR_FILE ; i++,f++)
		if (!f->f_count) break;
	if (i>=NR_FILE)
		return -EINVAL;

	// 此时我们让进程对应文件句柄fd的文件结构指针指向搜索到的文件结构，并令文件引用计数
	// 递增1。然后调用函数open_namei执行打开操作，若返回值小于0，则说明出错，于是释放
	// 刚申请到的文件结构，返回出错码i。若文件打开操作成功，则inode是已打开文件的i节点指针。
	(current->filp[fd]=f)->f_count++;
	if ((i=open_namei(filename,flag,mode,&inode))<0) {
		current->filp[fd]=NULL;
		f->f_count=0;
		return i;
	}
	/* ttys are somewhat special (ttyxx major==4, tty major==5) */
	// 根据已打开文件的i节点的属性字段，我们可以知道文件的具体类型。对于不同类型的文件，
	// 我们需要作一些特别的处理。如果打开的是字符设备文件，那么，对于主设备号是4的字符文
	// 件（例如/dev/ttyO),如果当前进程是进程组首领并且当前进程的tty字段小于0（没有终端）
	// 则设置当前进程的tty号为该i节点的子设备号，并设置当前进程tty对应的tty表项的父进
	// 程组号等于当前进程的进程组号。表示为该进程组（会话期）分配控制终端。对于主设备号是
	// 5的字符文件(/dev/tty),若当前进程没有tty,则说明出错，于是放回i节点和申请到的文件结构，返回出错码（无许可）。
	// (注：这段代码存在问题)。
	if (S_ISCHR(inode->i_mode))
		if (MAJOR(inode->i_zone[0])==4) {
			if (current->leader && current->tty<0) {
				current->tty = MINOR(inode->i_zone[0]);
				tty_table[current->tty].pgrp = current->pgrp;
			}
		} else if (MAJOR(inode->i_zone[0])==5)
			if (current->tty<0) {
				iput(inode);
				current->filp[fd]=NULL;
				f->f_count=0;
				return -EPERM;
			}
	/* Likewise with block-devices: check for floppy_change */
	// 同样对于块设备文件：需要检查盘片是否被更换*/
	// 如果打开的是块设备文件，则检查盘片是否更换过。若更换过则需要让高速缓冲区中该设备的所有缓冲块失效。
	if (S_ISBLK(inode->i_mode))
		check_disk_change(inode->i_zone[0]);

	// 现在我们初始化打开文件的文件结构。设置文件结构属性和标志，置句柄引用计数为1，并
	// 设置i节点字段为打开文件的1节点，初始化文件读写指针为0。最后返回文件句柄号。
	f->f_mode = inode->i_mode;
	f->f_flags = flag;
	f->f_count = 1;
	f->f_inode = inode;
	f->f_pos = 0;
	return (fd);
}

// 创建文件系统调用
// 参数pathname是路径名，mode与上面的sys_open函数相同.
// 成功则返回文件句柄，否则返回出错码
int sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}

// 关闭文件系统调用
// 参数fd是文件句柄
// 成功则返回0，否则返回出错码
int sys_close(unsigned int fd)
{	
	struct file * filp;

	// 首先检查参数有效性。若给出的文件句柄值大于程序同时能打开的文件数NR_OPEN,.则返回
	// 出错码（参数无效）。然后复位进程的执行时关闭文件句柄位图对应位。若该文件句柄对应
	// 的文件结构指针是LL,则返回出错码。
	if (fd >= NR_OPEN)
		return -EINVAL;
	current->close_on_exec &= ~(1<<fd);	// 子进程exec时更加不需要考虑关闭了,主进程都关了,没有打开何来关闭
	if (!(filp = current->filp[fd]))	// 拿到file_table中的指针
		return -EINVAL;
	
	// 现在置该文件句柄的文件结构指针为LL。若在关闭文件之前，对应文件结构中的句柄用
	// 计数已经为0，则说明内核出错，停机。否则将对应文件结构的引用计数减1。此时如果它还
	// 不为0，则说明有其他进程正在使用该文件，于是返回0（成功）。如果引用计数已等于0，
	// 说明该文件已经没有进程引用，该文件结构已变为空闲。则释放该文件1节点，返回0。
	current->filp[fd] = NULL;			// 本进程不再指向改文件
	if (filp->f_count == 0)
		panic("Close: file count is 0");
	if (--filp->f_count)				
		return (0);
	iput(filp->f_inode);//该进程是最后使用文件的
	return (0);
}
