; ! bootsect.s程序是磁盘引导块程序,编译后会驻留在磁盘的第一个扇区中（引导扇区,0磁道（柱面）,0磁头,第1个扇区)。实模式下运行的16位代码程序,Intel汇编,as86 的 as和ld.在PC机加电ROM BIOS自检后,将被BIOS加载到内存Ox7C00处进行执行。
; ! SYS_SIZE 是要加载的系统模块的长度,单位是节, 1节=16字节.  
; ! 0x300节 = 0x30000字节 = 196KB(1024字节为1kB,即192KB)
; ! versions of linux 
; !
SYSSIZE = 0x3000 # 之前由linux/Makefile:92 动态自动生成,0.11之后直接给出最大默认值
; !
; !	bootsect.s		(C) 1991 Linus Torvalds
; !
; ! bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves # BIOS会在0x7c00处加载 bootsect
; ! iself out of the way to address 0x90000, and jumps there.              # bootsect 把自己移动到内存绝对地址0x90000 (576KB)
; !
; ! It then loads 'setup' directly after itself (0x90200), and the system  # 在其后0x90200 (576.5KB) 处加载 setup, 0x10000 处加载system
; ! at 0x10000, using BIOS interrupts. 
; !
; ! NOTE! currently system is at most 8*65536 bytes long. This should be no # 0x90000 - 0x10000 = 0x80000(512KB)
; ! problem, even in the future. I want to keep it simple. This 512 kB      # 只要system在512KB内,没问题
; ! kernel size should be enough, especially as this doesn't contain the    # 且内核中木有buffer cache
; ! buffer cache as in minix
; !
; ! The loader has been made as simple as possible, and continuos           # 加截程序已经做得够简单了,所以持续的读出错将导致死循环。只能手工重启。
; ! read errors will result in a unbreakable loop. Reboot by hand. It       # 只要可能,通过一次读取所有的扇区,加载过程可以做得很快。
; ! loads pretty fast by getting whole sectors at a time whenever possible.

.globl begtext, begdata, begbss, endtext, enddata, endbss ;伪指令.globl/.global后定义的标识符是外部/全局的
.text         ; .text.data.bss定义代码段,数据段,未初始化数据段.链接时会合并,这里在同一重叠地址范围内,程序实际上没有分段
begtext:      ;!另外,后面带冒号的字符串是标号,例如下面的'begtext:。一条汇编语句通常由标号（可选）、指令助记符（指令名）和操作数三个字段组成。标号位于一条指令的第一个字段。它代表其所在位置的地址,通常指明一个跳转指令的目标位置。
.data
begdata:
.bss
begbss:
.text

SETUPLEN = 4				#! nr of setup-sectors # setup程序的扇区数(setup-sectors)值：
BOOTSEG  = 0x07c0			#! original address of boot-sector # bootsect的原始地址(是段地址,以下同
INITSEG  = 0x9000			#! we move boot here - out of the way # bootsect移动到这里
SETUPSEG = 0x9020			#! setup starts here # setup地址
SYSSEG   = 0x1000			#! system loaded at 0x10000 (65536). # system地址
ENDSEG   = SYSSEG + SYSSIZE		#! where to stop loading # 停止加载的段地址

; ! ROOT_DEV:	0x000 - same type of floppy as boot. # 根文件系统设备使用与引导时同样的软驱设备：
; !		0x301 - first partition on first drive etc   # 根文件系统设备在第一个硬盘的第一个分区上,等等：
ROOT_DEV = 0x306

entry start # 伪指令entry迫使链接程序在生成的执行程序(a.out)中包含指定的标识符或标号。47--56行作用是将自身(bootsect)从目前段位置0x07c0(31KB)移动到0x9000(576KB)处,共256字(512字节),然后跳转到移动后代码的go标号处,也即本程序的下一语句处。
start:
	mov	ax,#BOOTSEG      # ds段寄存器设置 0x7c0
	mov	ds,ax
	mov	ax,#INITSEG      # es段寄存器设置 0x9000
	mov	es,ax
	mov	cx,#256          # 设置移动计数值 256字
	sub	si,si            # mov si,0
	sub	di,di            # mov di, 0清零操作
	rep                  # 重复rep后面的指令cx(256)次 “REP与MOVS或STOS串操作指令相结合使用,完成一组字符的传送或建立一组相同 ---- 数据的字符串.” 例如 rep movs,重复的是movs指令, 就是说rep重复的是跟在它后面的一条指令
	movw                 # 从ds[si] 移动cx个字 到es[di] https://blog.csdn.net/GMingZhou/article/details/78148605 DS, ES, SS, DI, SI, BP, SP, IP, FS 寄存器
	jmpi	go,INITSEG   # 段间跳转(Jump Intersegment)。这里INITSEG指出跳转到的段地址,标号go是段内偏移地址.
go:	mov	ax,cs            # 段间跳转执行go时,cs(代码段寄存器)指向0x9000
	mov	ds,ax			 # 把ds,es,ss都设置成指向0x9000
	mov	es,ax
; ! put stack at 0x9ff00.# 调整栈顶指针指向0x9FF00
	mov	ss,ax            # sp栈顶指针还是要搞大些  0x9000(节):0xFF00 
	mov	sp,#0xFF00		! arbitrary value >>512

; ! load the setup-sectors directly after the bootblock. 在bootsect程序块后紧根着加载setup模块的代码数据。
; ! Note that 'es' is already set up. 注意es已经设置好了。（在移动代码时es已经指向目的段地址处0x9000)

load_setup: # 68-77行的用途是利用BIOS中断INT0x13将setup模块从磁盘第2个扇区开始读到0x90200开始处,共读4个扇区。如果读出错,则复位驱动器,并重试,没有退路。INT0x13的使用方法如下:
	mov	dx,#0x0000			#! drive 0, head 0             INT 13h 的使用方法如下;// dh = 磁头号；				  dl = 驱动器号（如果是硬盘则要置为7）；
	mov	cx,#0x0002			#! sector 2, track 0           						;// ch = 磁道（柱面）号的低8位；  cl = 开始扇区（0－5位）,磁道号高2位（6－7）；
	mov	bx,#0x0200			#! address = 512, in INITSEG   						;// es:bx ->指向数据缓冲区；  如果出错则CF标志置位。 
	mov	ax,#0x0200+SETUPLEN	#! service 2, nr of sectors     					;// ah = 02h - 读磁盘扇区到内存；al = 需要读出的扇区数量 SETUPLEN = 4；
	int	0x13				#! read it                     
	jnc	ok_load_setup		#! ok - continue
	mov	dx,#0x0000
	mov	ax,#0x0000			#! reset the diskette
	int	0x13                # 复位驱动器 --> 通过重设dx,ax再次执行int 0x13
	j	load_setup

ok_load_setup:

; ! Get disk drive parameters, specifically nr of sectors(扇区)/track(磁道)

	mov	dl,#0x00
	mov	ax,#0x0800		# ! AH=8 is get drive parameters 返回信息的格式 : 292行
	int	0x13
	mov	ch,#0x00
	seg cs              # 该指令表示下一条语句的操作数在cs段寄存器所指的段中,它只影响其下一条语句。实际上本程序的代码段和数据段都在同一段cs/ds/es相同,可以不适用seg cs
	mov	sectors,cx      # 此时cx中的cl=每磁道最大扇区数(位0-5),最大磁道号高2位(位6-7),dl=0表示软盘磁道号高2位肯定为0.即cx=扇区数
	mov	ax,#INITSEG
	mov	es,ax           # 读取磁盘参数中断使得es改变,再该回去

; ! Print some inane message
# 显示信息：Loading system..,共显示包括回车和换行控制字符在内的24个字符. BI0S中断0x10 功能号ah=0x03,读光标位置
	mov	ah,#0x03		# ! read cursor pos  
	xor	bh,bh          	# 输入：bh=页号,   返回：ch=扫描开始线：cl=扫描结束线：dh=行号(0x00顶端)：dl=列号(0x00最左边)。
	int	0x10
# BIOS中断0x10, 功能号ah=0x13h,显示字符串 传入参数及返回信息格式: 302行
	mov	cx,#24          # 共显示24字符
	mov	bx,#0x0007		# ! page 0, attribute 7 (normal)
	mov	bp,#msg1        # es:bp 指向要显示的字符串
	mov	ax,#0x1301		# ! write string, move cursor
	int	0x10            # 写字符串并移动光标到串结尾处

; ! ok, we've written the message, now
; ! 加载 system (at 0x10000) 

	mov	ax,#SYSSEG
	mov	es,ax		    # ! segment of 0x010000  es存放system的段地址
	call	read_it     # 读磁盘上的system模块,es作为输入参数
	call	kill_motor  # 关闭软驱的线性马达

; ! After that we check which root-device to use. If the device is  # 如果指定了root-device(根文件系统设备),直接用
; ! defined (!= 0), nothing is done and the given device is used.
; ! Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
; ! on the number of sectors that the BIOS reports currently.       # 否则从 PS0 和 at0 中挑

	seg cs
	mov	ax,root_dev     # 取root_dev处的设备号判断是否已经定义
	cmp	ax,#0
	jne	root_defined
	seg cs
	mov	bx,sectors      # 取上面第88行保存的每磁道扇区数。。因为是可引导的驱动器，所以肯定是A驱。
	mov	ax,#0x0208		#! /dev/ps0 - 1.2Mb
	cmp	bx,#15          # 如果sectors=15则说明是1.2B的驱动器
	je	root_defined    
	mov	ax,#0x021c		#! /dev/PS0 - 1.44Mb
	cmp	bx,#18          # 如果sectors=18,则说明是1.4WB软驱
	je	root_defined
undef_root:
	jmp undef_root
root_defined:
	seg cs
	mov	root_dev,ax     # 设备号保存至root_dev

; ! after that (everyting loaded), we jump to 所有程序加载完毕，直接跳转到被加载在bootsect后面的setup程序
; ! the setup-routine loaded directly after
; ! the bootblock:

	jmpi	0,SETUPSEG # 段间跳转 0x9020:0000(setup.s程序的开始处)去执行。至此bootsect程序加载完毕

; ! This routine loads the system at address 0x10000, making sure  # 该子程序将系统模块加载到内存地址0x10000处，并确定没有跨越6KB的内存边界。
; ! no 64kB boundaries are crossed. We try to load it as fast as   # 我们试图尽快地进行加载，只要可能，就每次加截整条磁道的数据。
; ! possible, loading whole tracks whenever we can.
; !
; ! in:	es - starting address segment (normally 0x1000)            # 输入：es-开始内存地址段值(通常是0x1000)
; !
sread:	.word 1+SETUPLEN	#! sectors read of current track '1+SETUPLEN'表示开始时已经读进1个引导扇区和setup程序所占的扇区数SETUPLEN. 当前磁道中已读扇区数
head:	.word 0				#! current head
track:	.word 0				#! current track

read_it:                # :318
	mov ax,es
	test ax,#0x0fff     # es = #0x?000 test 之后zf标志位被设置，zf不等于0,即不是64kb的倍数,有问题
die:	jne die			#! es must be at 64kB boundary   			es必须是64KB的地址边界
	xor bx,bx		    #! bx is starting address within segment 	bs为段内偏移,置零
rp_read:                # 接着判断是否已经读入全部数据。比较当前所读段是否就是系统数据末端所处的段(ENDSEG),如果不是就跳转至下面ok1read标号处继续读数据。否则退出子程序返回。
	mov ax,es
	cmp ax,#ENDSEG		#! have we loaded all yet?
	jb ok1_read
	ret
ok1_read:               # :326
	seg cs              # 
	mov ax,sectors      # 取每磁道的扇区数            https://cloud.tencent.com/developer/article/1129947 存储容量 ＝ 磁头数 × 磁道(柱面)数 × 每道扇区数 × 每扇区字节数
	sub ax,sread        # 减去当前已读扇区数
	mov cx,ax           # cx=ax=当前磁道未读扇区数
	shl cx,#9           # shl左移  9位 cx = cx*512
	add cx,bx           # cx = bx(段内当前偏移值)
	jnc ok2_read        # jnc如果进位位没有置位则跳转  ---> 若没有超过64kB字节
	je ok2_read         
	xor ax,ax           # 若加上此次将读磁道上所有未读扇区时会超过64KB,则计算此时最多能读入的字节数：
	sub ax,bx			# (64KB-段内读偏移位置)，再转换成需读取的扇区数。其中0减某数就是取该数64KB的补值。
	shr ax,#9
ok2_read:               # :331
	call read_track     # 读当前磁道上指定开始扇区和需读扇区数的数据。
	mov cx,ax           # cx = 该次操作已经读取的扇区数
	add ax,sread        # 加上当前磁道上已经读取的扇区数
	seg cs
	cmp ax,sectors      # 如果当前磁道上的扇区还有未读，跳转到ok3_read
	jne ok3_read        # 若该磁道的当前磁头面所有扇区已经读取，则读该磁道的下一磁头面(1号磁头)上的数据。
	mov ax,#1           # 已经完成则读取下一磁道
	sub ax,head         # 判断当前磁头号
	jne ok4_read        # 如果是0磁头,则再去读1磁头面上的扇区数据
	inc track           # 否则去读取下一磁道
ok4_read:               
	mov head,ax         # 保存当前磁头号
	xor ax,ax           # 清理当前磁道已读扇区数
ok3_read:               # 如果当前磁道上的还有未读扇区，则首先保存当前磁道已读扇区数，然后调整存放数据处的开始位置。若小于64KB边界值，则跳转到rp read(156行)处，继续读数据。
	mov sread,ax        # 保存当前磁道已读扇区数
	shl cx,#9           # 上次已读扇区数*512字节
	add bx,cx           # 调整当前段内数据开始位置
	jnc rp_read
	mov ax,es           # 否则说明已经读取64KB数据，调整当前段，为读取下一段数据准备
	add ax,#0x1000      # 段基址调整为指向下一个64KB内存开始处
	mov es,ax
	xor bx,bx           # 清段内数据开始偏移值
	jmp rp_read       
# 文件的记录在同一盘组上存放时，应先集中放在一个柱面上，然后再顺序存放在相邻的柱面上，对应同一柱面，则应该按盘面的次序顺序存放。
read_track:             # 读取当前磁道上指定开始扇区和需读取扇区的数据到es:bx开始处
	push ax             # al需要读取扇区数 es:bx 缓冲区开始位置
	push bx
	push cx
	push dx
	mov dx,track        # 读取磁道号
	mov cx,sread        # 读取当前磁道已读扇区数
	inc cx              # cl=开始读扇区
	mov ch,dl           # ch=当前磁道号
	mov dx,head         # 取当前磁头号
	mov dh,dl           # dh=磁头号
	mov dl,#0           # dl=驱动器号(为0表示当前A驱动器)
	and dx,#0x0100      # 磁头号不大于1
	mov ah,#2           # ah=2 读磁盘扇区功能号
	int 0x13            # 
	jc bad_rt           # 出错则跳转至bad_rt
	pop dx
	pop cx
	pop bx
	pop ax
	ret
bad_rt:	mov ax,#0      # 读磁盘出错,执行驱动器复位操作(磁盘中断功能号0),再跳转到read_track处重试
	mov dx,#0
	int 0x13
	pop dx
	pop cx
	pop bx
	pop ax
	jmp read_track

/*
 * This procedure turns off the floppy drive motor, so  子程序用于关闭软驱的马达,这样进入内核后知道
 * that we enter the kernel in a known state, and       其所处的状态,之后无需担心
 * don't have to worry about it later.
 */
kill_motor:
	push dx
	mov dx,#0x3f2       # 软驱控制卡的数字输出寄存器(D0)端口，只写。
	mov al,#0           # A驱动器，关闭FDC,禁止DMA和中断请求，关闭马达。
	outb                # 将al中的内容输出到dx指定的端口去。
	pop dx
	ret

sectors:
	.word 0             # 存放当前启动软盘每磁道的扇区数

msg1:
	.byte 13,10         # 回车换行的ASCII码
	.ascii "Loading system ..."
	.byte 13,10,13,10
# 表示下面语句从地址508(0x1FC)开始，所以root_dev在启动扇区的第508开始的2个字节中。
.org 508              
root_dev:
	.word ROOT_DEV      # 存放 根文件系统所在设备号(init/main.c中会用)
boot_flag:              # 启动盘具有有效引导扇区的标志。仅供BIOS程序加载引导扇区时识别使用。必须位于
	.word 0xAA55        # 引导扇区的最后两个字节中

.text
endtext:
.data
enddata:
.bss
endbss:

; 补充资料


; 设备号0x306指定根文件系统设备是第2个硬盘的第1个分区。当年Lius是在第2个硬盘上安装
; 了Linux0.11系统,所以这里RO0TDEV被设置为0x306。在编译这个内核时你可以根据自己根文件
; 系统所在设备位置修改这个设备号。这个设备号是Liux系统老式的硬盘设备号命名方式,硬盘设备
; 号具体值的含义如下：


;//指定根文件系统设备是第1个硬盘的第1个分区。这是Linux老式的硬盘命名
;//方式,具体值的含义如下：
;//设备号 ＝ 主设备号*256 ＋ 次设备号 
;//          (也即 dev_no = (major<<8 + minor)
;//(主设备号：1－内存,2－磁盘,3－硬盘,4－ttyx,5－tty,6－并行口,7－非命名管道)
;//300 - /dev/hd0 － 代表整个第1个硬盘
;//301 - /dev/hd1 － 第1个盘的第1个分区
;//... ...
;//304 - /dev/hd4 － 第1个盘的第4个分区
;//305 - /dev/hd5 － 代表整个第2个硬盘
;//306 - /dev/hd6 － 第2个盘的第1个分区
;//... ...
;//309 - /dev/hd9 － 第2个盘的第4个分区 

; 57行之后 CPU在已移动到0x90000位置处的代码中执行。
; 这段代码设置几个段寄存器,包括栈寄存器ss和sp。栈指针sp只要指向远大于512字节偏移(即
; 地址0x90200)处都可以。因为从0x90200地址开始处还要放置setup程序,而此时setup程序大约
; 为4个扇区,因此sp要指向大于(0x200+0x200*4+堆栈大小)处。
; 实际上BI0S把引导扇区加截到0x7c00处并把执行权交给引导程序时,ss=0x00,sp=0xfffe。


; !取磁盘驱动器参数INT0x13调用格式和返回信息如下：
; ！ah=0x08 dl=驱动器号（如果是硬盘则要置位7为1）。
; !返回信息：
; 如果出错则CF置位,并且ah=状态码。
; ah=0,al=0,bl=驱动器类型(AT/PS2)
; ch=最大磁道号的低8位,cl=每磁道最大扇区数（位0-5）,最大磁道号高2位（位6-7）
; dh=最大磁头数,dl=驱动器数量,
; es:di-→软驱磁盘参数表。

; BI0S中断0x10功能号ah=0x13,显示字符串。
; 输入：al=放置光标的方式及规定属性。0x01-表示使用bl中的属性值,光标停在字符串结尾处。
; es:bp此寄存器对指向要显示的字符串起始位置处。cx=显示的字符串字符数。bh=显示页面号：
; bl=字符属性。dh=行号：dl=列号。

; 在Linux中软驱的主设备号是2(参见第43行的注释),次设备号=type*4+nr,其中
; nr为0-3分别对应软驱A、B、C或D:type是软驱的类型(2→1.2MB或7→1.44MB等)。
; 因为7*4+0=28,所以/dev/PS0(2,28)指的是1.44BA驱动器,其设备号是0x021c
; 同理/dev/at0(2,8)指的是1.2BA驱动器,其设备号是0x0208。
; 下面root_dev定义在引导扇区508,509字节处,指根文件系统所在设备号。0x0306指第2
; 个硬盘第1个分区。这里默认为0x0306是因为当时Linus开发Linux系统时是在第2个硬
; 盘第】个分区中存放根文件系统。这个值需要根据你自己根文件系统所在硬盘和分区进行修
; 改。例如,如果你的根文件系统在第1个硬盘的第1个分区上,那么该值应该为0x0301,即
; (0x01,0x03)。如果根文件系统是在第2个Bochs软盘上,那么该值应该为0x021D,即
; (0x1D,0x02)。当编译内核时,你可以在Makefi1e文件中另行指定你自己的值,内核映像
; 文件Image的创建程序tools/build会使用你指定的值来设置你的根文件系统所在设备号。


; !首先测试输入的段值。从盘上读入的数据必须存放在位于内存地址64KB的边界开始处，否则进入死
; !循环。清bx寄存器，用于表示当前段内存放数据的开始位置。
; !153行上的指令test以比特位逻辑与两个操作数。若两个操作数对应的比特位都为1，则结果值的
; !对应比特位为1，否则为0。该操作结果只影响标志（零标志ZF等）。例如，若AX=0x1000,那么
; !test指令的执行结果是(0x1000&0x0fff)=0x0000,于是ZF标志置位。此时即下一条指令jne
; !条件不成立。

; !计算和验证当前磁道需要读取的扇区数，放在ax寄存器中。
; !根据当前磁道还未读取的扇区数以及段内数据字节开始偏移位置，计算如果全部读取这些未读扇区
; !所读总字节数是否会超过6KB段长度的限制。若会超过，则根据此次最多能读入的字节数(6KB-
; !段内偏移位置)，反算出此次需要读取的扇区数。

; 读当前磁道上指定开始扇区(cl)和需读扇区数(al)的数据到es:bx开始处。然后统计当前磁道
; 上已经读取的扇区数并与磁道最大扇区数sectors作比较。如果小于sectors说明当前磁道上的还
; 有扇区未读。于是跳转到ok3_read处继续操作。


; 下面第235行上的值0x3f2是软盘控制器的一个端口，被称为数字输出寄存器(D0R)端口。它是
; 一个8位的寄存器，其位7-位4分别用于控制4个软驱(D-A)的启动和关闭。位3-一位2用于
; 允许/禁止DMA和中断请求以及启动/复位软盘控制器FDC。位1-位0用于选择选择操作的软驱。
; 第236行上在l中设置并输出的0值，就是用于选择A驱动器，关闭FDC,禁止DMA和中断请求，
; 关闭马达。有关软驱控制卡编程的详细信息请参见kernel/b1kdrv/f1oppy.c程序后面的说明。