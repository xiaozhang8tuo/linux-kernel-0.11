#define move_to_user_mode() \
__asm__ ("movl %%esp,%%eax\n\t" \
	"pushl $0x17\n\t" \
	"pushl %%eax\n\t" \
	"pushfl\n\t" \
	"pushl $0x0f\n\t" \
	"pushl $1f\n\t" \
	"iret\n" \
	"1:\tmovl $0x17,%%eax\n\t" \
	"movw %%ax,%%ds\n\t" \
	"movw %%ax,%%es\n\t" \
	"movw %%ax,%%fs\n\t" \
	"movw %%ax,%%gs" \
	:::"ax")

#define sti() __asm__ ("sti"::)    
#define cli() __asm__ ("cli"::)   //禁止中断发生
#define nop() __asm__ ("nop"::)

#define iret() __asm__ ("iret"::)

#define _set_gate(gate_addr,type,dpl,addr) \
__asm__ ("movw %%dx,%%ax\n\t" \
	"movw %0,%%dx\n\t" \
	"movl %%eax,%1\n\t" \
	"movl %%edx,%2" \
	: \
	: "i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
	"o" (*((char *) (gate_addr))), \
	"o" (*(4+(char *) (gate_addr))), \
	"d" ((char *) (addr)),"a" (0x00080000))

#define set_intr_gate(n,addr) \
	_set_gate(&idt[n],14,0,addr)

#define set_trap_gate(n,addr) \
	_set_gate(&idt[n],15,0,addr)

#define set_system_gate(n,addr) \
	_set_gate(&idt[n],15,3,addr)

#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*(gate_addr) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*((gate_addr)+1) = (((base) & 0x0000ffff)<<16) | \
		((limit) & 0x0ffff); }



// 在全局表中设置任务状态段/局部表描述符。状态段和局部表段的长度均被设置成104字节。
// 参数:
// n-在全局表中描述符项n所对应的地址:
// addr-状态段/局部表所在内存的基地址。
// type-描述符中的标志类型字节。
// 
// %0-eax(地址addr):%1-(描述符项n的地址):%2-(描述符项n的地址偏移2处):
// %3-(描述符项n的地址偏移4处):%4-(描述符项n的地址偏移5处):
// %5-(描述符项n的地址偏移6处):6-(描述符项n的地址偏移7处):
#define _set_tssldt_desc(n,addr,type) \
__asm__ ("movw $104,%1\n\t" \			//将TSS(或LDT)长度放入描述符长度域（第0-1字节）。
	"movw %%ax,%2\n\t" \				//将基地址的低字放入描述符第2-3字节。
	"rorl $16,%%eax\n\t" \				//将基地址高字右循环移入x中（低字则进入高字处）。
	"movb %%al,%3\n\t" \				//将基地址高字中低字节移入描述符第4字节。
	"movb $" type ",%4\n\t" \			//将标志类型字节移入描述符的第5字节。
	"movb $0x00,%5\n\t" \				//描述符的第6字节置0。
	"movb %%ah,%6\n\t" \				//将基地址高字中高字节移入描述符第7字节。
	"rorl $16,%%eax" \					//再右循环16比特，eax恢复原值。
	::"a" (addr), "m" (*(n)), "m" (*(n+2)), "m" (*(n+4)), \
	 "m" (*(n+5)), "m" (*(n+6)), "m" (*(n+7)) \
	)

// 在全局表中设置任务状态段描述符。
// n是该描述符的指针:addr是描述符项中段的基地址值。任务状态段描述符的类型是0x89。
#define set_tss_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x89")
// 在全局表中设置局部表描述符。
// n是该描述符的指针:addr是描述符项中段的基地址值。局部表段描述符的类型是0x82。
#define set_ldt_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x82")
