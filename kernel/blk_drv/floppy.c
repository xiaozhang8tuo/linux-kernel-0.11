/*
 *  linux/kernel/floppy.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
02.12.9~修改成静态变量，以适应复位和重新校正操作。这使得某些事情
做起来较为方便(output_byte复位检查等)，并且意味着在出错时中断跳转
要少一些，所以也希望代码能更容易被理解。
 */

/*
 * This file is certainly a mess. I've tried my best to get it working,
 * but I don't like programming floppies, and I have only one anyway.
 * Urgel. I should check for more errors, and do more graceful error
 * recovery. Seems there are problems with several drives. I've tried to
 * correct them. No promises. 
 */

/*
 * 如同hd.c文件一样，该文件中的所有子程序都能够被中断调用，所以需要特别
 * 地小心。硬件中断处理程序是不能睡眠的，否则内核就会死机。因此不能
 * 直接调用”floppy-on”,而只能设置一个特珠的定时中断等。
 *
 * Also, I'm not certain this works on more than 1 floppy. Bugs may
 * abund.
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR 2
#include "blk.h"

static int recalibrate = 0;		// 是否需要重新校正磁头位置（磁头归零道）
static int reset = 0;			// 是否需要进行复位操作。
static int seek = 0;			// 是否需要执行寻道操作

// 当前数字输出寄存器DOR(Digital Output Register),定义在kernel/sched.c
// 该变量含有软驱操作中的重要标志，包括选择软驱、控制电机启动、启动复位软盘控制器以
// 及允许/禁止DMA和中断请求。请见程序列表后对DOR寄存器的说明。
extern unsigned char current_DOR;

// 字节直接数输出(嵌入汇编宏)，把值val输出到port端口
#define immoutb_p(val,port) \
__asm__("outb %0,%1\n\tjmp 1f\n1:\tjmp 1f\n1:"::"a" ((char) (val)),"i" (port))

// 这两个宏定义用于计算软驱的设备号
// 参数x是次设备号。次设备号=TYPE*4+DRIVE
#define TYPE(x) ((x)>>2)		// 软驱类型(2-1 2MB, 7-1 44MB)
#define DRIVE(x) ((x)&0x03)		// 软驱序号(0-3对应A-D)
/*
 * Note that MAX_ERRORS=8 doesn't imply that we retry every bad read
 * max 8 times - some types of errors increase the errorcount by 2,
 * so we might actually retry only 5-6 times before giving up.
 */
#define MAX_ERRORS 8

/*
 * globals used by 'result()'
 */
// 这些状态字节中各比特位的含义请参见include/linux/fdreg.h头文件
#define MAX_REPLIES 7									// FDC最多返回7字节的结果信息
static unsigned char reply_buffer[MAX_REPLIES];			// 存放FDC返回的应答结果信息
#define ST0 (reply_buffer[0])							// 结果状态字节0 
#define ST1 (reply_buffer[1])							// 结果状态字节1
#define ST2 (reply_buffer[2])							// 结果状态字节2
#define ST3 (reply_buffer[3])							// 结果状态字节3

/*
 * This struct defines the different floppy types. Unlike minix
 * linux doesn't have a "search for right type"-type, as the code
 * for that is convoluted and weird. I've got enough problems with
 * this driver as it is.
 *
 * The 'stretch' tells if the tracks need to be boubled for some
 * types (ie 360kB diskette in 1.2MB drive etc). Others should
 * be self-explanatory.
 * 对某些类型的软盘(例如在1.2MB驱动器中的360kB软盘等），'stretch'
 * 用于检测磁道是否需要特珠处理。
 */
// 软盘结构
static struct floppy_struct {
	unsigned int size, sect, head, track, stretch;	// 大小(扇区数),每磁道扇区数,磁头数,磁道数,对磁道是否需要特殊处理 
	unsigned char gap,rate,spec1;					// 扇区间隙长度(字节数),数据传输速率,参数(高4位步进速率,低四位磁头卸载时间)
} floppy_type[] = {
	{    0, 0,0, 0,0,0x00,0x00,0x00 },	/* no testing */
	{  720, 9,2,40,0,0x2A,0x02,0xDF },	/* 360kB PC diskettes */
	{ 2400,15,2,80,0,0x1B,0x00,0xDF },	/* 1.2 MB AT-diskettes */
	{  720, 9,2,40,1,0x2A,0x02,0xDF },	/* 360kB in 720kB drive */
	{ 1440, 9,2,80,0,0x2A,0x02,0xDF },	/* 3.5" 720kB diskette */
	{  720, 9,2,40,1,0x23,0x01,0xDF },	/* 360kB in 1.2MB drive */
	{ 1440, 9,2,80,0,0x23,0x01,0xDF },	/* 720kB in 1.2MB drive */
	{ 2880,18,2,80,0,0x1B,0x00,0xCF },	/* 1.44MB diskette */
};
/*
 * Rate is 0 for 500kb/s, 2 for 300kbps, 1 for 250kbps
 * Spec1 is 0xSH(步进速率|磁头卸载时间), 
 * where S is stepping rate (F=1ms, E=2ms, D=3ms etc),
 * H is head unload time (1=16ms, 2=32ms, etc)
 *
 * Spec2 is (HLD<<1 | ND), where HLD is head load time (1=2ms, 2=4 ms etc)
 * and ND is set means no DMA(不使用DMA). Hardcoded to 6 (HLD=6ms, use DMA).
 */

// floppy_interrupt是system_call.s程序中软驱中断处理过程标号。这里将在软盘初始
// 化函数floppy_init使用它初始化中断陷阱门描述符。
extern void floppy_interrupt(void);
// 这是boot/head.s第132行处定义的临时软盘缓冲区。如果请求项的缓冲区处于内存1MB
// 以上某个地方，则需要将DMA缓冲区设在临时缓冲区域处。因为8237A芯片只能在1MB地
// 址范围内寻址。
extern char tmp_floppy_area[1024];

/*
 * These are global variables, as that's the easiest way to give
 * information to interrupts. They are the data used for the current
 * request.
 */
// 下面是一些全局变量，因为这是将信息传给中断程序最简单的方式。它们用于当前请求项的数据。
static int cur_spec1 = -1;							//当前软盘参数spec1
static int cur_rate = -1;							//当前软盘转速rate
static struct floppy_struct * floppy = floppy_type;	//软盘类型结构数组指针
static unsigned char current_drive = 0;				//当前驱动器号
static unsigned char sector = 0;					//当前扇区号
static unsigned char head = 0;						//当前磁头号
static unsigned char track = 0;						//当前磁道号
static unsigned char seek_track = 0;				//寻道磁道号
static unsigned char current_track = 255;			//当前磁头所在磁道号
static unsigned char command = 0;					//命令
unsigned char selected = 0;							//软驱已选定标志,在处理请求项之前要首先选定软驱
struct task_struct * wait_on_floppy_select = NULL;	//等待选定软驱的任务队列

// 取消选定软驱。
// 如果函数参数指定的软驱当前并没有被选定，则显示警告信息。然后复位软驱已选定标志
// selected,并唤醒等待选择该软驱的任务。数字输出寄存器(DOR)的低2位用于指定选择的软驱(0-3对应A-D)
void floppy_deselect(unsigned int nr)
{
	if (nr != (current_DOR & 3))
		printk("floppy_deselect: drive not selected\n\r");
	selected = 0;						//复位软驱已选定标志
	wake_up(&wait_on_floppy_select);	//唤醒等待的任务
}

/*
 * floppy-change is never called from an interrupt, so we can relax a bit
 * here, sleep etc. Note that floppy-on tries to set current_DOR to point
 * to the desired drive, but it will probably not survive the sleep if
 * several floppies are used at the same time: thus the loop.
 * floppy-change不是从中断程序中调用的，所以这里我们可以轻松一下，睡眠等。
 * 注意floppy--on会尝试设置current_DOR指向所需的驱动器，但当同时使用几个
 * 软盘时不能睡眠:因此此时只能使用循环方式。
 */

// 检测指定软驱中软盘更换情况
// 参数nr是软驱号。如果软盘更换了则返回1，否则返回0
// 该函数首先选定参数指定的软驱nr,然后测试软盘控制器的数字输入寄存器DIR的值，以判
// 断驱动器中的软盘是否被更换过。该函数由程序fs/buffer.c中的check_disk_change函数调用
int floppy_change(unsigned int nr)
{

// 首先要让软驱中软盘旋转起来并达到正常工作转速。这需要花费一定时间。采用的方法是利
// 用kernel/sched.c中软盘定时函数do_floppy_timer进行一定的延时处理。floppy_on
// 函数则用于判断延时是否到(mon_timer[nr]==0?),若没有到则让当前进程继续睡眠等待。
// 若延时到则do_floppy_timer会唤醒当前进程
repeat:
	floppy_on(nr);	//启动并等待指定软驱nr
	// 在软盘启动（旋转）之后，我们来查看一下当前选择的软驱是不是函数参数指定的软驱nr,
	// 如果当前选择的软驱不是指定的软驱nr,并且已经选定了其他软驱，则让当前任务进入可
	// 中断等待状态，以等待其他软驱被取消选定。参见上面floppy_deselect,如果当前没
	// 有选择其他软驱或者其他软驱被取消选定而使当前任务被唤醒时，当前软驱仍然不是指定
	// 的软驱nr,则跳转到函数开始处重新循环等待。
	while ((current_DOR & 3) != nr && selected)
		interruptible_sleep_on(&wait_on_floppy_select);
	if ((current_DOR & 3) != nr)
		goto repeat;
	// 现在软盘控制器已选定我们指定的软驱。于是取数宁输入寄存器DIR的值，如果其最高
	// 位（位7）置位，则表示软盘已更换，此时即可关闭马达并返回1退出。否则关闭马达返
	// 回0退出。表示磁盘没有被更换。
	if (inb(FD_DIR) & 0x80) {
		floppy_off(nr);
		return 1;
	}
	floppy_off(nr);
	return 0;
}

// 复制内存缓冲块，共1024字节
// 从内存地址from处复制1024字节数据到地址to处
#define copy_buffer(from,to) \
__asm__("cld ; rep ; movsl" \
	::"c" (BLOCK_SIZE/4),"S" ((long)(from)),"D" ((long)(to)) \
	:"cx","di","si")

// 设置（初始化）软盘DMA通道
// 软盘中数据读写操作是使用DMA进行的。因此在每次进行数据传输之前需要设置DWA芯片
// 上专门用于软驱的通道2。
static void setup_DMA(void)
{
	long addr = (long) CURRENT->buffer;	//当前请求项缓冲区所处的内存地址

	// 首先检测请求项的缓冲区所在位置。如果缓冲区处于内存1MB以上的某个地方，则需要将
	// DMA缓冲区设在临时缀冲区域(tmp_floppy_area)处。因为8237A芯片只能在1MB地址范
	// 围内寻址。如果是写盘命令，则还需要把数据从请求项缓冲区复制到该临时区域。
	cli();
	if (addr >= 0x100000) {
		addr = (long) tmp_floppy_area;
		if (command == FD_WRITE)
			copy_buffer(CURRENT->buffer,tmp_floppy_area);
	}
	/* mask DMA 2 */
	// 接下来我们开始设置DMA通道2。在开始设置之前需要先屏蔽该通道。单通道屏蔽寄存器
	// 端口为0x0A。位0-1指定DA通道(0-3)，位2：1表示屏蔽，0表示允许请求。然后向
	// DMA控制器揣口12和11写入方式字（读盘是0x46,写盘则是0x4A)。再写入传输使用
	// 缓冲区地址addr和需要传输的字节数0x3ff(0--1023)。最后复位对DMA通道2的屏蔽，
	// 开放DMA2请求DREQ信号。
	immoutb_p(4|2,10);
	
	/* output command byte. I don't know why, but everyone (minix, */
	/* sanches & canton) output this twice, first to 12 then to 11 */
	// 下面嵌入汇缩代码向DMA控制器的“清除先后触发器”端口12和方式寄存器端口11写入方式字（读盘时是0x46,写盘是0x4A)
	// 由于各通道的地址和计数寄存器都是16位的，因此在设置他们时都需要分2次进行操作。
	// 一次访问低字节，另一次访问高字节。而实际在写哪个字节则由先后触发器的状态决定。
	// 当触发器为0时，则访问低字节：当字节触发器为1时，则访问高字节。每访问一次，
	// 该触发器的状态就变化一次。而写端口12就可以将触发器置成0状态，从而对16位寄存
	// 器的设置从低字节开始。
 	__asm__("outb %%al,$12\n\tjmp 1f\n1:\tjmp 1f\n1:\t"
	"outb %%al,$11\n\tjmp 1f\n1:\tjmp 1f\n1:"::
	"a" ((char) ((command == FD_READ)?DMA_READ:DMA_WRITE)));
	
	/* 8 low bits of addr */					// 向DMA通道2写入基/当前地址寄存器（端口4）
	immoutb_p(addr,4);
	addr >>= 8;
	/* bits 8-15 of addr */
	immoutb_p(addr,4);
	addr >>= 8;
	/* bits 16-19 of addr */					// DMA只可以在1MB内存空间内寻址，其高16-19位地址需放入页面寄存器（端口0x81)
	immoutb_p(addr,0x81);
	/* low 8 bits of count-1 (1024-1=0x3ff) */ 	// 向DMA通道2写入基/当前字节计数器值（端口5）
	immoutb_p(0xff,5);
	/* high 8 bits of count-1 */ 				// 一次共传输1024字节（两个扇区）
	immoutb_p(3,5);
	/* activate DMA 2 */
	immoutb_p(0|2,10);
	sti();
}

// 向软驱控制器输出一个字节命令或参数
// 在向控制器发送一个字节之前，控制器需要处于准备好状态，并且数据传输方向必须设置
// 成从CPU到FDC,因此函数需要首先读取控制器状态信息。这里使用了循环查询方式，以
// 作适当延时。若出错，则会设置复位标志reset
static void output_byte(char byte)
{
	int counter;
	unsigned char status;

	if (reset)
		return;
	// 循环读取主状态控制器FD_STATUS(0x34)的状态。如果所读状态是STATUS_READY并且
	// 方向位STATUS_DIR=0(CPU->FDC),则向数据端口输出指定字节。
	for(counter = 0 ; counter < 10000 ; counter++) {
		status = inb_p(FD_STATUS) & (STATUS_READY | STATUS_DIR);
		if (status == STATUS_READY) {
			outb(byte,FD_DATA);
			return;
		}
	}
	reset = 1;
	printk("Unable to send byte to FDC\n\r");
}

// 读取FDC执行的结果信息。
// 结果信息最多7个字节，存放在数组reply_buffer[]中。返回读入的结果字节数，若返回
// 值=-1，则表示出错。程序处理方式与上面函数类似。
static int result(void)
{
	int i = 0, counter, status;
	// 若复位标志已置位,则立刻退出.去执行后续程序中的复位操作。否则循环读取主状态拉制器FD_STATUS(0x34)的状态。
	if (reset)
		return -1;
	for (counter = 0 ; counter < 10000 ; counter++) {
		status = inb_p(FD_STATUS)&(STATUS_DIR|STATUS_READY|STATUS_BUSY);
		// 如果控制器状态是READY,表示已经没有数据可取，则返回已读取的字节数i。
		if (status == STATUS_READY)
			return i;
		// 如果控制器状态是方向标志置位(CPU←FDC)、已准备好、忙，表示有数据可读取。于是
		// 把控制器中的结果数据读入到应答结果数组中。最多读取MAX_REPLIES(7)个字节。
		if (status == (STATUS_DIR|STATUS_READY|STATUS_BUSY)) {
			if (i >= MAX_REPLIES)
				break;
			reply_buffer[i++] = inb_p(FD_DATA);
		}
	}
	// 如果到循环1万次结束时还不能发送，则置复位标志
	reset = 1;
	printk("Getstatus times out\n\r");
	return -1;
}

// 软盘读写出错处理函数
// 该函数根据软盘读写出错次数来确定需要采取的进一步行动。如果当前处理的请求项出错
// 次数大于规定的最大出错次数MAX_ERRORS(8次)，则不再对当前请求项作进一步的操作
// 尝试。如果读/写出错次数已经超过MAX_ERRORS/2,则需要对软驱作复位处理，于是设置
// 复位标志reset.。否则若出错次数还不到最大值的一半，则只需重新校正一下磁头位置，
// 于是设置重新校正标志recalibrate。真正的复位和重新校正处理会在后续的程序中进行。
static void bad_flp_intr(void)
{
	// 首先把当前请求项出错次数增1。如果当前请求项出错次数大于最大允许出错次数，则取
	// 消选定当前软驱，并结束该清求项（缓冲区内容没有被更新）。
	CURRENT->errors++;
	if (CURRENT->errors > MAX_ERRORS) {
		floppy_deselect(current_drive);
		end_request(0);
	}
	// 如果当前请求项出错次数大于最大允许出错次数的一半，则置复位标志，需对软驱进行复
	// 位操作，然后再试。否则软驱需重新校正一下再试。
	if (CURRENT->errors > MAX_ERRORS/2)
		reset = 1;
	else
		recalibrate = 1;
}	

/*
 * OK,下面的中断处理函数是在DMA读/写成功后调用的，这样我们就可以检查
 * 执行结果，并复制缓冲区中的数据。
 */
// 软盘读写操作中断调用函数。
// 该函数在软驱控制器操作结束后引发的中断处理过程中被调用。函数首先读取操作结果状
// 态信息，据此判断操作是否出现问题并作相应处理。如果读/写操作成功，那么若请求项
// 是读操作并且其缓冲区在内存1MB以上位置，则需要把数据从软盘临时缓冲区复制到请求
// 项的缓冲区。
static void rw_interrupt(void)
{
	// 读取FDC执行的结果信息。如果返回结果字节数不等于7，或者状态字节0,1或2中存在
	// 出错标志，那么若是写保护就显示出错信息，释放当前驱动器，并结束当前请求项。否则
	// 就执行出错计数处理。然后继续执行软盘请求项操作。以下状态的含义参见fdreg.h文件
	// (0xf8= STO_INTR | STO_SE | STO_ECE | STO_NR
	// (0xbf= ST1_EOC | ST1_CRC | ST1_OR | ST1_ND | ST1_WP | ST1_MAM 应该是Oxb7)
	// (0x73= ST2_CM | ST2_CRC | ST2_WC | ST2_BC | ST2_MAM
	if (result() != 7 || (ST0 & 0xf8) || (ST1 & 0xbf) || (ST2 & 0x73)) {
		if (ST1 & 0x02) {
			printk("Drive %d is write protected\n\r",current_drive);
			floppy_deselect(current_drive);
			end_request(0);
		} else
			bad_flp_intr();
		do_fd_request();
		return;
	}
	if (command == FD_READ && (unsigned long)(CURRENT->buffer) >= 0x100000)
		copy_buffer(tmp_floppy_area,CURRENT->buffer);	// 因为DMA只能寻址1MB, 所以读数据的结果被放在tmp处(系统内存的1MB以内)
	
	// 释放当前软驱（取消选定），执行当前请求项结束处理：唤醒等待该请求项的进行，唤醒
	// 等待空闲请求项的进程（若有的话），从软驱设备请求项链表中删除本请求项。再继续执
	// 行其他软盘请求顶操作。
	floppy_deselect(current_drive);
	end_request(1);
	do_fd_request();
}

// 设置DMA通道2并向软盘控制器输出命令和参数（输出1字节命令+0~7字节参数）
// 若reset标志没有置位，那么在该函数退出并且软盘控制器执行完相应读/写操作后就会
// 产生一个软盘中断请求，并开始执行软盘中断处理程序。
inline void setup_rw_floppy(void)
{
	


	setup_DMA();						// 初始化软盘DMA通道
	do_floppy = rw_interrupt;			// 置软盘中断调用函数指针。
	output_byte(command);				// 发送命令字节。
	output_byte(head<<2 | current_drive);	// 参数:磁头号+驱动器号
	output_byte(track);					// 磁道号
	output_byte(head);					// 磁头号
	output_byte(sector);				// 起始扇区
	output_byte(2);		/* sector size = 512 */
	output_byte(floppy->sect);			// 每磁道扇区数
	output_byte(floppy->gap);			// 扇区间隔长度
	output_byte(0xFF);	/* sector size (0xff when n!=0 ?) */
	if (reset)
		do_fd_request();
}

/*
 * This is the routine called after every seek (or recalibrate) interrupt
 * from the floppy controller. Note that the "unexpected interrupt" routine
 * also does a recalibrate, but doesn't come here.
 */
// 寻道处理结束后中断过程中调用的C函数。
// 首先发送检测中断状态命令，获得状态信息ST0和磁头所在磁道信息。若出错则执行错误
// 计数检测处理或取消本次软盘操作请求项。否则根据状态信息设置当前磁道变量，然后调
// 用函数seup_w_loppy设置DMA并输出软盘读写命令和参数，
static void seek_interrupt(void)
{
	/* sense drive status */
	// 首先发送检测中断状态命令，以获取寻道操作执行的结果。该命令不带参数。返回结果信
	// 息是两个字节：ST0和磁头当前磁道号。然后读取FDC执行的结果信息。如果返回结果字
	// 节数不等于2，或者ST0不为寻道结束，或者磁头所在磁道(ST1)不等于设定磁道，则说
	// 明发生了错误。于是执行检测错误计数处理，然后继续执行软盘请求项或执行复位处理。
	output_byte(FD_SENSEI);
	if (result() != 2 || (ST0 & 0xF8) != 0x20 || ST1 != seek_track) {
		bad_flp_intr();
		do_fd_request();
		return;
	}
	// 若寻道操作成功，则继续执行当前请求项的软盘操作，即向软盘控制器发送命令和参数
	current_track = ST1;		//设置当前磁道
	setup_rw_floppy();			//设置DMA并输出软盘操作命令和参数
}

/*
 * This routine is called when everything should be correctly set up
 * for the transfer (ie floppy motor is on and the correct floppy is
 * selected).
 */
static void transfer(void)
{
	if (cur_spec1 != floppy->spec1) {
		cur_spec1 = floppy->spec1;
		output_byte(FD_SPECIFY);
		output_byte(cur_spec1);		/* hut etc */
		output_byte(6);			/* Head load time =6ms, DMA */
	}
	if (cur_rate != floppy->rate)
		outb_p(cur_rate = floppy->rate,FD_DCR);
	if (reset) {
		do_fd_request();
		return;
	}
	if (!seek) {
		setup_rw_floppy();
		return;
	}
	do_floppy = seek_interrupt;
	if (seek_track) {
		output_byte(FD_SEEK);
		output_byte(head<<2 | current_drive);
		output_byte(seek_track);
	} else {
		output_byte(FD_RECALIBRATE);
		output_byte(head<<2 | current_drive);
	}
	if (reset)
		do_fd_request();
}

/*
 * Special case - used after a unexpected interrupt (or reset)
 */
static void recal_interrupt(void)
{
	output_byte(FD_SENSEI);
	if (result()!=2 || (ST0 & 0xE0) == 0x60)
		reset = 1;
	else
		recalibrate = 0;
	do_fd_request();
}

void unexpected_floppy_interrupt(void)
{
	output_byte(FD_SENSEI);
	if (result()!=2 || (ST0 & 0xE0) == 0x60)
		reset = 1;
	else
		recalibrate = 1;
}

static void recalibrate_floppy(void)
{
	recalibrate = 0;
	current_track = 0;
	do_floppy = recal_interrupt;
	output_byte(FD_RECALIBRATE);
	output_byte(head<<2 | current_drive);
	if (reset)
		do_fd_request();
}

static void reset_interrupt(void)
{
	output_byte(FD_SENSEI);
	(void) result();
	output_byte(FD_SPECIFY);
	output_byte(cur_spec1);		/* hut etc */
	output_byte(6);			/* Head load time =6ms, DMA */
	do_fd_request();
}

/*
 * reset is done by pulling bit 2 of DOR low for a while.
 */
static void reset_floppy(void)
{
	int i;

	reset = 0;
	cur_spec1 = -1;
	cur_rate = -1;
	recalibrate = 1;
	printk("Reset-floppy called\n\r");
	cli();
	do_floppy = reset_interrupt;
	outb_p(current_DOR & ~0x04,FD_DOR);
	for (i=0 ; i<100 ; i++)
		__asm__("nop");
	outb(current_DOR,FD_DOR);
	sti();
}

static void floppy_on_interrupt(void)
{
/* We cannot do a floppy-select, as that might sleep. We just force it */
	selected = 1;
	if (current_drive != (current_DOR & 3)) {
		current_DOR &= 0xFC;
		current_DOR |= current_drive;
		outb(current_DOR,FD_DOR);
		add_timer(2,&transfer);
	} else
		transfer();
}

void do_fd_request(void)
{
	unsigned int block;

	seek = 0;
	if (reset) {
		reset_floppy();
		return;
	}
	if (recalibrate) {
		recalibrate_floppy();
		return;
	}
	INIT_REQUEST;
	floppy = (MINOR(CURRENT->dev)>>2) + floppy_type;
	if (current_drive != CURRENT_DEV)
		seek = 1;
	current_drive = CURRENT_DEV;
	block = CURRENT->sector;
	if (block+2 > floppy->size) {
		end_request(0);
		goto repeat;
	}
	sector = block % floppy->sect;
	block /= floppy->sect;
	head = block % floppy->head;
	track = block / floppy->head;
	seek_track = track << floppy->stretch;
	if (seek_track != current_track)
		seek = 1;
	sector++;
	if (CURRENT->cmd == READ)
		command = FD_READ;
	else if (CURRENT->cmd == WRITE)
		command = FD_WRITE;
	else
		panic("do_fd_request: unknown command");
	add_timer(ticks_to_floppy_on(current_drive),&floppy_on_interrupt);
}

void floppy_init(void)
{
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	set_trap_gate(0x26,&floppy_interrupt);
	outb(inb_p(0x21)&~0x40,0x21);
}
