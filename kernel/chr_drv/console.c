/*
 *  linux/kernel/console.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	console.c
 *
 * This module implements the console io functions
 *	'void con_init(void)'
 *	'void con_write(struct tty_queue * queue)'
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl.
 */

/*
 *  NOTE!!! We sometimes disable and enable interrupts for a short while
 * (to put a word in video IO), but this will work even for keyboard
 * interrupts. We know interrupts aren't enabled when getting a keyboard
 * interrupt, as we use trap-gates. Hopefully all is well.
 * 注意！！我们有时短暂地禁止和允许中断（当输出一个字(word)到视频IO),但
 * 即使对于键盘中断这也是可以工作的。因为我们使用陷门，所以我们知道在处理
 * 一个键盘中断过程时中断被禁止。希望一切均正常。
 */

/*
 * Code to check for different video-cards mostly by Galen Hunt,
 * <g-hunt@ee.utah.edu>
 */

#include <linux/sched.h>
#include <linux/tty.h>		// tty头文件，定义有关tty_io,串行通信方面的参数常数
#include <asm/io.h>			// io头文件,定义硬件端口输入/输出宏汇编语句
#include <asm/system.h>

/*
 * These are set up by the setup-routine at boot-time:
 这些是seup程序在引导启动系统时设置的参数：
参见对boot/setup.s的注释和setup程序读取并保留的系统参数表。
 */

#define ORIG_X			(*(unsigned char *)0x90000)		//初始光标列号
#define ORIG_Y			(*(unsigned char *)0x90001)		//初始光标行号
#define ORIG_VIDEO_PAGE		(*(unsigned short *)0x90004)//显示页面
#define ORIG_VIDEO_MODE		((*(unsigned short *)0x90006) & 0xff)			//显示模式
#define ORIG_VIDEO_COLS 	(((*(unsigned short *)0x90006) & 0xff00) >> 8)	//字符列数
#define ORIG_VIDEO_LINES	(25)											//字符行数
#define ORIG_VIDEO_EGA_AX	(*(unsigned short *)0x90008)					//?
#define ORIG_VIDEO_EGA_BX	(*(unsigned short *)0x9000a)					//显示内存大小和色彩模式
#define ORIG_VIDEO_EGA_CX	(*(unsigned short *)0x9000c)					//显示卡特性参数
// 定义显示器单色/彩色 显示模式类型符号常数
#define VIDEO_TYPE_MDA		0x10	/* Monochrome Text Display	*/			//单色文本
#define VIDEO_TYPE_CGA		0x11	/* CGA Display 			*/				//CGA显示器
#define VIDEO_TYPE_EGAM		0x20	/* EGA/VGA in Monochrome Mode	*/		//EGA/VGA单色
#define VIDEO_TYPE_EGAC		0x21	/* EGA/VGA in Color Mode	*/			//彩色

#define NPAR 16							// 转义字符序列中最大参数的个数

extern void keyboard_interrupt(void);	// 键盘中断处理程序

static unsigned char	video_type;		/* Type of display being used	*/		//显示器类型
static unsigned long	video_num_columns;	/* Number of text columns	*/		//屏幕文本列数
static unsigned long	video_size_row;		/* Bytes per row		*/			//屏幕文本每行使用的字节数
static unsigned long	video_num_lines;	/* Number of test lines		*/		//屏幕文本行数
static unsigned char	video_page;		/* Initial video page		*/			//初始显示页面
static unsigned long	video_mem_start;	/* Start of video RAM		*/		//显示内存起始地址
static unsigned long	video_mem_end;		/* End of video RAM (sort of)	*/	//显示内存结束地址
static unsigned short	video_port_reg;		/* Video register select port	*/	//显示控制索引寄存器端口
static unsigned short	video_port_val;		/* Video register value port	*/	//显示控制数据寄存器端口
static unsigned short	video_erase_char;	/* Char+Attrib to erase with	*/	//擦除字符属性及字符(0x0720)

// 一下这些变量用于屏幕卷屏操作 (origin表示移动的虚拟窗口左上角原点内存地址)
static unsigned long	origin;		/* Used for EGA/VGA fast scroll	*/	// 用于EGA/VGA快速滚屏,滚屏起始内存地址
static unsigned long	scr_end;	/* Used for EGA/VGA fast scroll	*/	// 滚屏末端内存地址
static unsigned long	pos;											// 当前光标对应的显存位置
static unsigned long	x,y;											// 当前光标位置
static unsigned long	top,bottom;										// 滚动时项行行号,底行行号
static unsigned long	state=0;										// state用于标明处理ESC转义序列时的当前步骤。par[]用于存放ESC序列的中间处理参数 ANSI转义字符序列处理状态
static unsigned long	npar,par[NPAR];									// ANSI转义字符序列参数个数和参数数组
static unsigned long	ques=0;											// 收到问号字符标志
static unsigned char	attr=0x07;										// 字符属性(黑底白字)

static void sysbeep(void);												// 系统蜂鸣函数

/*
 * this is what the terminal answers to a ESC-Z or csi0c		// 下面是终端回应ESC-Z或csi0c请求的应答(=vt100响应)
 * query (= vt100 response).
 */
// csi-控制序列导码(Control Sequence Introducer)。
// 主机通过发送不带参数或参数是0的设备属性(DA)控制序列('ESC[c'或'ESC[0c')
// 要求终端应答一个设备属性控制序列(ESC Z的作用与此相同)，终端则发送以下序列来响应
// 主机。该序列（即’ESC[?1:2c')表示终端是高级视频终端。
#define RESPONSE "\033[?1;2c"

// origin---------(x)-----
// |
// |(y)           x,y
// |

/* NOTE! gotoxy thinks x==video_num_columns is ok */
// 跟踪光标当前位置。
// 参数：new_x-光标所在列号：new_y-光标所在行号。
// 更新当前光标位置变量x,y,并修正光标在显示内存中的对应位置pos
static inline void gotoxy(unsigned int new_x,unsigned int new_y)
{
	// 首先检查参数的有效性。如果给定的光标列号超出显示器列数，或者光标行号不低于显示的
	// 最大行数，则退出。否则就更新当前光标变量和新光标位置对应在显示内存中位置pos。
	if (new_x > video_num_columns || new_y >= video_num_lines)
		return;
	x=new_x;
	y=new_y;
	pos=origin + y*video_size_row + (x<<1);		//1列用2个字节表示，所以x<<1
}

// 设置滚屏起始显示内存地址
static inline void set_origin(void)
{
// 首先向显示寄存器选择端口video_port_reg输出12，即选择显示控制数据寄存器rl2,然后
// 写入卷屏起始地址高字节。向右移动9位，表示向右移动8位再除以2（屏幕上1个字符用2
// 字节表示)。再选择显示控制数据寄存器13，然后写入卷屏起始地址低字节。向右移动1位
// 表示除以2，同样代表屏幕上1个字符用2字节表示。输出值是相对于默认显示内存起始位置
// video_mem_start操作的，例如对于彩色模式，viedo_mem_start=物理内存地址0xb8000.
	cli();
	outb_p(12, video_port_reg);			// 选择数据寄存器r12,输出卷屏起始位置高字节
	outb_p(0xff&((origin-video_mem_start)>>9), video_port_val);
	outb_p(13, video_port_reg);			// 选择数据寄存器r13,输出卷屏起始位置低字节
	outb_p(0xff&((origin-video_mem_start)>>1), video_port_val);
	sti();
}

// 向上卷动一行。
// 将屏幕滚动区域中内容向下移动一行，并在区域顶出现的新行上添加空格字符。滚屏区域必须
// 起码是2行或2行以上。参见程序列表后说明。
static void scrup(void)
{
	// 首先判断显示卡类型。对于EGA/VGA卡，我们可以指定屏内行范围（区域）进行滚屏操作
	// 而NDA单色显示卡只能进行整屏滚屏操作。该函数对EGA和NDA显示类型进行分别处理。如果
	// 显示类型是EGA,则还分为整屏窗口移动和区域内窗口移动。这里首先处理显示卡是EGA/VGA
	// 显示类型的情况。
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
		// 如果移动起始行top=0,移动最底行bottom=video_num1ines=25,则表示整屏窗口向下
		// 移动。于是把整个屏幕窗口左上角对应的起始内存位置origin调整为向下移一行对应的内存
		// 位置，同时也跟踪调整当前光标对应的内存位置以及屏幕末行末端字符指针scr_end的位置。
		// 最后把新屏幕滚动窗口内存起始位置值origin写入显示控制器中。
		if (!top && bottom == video_num_lines) {
			origin += video_size_row;
			pos += video_size_row;
			scr_end += video_size_row;
			// 如果屏幕窗口末端所对应的显示内存指针scr_end超出了实际显示内存末端，则将屏幕内容
			// 除第一行以外所有行对应的内存数据移动到显示内存的起始位置video_mem_start处，并在
			// 整屏窗口向下移动出现的新行上填入空格字符。然后根据屏幕内存数据移动后的情况，重新
			// 调整当前屏幕对应内存的起始指针、光标位置指针和屏幕末带对应内存指针scr_end,
			// 这段嵌入汇缩程序首先将（屏幕字符行数-1）行对应的内存数据移动到显示内存起始位置
			// video_mem_start处，然后在随后的内存位置处添加一行空格（擦除）字符数据。
			// %0-eax(擦除字符+属性)：%1-ecx((屏幕字符行数-1)*行所对应的字符数/2，以长字移动)，
			// %2-edi(显示内存起始位置video_mem_start)) %3-esi(屏幕窗口内存起始位置origin).
			// 移动方向：[edi]->[esi],移动ecx个长字。
			if (scr_end > video_mem_end) {
				__asm__("cld\n\t"		//请方向位
					"rep\n\t"
					"movsl\n\t"			//数据移动到显存起始处
					"movl _video_num_columns,%1\n\t"
					"rep\n\t"			//在新行上填入空格字符
					"stosw"
					::"a" (video_erase_char),
					"c" ((video_num_lines-1)*video_num_columns>>1),
					"D" (video_mem_start),
					"S" (origin)
					:"cx","di","si");
				scr_end -= origin-video_mem_start;
				pos -= origin-video_mem_start;
				origin = video_mem_start;
			// 如果调整后的屏幕末端对应的内存指针scr_end没有超出显示内存的术端video mem_end,
			// 则只需在新行上填入擦除字符（空格字符）。
			// %0-eax(擦除宁符+属性)：%1-eex(屏幕字符行数)：%2-edi(最后1行开始处对应内存位置)
			} else {
				__asm__("cld\n\t"
					"rep\n\t"
					"stosw"					// 填入擦除字符(空格字符)
					::"a" (video_erase_char),
					"c" (video_num_columns),
					"D" (scr_end-video_size_row)
					:"cx","di");
			}
			set_origin();// 然后把新屏幕滚动窗口内存起始位置值origin写入显示控制器中
		// 否则表示不是整屏移动。即表示从指定行top开始到bottom区域中的所有行向上移动1行，
		// 指定行top被删除。此时直接将屏幕从指定行top到屏幕末端所有行对应的显示内存数据向
		// 上移动1行，并在最下面新出现的行上填入擦除字符。
		// %0·eax(擦除字符+属性)：1一ecx(top行下1行开始到bottom行所对应的内存长字数)：
		// %2-edi(top行所处的内存位置)：3一esi(top+1行所处的内存位置)。
		} else {
			__asm__("cld\n\t"
				"rep\n\t"					// 循环操作,将top+1到bottom行
				"movsl\n\t"					// 所对应的内存块移动到top行开始处
				"movl _video_num_columns,%%ecx\n\t"
				"rep\n\t"					// 在新行上填入擦除字符
				"stosw"
				::"a" (video_erase_char),
				"c" ((bottom-top-1)*video_num_columns>>1),
				"D" (origin+video_size_row*top),
				"S" (origin+video_size_row*(top+1))
				:"cx","di","si");
		}
	}
	else		/* Not EGA/VGA */
	{
		// 如果显示类型不是EGA(而是MDA),则执行下面移动操作。因为MDA显示控制卡只能整屏
		// 动，并且会自动调整超出显示范围的情况，即会自动翻卷指针，所以这里不对屏幕内容对应内
		// 存超出显示内存的情况单独处理。处理方法与EG非整屏移动情况完全一样。
		__asm__("cld\n\t"
			"rep\n\t"
			"movsl\n\t"
			"movl _video_num_columns,%%ecx\n\t"
			"rep\n\t"
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom-top-1)*video_num_columns>>1),
			"D" (origin+video_size_row*top),
			"S" (origin+video_size_row*(top+1))
			:"cx","di","si");
	}
}
// 向下卷动一行。
// 将屏幕滚动窗口向上移动一行，相应屏幕滚动区域内容向下移动1行。并在移动开始行的上
// 方出现一新行。参见程序列表后说明。处理方法与scrup相似，只是为了在移动显示内存
// 数据时不会出现数据覆盖的间题，复制操作是以逆向进行的，即先从屏幕倒数第2行的最后
// 一个字符开始复制到最后一行，再将倒数第3行复制到倒数第2行等等。因为此时对EGA/
// VGA显示类型和NDA类型的处理过程完全一样，所以该函数实际上没有必要写两段相同的代
// 码。即这里if和else语句块中的操作完全一样！
static void scrdown(void)
{
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
		// %0一eax(擦除字符+属性)：%1-ecx(top行到bottom-1行的行数所对应的内存长字数)
		// %2-edi(窗口右下角最后一个长字位置)：%3-esi(窗口倒数第2行最后一个长字位置)
		// 移动方向：[esi]->[edi],移动ecx个长字。
		__asm__("std\n\t"
			"rep\n\t"
			"movsl\n\t"
			"addl $2,%%edi\n\t"	/* %edi has been decremented by 4 */
			"movl _video_num_columns,%%ecx\n\t"
			"rep\n\t"
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom-top-1)*video_num_columns>>1),
			"D" (origin+video_size_row*bottom-4),
			"S" (origin+video_size_row*(bottom-1)-4)
			:"ax","cx","di","si");
	}
	else		/* Not EGA/VGA */
	{
		__asm__("std\n\t"
			"rep\n\t"
			"movsl\n\t"
			"addl $2,%%edi\n\t"	/* %edi has been decremented by 4 */
			"movl _video_num_columns,%%ecx\n\t"
			"rep\n\t"
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom-top-1)*video_num_columns>>1),
			"D" (origin+video_size_row*bottom-4),
			"S" (origin+video_size_row*(bottom-1)-4)
			:"ax","cx","di","si");
	}
}
// 光标位置下移一行(lf-line feed换行)。
// 如果光标没有处在最后一行上，则直接修改光标当前行变量y++,并调整光标对应显示内存
// 位置pos(加上一行字符所对应的内存长度)。否则就需要将屏幕窗口内容上移一行。
// 函数名称lf(line feed换行)是指处理控制字符LF。
static void lf(void)
{
	if (y+1<bottom) {
		y++;
		pos += video_size_row;
		return;
	}
	scrup();
}
// 光标在同列上移一行。
// 如果光标不在屏幕第一行上，则直接修改光标当前行标量y--，并调整光标对应显示内存位置
// pos,减去屏幕上一行字符所对应的内存长度字节数。否则需要将屏幕窗口内容下移一行。
// 函数名称ri(reverse index反向索引)是指控制字符RI或转义序列ESC M·
static void ri(void)
{
	if (y>top) {
		y--;
		pos -= video_size_row;
		return;
	}
	scrdown();
}

// 光标回到第1列(0列)。
// 调整光标对应内存位置pos。光标所在列号*2即是0列到光标所在列对应的内存字节长度。
// 函数名称cr(carriage return回车)指明处理的控制字符是回车字符。
static void cr(void)
{
	pos -= x<<1;
	x=0;
}
// 擦除光标前一字符（用空格替代）(del-delete删除)。
// 如果光标没有处在0列，则将光标对应内存位置pos后退2字节（对应屏幕上一个字符），
// 然后将当前光标变量列值减1，并将光标所在位置处字符擦除。
static void del(void)
{
	if (x) {
		pos -= 2;
		x--;
		*(unsigned short *)pos = video_erase_char;
	}
}

// 删除屏幕上与光标位置相关的部分。
// ANSI控制序列：'ESC[Ps J'(Ps=0-删除光标处到屏幕底端：1-刑除屏幕开始到光标处：
// 2~整屏删除)。本函数根据指定的控制序列具体参数值，执行与光标位置相关的删除操作，
// 并且在擦除字符或行时光标位置不变，
// 函数名称csi_J(CSI一Control Sequence Introducer,即控制序列引导码)指明对控制
// 序列“CSI Ps J”进行处理。
// 参数：par 对应上面控制序列中Ps的值。
static void csi_J(int par)
{
	long count __asm__("cx");
	long start __asm__("di");

	switch (par) {
		case 0:	/* erase from cursor to end of display */
			count = (scr_end-pos)>>1;		// 擦除光标到屏幕底端所有字符
			start = pos;
			break;
		case 1:	/* erase from start to cursor */
			count = (pos-origin)>>1;		// 删除从屏幕开始到光标处的字符
			start = origin;
			break;
		case 2: /* erase whole display */	// 删除整个屏幕上的所有字符
			count = video_num_columns * video_num_lines;
			start = origin;
			break;
		default:
			return;
	}
	// 然后使用擦除字符填写被删除字符的地方。
	// %0-eex(删除的字符数count %1-edi(刑除操作开始地址) %2-eax(填入的擦除字符)
	__asm__("cld\n\t"
		"rep\n\t"
		"stosw\n\t"
		::"c" (count),
		"D" (start),"a" (video_erase_char)
		:"cx","di");
}

// 删除一行上与光标位置相关的部分。
// ANSI转义字符序列：'ESC[ Ps K'(Ps=0删除到行尾：1从开始删除：2整行都删除)
// 本函数根据参数擦除光标所在行的部分或所有字符。擦除操作从屏幕上移走字符但不影响其
// 他字符。擦除的字符被丢弃。在擦除字符或行时光标位置不变。
// 参数：par一对应上面控制序列中Ps的值。
static void csi_K(int par)
{
	long count __asm__("cx");
	long start __asm__("di");

	switch (par) {
		case 0:	/* erase from cursor to end of line */
			if (x>=video_num_columns)	//删除光标到行尾所有字符
				return;
			count = video_num_columns-x;
			start = pos;
			break;
		case 1:	/* erase from start of line to cursor */
			start = pos - (x<<1);		//删除从行开始到光标处
			count = (x<video_num_columns)?x:video_num_columns;
			break;
		case 2: /* erase whole line */	//将整行字符全删除
			start = pos - (x<<1);
			count = video_num_columns;
			break;
		default:
			return;
	}
	// 然后使用擦除字符填写被删除字符的地方。
	// %0-eex(删除的字符数count %1-edi(刑除操作开始地址) %2-eax(填入的擦除字符)
	__asm__("cld\n\t"
		"rep\n\t"
		"stosw\n\t"
		::"c" (count),
		"D" (start),"a" (video_erase_char)
		:"cx","di");
}
// 设置显示字符属性。
// ANSI转义序列：'ESC[Ps m。Ps=0默认属性：1加粗：4加下划线：7反显：27正显。
// 该控制序列根据参数设置字符显示属性。以后所有发送到终端的字符都将使用这里指定的属
// 性，直到再次执行本控制序列重新设置字符显示的属性。对于单色和彩色显示卡，设置的属
// 性是有区别的，这里仅作了简化处理。
void csi_m(void)
{
	int i;

	for (i=0;i<=npar;i++)
		switch (par[i]) {
			case 0:attr=0x07;break;
			case 1:attr=0x0f;break;
			case 4:attr=0x0f;break;
			case 7:attr=0x70;break;
			case 27:attr=0x07;break;
		}
}
// 设置显示光标。
// 根据光标对应显示内存位置pos,设置显示控制器光标的显示位置。
static inline void set_cursor(void)
{
	// 首先使用索引寄存器端口选择显示控制数据寄存器14（光标当前显示位置高字节），然后
	// 写入光标当前位置高字节（向右移动9位表示高字节移到低字节再除以2）。是相对于默认
	// 显示内存操作的。再使用索引寄存器选择15，并将光标当前位置低字节写入其中。
	cli();
	outb_p(14, video_port_reg);
	outb_p(0xff&((pos-video_mem_start)>>9), video_port_val);
	outb_p(15, video_port_reg);
	outb_p(0xff&((pos-video_mem_start)>>1), video_port_val);
	sti();
}

// 发送对VT100的响应序列。
// 即为响应主机请求终端向主机发送设备属性(DA)。主机通过发送不带参数或参数是0的DA
// 控制序列('ESC[0e'或'ESC Z')要求终端发送一个设备属性(DA)控制序列，终端则发
// 送85行上定义的应答序列（即 ESC[?1:2c)来响应主机的序列，该序列告诉主机本终瑞
// 是具有高级视频功能的VT100兼容终端。处理过程是将应答序列放入读缓冲队列中，并使用
// copy_to_cooked函数处理后放入辅助队列中。
static void respond(struct tty_struct * tty)
{
	char * p = RESPONSE;

	cli();
	while (*p) {
		PUTCH(*p,tty->read_q);	//应答字符放入读队列
		p++;
	}
	sti();
	copy_to_cooked(tty);		//转换成规范模式(放入辅助队列)
}

// 在光标处插入一空格字符。
// 把光标开始处的所有字符右移一格，并将擦除字符插入在光标所在处
static void insert_char(void)
{
	int i=x;
	unsigned short tmp, old = video_erase_char;	// 擦除字符(加属性)
	unsigned short * p = (unsigned short *) pos;// 光标对应内存位置

	while (i++<video_num_columns) {
		tmp=*p;
		*p=old;
		old=tmp;
		p++;
	}
}
// 在光标处插入一行。
// 将屏幕窗口从光标所在行到窗口底的内容向下卷动一行。光标将处在新的空行上。
static void insert_line(void)
{
	int oldtop,oldbottom;

	// 首先保存屏幕窗口卷动开始行top和最后行bottom值，然后从光标所在行让屏幕内容向下
	// 滚动一行。最后恢复屏幕窗口卷动开始行top和最后行bottom的原来值。
	oldtop=top;
	oldbottom=bottom;			//设置屏幕卷动开始行和结束行
	top=y;
	bottom = video_num_lines;
	scrdown();					//从光标开始，屏幕内容向下滚动一行
	top=oldtop;
	bottom=oldbottom;
}
// 删别除一个字符。
// 删除光标处的一个字符，光标右边的所有字符左移一格。
static void delete_char(void)
{
	int i;
	unsigned short * p = (unsigned short *) pos;

	// 如果光标的当前列位置x超出屏幕最右列，则返回。否则从光标右一个字符开始到行末所有
	// 字符左移一格。然后在最后一个字符处填入擦除字符。
	if (x>=video_num_columns)
		return;
	i = x;
	while (++i < video_num_columns) {// 光标右所有字符左移1格
		*p = *(p+1);
		p++;
	}
	*p = video_erase_char;			// 最后填入擦除字符
}

// 删除光标所在行。
// 删除光标所在的一行，并从光标所在行开始屏幕内容上卷一行
static void delete_line(void)
{
	int oldtop,oldbottom;
	// 首先保存屏幕卷动开始行top和最后行bottom值，然后从光标所在行让屏幕内容向上滚动
	// 一行。最后恢复屏幕卷动开始行top和最后行bottom的原来值。
	oldtop=top;
	oldbottom=bottom;
	top=y;
	bottom = video_num_lines;
	scrup();
	top=oldtop;
	bottom=oldbottom;
}

// 在光标处插入nr个字符。
// ANSI转义字符序列：'ESC[ Pn @。在当前光标处插入1个或多个空格字符。P是插入的宁
// 符数。默认是1。光标将仍然处于第1个插入的空格字符处。在光标与右边界的字符将右移。
// 超过右边界的字符将被丢失。
// 参数nr=转义字符序列中的参数n。
static void csi_at(unsigned int nr)
{
	// 如果插入的字符数大于一行字符数，则截为一行字符数：若插入字符数r为0，则插入1个
	// 字符。然后循环插入指定个空格字符。
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_char();
}
// 在光标位置处插入nr行。
// NSI转义字符序列：'ESC[Pn L'。该控制序列在光标处插入1行或多行空行。操作完成后
// 光标位置不变。当空行被插入时，光标以下滚动区域内的行向下移动。滚动出显示页的行就
// 丢失。
// 参数nr=转义字符序列中的参数Pn
static void csi_L(unsigned int nr)
{
	// 如果插入的行数大于屏幕最多行数，则截为屏幕显示行数：若插入行数nr为0，则插入1行
	// 然后循环插入指定行数r的空行
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_line();
}

// 删除光标处的nr个字符。
// ANSI转义序列：'ESC[PnP'。该控制序列从光标处删除Pn个字符。当一个字符被删除时，
// 光标右所有字符都左移。这会在右边界处产生一个空字符。其属性应该与最后一个左移字符
// 相同，但这里作了简化处理，仅使用字符的默认属性（黑底白字空格0x0720)来设置空字符。
// 参数nr=转义字符序列中的参数Pn。
static void csi_P(unsigned int nr)
{
	// 如果删除的字符数大于一行字符数，则截为一行字符数：若刑除字符数为0，则删除1个
	// 字符。然后循环删除光标处指定字符数nr。
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		delete_char();
}

// 删除光标处的nr行。
// ANSI转义序列：'ESC[Pn M 该控制序列在滚动区域内，从光标所在行开始删除1行或多
// 行。当行被删除时，滚动区域内的被删行以下的行会向上移动，并且会在最底行添加1空行。
// 若P大于显示页上剩余行数，则本序列仅别除这些剩余行，并对滚动区域外不起作用。
// 参数nr=转义字符序列中的参数Pn。
static void csi_M(unsigned int nr)
{
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr=1;
	while (nr--)
		delete_line();
}

static int saved_x=0;		//保存的光标列号
static int saved_y=0;		//保存的光标行号

// 保存当前光标位置
static void save_cur(void)
{
	saved_x=x;
	saved_y=y;
}
// 恢复保存的光标位置
static void restore_cur(void)
{
	gotoxy(saved_x, saved_y);
}

// 控制台写函数。
// 从终端对应的tty写缓冲队列中取字符，针对每个字符进行分析。若是控制字符或转义或控制
// 序列，则进行光标定位、字符删除等的控制处理：对于普通字符就直接在光标处显示。
// 参数tty是当前控制台使用的tty结构指针。
void con_write(struct tty_struct * tty)
{
	int nr;
	char c;

	// 首先取得写缓冲队列中现有字符数，然后针对队列中的每个字符进行处理。在处理每个字
	// 符的循环过程中，首先从写队列中取一字符c,根据前面处理字符所设置的状态state分步
	// 峰进行处理。状态之回的转换关系为：
	// stae=0:初始状态，或者原是状态4：或者原是状态1，但字符不是'['：
	// 		1:原是状态0，并且字符是转义字符ESC(0x1b=033=27)。处理后恢复状态0。
	// 		2:原是状态1，并且字符是”[：
	// 		3:原是状卷2，或者原是状态3，并且字符是：或数字。
	// 		4:原是状态3，并且字符不是”：’或数字：处理后恢复状态0。
	nr = CHARS(tty->write_q);
	while (nr--) {
		GETCH(tty->write_q,c);
		switch(state) {
			case 0:
				// 如果从写队列中取出的字符是普通显示字符代码，就直接从当前映射字符集中取出对应的显示
				// 字符，并放到当前光标所处的显示内存位置处，即直接显示该字符。然后把光标位置右移一个
				// 字符位置。具体地，如果字符不是控制字符也不是扩展字符，即(31<c<127)，那么，若当前光
				// 标处在行末端或末端以外，则将光标移到下行头列。并调整光标位置对应的内存指针0s。然
				// 后将字符c写到显示内存中pos处，并将光标右移1列，同时也将pos对应地移动2个字节。
				if (c>31 && c<127) {
					if (x>=video_num_columns) {
						x -= video_num_columns;
						pos -= video_size_row;
						lf();
					}
					__asm__("movb _attr,%%ah\n\t"
						"movw %%ax,%1\n\t"
						::"a" (c),"m" (*(short *)pos)
						:"ax");
					pos += 2;
					x++;
				// 如果字符c是转义字符ESC,则转换状态state到1
				} else if (c==27)
					state=1;
				// 如果c是换行符LF(10),或垂直制表符VT(11),或换页符FF(12),则光标移动到下1行
				else if (c==10 || c==11 || c==12)
					lf();
				// 如果c是回车符CR(13),则将光标移动到头列(0列)
				else if (c==13)
					cr();
				// 如果c是DEL(127),则将光标左边字符擦除（用空格字符替代），并将光标移到被擦除位置
				else if (c==ERASE_CHAR(tty))
					del();
				// 如果c是BS(backspace,8),则将光标左移1格，并相应调整光标对应内存位置指针pos
				else if (c==8) {
					if (x) {
						x--;
						pos -= 2;
					}
				// 如果字符c是水平制表符HT(9),则将光标移到8的倍数列上。若此时光标列数超出屏幕最大列数，则将光标移到下一行上。
				} else if (c==9) {
					c=8-(x&7);
					x += c;
					pos += c<<1;
					if (x>video_num_columns) {
						x -= video_num_columns;
						pos -= video_size_row;
						lf();
					}
					c=9;
				// 如果字符c是响铃符BEL(7),则调用蜂鸣函数，是扬声器发声
				} else if (c==7)
					sysbeep();
				break;
			case 1:
				state=0;
				if (c=='[')			// ESC [ : CSI序列
					state=2;
				else if (c=='E')	// ESC E : 光标下移1行回0列
					gotoxy(0,y+1);
				else if (c=='M')	// ESC M : 光标上移1行
					ri();
				else if (c=='D')	// 下一一行
					lf();
				else if (c=='Z')	// 设备属性查询
					respond(tty);
				else if (x=='7')	// 保存光标位置
					save_cur();
				else if (x=='8')	// 恢复光标位置
					restore_cur();
				break;
			// 如果在状态1（是转义字符ESC)时收到字符’['，则表明是一个控制序列引导码CSI,于是
			// 转到这里状态2来处理。首先对ESC转义字符序列保存参数的数组par[]清零，索引变量
			// par指向首项，并且设置状态为3。若此时字符不是?，则直接转到状态3去处理，若此时
			// 字符是?，说明这个序列是终端设备私有序列，后面会有一个功能字符。于是去读下一字符
			// 再到状态3处理代码处。否则直接进入状态3继续处理。
			case 2:
				for(npar=0;npar<NPAR;npar++)
					par[npar]=0;
				npar=0;
				state=3;
				if (ques=(c=='?'))
					break;
			// 状态3用于把转义字符序列中的数字字符转换成数值保存在par[]数组中。如果原是状态2，
			// 或者原来就是状态3，但原字符是：或数字，则在状态3处理。此时，如果字符c是分号
			// ”:’,并且数组pr未满，则索引值加1，准备处理下一个字符。
			case 3:
				if (c==';' && npar<NPAR-1) {
					npar++;
					break;
				// 否则，如果字符c是数字字符’0'-'9'，则将该字符转换成数值并与npar所索引的项组成
				// 10进制数，并准备处理下一个字符。否则就直接转到状态4。
				} else if (c>='0' && c<='9') {
					par[npar]=10*par[npar]+c-'0';
					break;
				} else state=4;
			// 状态4是处理转义字符序列的最后一步。根据前面几个状态的处理我们已经获得了转义字符
			// 序列的前几部分，现在根据参数字符串中最后一个字符（命令）来执行相关的操作。如果原
			// 状态是状态3，并且字符不是'：'或数字，则转到状态4处理。首先复位状态state=0。
			case 4:
				state=0;
				switch(c) {
					case 'G': case '`':			// CSI Pn G 光标水平移动
						if (par[0]) par[0]--;
						gotoxy(par[0],y);
						break;
					case 'A':					// 光标上移
						if (!par[0]) par[0]++;	// 第1个参数代表光标上移的行数。若参数为0则上移1行
						gotoxy(x,y-par[0]);
						break;
					case 'B': case 'e':			// 光标下移
						if (!par[0]) par[0]++;
						gotoxy(x,y+par[0]);
						break;
					case 'C': case 'a':			// 右移
						if (!par[0]) par[0]++;
						gotoxy(x+par[0],y);
						break;
					case 'D':					// 左移
						if (!par[0]) par[0]++;
						gotoxy(x-par[0],y);
						break;
					case 'E':					// 下移回0列
						if (!par[0]) par[0]++;
						gotoxy(0,y+par[0]);
						break;
					case 'F':					// 上移回0列
						if (!par[0]) par[0]++;
						gotoxy(0,y-par[0]);
						break;
					case 'd':					// 在当前列置行位置
						if (par[0]) par[0]--;
						gotoxy(x,par[0]);
						break;
					case 'H': case 'f':			// 光标定位
						if (par[0]) par[0]--;
						if (par[1]) par[1]--;
						gotoxy(par[1],par[0]);
						break;
					case 'J':
						csi_J(par[0]);
						break;
					case 'K':
						csi_K(par[0]);
						break;
					case 'L':
						csi_L(par[0]);
						break;
					case 'M':
						csi_M(par[0]);
						break;
					case 'P':
						csi_P(par[0]);
						break;
					case '@':
						csi_at(par[0]);
						break;
					case 'm':
						csi_m();
						break;
					case 'r':			// 设置滚屏的上下界
						if (par[0]) par[0]--;
						if (!par[1]) par[1] = video_num_lines;
						if (par[0] < par[1] &&
						    par[1] <= video_num_lines) {
							top=par[0];
							bottom=par[1];
						}
						break;
					case 's':
						save_cur();
						break;
					case 'u':
						restore_cur();
						break;
				}
		}
	}
	set_cursor();	// 最后根据上面设置的光标位置，向显示控制器发送光标显示位置
}

/*
 *  void con_init(void);
 *
 * This routine initalizes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequece.
 *
 * Reads the information preserved by setup.s to determine the current display
 * type and sets everything accordingly.
 * 这个子程序初始化控制台中断，其他什么都不做。如果你想让屏幕干净的话，就使用
// 适当的转义字符序列调用tty_write函数。
// 读取setup.s程序保存的信息，用以确定当前显示器类型，并且设置所有相关参数。
 */

// 控制台初始化程序。在init/main.c中被调用，
// 该函数首先根据setup.s程序取得的系统硬件参数初始化设置几个本函数专用的静态全局变
// 量。然后根据显示卡模式（单色还是彩色显示）和显示卡类型(EGA/VGA还是CGA)分别设
// 置显示内存起始位置以及显示索寄存器和显示数值寄存器瑞口号。最后设置键盘中断陷阱
// 描述符并复位对键盘中断的屏蔽位，以允许键盘开始工作。
void con_init(void)
{
	register unsigned char a;
	char *display_desc = "????";
	char *display_ptr;

	video_num_columns = ORIG_VIDEO_COLS;
	video_size_row = video_num_columns * 2;
	video_num_lines = ORIG_VIDEO_LINES;
	video_page = ORIG_VIDEO_PAGE;
	video_erase_char = 0x0720;
	
	if (ORIG_VIDEO_MODE == 7)			/* Is this a monochrome display? */
	{
		video_mem_start = 0xb0000;
		video_port_reg = 0x3b4;
		video_port_val = 0x3b5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			video_type = VIDEO_TYPE_EGAM;
			video_mem_end = 0xb8000;
			display_desc = "EGAm";
		}
		else
		{
			video_type = VIDEO_TYPE_MDA;
			video_mem_end	= 0xb2000;
			display_desc = "*MDA";
		}
	}
	else								/* If not, it is color. */
	{
		video_mem_start = 0xb8000;
		video_port_reg	= 0x3d4;
		video_port_val	= 0x3d5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			video_type = VIDEO_TYPE_EGAC;
			video_mem_end = 0xbc000;
			display_desc = "EGAc";
		}
		else
		{
			video_type = VIDEO_TYPE_CGA;
			video_mem_end = 0xba000;
			display_desc = "*CGA";
		}
	}

	/* Let the user known what kind of display driver we are using */
	// 然后我们在屏幕的右上角显示描述字符串。采用的方法是直接将字符串写到显示内存的相应
	// 位置处。首先将显示指针display_ptr指到屏幕第1行右端差4个字符处（每个字符需2个
	// 字节，因此减8)，然后循环复制字符串的字符，并且每复制1个字符都空开1个属性字节。
	display_ptr = ((char *)video_mem_start) + video_size_row - 8;
	while (*display_desc)
	{
		*display_ptr++ = *display_desc++;
		display_ptr++;
	}
	
	/* Initialize the variables used for scrolling (mostly EGA/VGA)	*/
	
	origin	= video_mem_start;
	scr_end	= video_mem_start + video_num_lines * video_size_row;
	top	= 0;
	bottom	= video_num_lines;

	// 最后初始化当前光标所在位置和光标对应的内存位置pos。并设置设置键盘中断0x21陷阱门
	// 描述符，&keyboard_interrupt是键盘中断处理过程地址。取消8259A中对键盘中断的屏蔽，
	// 允许响应键盘发出的IQ1请求信号。最后复位键盘控制器以允许键盘开始正常工作。
	gotoxy(ORIG_X,ORIG_Y);
	set_trap_gate(0x21,&keyboard_interrupt);
	outb_p(inb_p(0x21)&0xfd,0x21);	// 取消对键盘的中断屏蔽，允许IRQ1
	a=inb_p(0x61);					// 读取键盘端口0x61
	outb_p(a|0x80,0x61);			// 设置禁止键盘工作
	outb(a,0x61);					// 再允许键盘工作
}
/* from bsd-net-2: */

void sysbeepstop(void)
{
	/* disable counter 2 */
	outb(inb_p(0x61)&0xFC, 0x61);
}

int beepcount = 0;

// 开通蜂鸣。
// 8255A芯片PB端口的位1用作杨声器的开门信号：位0用作8253定时器2的门信号，该定时
// 器的输出脉冲送往扬声器，作为扬声器发声的频率。因此要使扬声器蜂鸣，需要两步：首先开
// 启PB端口(0x61)位1和位0（置位），然后设置定时器2通道发送一定的定时频率即可。
// 参见boot/setup.s程序后8259A芯片编程方法和kernel/sched.c程序后的定时器编程说明。
static void sysbeep(void)
{
	/* enable counter 2 */
	outb_p(inb_p(0x61)|3, 0x61);
	/* set command for counter 2, 2 byte write */
	outb_p(0xB6, 0x43);
	/* send 0x637 for 750 HZ */
	outb_p(0x37, 0x42);
	outb(0x06, 0x42);
	/* 1/8 second */
	beepcount = HZ/8;	
}
