/*
 *  linux/fs/stat.c
 *  该程序实现取文件状态信息系统调用stat和stat,并将信息存放在用户的文件状态结构缓冲区中。
 *	stat是利用文件名取信息，而fstat是使用文件句柄（描述符）来取信息。
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <sys/stat.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
/*
struct stat {
	dev_t	st_dev;			// 含有文件的设备号。
	ino_t	st_ino;			// 文件i节点号。
	umode_t	st_mode;		// 文件类型和属性（见下面）。
	nlink_t	st_nlink;		// 指定文件的连接数。
	uid_t	st_uid;			// 文件的用户（标识）号。
	gid_t	st_gid;			// 文件的组号。
	dev_t	st_rdev;		// 设备号（如果文件是特殊的字符文件或块文件）。
	off_t	st_size;		// 文件大小（字节数）（如果文件是常规文件）。
	time_t	st_atime;		// 上次（最后）访问时间。
	time_t	st_mtime;		// 最后修改时间。
	time_t	st_ctime;		// 最后节点修改时间。
};*/


// 复荆文件状态信息
// 参数inode是文件i节点，statbuf是用户数据空间中stat文件状态结构指针，用于存放取
// 得的状态信息。
static void cp_stat(struct m_inode * inode, struct stat * statbuf)
{
	struct stat tmp;
	int i;

	// 首先验证（或分配）存放数据的内存空间。然后临时复制相应节点上的信息。
	verify_area(statbuf,sizeof (* statbuf));
	tmp.st_dev = inode->i_dev;
	tmp.st_ino = inode->i_num;
	tmp.st_mode = inode->i_mode;
	tmp.st_nlink = inode->i_nlinks;
	tmp.st_uid = inode->i_uid;
	tmp.st_gid = inode->i_gid;
	tmp.st_rdev = inode->i_zone[0];
	tmp.st_size = inode->i_size;
	tmp.st_atime = inode->i_atime;
	tmp.st_mtime = inode->i_mtime;
	tmp.st_ctime = inode->i_ctime;
	// 复制到用户缓冲区
	for (i=0 ; i<sizeof (tmp) ; i++)
		put_fs_byte(((char *) &tmp)[i],&((char *) statbuf)[i]);
}

// 文件状态系统调用。
// 根据给定的文件名获取相关文件状态信息。
// 参数filename是指定的文件名，statbuf是存放状态信息的缓冲区指针。
// 返回：成功返回0，若出错则返回出错码。
int sys_stat(char * filename, struct stat * statbuf)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	cp_stat(inode,statbuf);
	iput(inode);
	return 0;
}

// 文件状态系统调用。
// 根据给定的文件句柄获取相关文件状态信息。
// 参数fd是指定文件的句柄（描述符），statbuf是存放状态信息的缓冲区指针。
// 返回：成功返回0，若出错则返回出错码。
int sys_fstat(unsigned int fd, struct stat * statbuf)
{
	struct file * f;
	struct m_inode * inode;

	if (fd >= NR_OPEN || !(f=current->filp[fd]) || !(inode=f->f_inode))
		return -EBADF;
	cp_stat(inode,statbuf);
	return 0;
}
