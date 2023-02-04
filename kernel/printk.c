/*
 *  linux/kernel/printk.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * When in kernel-mode, we cannot use printf, as fs is liable to
 * point to 'interesting' things. Make a printf with fs-saving, and
 * all is well.
当处于内核模式时，我们不能使用printf,因为寄存器fs指向其他不感兴趣
的地方。自己编制一个printf并在使用前保存fs,一切就解决了。
 */
#include <stdarg.h>
#include <stddef.h>

#include <linux/kernel.h>

static char buf[1024];

extern int vsprintf(char * buf, const char * fmt, va_list args);


// 内核使用的显示函数。
// 只能在内核代码中使用。由于实际调用的显示函数tty_write()默认使用的显示数据在段寄
// 存器fs所指向的用户数据区中，因此这里需要哲时保存fs,并让s指向内核数据段。在显
// 示完之后再恢复原s段的内容。
int printk(const char *fmt, ...)
{
	va_list args;
	int i;

//运行参数处理开始函数。然后使用格式串fmt将参数列表args输出到buf中。返回值i等于
//输出字符串的长度。再运行参数处理结束函数。
	va_start(args, fmt);
	i=vsprintf(buf,fmt,args);
	va_end(args);
	__asm__("push %%fs\n\t"
		"push %%ds\n\t"
		"pop %%fs\n\t"          //fs=ds
		"pushl %0\n\t"          //把字符串长度i压入堆栈(这个三个入栈是调用参数) 
		"pushl $_buf\n\t"
		"pushl $0\n\t"          //显示通道号0
		"call _tty_write\n\t"   //int()(unsigned channel, char * buf, int nr)
		"addl $8,%%esp\n\t"		//丢弃两个入栈参数(channel, buf)
		"popl %0\n\t"			//弹出字符串长度值,作为返回值
		"pop %%fs"				//恢复fs
		::"r" (i):"ax","cx","dx"); //通知编译器, 寄存器ax,cx,dx值可能已经改变
	return i;                   //返回字符串长度
}
