#ifndef _TERMIOS_H
#define _TERMIOS_H
// 该文件含有终端IO接口定义.包括termios数据结构和一些对通用终端接口设置的函数原型.这些
// 函数用来读取或设置终端的属性、线路控制、读取或设置波特率以及读取或设置终瑞前端进程的组
// 虽然这是Linux早期的头文件,但己完全符合目前的POSIX标准,并作了适当的扩展

#define TTY_BUF_SIZE 1024	//tty中的缓冲区长度

/* 0x54 is just a magic number to make these relatively uniqe ('T') */

// tty设备的ioctl调用命令集.ioctl将命令编码在低位字中
//下面名称TC[*]的含义是tty控制命令
#define TCGETS		0x5401	// 取相应终端termios结构中的信息 tcgetattr
#define TCSETS		0x5402	// 设置相应终端termios结构中的信息 tcsetattr
#define TCSETSW		0x5403	// 在设置终端termios的信息之前,需要先等待输出队列中所有数据处理完（耗尽）.对于修改参数会影响输出的情况,就需要使用这种形式（参见tcsetattr,TCSADRAIN选项）
#define TCSETSF		0x5404	// 在设置termios的信息之前,需要先等待输出队列中所有数据处理完,并且刷新（清空）输入队列再设置（参见tcsetattr,TCSAFLUSH选项）
#define TCGETA		0x5405	// 取相应终端termios结构中的信息
#define TCSETA		0x5406	// 设置相应终端termios结构中的信息 tcsetattr
#define TCSETAW		0x5407	// 在设置终端termios的信息之前,需要先等待输出队列中所有数据处理完（耗尽）.对于修改参数会影响输出的情况,就需要使用这种形式（参见tcsetattr,TCSADRAIN）DRAIN通常指的是从缓冲区或数据流中读取和删除数据的过程
#define TCSETAF		0x5408	// 在设置termios的信息之前,需要先等待输出队列中所有数据处理完,并且刷新（清空）输入队列再设置, 参见tcsetattr ,TCSAFLUSH选项)
#define TCSBRK		0x5409	// 等待输出队列处理完毕（空）,如果参数值是0,则发送一个break
#define TCXONC		0x540A	// 开始/停止控制.如果参数值是0,则挂起输出：如果是1,则重新开启挂起的输出：如果是2,则挂起输入：如果是3,则重新开启挂起的输入,见tcflow
#define TCFLSH		0x540B	// 刷新已写输出但还没发送或已收但还没有读数据.如果参数是0,则刷新（清空）输入队列：如果是1则刷新输出队列：如果是2,则刷新输入和输出队列,见tcflush

//下面名称TIOC[*]的含义是tty输入输出控制命令
#define TIOCEXCL	0x540C	// 设置终端串行线路专用摸式
#define TIOCNXCL	0x540D	// 复位终端串行线路专用摸式
#define TIOCSCTTY	0x540E	// 设置tty为控制终端.(TIOCNOTTY-禁止tty为控制终端)
#define TIOCGPGRP	0x540F	// 读取指定终端设备进程的组id
#define TIOCSPGRP	0x5410	// 读取指定终端设备进程的组id
#define TIOCOUTQ	0x5411	// 返回输出队列中还未送出的字符数
#define TIOCSTI		0x5412	// 摸拟终瑞输入.该命令以一个指向字符的指针作为参数,并假装该字符是在终端上键入的.用户必须在该控制终端上具有超级用户权限或具有读许可权限.
#define TIOCGWINSZ	0x5413	// 读取终端设备窗口大小信息
#define TIOCSWINSZ	0x5414	// 读取终端设备窗口大小信息 
#define TIOCMGET	0x5415	// 返回modem状态控制引线的当前状态比特位标志集 
#define TIOCMBIS	0x5416	// 设置单个modem状态控制引线的状态
#define TIOCMBIC	0x5417	// 复位单个modem状态控制引线的状态
#define TIOCMSET	0x5418	// 设置odem状态引线的状态.如果某一比特位置位,则modem对应的状态引线将置为有效
#define TIOCGSOFTCAR	0x5419	// 读取软件载波检测标志(1-开启：0-关闭).对于本地连接的终端或其他设备,软件载波标志是开启的,对于使用modem线路的终端或设备则是关闭的.为了能使用这两个ioctl调用,tty线路应该是以O_NDELAY方式打开的,这样open就不会等待载波.
#define TIOCSSOFTCAR	0x541A	// 设置软件载波检测标志(1-开启：0-关闭)
#define TIOCINQ		0x541B		// 返回输入队列中还未取走字符的数目

// 窗口大小(Window size)属性结构.在窗口环境中可用于基于屏幕的应用程序
// ioctls中的TIOCGWINSZ和TIOCSWINSZ可用来读取或设置这些信息
struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;// 宽度像素值
	unsigned short ws_ypixel;// 高度像素值
};

#define NCC 8
struct termio {
	unsigned short c_iflag;		/* input mode flags */		//输入模式标志
	unsigned short c_oflag;		/* output mode flags */		//输出模式标志
	unsigned short c_cflag;		/* control mode flags */	//控制模式标志
	unsigned short c_lflag;		/* local mode flags */		//本地模式标志
	unsigned char c_line;		/* line discipline */		//线路进程(速率)
	unsigned char c_cc[NCC];	/* control characters */	//控制字符数组
};

#define NCCS 17
struct termios {
	unsigned long c_iflag;		/* input mode flags */
	unsigned long c_oflag;		/* output mode flags */
	unsigned long c_cflag;		/* control mode flags */
	unsigned long c_lflag;		/* local mode flags */
	unsigned char c_line;		/* line discipline */
	unsigned char c_cc[NCCS];	/* control characters */
};

/* c_cc characters */
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSWTC 7		// 交换字符
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VEOL 11		// 行结束字符
#define VREPRINT 12	// 重显示字符
#define VDISCARD 13	
#define VWERASE 14
#define VLNEXT 15
#define VEOL2 16	// 行结束字符2

/* c_iflag bits */
#define IGNBRK	0000001  // 输入时忽路BREAK条件.
#define BRKINT	0000002  // 在BREAK时产生SIGINT信号.
#define IGNPAR	0000004  // 忽路奇偶校验出错的字符.
#define PARMRK	0000010  // 标记奇偶校验错.
#define INPCK	0000020  // 允许输入奇偶校险.
#define ISTRIP	0000040  // 屏被字符第8位.
#define INLCR	0000100  // 输入时将换行符L映射成回车符CR.
#define IGNCR	0000200  // 忽咯回车符CR.
#define ICRNL	0000400  // 在输入时将回车符CR映射成换行符L.
#define IUCLC	0001000  // 在输入时将大写字符转换成小写字符.
#define IXON	0002000  // 允许开始/停止(XON/XOFF)输出控制.
#define IXANY	0004000  // 允许任何字符重启输出.
#define IXOFF	0010000  // 允许开始/停止(XON/XOFF)输入控制.
#define IMAXBEL	0020000  // 输入队列满时响铃.

/* c_oflag bits */
#define OPOST	0000001  // 执行输出处理
#define OLCUC	0000002  // 在输出时将小写字符转换成大写字符
#define ONLCR	0000004  // 在输出时将换行符NL映射成回车一换行符CR-NL
#define OCRNL	0000010  // 在输出时将回车符CR映射成换行符NL
#define ONOCR	0000020  // 在0列不输出回车符CR
#define ONLRET	0000040  // 换行符NL执行回车符的功能
#define OFILL	0000100  // 延迟时使用填充字符而不使用时间延迟.
#define OFDEL	0000200  // 填充字符是ASCII码DEL.如果未设置,则使用ASCII NULL,
#define NLDLY	0000400  // 选择换行延迟.
#define   NL0	0000000  // 换行延迟类型0.
#define   NL1	0000400  // 换行延迟类型1.
#define CRDLY	0003000  // 选择回车延迟.
#define   CR0	0000000  // 回车延迟类型0.
#define   CR1	0001000  // 回车延迟类型1.
#define   CR2	0002000  // 回车延迟类型2.
#define   CR3	0003000  // 回车延迟类型3.
#define TABDLY	0014000  // 选择水平制表延迟.
#define   TAB0	0000000  // 水平制表延迟类型0.
#define   TAB1	0004000  // 水平制表延迟类型1.
#define   TAB2	0010000  // 水平制表延迟类型2,
#define   TAB3	0014000  // 水平制表延迟类型3.
#define   XTABS	0014000  // 将制表符TAB换成空格,该值表示空格数.
#define BSDLY	0020000  // 选择退格延迟.
#define   BS0	0000000  // 退格延迟类型0.
#define   BS1	0020000  // 退格延迟类型1.
#define VTDLY	0040000  // 纵向制表延迟.
#define   VT0	0000000  // 纵向制表延迟类型0.
#define   VT1	0040000  // 纵向制表延迟类型1.
#define FFDLY	0040000  // 选择换页延迟.
#define   FF0	0000000  // 换页延迟类型0.
#define   FF1	0040000  // 换页延迟类型1.

/* c_cflag bit meaning */
#define CBAUD	0000017 	//传输速率位屏被码
#define  B0	0000000			/* hang up 挂断线路*/
#define  B50	0000001		// 波特率50
#define  B75	0000002		// 波特率75
#define  B110	0000003		// 波特率110
#define  B134	0000004		// 波特率134
#define  B150	0000005		// 波特率150
#define  B200	0000006		// 波特率200
#define  B300	0000007		// 波特率300
#define  B600	0000010		// 波特率600
#define  B1200	0000011		// 波特率1200
#define  B1800	0000012		// 波特率1800
#define  B2400	0000013		// 波特率2400
#define  B4800	0000014		// 波特率4800
#define  B9600	0000015		// 波特率9600
#define  B19200	0000016		// 波特率19200
#define  B38400	0000017		// 波特率38400
#define EXTA B19200	   		// 扩展波特率A	
#define EXTB B38400    		// 扩展波特率B
#define CSIZE	0000060		// 字符位宽度屏蔽码
#define   CS5	0000000		// 每字符5比特位
#define   CS6	0000020		// 每字符6比特位
#define   CS7	0000040		// 每字符7比特位
#define   CS8	0000060		// 每字符8比特位
#define CSTOPB	0000100		// 设置两个停止位,而不是1个
#define CREAD	0000200		// 允许接收
#define CPARENB	0000400		// 开启输出时产生奇偶位、输入时进行奇偶校险
#define CPARODD	0001000		// 输入/输入校险是奇校验
#define HUPCL	0002000		// 最后进程关闭后挂断
#define CLOCAL	0004000		// 忽路调制解调器(modem)控制线路
#define CIBAUD	03600000		/* input baud rate (not used) 输入波特率(未使用)*/
#define CRTSCTS	020000000000		/* flow control 流控*/

#define PARENB CPARENB		// 开始输出时产生奇偶位,输入时进行奇偶校验
#define PARODD CPARODD		// 输入/奇校验

/* c_lflag bits */
#define ISIG	0000001	 // 当收到字符INTR、QUIT、SUSP或DSUSP,产生相应的信号
#define ICANON	0000002  // 开启规范摸式（熟模式）
#define XCASE	0000004  // 若设置了ICANON,则终瑞是大写字符的
#define ECHO	0000010  // 回显输入字符
#define ECHOE	0000020  // 若设置了ICANON,则ERASE/WERASE将擦除前一字符/单词
#define ECHOK	0000040  // 若设置了ICANON,则KILL字符将擦除当前行
#define ECHONL	0000100  // 如设置了ICANON,则即使ECHO没有开启也回显NL字符
#define NOFLSH	0000200  // 当生成SIGINT和SIGQUIT信号时不刷新输入输出队列,当生成SIGSUSP信号时,刷新输入队列
#define TOSTOP	0000400  // 发送SIGTTO0信号到后台进程的进程组,该后台进程试图写自己的控制终端
#define ECHOCTL	0001000  // 若设置了ECHO,则除TAB、NL、START和STOP以外的ASCII控制信号将被回显成象X式样,X值是控制符+0x40
#define ECHOPRT	0002000  // 若设置了ICANON和IECHO,则字符在擦除时将显示
#define ECHOKE	0004000  // 若设置了ICANON,则KILL通过擦除行上的所有字符被回显
#define FLUSHO	0010000  // 输出被刷新.通过键入DISCARD字符,该标志被翻转
#define PENDIN	0040000  // 当下一个字符是读时,输入队列中的所有字符将被重显
#define IEXTEN	0100000  // 开启实现时定义的输入处理

/* modem lines */		// modem线路信号符号常数
#define TIOCM_LE	0x001  // 线路允许(Line Enable)
#define TIOCM_DTR	0x002  // 数据终端就绪(Data Terminal Ready)
#define TIOCM_RTS	0x004  // 请求发送(Request to Send)
#define TIOCM_ST	0x008  // 串行数据发送(Serial Transfer)
#define TIOCM_SR	0x010  // 串行数据接收(Serial Receive)
#define TIOCM_CTS	0x020  // 清除发送(Clear To Send)
#define TIOCM_CAR	0x040  // 载波监测(Carrier Detect)
#define TIOCM_RNG	0x080  // 响铃指示(Ring indicate)
#define TIOCM_DSR	0x100  // 数据设备就绪(Data Set Ready)
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RI	TIOCM_RNG

/* tcflow() and TCXONC use these */
#define	TCOOFF		0	//挂起输出
#define	TCOON		1	//重启被挂起的输出
#define	TCIOFF		2	//系统传输一个STOP字符，使设备停止向系统传输数据
#define	TCION		3	//系统传输一个START字符，使设备开始向系统传输数据

/* tcflush() and TCFLSH use these */
#define	TCIFLUSH	0	//清接收到的数据但不读
#define	TCOFLUSH	1	//清已写的数据但不传送
#define	TCIOFLUSH	2	//清接收到的数据但不读,清已写的数据但不传送

/* tcsetattr uses these */
#define	TCSANOW		0	//改变立即发生
#define	TCSADRAIN	1	//改变在所有已写的输出被传输之后发生
#define	TCSAFLUSH	2	//改变在所有已写的输出被传输之后并且在所有接收到但还没有读取的数据被丢弃之后发生

typedef int speed_t;

extern speed_t cfgetispeed(struct termios *termios_p);
extern speed_t cfgetospeed(struct termios *termios_p);
extern int cfsetispeed(struct termios *termios_p, speed_t speed);
extern int cfsetospeed(struct termios *termios_p, speed_t speed);
extern int tcdrain(int fildes);
extern int tcflow(int fildes, int action);
extern int tcflush(int fildes, int queue_selector);
extern int tcgetattr(int fildes, struct termios *termios_p);
extern int tcsendbreak(int fildes, int duration);
extern int tcsetattr(int fildes, int optional_actions,
	struct termios *termios_p);

#endif
