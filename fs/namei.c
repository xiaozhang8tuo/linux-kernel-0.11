/*
 *  linux/fs/namei.c
 *  本文件主要实现了根据目录名或文件名寻找到对应i节点的函数namei,
 *  以及一些关于目录的建立和删除、目录项的建立和删除等操作函数和系统调用。
 */

/*
 * Some corrections by tytso.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <string.h>
#include <fcntl.h>			// 文件控制头文件。文件及其描述符的操作控制常数符号的定义
#include <errno.h>
#include <const.h>
#include <sys/stat.h>		// 文件状态头文件。含有文件或文件系统状态结构stat()和常量


// 下面宏中右侧表达式是访问数组的一种特殊使用方法。它基于这样的一个事实，即用数组名和
// 数组下标所表示的数组项（例如a[b])的值等同于使用数组首指针（地址）加上该项偏移地址
// 的形式的值*(a+b),同时可知项a[b]也可以表示成b[a]的形式。因此对于字符数组项形式
// 为"LoveYou"[2](或者2["LoveYou"])就等同于*("LoveYou"+2)。另外，字符串"LoveYou"
// 在内存中被存储的位置就是其地址，因此数组项"LoveYou"[2]的值就是该字符串中索引值为2
// 的字符”v”所对应的ASCII码值0x76,或用八进制表示就是0166。在C语言中，字符也可以用
// 其ASCII码值来表示，方法是在字符的ASCII码值前面加一个反斜杠。例如字符“v”可以表示
// 成"\x76"或者"\166”。因此对于不可显示的字符（例如ASCII码值为0x00--0x1f的控制字符）
// 就可用其ASCII码值来表示。

// 下面是访问模式宏。x是头文件include/fcntl.h中定义的文件访问（打开）标志.
// 这个宏根据文件访问标志x的值来索引双引号中对应的数值。双引号中有4个八进制数值（实
// 际表示4个控制字符)："\004\002\006\377”，分别表示读、写和执行的权限为：r、w、rw
// 和wx(3)rwx(7)rwx(7),并且分别对应x的索引值0-3。例如，如果x为2，则该宏返回八进制值006，
// 表示可读可写(w)。另外，其中O_ACCMODE=00003,是索引值x的屏被码.
#define ACC_MODE(x) ("\004\002\006\377"[(x)&O_ACCMODE])

/*
 * comment out this line if you want names > NAME_LEN chars to be
 * truncated. Else they will be disallowed.
 */
/* #define NO_TRUNCATE */

#define MAY_EXEC 1		// 可执行(可进入)
#define MAY_WRITE 2		// 可写
#define MAY_READ 4		// 可读

/*
 *	permission()
 *	检测文件的权限
 * is used to check for read/write/execute permissions on a file.
 * I don't know if we should look at just the euid or both euid and
 * uid, but that should be easily changed.
 */
// 检测文件访问许可权限
// 参数: inode-文件的i节点指针  mask-访问属性屏蔽码
// 返回: 访问许可返回1,否则0
static int permission(struct m_inode * inode,int mask)
{
	int mode = inode->i_mode;

/* special case: not even root can read/write a deleted file */
	// 特珠情况：即使是超级用户(root)也不能读/写一个已被删除的文件*/
	// 如果i节点有对应的设备，但该i节点的链接计数值等于0，表示该文件已被删除，则返回。
	// 否则，如果进程的有效用户id(euid)与i节点的用户id相同，则取文件宿主的访问权限。
	// 否则，如果进程的有效组id(egid)与i节点的组id相同，则取组用户的访问权限。
	if (inode->i_dev && !inode->i_nlinks)
		return 0;
	else if (current->euid==inode->i_uid)
		mode >>= 6;
	else if (current->egid==inode->i_gid)
		mode >>= 3;
	//最后判断如果所取的的访问权限与屏蔽码相同，或者是超级用户，则返回1，否则返回0。 &0007即只取后3位2进制
	if (((mode & mask & 0007) == mask) || suser())
		return 1;
	return 0;
}

/*
不能使用strncmp字符串比较函数，因为名称不在我们的数据空间
(不在内核空间)。因而我们只能使用match, 问题不大，match同样
也处理一些完整的测试。
 *
 * NOTE! unlike strncmp, match 返回 1 成功, 0 失败.
 */

// 指定长度字符串比较函数。
// 参数: len-比较的字符串长度：name-文件名指针：de-目录项结构。
// 返回: 相同返回1，不同返回0。
static int match(int len, const char * name, struct dir_entry * de)
{
	register int same __asm__("ax");	// 定义了一个局部寄存器变量same。该变量将被保存在eax寄存器中，以便于高效访问。


	// 首先判断函数参数的有效性。如果目录项指针空，或者目录项i节点等于0，或者要比较的
	// 字符串长度超过文件名长度，则返回0。如果要比较的长度len小于NAME_LEN,但是目录项
	// 中文件名长度超过len,也返回0。
	if (!de || !de->inode || len > NAME_LEN)
		return 0;
	if (len < NAME_LEN && de->name[len])// 对目录项中文件名长度是否超过len的判断方法是检测name[len]是否为NULL.
		return 0;						// 若长度超过len,则name[1en]处就是一个不是NULL的普通字符。而对于长度为len的字符串name,字符name[len]就应该是NULL。
	
	// 然后使用嵌入汇编语句进行快速比较操作。它会在用户数据空间(fs段)执行字符串的比较操作
	// %0-eax(比较结果same):%l-eax(eax初值0)：%2-esi(名字指针)：
	// %3-edi(目录项名指针)：%1-ecx(比较的字节长度值len)

	// __asm__("cld\n\t"					// 清方向位
	// 	"fs ; repe ; cmpsb\n\t"			// 用户空间执行循环比较[esi++] [edi++]
	// 	"setz %%al"						// 有可能是循环结束也有可能是因为不相等提前退出, 检查zf标志，若结果一致(zf=1)则设置al=1(same=1)
	// 	:"=a" (same)
	// 	:"0" (0),"S" ((long) name),"D" ((long) de->name),"c" (len)
	// 	:"cx","di","si");
	return same;
}

/*
 *	find_entry()
 *
 *		在指定目录中寻找一个与名字匹配的目录项。返回一个含有找到目录项的高速
 *	缓冲块以及目录项本身（作为一个参数-res_dir)。该函数并不读取目录项的i节点-如果需要的话则自己操作。
 * 	由于有".."目录项，因此在操作期间也会对几种特殊情况分别处理一比如横越一个伪根目录以及安装点。
 */
// 查找指定目录和文件名的目录项。
// 参数：*dir-指定目录i节点的指针 | name-文件名 | namelen-文件名长度
// 该函数在指定目录的数据（文件）中搜索指定文件名的目录项。并对指定文件名是'..’的
// 情况根据当前进行的相关设置进行特珠处理。关于函数参数传递指针的指针的作用，请参
// 见linux/sched.c第151行前的注释.
// 返回：成功则函数高速缓冲区指针，并在*res_dir处返回的目录项结构指针。失败则返回
// 空指针NULL。
static struct buffer_head * find_entry(struct m_inode ** dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int entries;
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;
	struct super_block * sb;

#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
	// 首先计算本目录中目录项项数entries。目录i节点i_size字段中含有本目录包含的数据
	// 长度，因此其除以一个目录项的长度(16字节)即可得到该目录中目录项数。然后置空返回
	// 目录项结构指针。如果文件名长度等于0，则返回NULL, 退出。
	entries = (*dir)->i_size / (sizeof (struct dir_entry));
	*res_dir = NULL;
	if (!namelen)
		return NULL;
	
	// 1 修正*dir 目录i节点
	// 接下来我们对目录项文件名是..的情况进行特殊处理。如果当前进程指定的根节点就是
	// 函数参数指定的目录，则说明对于本进程来说，这个目录就是它的伪根目录，即进程只能访
	// 问该目录中的项而不能后退到其父目录中去。也即对于该进程本目录就如同是文件系统的根
	// 目录。因此我们需要将文件名修改为.
	// 否则，如果该目录的i节点号等于ROOT_IND(1号)的话，说明确实是文件系统的根i节点。
	// 则取文件系统的超级块。如果被安装到的i节点存在，则先放回原i节点，然后对被安装到
	// 的i节点进行处理。于是我们让*dir指向该被安装到的i节点：并且该i节点的引用数加1。
	// 即针对这种情况，我们悄悄地进行了“偷梁换柱”工程
	/* check for '..', as we might have to do some "magic" for it */
	if (namelen==2 && get_fs_byte(name)=='.' && get_fs_byte(name+1)=='.') {
/* '..' in a pseudo-root results in a faked '.' (just change namelen) */
		if ((*dir) == current->root)
			namelen=1;
		else if ((*dir)->i_num == ROOT_INO) {
/* '..' over a mount-point results in 'dir' being exchanged for the mounted
   directory-inode. NOTE! We set mounted, so that we can iput the new dir */
			sb=get_super((*dir)->i_dev);
			if (sb->s_imount) {
				iput(*dir);
				(*dir)=sb->s_imount;
				(*dir)->i_count++;
			}
		}
	}

	// 现在我们开始正常操作,查找指定文件名的目录项在什么地方。因此我们需要读取目录的数
	// 据，即取出目录i节点对应块设备数据区中的数据块（逻辑块）信息。这些逻辑块的块号保
	// 存在i节点结构的i_zone[]数组中。我们先取其中第1个块号。如果目录i节点指向的第
	// 一个直接磁盘块号为0，则说明该目录不含数据，这不正常。于是返回NULL退出。否则
	// 我们就从节点所在设备读取指定的目录项数据块。当然，如果不成功，则也返回NULL退出。
	if (!(block = (*dir)->i_zone[0]))
		return NULL;
	if (!(bh = bread((*dir)->i_dev,block)))
		return NULL;
	
	// 此时我们就在这个读取的目录i节点数据块中搜索匹配指定文件名的目录项。首先让de指
	// 向缓冲块中的数据块部分，并在不超过目录中目录项数的条件下，循环执行搜索。其中i是
	// 目录中的目录项索引号，在循环开始时初始化为0。
	i = 0;
	de = (struct dir_entry *) bh->b_data;
	while (i < entries) {
		if ((char *)de >= BLOCK_SIZE+bh->b_data) {
			brelse(bh);
			bh = NULL;
			if (!(block = bmap(*dir,i/DIR_ENTRIES_PER_BLOCK)) || //映射文件块x到磁盘的逻辑块y
			    !(bh = bread((*dir)->i_dev,block))) {
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;
		}
		// 如果找到匹配的目录项的话，则返回该目录项结构指针de和该目录项i节点指针*dir以
		// 及该目录项数据块指针b,并退出函数。否则继续在目录项数据块中比较下一个目录项。
		if (match(namelen,name,de)) {
			*res_dir = de;
			return bh;
		}
		de++;	//不匹配就查找下一个目录项
		i++;
	}
	brelse(bh);
	return NULL;
}

/*
 *	add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 * 注意 de(指定目录项结构指针)的i节点部分被设置为0-这表
 * 示在调用该函数和往目录项中添加信息之间不能去睡眠。因为如果睡眠，
 * 那么其他人（进程）可能会使用了该目录项。
 */
static struct buffer_head * add_entry(struct m_inode * dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;

	*res_dir = NULL;
#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
	if (!namelen)
		return NULL;
	if (!(block = dir->i_zone[0]))
		return NULL;
	if (!(bh = bread(dir->i_dev,block)))
		return NULL;
	// 此时我们就在这个目录节点数据块中循环查找最后未使用的空目录项。首先让目录项结构
	// 指针de指向缓冲块中的数据块部分，即第一个目录项处。其中i是目录中的目录项索引号，
	// 在循环开始时初始化为0。
	i = 0;
	de = (struct dir_entry *) bh->b_data;
	while (1) {
		// 如果当前目录项数据块已经搜索完毕，但还没有找到需要的空目录项，则释放当前目录项数
		// 据块，再读入目录的下一个逻辑块。如果对应的逻辑块不存在就创建一块。若读取或创建操
		// 作失败则返回空。如果此次读取的磁盘逻辑块数据返回的缓冲块指针为空，说明这块逻辑块
		// 可能是因为不存在而新创建的空块，则把目录项索引值加上一块逻辑块所能容纳的目录项数
		// DIR_ENTRIES_PER_BLOCK,用以跳过该块并继续搜索。否则说明新读入的块上有目录项数据，
		// 于是让目录项结构指针de指向该块的缓冲块数据部分，然后在其中继续搜索。其中192行
		// 上的i/DIR_ENTRIES_PER_BLOCK可计算得到当前搜索的目录项i所在目录文件中的块号，
		// 而create block0函数(inode.c,第145行)则可读取或创建出在设备上对应的逻辑块。
		if ((char *)de >= BLOCK_SIZE+bh->b_data) {
			brelse(bh);
			bh = NULL;
			block = create_block(dir,i/DIR_ENTRIES_PER_BLOCK);
			if (!block)
				return NULL;
			if (!(bh = bread(dir->i_dev,block))) {
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;
		}
		// 如果当前所操作的目录项序号乘上目录结构大小所的长度值已经超过了该目录i节点信息
		// 所指出的目录数据长度值i_size,则说明整个目录文件数据中没有由于删除文件留下的空
		// 目录项，因此我们只能把需要添加的新目录项附加到目录文件数据的未端处。于是对该处目
		// 录项进行设置（置该目录项的节点指针为空），并更新该目录文件的长度值（加上一个目
		// 录项的长度)，然后设置目录的节点已修改标志，再更新该目录的改变时间为当前时间。
		if (i*sizeof(struct dir_entry) >= dir->i_size) {
			de->inode=0;									// 新加的目录项指针的i节点部分被设置为0：这表示在调用该函数和往目录项中添加信息之间不能去睡眠。
			dir->i_size = (i+1)*sizeof(struct dir_entry);	// 因为如果睡眠，那么其他人（进程）可能会使用了该目录项。
			dir->i_dirt = 1;
			dir->i_ctime = CURRENT_TIME;
		}
		// 若当前搜索的目录项de的i节点为空，则表示找到一个还未使用的空闲目录项或是添加的
		// 新目录项。于是更新目录的修改时间为当前时间，并从用户数据区复制文件名到该目录项的
		// 文件名字段，置含有本目录项的相应高速缓冲块已修改标志。返回该目录项的指针以及该高
		// 速缓冲块的指针，退出。
		if (!de->inode) {
			dir->i_mtime = CURRENT_TIME;
			for (i=0; i < NAME_LEN ; i++)
				de->name[i]=(i<namelen)?get_fs_byte(name+i):0;
			bh->b_dirt = 1;
			*res_dir = de;
			return bh;
		}
		de++;
		i++;
	}
	brelse(bh);
	return NULL;
}

/*
 *	get_dir()
 *
 * Getdir traverses the pathname until it hits the topmost directory.
 * It returns NULL on failure.
 * 该函数根据给出的路径名进行搜索，直到达到最顶端的目录。
 * 如果失败则返回NULL。
 */
// 搜寻指定路径名的目录（或文件名）的i节点。
// 参数：pathname-路径名。
// 返回：目录或文件的i节点指针。失败时返回NULL。
// usr/src/linux  : src						  linux不是最顶层目录,因为后面没有/
// usr/src/linux/ : linux
// usr/src/linux/1.txt : linux                可知道最顶层目录是  [xxx]/的形式
static struct m_inode * get_dir(const char * pathname)
{
	char c;
	const char * thisname;
	struct m_inode * inode;
	struct buffer_head * bh;
	int namelen,inr,idev;
	struct dir_entry * de;

	// 搜索操作会从当前进程任务结构中设置的根（或伪根）i节点或当前工作目录i节点开始。
	// 因此首先需要判断进程的根i节点指针和当前工作目录i节点指针是否有效。如果当前进程
	// 没有设定根i节点，或者该进程根i节点指向是一个空闲i节点（引用为0），则系统出错
	// 停机。如果进程的当前工作目录i节点指针为空，或者该当前工作目录指向的i节点是一个
	// 空闲i节点，这也是系统有问题，停机。
	if (!current->root || !current->root->i_count)
		panic("No root inode");
	if (!current->pwd || !current->pwd->i_count)
		panic("No cwd inode");
	
	// 如果用户指定的路径名的第1个字符是/，则说明路径名是绝对路径名。则从根i节点开
	// 始操作。否则若第一个字符是其他字符，则表示给定的是相对路径名。应从进程的当前工作
	// 目录开始操作。则取进程当前工作目录的i节点。如果路径名为空，则出错返回NULL退出。
	// 此时变量inode指向了正确的i节点-进程的根i节点或当前工作目录i节点之一
	if ((c=get_fs_byte(pathname))=='/') {
		inode = current->root;
		pathname++;
	} else if (c)
		inode = current->pwd;
	else
		return NULL;	/* empty name is bad */
	
	// 然后针对路径名中的各个目录名部分和文件名进行循环处理。首先把得到的i节点引用计数
	// 增i，表示我们正在使用。在循环处理过程中，我们先要对当前正在处理的目录名部分（或
	// 文件名)的i节点进行有效性判断，并且把变量this_name指向当前正在处理的目录名部分
	// (或文件名)。如果该i节点不是目录类型的i节点，或者没有可进入该目录的访间许可，
	// 则放回该i节点，并返回NULL退出。当然，刚进入循环时，当前的i节点就是进程根i节
	// 点或者是当前工作目录的i节点。
	inode->i_count++;
	while (1) {						//	home/zyx/1.txt 一定是根据/去遍历的查找对应的inode
		thisname = pathname;		//  循环3次: home/zyx/1.txt    zyx/1.txt   1.txt(此时inode为zyx)
		if (!S_ISDIR(inode->i_mode) || !permission(inode,MAY_EXEC)) {
			iput(inode);
			return NULL;
		}

		// 每次循环我们处理路径名中一个目录名（或文件名）部分。因此在每次循环中我们都要从路
		// 径名字符串中分离出一个目录名（或文件名）。方法是从当前路径名指针pathname开始处
		// 搜索检测字符，直到字符是一个结尾符(NUL)或者是一个'/字符。此时变量namelen正
		// 好是当前处理目录名部分的长度，而变量thisname正指向该目录名部分的开始处。此时如
		// 果字符是结尾符ULL,则表明已经搜索到路径名末尾，并已到达最后指定目录名或文件名，
		// 则返回该i节点指针退出。
		// 注意！如果路径名中最后一个名称也是一个目录名，但其后面没有/字符，则函数不
		// 会返回该最后目录的i节点！例如：对于路径名usr/src/linux,该函数将只返回src/目录
		// 名的i节点。
		for(namelen=0;(c=get_fs_byte(pathname++))&&(c!='/');namelen++)
			/* nothing */ ;
		if (!c)
			return inode;
		// 在得到当前目录名部分（或文件名）后，我们调用查找目录项函数find_entry在当前处
		// 理的目录中寻找指定名称的目录项。如果没有找到，则放回该节点，并返回NULL退出。
		// 然后在找到的目录项中取出其i节点号inr和设备号idev,释放包含该目录项的高速缓冲
		// 块并放回该i节点。然后取节点号inr的i节点inode,并以该目录项为当前目录继续循
		// 环处理路径名中的下一目录名部分（或文件名）。
		if (!(bh = find_entry(&inode,thisname,namelen,&de))) {	// home zyx 1.txt 按顺序查
			iput(inode);
			return NULL;
		}
		inr = de->inode;
		idev = inode->i_dev;
		brelse(bh);
		iput(inode);
		if (!(inode = iget(idev,inr)))
			return NULL;
	}
}

/*
 *	dir_namei函数返回指定目录名的i节点指针，以及在最顶层目录的名称。
 */
// 参数：pathname-目录路径名：namelen-路径名长度：name-返回的最顶层目录名。
// 返回：指定目录名最顶层目录的i节点指针 | 最顶层目录名称及长度。      
// pathname: /home/zyx/video  则 namelen:5 name:video          
// 出错时返回NULL。注意!! 这里"最顶层目录"是指路径名中最靠近末端的目录。
// usr/src/linux  : 		返回src			name:linux		len:5					  
// usr/src/linux/ :			返回linux		name:""			len:0
// usr/src/linux/1.txt : 	返回linux       name:1.txt   	len:4
static struct m_inode * dir_namei(const char * pathname,
	int * namelen, const char ** name)
{
	char c;
	const char * basename;
	struct m_inode * dir;

	// 首先取得指定路径名最顶层目录的i节点。然后对路径名pathname进行搜索检测，查出
	// 最后一个/字符后面的名字字符串，计算其长度，并且返回最顶层目录的i节点指针。
	// 注意！如果路径名最后一个字符是斜杠字符'/，那么返回的目录名为空，并且长度为0。
	// 但返回的i节点指针仍然指向最后一个/'字符前目录名的i节点。参见第255行上的
	// “注意”说明。
	if (!(dir = get_dir(pathname)))
		return NULL;
	basename = pathname;
	while (c=get_fs_byte(pathname++))
		if (c=='/')
			basename=pathname;
	*namelen = pathname-basename-1;
	*name = basename;
	return dir;				// basename /home/zyx/1.txt  4  1.txt   basename /home/zyx/   zyx   0
}

/*
 *	namei()
 * 
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 */
// 取指定路径名的i节点 
// pathname:路径名
// 返回对应的i节点
// 即:
// usr/src/ 				src节点				dir_namei后返回src
// usr/src/linux			src下搜linux目录	iget后返回linux
// usr/src/linux/1.txt: 	linux下搜1.txt文件	iget后返回1.txt
struct m_inode * namei(const char * pathname)
{
	const char * basename;
	int inr,dev,namelen;
	struct m_inode * dir;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先查找指定路径的最顶层目录的目录名并得到其i节点，若不存在，则返回NULL退出。
	// 如果返回的最顶层名字的长度是0，则表示该路径名以一个目录名为最后一项。因此我们
	// 已经找到对应目录的i节点，可以直接返回该i节点退出
	if (!(dir = dir_namei(pathname,&namelen,&basename)))
		return NULL;
	if (!namelen)			/* special case: '/usr/'   以/结尾的字符串不会再继续了，在这里就返回了 */
		return dir;
	
	// 然后在返回的顶层目录中寻找指定文件名目录项的节点。注意！因为如果最后也是一个目				  
	// 录名，但其后没有加/，则不会返回该最后目录的i节点！例如：usr/src/linux,将只					// 
	// 返回src/目录名的i节点。因为函数dir_namei()把不以/结束的最后一个名字当作一个
	// 文件名来看待，所以这里需要单独对这种情况使用寻找目录项i节点函数find_entry进行
	// 处理。此时de中含有寻找到的目录项指针，而dir是包含该目录项的目录的i节点指针。
	bh = find_entry(&dir,basename,namelen,&de);//拿着该目录的indoe去找叫basename的目录项
	if (!bh) {
		iput(dir);
		return NULL;
	}

	// 接着取该目录项的i节点号和设备号，并释放包含该目录项的高速缓冲块并放回目录i节点。
	// 然后取对应节点号的i节点，修改其被访问时间为当前时间，并置已修改标志。最后返回该i节点指针。
	inr = de->inode;
	dev = dir->i_dev;
	brelse(bh);
	iput(dir);
	dir=iget(dev,inr);
	if (dir) {
		dir->i_atime=CURRENT_TIME;
		dir->i_dirt=1;
	}
	return dir;
}

/*
 *	open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.  如何实现一个open调用
 */
// namei
// 参数filename是文件名，flag是打开文件标志，它可取值：O_RDONLY(只读)、O_WRONLY
// (只写)或O_RDWR(读写)，以及O_CREAT(创建)、O_EXCL(被创建文件必须不存在)、
// O_APPEND(在文件尾添加数据)等其他一些标志的组合。如果本调用创建了一个新文件，则
// mode就用于指定文件的许可属性。这些属性有S_IRWXU(文件宿主具有读、写和执行权限)、
// S_IRUSR(用户具有读文件权限)、S_IRWXG(组成员具有读、写和执行权限)等等。对于新
// 创建的文件，这些属性只应用于将来对文件的访问，创建了只读文件的打开调用也将返回一
// 个可读写的文件句柄。参见相关文件sys/stat.h、fcntl.h。
// 返回：成功返回0，否则返回出错码：res_inode 返回对应文件路径名的i节点指针。
int open_namei(const char * pathname, int flag, int mode,
	struct m_inode ** res_inode)
{
	const char * basename;
	int inr,dev,namelen;
	struct m_inode * dir, *inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先对函数参数进行合理的处理。如果文件访问模式标志是只读(0)，但是文件截零标志
	// O_TRUNC却置位了，则在文件打开标志中添加只写标志O_WRONLY。这样做的原因是由于截零
	// 标志O_TRUNC必须在文件可写情况下才有效。然后使用当前进程的文件访问许可屏蔽码，屏
	// 蔽掉给定模式中的相应位，并添上普通文件标志I_REGULAR。该标志将用于打开的文件不存
	// 在而需要创建文件时，作为新文件的默认属性。
	if ((flag & O_TRUNC) && !(flag & O_ACCMODE))
		flag |= O_WRONLY;
	mode &= 0777 & ~current->umask;
	mode |= I_REGULAR;
	// 然后根据指定的路径名寻找到对应的i节点，以及最顶端目录名及其长度。此时如果最顶瑞
	// 目录名长度为0（例如'src/'这种路径名的情况），那么若操作不是读写、创建和文件长
	// 度截0，则表示是在打开一个目录名文件操作。于是直接返回该目录的i节点并返回0退出。
	// 否则说明进程操作非法，于是放回该i节点，返回出错码。
	if (!(dir = dir_namei(pathname,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {			/* special case: '/usr/' etc */
		if (!(flag & (O_ACCMODE|O_CREAT|O_TRUNC))) {
			*res_inode=dir;
			return 0;
		}
		iput(dir);
		return -EISDIR;
	}

	// 接着根据上面得到的最顶层目录名的i节点dir,在其中查找取得路径名字符串中最后的文
	// 件名对应的目录项结构de,并同时得到该目录项所在的高速缓冲区指针。如果该高速缓冲
	// 指针为NULL,则表示没有找到对应文件名的目录项，因此只可能是创建文件操作。此时如
	// 果不是创建文件，则放回该目录的节点，返回出错号退出。如果用户在该目录没有写的权
	// 力，则放回该目录的节点，返回出错号退出。
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		if (!(flag & O_CREAT)) {
			iput(dir);
			return -ENOENT;
		}
		if (!permission(dir,MAY_WRITE)) {
			iput(dir);
			return -EACCES;
		}

		// 现在我们确定了是创建操作并且有写操作许可。因此我们就在目录i节点对应设备上申请
		// 一个新的i节点给路径名上指定的文件使用。若失败则放回目录的i节点，并返回没有空
		// 间出错码。否则使用该新i节点，对其进行初始设置：置节点的用户id:对应节点访问模
		// 式：置已修改标志。然后并在指定目录dir中添加一个新目录项。
		inode = new_inode(dir->i_dev);
		if (!inode) {
			iput(dir);
			return -ENOSPC;
		}
		inode->i_uid = current->euid;
		inode->i_mode = mode;
		inode->i_dirt = 1;
		bh = add_entry(dir,basename,namelen,&de);
		// 如果返回的应该含有新目录项的高速缓冲区指针为NULL,则表示添加目录项操作失败。于是
		// 将该新i节点的引用连接计数诚1，放回该i节点与目录的i节点并返回出错码退出。否则
		// 说明添加目录项操作成功。于是我们来设置该新目录项的一些初始值：置i节点号为新申请
		// 到的i节点的号码：并置高速缓冲区已修改标志。然后释放该高速缓冲区，放回目录的i节
		// 点。返回新目录项的i节点指针，并成功退出。
		if (!bh) {
			inode->i_nlinks--;
			iput(inode);
			iput(dir);
			return -ENOSPC;
		}
		de->inode = inode->i_num;
		bh->b_dirt = 1;
		brelse(bh);
		iput(dir);
		*res_inode = inode;
		return 0;
	}

	// 若上面在目录中取文件名对应目录项结构的操作成功（即bh不为NULL),则说
	// 明指定打开的文件已经存在。于是取出该目录项的i节点号和其所在设备号，并释放该高速
	// 缓冲区以及放回目录的i节点。如果此时独占操作标志0EXCL置位，但现在文件已经存在，
	// 则返回文件已存在出错码退出。
	inr = de->inode;
	dev = dir->i_dev;
	brelse(bh);
	iput(dir);
	if (flag & O_EXCL)
		return -EEXIST;
	// 然后我们读取该目录项的i节点内容。若该i节点是一个目录的i节点并且访问模式是只
	// 写或读写，或者没有访问的许可权限，则放回该节点，返回访问权限出错码退出。
	if (!(inode=iget(dev,inr)))
		return -EACCES;
	if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) ||
	    !permission(inode,ACC_MODE(flag))) {
		iput(inode);
		return -EPERM;
	}
	
	// 接着我们更新该i节点的访问时间字段值为当前时间。如果设立了截0标志，则将该i节
	// 点的文件长度截为0。最后返回该目录项i节点的指针，并返回0（成功）。
	inode->i_atime = CURRENT_TIME;
	if (flag & O_TRUNC)
		truncate(inode);
	*res_inode = inode;
	return 0;
}

// 创建一个设备特殊文件或普通文件节点(node)。
// 该函数创建名称为filename,由mode和dev指定的文件系统节点（普通文件、设备特殊文件或命名管道)。
// 参数：filename-路径名：mode-指定使用许可以及所创建节点的类型：dev-设备号。
// 返回：成功则返回0，否则返回出错码。
int sys_mknod(const char * filename, int mode, int dev)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;
	
	// 首先检查操作许可和参数的有效性并取路径名中顶层目录的节点。如果不是超级用户，则
	// 返回访问许可出错码。如果找不到对应路径名中顶层目录的1节点，则返回出错码。如果最
	// 顶端的文件名长度为0，则说明给出的路径名最后没有指定文件名，放回该目录i节点，返
	// 回出错码退出。如果在该目录中没有写的权限，则放回该目录的节点，返回访问许可出错
	// 码退出。如果不是超级用户，则返回访问许可出错码。
	if (!suser())
		return -EPERM;
	if (!(dir = dir_namei(filename,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}

	// 然后我们搜索一下路径名指定的文件是否已经存在。若已经存在则不能创建同名文件节点。
	// 如果对应路径名上最后的文件名的目录项已经存在，则释放包含该目录项的缓冲区块并放回
	// 目录的i节点，返回文件已经存在的出错码退出。
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}

	// 否则我们就申请一个新的i节点，并设置该节点的属性摸式。如果要创建的是块设备文件
	// 或者是字符设备文件，则令i节点的直接逻辑块指针0等于设备号。即对于设备文件来说，
	// 其i节点的i_zone[0]中存放的是该设备文件所定义设备的设备号。然后设置该i节点的修
	// 改时间、访间时间为当前时间，并设置i节点已修改标志。
	inode = new_inode(dir->i_dev);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_mode = mode;
	if (S_ISBLK(mode) || S_ISCHR(mode))
		inode->i_zone[0] = dev;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	inode->i_dirt = 1;
	// 接着为这个新的i节点在目录中新添加一个目录项。如果失收（包含该目录项的高速缓冲
	// 块指针为NULL),则放回目录的i节点：把所申请的i节点引用连接计数复位，并放回该
	// i节点，返回出错码退出。
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		inode->i_nlinks=0;
		iput(inode);
		return -ENOSPC;
	}
	// 现在添加目录项操作也成功了，于是我们来设置这个目录项内容。令该目录项的节点字
	// 段等于新i节点号，并置高速缓冲区已修改标志，放回目录和新的i节点，释放高速缓冲
	// 区，最后返回0（成功）。
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}


// 创建一个目录。
// 参数：pathname-路径名：mode-目录使用的权限属性。
// 返回：成功则返回0，否则返回出错码。
int sys_mkdir(const char * pathname, int mode)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh, *dir_block;
	struct dir_entry * de;

	// 首先检查操作许可和参数的有效性并取路径名中顶层目录的i节点。
	// 如果不是超级用户，则返回访问许可出错码。
	// 如果找不到对应路径名中顶层目录的i节点，则返回出错码。
	// 如果最顶端的文件名长度为0，则说明给出的路径名最后没有指定文件名，放回该目录i节点，返回出错码退出。
	// 如果在该目录中没有写的权限，则放回该目录的节点，返回访问许可出错码退出。
	if (!suser())
		return -EPERM;
	if (!(dir = dir_namei(pathname,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}

	// 然后我们搜索一下路径名指定的目录名是否已经存在。若已经存在则不能创建同名目录节点。
	// 如果对应路径名上最后的目录名的目录项已经存在，则释放包含该目录项的缓冲区块并放回
	// 目录的i节点，返回文件已经存在的出错码退出。否则我们就申请一个新的i节点，并设置
	// 该i节点的属性模式：置该新i节点对应的文件长度为32字节(2个目录项的大小)、置
	// 节点己修改标志，以及节点的修改时间和访问时间。2个目录项分别用于.和..。
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = new_inode(dir->i_dev);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_size = 32;
	inode->i_dirt = 1;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	
	// 接着为该新i节点申请一用于保存目录项数据的磁盘块，用于保存目录项结构信息。并令i
	// 节点的第一个直接块指针等于该块号。如果申请失败则放回对应目录的i节点：复位新申请
	// 的i节点连接计数：放回该新的i节点，返回没有空间出错码退出。否则置该新的i节点已
	// 修改标志。
	if (!(inode->i_zone[0]=new_block(inode->i_dev))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ENOSPC;
	}
	inode->i_dirt = 1;
	
	// 从设备上读取新申请的磁盘块（目的是把对应块放到高速缓冲区中）。若出错，则放回对应
	// 目录的节点：释放申请的磁盘块：复位新申请的i节点连接计数：放回该新的i节点，返
	// 回没有空间出错码退出。
	if (!(dir_block=bread(inode->i_dev,inode->i_zone[0]))) {
		iput(dir);
		free_block(inode->i_dev,inode->i_zone[0]);
		inode->i_nlinks--;
		iput(inode);
		return -ERROR;
	}

	// 然后我们在缓冲块中建立起所创建目录文件中的2个默认的新目录项(’.’和'..’)结构数
	// 据。首先令de指向存放目录项的数据块，然后置该目录项的i节点号字段等于新申请的i
	// 节点号，名字字段等于”.”。然后e指向下一个目录项结构，并在该结构中存放上级目录
	// 的i节点号和名字”.”。然后设置该高速缓冲块已修改标志，并释放该缓冲块。再初始化
	// 设置新i节点的模式字段，并置该i节点己修改标志。
	de = (struct dir_entry *) dir_block->b_data;
	de->inode=inode->i_num;
	strcpy(de->name,".");
	de++;
	de->inode = dir->i_num;
	strcpy(de->name,"..");
	inode->i_nlinks = 2;
	dir_block->b_dirt = 1;
	brelse(dir_block);
	inode->i_mode = I_DIRECTORY | (mode & 0777 & ~current->umask);
	inode->i_dirt = 1;

	// 现在我们在指定目录中新添加一个目录项，用于存放新建目录的节点号和目录名。如果
	// 失败（包含该目录项的高速缓冲区指针为NULL),则放回目录的i节点：所申请的i节点
	// 引用连接计数复位，并放回该1节点。返回出错码退出。
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		free_block(inode->i_dev,inode->i_zone[0]);
		inode->i_nlinks=0;
		iput(inode);
		return -ENOSPC;
	}

	// 最后令该新目录项的i节点字段等于新i节点号，并置高速缓冲块已修改标志，放回目录
	// 和新的i节点，释放高速缓冲块，最后返回0（成功）。
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	dir->i_nlinks++;
	dir->i_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
static int empty_dir(struct m_inode * inode)
{
	int nr,block;
	int len;
	struct buffer_head * bh;
	struct dir_entry * de;

	len = inode->i_size / sizeof (struct dir_entry);
	if (len<2 || !inode->i_zone[0] ||
	    !(bh=bread(inode->i_dev,inode->i_zone[0]))) {
	    	printk("warning - bad directory on dev %04x\n",inode->i_dev);
		return 0;
	}
	de = (struct dir_entry *) bh->b_data;
	if (de[0].inode != inode->i_num || !de[1].inode || 
	    strcmp(".",de[0].name) || strcmp("..",de[1].name)) {
	    	printk("warning - bad directory on dev %04x\n",inode->i_dev);
		return 0;
	}
	nr = 2;
	de += 2;
	while (nr<len) {
		if ((void *) de >= (void *) (bh->b_data+BLOCK_SIZE)) {
			brelse(bh);
			block=bmap(inode,nr/DIR_ENTRIES_PER_BLOCK);
			if (!block) {
				nr += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			if (!(bh=bread(inode->i_dev,block)))
				return 0;
			de = (struct dir_entry *) bh->b_data;
		}
		if (de->inode) {
			brelse(bh);
			return 0;
		}
		de++;
		nr++;
	}
	brelse(bh);
	return 1;
}

int sys_rmdir(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (!suser())
		return -EPERM;
	if (!(dir = dir_namei(name,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if ((dir->i_mode & S_ISVTX) && current->euid &&
	    inode->i_uid != current->euid) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	if (inode->i_dev != dir->i_dev || inode->i_count>1) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	if (inode == dir) {	/* we may not delete ".", but "../dir" is ok */
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTDIR;
	}
	if (!empty_dir(inode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTEMPTY;
	}
	if (inode->i_nlinks != 2)
		printk("empty directory has nlink!=2 (%d)",inode->i_nlinks);
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	inode->i_nlinks=0;
	inode->i_dirt=1;
	dir->i_nlinks--;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_dirt=1;
	iput(dir);
	iput(inode);
	return 0;
}

int sys_unlink(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (!(dir = dir_namei(name,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -ENOENT;
	}
	if ((dir->i_mode & S_ISVTX) && !suser() &&
	    current->euid != inode->i_uid &&
	    current->euid != dir->i_uid) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	if (S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!inode->i_nlinks) {
		printk("Deleting nonexistent file (%04x:%d), %d\n",
			inode->i_dev,inode->i_num,inode->i_nlinks);
		inode->i_nlinks=1;
	}
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	inode->i_nlinks--;
	inode->i_dirt = 1;
	inode->i_ctime = CURRENT_TIME;
	iput(inode);
	iput(dir);
	return 0;
}

int sys_link(const char * oldname, const char * newname)
{
	struct dir_entry * de;
	struct m_inode * oldinode, * dir;
	struct buffer_head * bh;
	const char * basename;
	int namelen;

	oldinode=namei(oldname);
	if (!oldinode)
		return -ENOENT;
	if (S_ISDIR(oldinode->i_mode)) {
		iput(oldinode);
		return -EPERM;
	}
	dir = dir_namei(newname,&namelen,&basename);
	if (!dir) {
		iput(oldinode);
		return -EACCES;
	}
	if (!namelen) {
		iput(oldinode);
		iput(dir);
		return -EPERM;
	}
	if (dir->i_dev != oldinode->i_dev) {
		iput(dir);
		iput(oldinode);
		return -EXDEV;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		iput(oldinode);
		return -EACCES;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		iput(oldinode);
		return -EEXIST;
	}
	bh = add_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		iput(oldinode);
		return -ENOSPC;
	}
	de->inode = oldinode->i_num;
	bh->b_dirt = 1;
	brelse(bh);
	iput(dir);
	oldinode->i_nlinks++;
	oldinode->i_ctime = CURRENT_TIME;
	oldinode->i_dirt = 1;
	iput(oldinode);
	return 0;
}
