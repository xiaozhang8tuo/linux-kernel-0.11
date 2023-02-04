/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */
// 定义宏"__LIBRARY__" 是为了包括定义在unistd.h中的内嵌汇编代码信息
#define __LIBRARY__
// *.h头文件所在的默认目录是include/,则在代码中就不用明确指明其位置,如果不是UNIX的
// 标准头文件，则需要指明所在的目录，并用双引号括住,unistd.h是标准符号常数与类型文件,
// 其中定义了各种符号常数和类型，并声明了各种函数,如果还定义了符号LIBRARY,则还会
// 包含系统调用号和内嵌汇编代码syscal100等,

#include <unistd.h>
#include <time.h>   // 时间类型头文件,主要定义了tm结构和一些有关时间的函数原型

/*
	我们需要下面这些内嵌语句,从内核空间创建进程将导致没有写时复制(COPY ON WRITE)!!
直到执行一个execve调用,这对堆栈可能带来问题,处理方法是在fork调用后不让main
使用任何堆栈,因此就不能有函数调用,这意味着fork也要使用内嵌的代码,否则我们在从
fork退出时就要使用堆栈了
	实际上只有pause和fork需要使用内嵌方式,以保证从mian中不会弄乱堆栈.但还是定义了其他的一些函数
 */
// Linux在内核空间创建进程时不使用写时复制技术(Copy on write),main()在移动到用户
// 模式(到任务0)后执行内嵌方式的fork和pause,因此可保证不使用任务0的用户栈,
// 在执行move_to_user_mode之后，本程序main()就以任务0的身份在运行了,而任务0是所
// 有将创建子进程的父进程,当它创建一个子进程时(init进程)，由于任务1代码属于内核
// 空间，因此没有使用写时复制功能,此时任务0的用户栈就是任务1的用户栈，即它们共同
// 使用一个栈空间,因此希望在main.c运行在任务0的环境下时不要有对堆栈的任何操作，以
// 免弄乱堆栈,而在再次执行fork()并执行过execve()函数后，被加载程序已不属于内核空间，
// 因此可以使用写时复制技术了,参见上一章“Liux内核使用内存的方法”一节内容,


static inline _syscall0(int,fork)
/*
static line int fork(void) 
{ 
	long __res;
	__asm__ volatile ("int $0x80" : "=a" (__res) : "0" (__NR_fork));
	if (__res >= 0) 
		return (int) __res;
	errno = -__res;
	return -1; 
}
*/
static inline _syscall0(int,pause)
static inline _syscall1(int,setup,void *,BIOS)  //setup(void* BIOS)系统调用，仅用于linux初始化(仅在这个程序中调用)
static inline _syscall0(int,sync)               //系统调用,更新文件系统

#include <linux/tty.h>      //tty头文件，定义了有关tty_io,串行通信方面的参数、常数,
#include <linux/sched.h>	//调度程序头文件，定义了任务结构task_struct、第1个初始任务
							//的数据,还有一些以宏的形式定义的有关描述符参数设置和获取的
							//嵌入式汇编函数程序,
#include <linux/head.h>     //head头文件,定义了段描述符的简单结构,和价格选择符常量
#include <asm/system.h>     //系统头文件,以宏的形式定义了许多有关设置或修改 描述符/中断门等嵌入式汇编子程序
#include <asm/io.h>         //io头文件,以宏的嵌入式汇编程序形式定义对io端口操作的函数

#include <stddef.h>         //标准定义头文件,定义了NULL,offsetof(TYPE,MEMBER),
#include <stdarg.h>			//标准参数头文件,以宏的形式定义变量参数列表,主要说明了以个
							//类型(va_list)和三个宏(va_start,va_arg和va_end),vsprintf、vprintf,vfpringf
#include <unistd.h>
#include <fcntl.h>			//文件控制头文件,用于文件及其描述符的操作控制常数符号的定义,
#include <sys/types.h>		//类型头文件,定义了基本的系统数据类型,

#include <linux/fs.h>		//文件系统头文件,定义文件表结构(file,buffer_head,m_inode等),

static char printbuf[1024]; 				//静态字符数组，用作内核显示信息的缓存

extern int vsprintf();                      //送格式化输出到一字符串中(vsprintf.c )
extern void init(void);						//函数原型,初始化
extern void blk_dev_init(void);				//块设备初始化(blk_drv/ll_rw_blk.c)
extern void chr_dev_init(void);				//字符设备初始化(chr_drv/tty_io.c)
extern void hd_init(void);					//硬盘初始化程序(blk_drv/hd.c)
extern void floppy_init(void);				//软驱初始化程序(blk_drv/flopy.c)
extern void mem_init(long start, long end); 		//内存管理初始化(mm/memory.c)
extern long rd_init(long mem_start, int length);    //虚拟盘初始化(blk_drv/ramdisk.c)
extern long kernel_mktime(struct tm * tm);			//计算机系统开机启动时间
extern long startup_time;							//内核启动时间(开机时间)

/*
 * This is set up by the setup-routine at boot-time 这些数据是在boot期间由setup.s程序设置的
 */
// 下面三行分别将指定的线性地址强行转换为给定数据类型的指针，并获取指针所指内容,由于
// 内核代码段被映射到从物理地址零开始的地方，因此这些线性地址正好也是对应的物理地址,
// 这些指定地址处内存值的含义请参见第6章的表6-2(setup程序读取并保存的参数),
// drive_info结构请参见下面第1O2行,
#define EXT_MEM_K (*(unsigned short *)0x90002)        //扩展内存数 系统从1MB开始的扩展内存数值(KB)
#define DRIVE_INFO (*(struct drive_info *)0x90080)	  //硬盘参数表,第一个硬盘的参数表
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)	  //根设备号: 根文件系统所在的设备号(bootsect.s中设置)


/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

//这段宏读取COS实时时钟信息,outb_p和inb_p是include/asm/io.h中定义的端口输入输出宏,
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \       //0x70是写地址端口,0x80|addr是要读取的CMOS内存地址
inb_p(0x71); \					//0x71是读数据端口
})

//定义宏，将BCD码转换成二进制数值, BCD码利用半个字节(4bit)表示一个10进制数，因此
//1个字节表示2个10进制数. val&15(1111)取BCD的低四位表示的10进制个位数,而val>>4取BCD表示的10进制十位数,再*10,最后两者相加
//即一个字节BCD码的实际二进制数值
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)


//该函数读取CMOS实时钟信息作为开机时间，并保存到全局变量startup_time中,参见后面CMOS内存列表说明,其中调用的函数kernel_mktime
//用于计算从1970.1.1 0时刻到开机经历的秒数，作为开机时间(kernel/mktime.c)
static void time_init(void)
{
	struct tm time;                          //时间结构tm定义在time.h中


	// CMOS的访问速度慢，为了减小时间误差，在读取下面所有值之后，如果秒值发生变化，就重新读取,把与CMOS的时差控制在1s
	do {
		time.tm_sec = CMOS_READ(0);          //当前时间秒值
		time.tm_min = CMOS_READ(2);			 //当前时间分钟值
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));   
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;							//tm中月份范围0-11
	startup_time = kernel_mktime(&time);    //计算开机时间
}


//下面定义一些局部变量
static long memory_end = 0;                 //机器具有的物理内存容量(字节数)
static long buffer_memory_end = 0;			//高速缓冲区末端地址
static long main_memory_start = 0;			//主内存 (将用于分页) 开始的位置

struct drive_info { char dummy[32]; } drive_info; //用于存放硬盘参数表信息

void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * 此时中断仍然被禁止,做完必要的设置后就将其开启
 */
// 下面这段代码用于保存
// 根设备号->ROOT_DEV  高速缓存末端地址->buffer_memory_end
// 机器内存数->memory_end 主存开始地址->main_memory_start
 	ROOT_DEV = ORIG_ROOT_DEV;                    //ROOT_DEV定义在fs/super.c
 	drive_info = DRIVE_INFO;					 //复制0x90080处的硬盘参数
	memory_end = (1<<20) + (EXT_MEM_K<<10);      //内存大小1MB+扩展内存 EXT_MEM_K kb
	memory_end &= 0xfffff000;                    //忽略不到4kB(1页)的内存数
	if (memory_end > 16*1024*1024)				 //超过16MB,按16MB算
		memory_end = 16*1024*1024;
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 4*1024*1024;		 //内存>12MB,则设置缓冲区末端4MB
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;		 //>6MB,则设置缓冲区末端2MB
	else
		buffer_memory_end = 1*1024*1024;		 //1MB
	main_memory_start = buffer_memory_end;		 //主存的起始位=缓冲区的末端

//如果在MKfile中定义了内存虚拟盘号RAMKDISK,则初始化虚拟盘,此时主内存将减少, ramdisk.c
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif
//以下是内核进行所有方面的初始化工作,
	mem_init(main_memory_start,memory_end); 		//主内存区初始化(mm/memory.c)
	trap_init();									//陷阱门(硬件中断向量)初始化 (traps.c)
	blk_dev_init();//块设备							
	chr_dev_init();//字符设备
	tty_init();//tty初始化
	time_init();//设置开机时间
	sched_init();//调度程序初始化
	buffer_init(buffer_memory_end);//缓冲管理初始化,建立内存链表
	hd_init();//硬盘
	floppy_init();//软驱
	sti();											//初始化工作完成,开启中断

// 下面过程通过堆栈中设置的参数,利用中断返回指令启动任务0执行
	move_to_user_mode();//转移到用户模式
	if (!fork()) {		/* we count on this going ok */
		init();			//子进程中执行
	}
/*
	注意！！对于任何其他的任务，’pause()'将意味着我们必须等待收到一个信号
才会返回就绪态，但任务0(task0)是唯一例外情况(参见'schedule()'),
因为任务0在任何空闲时间里都会被微活(当没有其他任务在运行时)，因此
对于任务0'pause()'仅意味着我们返回来查看是否有其他任务可以运行，如果
没有的话我们就回到这里，一直循环执行'pause()’,
	pause()系统调用(kernel/sched.c)会把任务0转换成可中断等待状态，再执行调度函数
但是调度函数只要发现系统中没有其他任务可以运行时就会切换到任务0，而不依赖于任务0的
状态,
 */
	for(;;) pause();
}

// 下面函数产生格式化信息并输出到标准输出设备stdout(1),这里是指屏幕上显示,参数'*mt
// 指定输出将采用的格式，参见标准C语言书籍,该子程序正好是vsprintf如何使用的一个简单
// 列子,该程序使用vsprintf()将格式化的字符串放入printbuf缓冲区，然后用write将缓冲
// 区的内容输出到标准设备(I-stdout),vsprintf()函数的实现见kernel/vsprintf.c,
static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

//读取并执行/etc/rc文件时所使用的命令行参数和环境参数,
static char * argv_rc[] = { "/bin/sh", NULL };   //调用执行程序时参数的字符串数组
static char * envp_rc[] = { "HOME=/", NULL };    //调用执行程序时的环境字符串数组


//运行登录shell时所使用的命令行参数和环境参数
//argv[0]中的字符“-”是传递给shell程序sh的一个标志,通过识别该标志，
//sh程序会作为登录shell执行,其执行过程与在shell提示符下执行sh不一样,
static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };


// 在main()中已经进行了系统初始化，包括内存管理、各种硬件设备和驱动程序,init()函数
// 运行在任务0第1次创建的子进程(任务1)中,它首先对第一个将要执行的程序(shell)
// 的环境进行初始化，然后以登录shell方式加载该程序并执行之,
void init(void)
{
	int pid,i;

// setup()是一个系统调用,用于读取硬盘参数包括分区表信息并加截虚拟盘(若存在的话)和
// 安装根文件系统设备,该函数用46行上的宏定义，对应函数是sys_setup(),在块设备子目录
// kernel/blk_drv/hd.c行,
	setup((void *) &drive_info);

// 下面以读写访间方式打开设备“/dev/tty0”,它对应终瑞控制台,由于这是第一次打开文件
// 操作，因此产生的文件句柄号(文件描述符)肯定是0,该句柄是UNIX类操作系统默认的控
// 制台标准输入句柄stdin,这里再把它以读和写的方式分别打开是为了复制产生标准输出(写)
// 句柄stdout和标准出错输出句柄stderr,
// 函数前面的“(void)”前缀用于表示强制函数无需返回值,
	(void) open("/dev/tty0",O_RDWR,0);
	(void) dup(0);     //复制句柄,产生句柄1号--stdout标准输出设备
	(void) dup(0);	   //复制句柄,产生句柄2号--stderr标准出错设备
// 下面打印缓冲区块数和总字节数，每块1024字节，以及主内存区空闲内存字节数,
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);

// 下面fork()用于创建一个子进程(任务2),对于被创建的子进程，fork()将返回0值，对于
// 原进程(父进程)则返回子进程的进程号pid,该子
// 进程关闭了句柄0(stdin)、以只读方式打开/etc/rc文件，并使用execve()函数将进程自身
// 替换成bin/sh程序(即shell程序)，然后执行bin/sh程序,所携带的参数和环境变量分
// 别由argv_rc和envp_rc数组给出,关闭句柄0并立刻打开/etc/rc文件的作用是把标准输入
// stdin重定向到/etc/rc文件,这样shell程序bin/sh就可以运行rc文件中设置的命令,由
// 于这里sh的运行方式是非交互式的，因此在执行完rc文件中的命令后就会立刻退出，进程2
// 也随之结束,关于execve()函数说明请参见fs/exec.c程序,
// 函数exit()退出时的出错码1-操作未许可：2-文件或目录不存在,
	if (!(pid=fork())) {
		close(0);
		if (open("/etc/rc",O_RDONLY,0))  //把标准输入stdin重定向到/etc/rc中
			_exit(1);                    //如果打开文件失败，则退出(1ib/_exit.c,10)
		execve("/bin/sh",argv_rc,envp_rc); 
		_exit(2);                        //若execve()执行失败则退出
	}

// 下面还是父进程(1)执行的语句,wait()等待子进程停止或终止，返回值应是子进程的进程号
// (pid),这三句的作用是父进程等待子进程的结束,&i是存放返回状态信息的位置,如果wait()
// 返回值不等于子进程号，则继续等待,
	if (pid>0)
		while (pid != wait(&i))
			/* nothing */;

// 如果执行到这里，说明刚创建的子进程的执行已停止或终止了,下面循环中首先再创建一个子
// 进程，如果出错，则显示“初始化程序创建子进程失败”信息并继续执行,对于所创建的子进
// 程将关闭所有以前还遗留的句柄(stdin,stdout,stderr),新创建一个会话并设置进程组号，
// 然后重新打开/dev/tty0作为stdin,并复制成stdout和stderr,再次执行系统解释程序
// /bin/sh,但这次执行所选用的参数和环境数组另选了一套(见上面165-167行),然后父进
// 程再次运行wait()等待,如果子进程又停止了执行，则在标准输出上显示出错信息“子进程
// pid停止了运行，返回码是i”,然后继续重试下去，形成一个“大”循环,此外，wait()
// 的另外一个功能是处理孤儿进程,如果一个进程的父进程先终止了，那么这个进程的父进程
// 就会被设置为这里的init进程(进程1)，并由init进程负责释放一个已终止进程的任务数
// 据结构等资源,
	while (1) {
		if ((pid=fork())<0) {
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) {
			close(0);close(1);close(2);
			setsid();							//创建新的会话期
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();
	}
	_exit(0);	/* NOTE! _exit, not exit() */
// _exit()和exit()都用于正常终止一个函数,但_exit直接是一个sys_exit系统调用，而
// exit()则通常是普通函数库中的一个函数,它会先执行一些清除操作，例如调用执行各终止
// 处理程序、关闭所有标准I0等，然后调用sys_exit,
}
