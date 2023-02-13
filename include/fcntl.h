#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

/* open/fcntl - NOCTTY, NDELAY isn't implemented yet */
#define O_ACCMODE	00003
#define O_RDONLY	   00						// 只读方式打开文件
#define O_WRONLY	   01						// 只写方式打开文件
#define O_RDWR		   02						// 读写方式打开文件
// 下面是文件创建和操作标志，用于open。可与上面访问模式用'位或'的方式一起使用。
#define O_CREAT		00100	/* not fcntl */		// 如果文件不存在就创建 fcntl函数不用
#define O_EXCL		00200	/* not fcntl */		// 独占使用文件标志
#define O_NOCTTY	00400	/* not fcntl */		// 不分配控制终端
#define O_TRUNC		01000	/* not fcntl */		// 若文件已存在且是写操作,则长度截为0
#define O_APPEND	02000						// 以添加方式打开,文件指针置为文件尾
#define O_NONBLOCK	04000	/* not fcntl */		// 以非阻塞方式打开和操作文件
#define O_NDELAY	O_NONBLOCK					// 以非阻塞方式打开和操作文件

/* Defines for fcntl-commands. Note that currently
 * locking isn't supported, and other things aren't really
 * tested.
 */
#define F_DUPFD		0	/* dup */
#define F_GETFD		1	/* get f_flags */
#define F_SETFD		2	/* set f_flags */
#define F_GETFL		3	/* more flags (cloexec) */
#define F_SETFL		4
#define F_GETLK		5	/* not implemented */
#define F_SETLK		6
#define F_SETLKW	7

/* for F_[GET|SET]FL */
#define FD_CLOEXEC	1	/* actually anything with low bit set goes */

/* Ok, these are locking features, and aren't implemented at any
 * level. POSIX wants them.
 */
#define F_RDLCK		0
#define F_WRLCK		1
#define F_UNLCK		2

/* Once again - not implemented, but ... */
struct flock {
	short l_type;
	short l_whence;
	off_t l_start;
	off_t l_len;
	pid_t l_pid;
};

extern int creat(const char * filename,mode_t mode);
extern int fcntl(int fildes,int cmd, ...);
extern int open(const char * filename, int flags, ...);

#endif
