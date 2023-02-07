/*
 *  linux/kernel/mktime.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <time.h>

/*
该该程序只有一个函数kernel mktime(),仅供内核使用。计算从1970年1月1日0时起到开机当日
经过的秒数（日历时间），作为开机时间。该函数与标准C库中提供的mktime0函数的功能完全一样，
都是将结构表示的时间转换成UNIX日历时间。但是由于内核不是普通程序，不能调用开发环境库
中的函数，因此这里就必须自己专门编写一个了。
 * This isn't the library routine, it is only used in the kernel.
 * as such, we don't care about years<1970 etc, but assume everything
 * is ok. Similarly, TZ etc is happily ignored. We just do everything
 * as easily as possible. Let's find something public for the library
 * routines (although I think minix times is public).
 */
/*
 * PS. I hate whoever though up the year 1970 - couldn't they have gotten
 * a leap-year instead? I also hate Gregorius, pope or no. I'm grumpy.
 */
#define MINUTE 60
#define HOUR (60*MINUTE)
#define DAY (24*HOUR)
#define YEAR (365*DAY)

/* interestingly, we assume leap-years */
// 考虑了闰年，以年为界限定义了每个月开始时的秒数事件
static int month[12] = {
	0,
	DAY*(31),
	DAY*(31+29),
	DAY*(31+29+31),
	DAY*(31+29+31+30),
	DAY*(31+29+31+30+31),
	DAY*(31+29+31+30+31+30),
	DAY*(31+29+31+30+31+30+31),
	DAY*(31+29+31+30+31+30+31+31),
	DAY*(31+29+31+30+31+30+31+31+30),
	DAY*(31+29+31+30+31+30+31+31+30+31),
	DAY*(31+29+31+30+31+30+31+31+30+31+30)
};
// 该函数计算从1970年1月1日0时起到开机当日经过的秒数，作为开机时间。
// 参数tm中各字段已经在init/main.c中被赋值，信息取自CMOS。
long kernel_mktime(struct tm * tm)
{
	long res;
	int year;
// 首先计算70年到现在经过的年数。因为是2位表示方式，所以会有2000年问题。我们可以
// 简单地在最前面添加一条语句来解决这个问题：if(tm->tm_year<70)tm->tm_year+=100:
// 由于UNIX计年份y是从1970年算起。到1972年就是一个闰年，因此过3年(71,72,73)
// 就是第1个闰年，这样从1970年开始的闰年数计算方法就应该是为1+(y-3)/4,即为
// (y+1)/4。
// res=这些年经过的秒数时间+每个闰年时多1天的秒数时间+当年到当月时的秒数。
// 另外，month[]数组中已经在2月份的天数中包含进了闰年时的天数，即2月份天数
// 多算了1天。因此，若当年不是闰年并且当前月份大于2月份的话，我们就要减去这天。因
// 为从70年开始算起，所以当年是闰年的判断方法是(y+2)能被4除尽。若不能除尽（有余
// 数)就不是闰年。
	year = tm->tm_year - 70;
/* magic offsets (y+1) needed to get leapyears right.*/
	res = YEAR*year + DAY*((year+1)/4);             //365天一年,闰年多一天
	res += month[tm->tm_mon];						//到当月
/* and (y+2) here. If it wasn't a leap-year, we have to adjust */
	if (tm->tm_mon>1 && ((year+2)%4))				//闰年的2月减1天，month数组中按29天算的
		res -= DAY;
	res += DAY*(tm->tm_mday-1);
	res += HOUR*tm->tm_hour;
	res += MINUTE*tm->tm_min;
	res += tm->tm_sec;
	return res;
}
