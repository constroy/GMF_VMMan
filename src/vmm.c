#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "vmm.h"

//二级页表
PageTableItem bi_pageTable[FIRST_TABLE_SIZE][SECOND_TABLE_SIZE];

/* 实存空间 */
BYTE actMem[ACTUAL_MEMORY_SIZE];
/* 用文件模拟辅存空间 */
FILE *ptr_auxMem;
/* 物理块使用标识 */
BOOL blockStatus[BLOCK_SUM];
/* 访存请求 */
Ptr_MemoryAccessRequest ptr_memAccReq;
/* FIFO file descriptor */
int fifo;

/* 初始化环境 */
void do_init()
{
	int i, j, k;
	int firstNum, secondNum;
	srandom(time(NULL));

	for(i = 0; i < PAGE_SUM; i++)
	{
		firstNum = i / SECOND_TABLE_SIZE;
		secondNum = i % SECOND_TABLE_SIZE;

		bi_pageTable[firstNum][secondNum].pageNum = i;
		bi_pageTable[firstNum][secondNum].filled = FALSE;
		bi_pageTable[firstNum][secondNum].edited = FALSE;
		bi_pageTable[firstNum][secondNum].count = 0;
		bi_pageTable[firstNum][secondNum].ownerID = random() % MAX_PROC_NUM;
		//页面老化算法页表项参数初始化
		bi_pageTable[firstNum][secondNum].R = 0;
		for(k = 0; k < 8; k++) {
			bi_pageTable[firstNum][secondNum].counter[k] = 0;
		}

		//使用随机数设置该页的保护类型
		bi_pageTable[firstNum][secondNum].proType = random() % 7 + 1;
		//设置该页对应的辅存地址 
		bi_pageTable[firstNum][secondNum].auxAddr = i * PAGE_SIZE;
	}

	for (j = 0; j < BLOCK_SUM; j++)
	{
		if (random() % 2 == 0)
		{
			firstNum = j / SECOND_TABLE_SIZE;
			secondNum = j % SECOND_TABLE_SIZE;
			do_page_in(&bi_pageTable[firstNum][secondNum], j);
			bi_pageTable[firstNum][secondNum].blockNum = j;
			bi_pageTable[firstNum][secondNum].filled = TRUE;
			blockStatus[j] = TRUE;
		}
		else
		{
			blockStatus[j] = FALSE;
		}
	}
}


/* 响应请求 */
void do_response()
{
	Ptr_PageTableItem ptr_pageTabIt;
	unsigned int offAddr, firstNum, secondNum;
	unsigned int actAddr;
	unsigned int i;	
	int j;	

	
	/* 检查地址是否越界 */
	if (ptr_memAccReq->virAddr < 0 || ptr_memAccReq->virAddr >= VIRTUAL_MEMORY_SIZE)
	{
		do_error(ERROR_OVER_BOUNDARY);
		return;
	}

	// 计算一级页表页号,二级页表页号和页内偏移值 
	firstNum = ptr_memAccReq->virAddr / PAGE_SIZE / SECOND_TABLE_SIZE;
	secondNum = ptr_memAccReq->virAddr / PAGE_SIZE % SECOND_TABLE_SIZE;
	offAddr = ptr_memAccReq->virAddr % PAGE_SIZE;
	printf("一级页表页号为：%u\t二级页表页号为：%u\t页内偏移为：%u\n", firstNum, secondNum, offAddr);
	printf("第%u页\n", firstNum * SECOND_TABLE_SIZE + secondNum);

	/* 获取对应页表项 */
//	ptr_pageTabIt = &pageTable[pageNum];
							
	//在页表中获取页表项
	ptr_pageTabIt = &bi_pageTable[firstNum][secondNum];
	
	/* 根据特征位决定是否产生缺页中断 */
	if (!ptr_pageTabIt->filled)
	{
		do_page_fault(ptr_pageTabIt);
	}
	
	actAddr = ptr_pageTabIt->blockNum * PAGE_SIZE + offAddr;
		
	//实地址
	printf("实地址为：%u\n", actAddr);

	/*每进行一次请求执行均更新页面老化算法访问位*/
	for(i = 0; i < PAGE_SUM; i++)
	{
		firstNum = i / SECOND_TABLE_SIZE;
		secondNum = i % SECOND_TABLE_SIZE;
		bi_pageTable[firstNum][secondNum].R = 0;
	}
	/* 检查process访问权限 */
	if (ptr_memAccReq->PID != ptr_pageTabIt->ownerID)
	{
		do_error(ERROR_NO_AUTHORUTY);
		return;
	}
	/* 检查页面访问权限并处理访存请求 */
	switch (ptr_memAccReq->reqType)
	{
		case REQUEST_READ: //读请求
		{
			ptr_pageTabIt->count++;
			if (!(ptr_pageTabIt->proType & READABLE)) //页面不可读
			{
				do_error(ERROR_READ_DENY);
				return;
			}

			/*页面老化算法更新访问位及访问计数器*/
			ptr_pageTabIt->R = 1;
			
			/* 读取实存中的内容 */
			printf("读操作成功：值为%02X\n", actMem[actAddr]);
			break;
		}
		case REQUEST_WRITE: //写请求
		{
			ptr_pageTabIt->count++;
			if (!(ptr_pageTabIt->proType & WRITABLE)) //页面不可写
			{
				do_error(ERROR_WRITE_DENY);	
				return;
			}

			/*页面老化算法更新访问位及访问计数器*/
			ptr_pageTabIt->R = 1;

			/* 向实存中写入请求的内容 */
			actMem[actAddr] = ptr_memAccReq->value;
			ptr_pageTabIt->edited = TRUE;			
			printf("写操作成功\n");
			break;
		}
		case REQUEST_EXECUTE: //执行请求
		{
			ptr_pageTabIt->count++;
			if (!(ptr_pageTabIt->proType & EXECUTABLE)) //页面不可执行
			{
				do_error(ERROR_EXECUTE_DENY);
				return;
			}
			
			/*页面老化算法更新访问位及访问计数器*/
			ptr_pageTabIt->R = 1;

			printf("执行成功\n");
			break;
		}
		default: //非法请求类型
		{	
			do_error(ERROR_INVALID_REQUEST);
			return;
		}
	}

	/*每进行一次请求执行均更新页面老化算法访问计数位*/
	for(i = 0; i < PAGE_SUM; i++) {
		firstNum = i / SECOND_TABLE_SIZE;
		secondNum = i % SECOND_TABLE_SIZE;
		for(j = 6; j >= 0; j--) {
			bi_pageTable[firstNum][secondNum].counter[j+1] = bi_pageTable[firstNum][secondNum].counter[j];
		}
		bi_pageTable[firstNum][secondNum].counter[0] = bi_pageTable[firstNum][secondNum].R;
	}
}

/* 处理缺页中断 */
void do_page_fault(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int i, k;
	printf("产生缺页中断，开始进行调页...\n");
	for (i = 0; i < BLOCK_SUM; i++)
	{
		if (!blockStatus[i])
		{
			/* 读辅存内容，写入到实存 */
			do_page_in(ptr_pageTabIt, i);
			
			/* 更新页表内容 */
			ptr_pageTabIt->blockNum = i;
			ptr_pageTabIt->filled = TRUE;
			ptr_pageTabIt->edited = FALSE;
			ptr_pageTabIt->count = 0;

			//页面老化算法参数更新
			ptr_pageTabIt->R = 0;
			for(k = 0; k < 8; k++) {
				ptr_pageTabIt->counter[k] = 0;
			}
			
			blockStatus[i] = TRUE;
			return;
		}
	}
	/* 没有空闲物理块，进行页面替换 */
	//do_LFU(ptr_pageTabIt);
	do_pageAging(ptr_pageTabIt);
}

/* 根据LFU算法进行页面替换 */
void do_LFU(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int i, min, page;
	int firstNum, secondNum;
	printf("没有空闲物理块，开始进行LFU页面替换...\n");
	for (i = 0, min = 0xFFFFFFFF, page = 0; i < PAGE_SUM; i++)
	{
		firstNum = i / SECOND_TABLE_SIZE;
		secondNum = i % SECOND_TABLE_SIZE;
		// if (pageTable[i].count < min)
		// {
		// 	min = pageTable[i].count;
		// 	page = i;
		// }
		if(bi_pageTable[firstNum][secondNum].filled == TRUE && bi_pageTable[firstNum][secondNum].count < min)
		{
			min = bi_pageTable[firstNum][secondNum].count;
			page = i;
		}
	}
	printf("选择第%u页进行替换\n", page);

	// convert page to firstNum & secondNum
	firstNum = page / SECOND_TABLE_SIZE;
	secondNum = page % SECOND_TABLE_SIZE;

	// if (pageTable[page].edited)
	// {
	// 	/* 页面内容有修改，需要写回至辅存 */
	// 	printf("该页内容有修改，写回至辅存\n");
	// 	do_page_out(&pageTable[page]);
	// }
	// pageTable[page].filled = FALSE;
	// pageTable[page].count = 0;

	if(bi_pageTable[firstNum][secondNum].edited)
	{
		/* 页面内容有修改，需要写回至辅存 */
		printf("该页内容有修改，写回至辅存\n");
		do_page_out(&bi_pageTable[firstNum][secondNum]);
	}
	bi_pageTable[firstNum][secondNum].filled = FALSE;
	bi_pageTable[firstNum][secondNum].count = 0;

	/* 读辅存内容，写入到实存 */
	// do_page_in(ptr_pageTabIt, pageTable[page].blockNum);
	do_page_in(ptr_pageTabIt, bi_pageTable[firstNum][secondNum].blockNum);
	
	/* 更新页表内容 */
	// ptr_pageTabIt->blockNum = pageTable[page].blockNum;
	ptr_pageTabIt->blockNum = bi_pageTable[firstNum][secondNum].blockNum;
	ptr_pageTabIt->filled = TRUE;
	ptr_pageTabIt->edited = FALSE;
	ptr_pageTabIt->count = 0;
	printf("页面替换成功\n");
}

/* 根据页面老化算法进行页面替换 */
void do_pageAging(Ptr_PageTableItem ptr_pageTabIt) {
	
	unsigned int min[8], i, j, k, page;
	int firstNum, secondNum;
	for(k = 0; k < 8; k++) {
		min[k] = 1;
	}
	printf("没有空闲物理块，开始进行页面老化页面替换...\n");

	for (i = 0, page = 0; i < PAGE_SUM; i++)
	{
		firstNum = i / SECOND_TABLE_SIZE;
		secondNum = i % SECOND_TABLE_SIZE;

		if(bi_pageTable[firstNum][secondNum].filled == TRUE)
		{
			for(k = 0; k < 8; k++)
			{
				firstNum = i / SECOND_TABLE_SIZE;
				secondNum = i % SECOND_TABLE_SIZE;
				if (bi_pageTable[firstNum][secondNum].counter[k] < min[k])
				{
					for(j = 0; j < 8; j++)
					{
						min[j] = bi_pageTable[firstNum][secondNum].counter[j]; 
					}
					page = i;
					break;
				}
			}
		}
	}
	printf("选择第%u页进行替换\n", page);

	// convert page to firstNum & secondNum
	firstNum = page / SECOND_TABLE_SIZE;
	secondNum = page % SECOND_TABLE_SIZE;

	// if (pageTable[page].edited)
	// {
	// 	/* 页面内容有修改，需要写回至辅存 */
	// 	printf("该页内容有修改，写回至辅存\n");
	// 	do_page_out(&pageTable[page]);
	// }
	// pageTable[page].filled = FALSE;
	// pageTable[page].count = 0;

	if(bi_pageTable[firstNum][secondNum].edited)
	{
		/* 页面内容有修改，需要写回至辅存 */
		printf("该页内容有修改，写回至辅存\n");
		do_page_out(&bi_pageTable[firstNum][secondNum]);
	}
	bi_pageTable[firstNum][secondNum].filled = FALSE;
	bi_pageTable[firstNum][secondNum].count = 0;


	/* 读辅存内容，写入到实存 */
	// do_page_in(ptr_pageTabIt, pageTable[page].blockNum);
	do_page_in(ptr_pageTabIt, bi_pageTable[firstNum][secondNum].blockNum);
	
	/* 更新页表内容 */
	// ptr_pageTabIt->blockNum = pageTable[page].blockNum;
	ptr_pageTabIt->blockNum = bi_pageTable[firstNum][secondNum].blockNum;
	ptr_pageTabIt->filled = TRUE;
	ptr_pageTabIt->edited = FALSE;
	ptr_pageTabIt->count = 0;

	//页面老化算法参数更新
	ptr_pageTabIt->R = 0;
	for(k = 0; k < 8; k++) {
		ptr_pageTabIt->counter[k] = 0;
	}

	printf("页面替换成功\n");		
}

/* 将辅存内容写入实存 */
void do_page_in(Ptr_PageTableItem ptr_pageTabIt, unsigned int blockNum)
{
	unsigned int readNum;
	if (fseek(ptr_auxMem, ptr_pageTabIt->auxAddr, SEEK_SET) < 0)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%lu\tftell=%lu\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
#endif
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}
	if ((readNum = fread(actMem + blockNum * PAGE_SIZE, 
		sizeof(BYTE), PAGE_SIZE, ptr_auxMem)) < PAGE_SIZE)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%lu\tftell=%lu\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
		printf("DEBUG: blockNum=%u\treadNum=%u\n", blockNum, readNum);
		printf("DEGUB: feof=%d\tferror=%d\n", feof(ptr_auxMem), ferror(ptr_auxMem));
#endif
		do_error(ERROR_FILE_READ_FAILED);
		exit(1);
	}
	printf("调页成功：辅存地址%lu-->>物理块%u\n", ptr_pageTabIt->auxAddr, blockNum);
}

/* 将被替换页面的内容写回辅存 */
void do_page_out(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int writeNum;
	if (fseek(ptr_auxMem, ptr_pageTabIt->auxAddr, SEEK_SET) < 0)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%p\tftell=%ld\n", ptr_pageTabIt, ftell(ptr_auxMem));
#endif
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}
	if ((writeNum = fwrite(actMem + ptr_pageTabIt->blockNum * PAGE_SIZE, 
		sizeof(BYTE), PAGE_SIZE, ptr_auxMem)) < PAGE_SIZE)
	{
#ifdef DEBUG
		printf("DEBUG: auxAddr=%lu\tftell=%ld\n", ptr_pageTabIt->auxAddr, ftell(ptr_auxMem));
		printf("DEBUG: writeNum=%u\n", writeNum);
		printf("DEGUB: feof=%d\tferror=%d\n", feof(ptr_auxMem), ferror(ptr_auxMem));
#endif
		do_error(ERROR_FILE_WRITE_FAILED);
		exit(1);
	}
	printf("写回成功：物理块%lu-->>辅存地址%03X\n", ptr_pageTabIt->auxAddr, ptr_pageTabIt->blockNum);
}

/* 错误处理 */
void do_error(ERROR_CODE code)
{
	switch (code)
	{
		case ERROR_NO_AUTHORUTY:
		{
			printf("访存失败：该进程没有权限\n");
			break;
		}
		case ERROR_READ_DENY:
		{
			printf("访存失败：该地址内容不可读\n");
			break;
		}
		case ERROR_WRITE_DENY:
		{
			printf("访存失败：该地址内容不可写\n");
			break;
		}
		case ERROR_EXECUTE_DENY:
		{
			printf("访存失败：该地址内容不可执行\n");
			break;
		}		
		case ERROR_INVALID_REQUEST:
		{
			printf("访存失败：非法访存请求\n");
			break;
		}
		case ERROR_OVER_BOUNDARY:
		{
			printf("访存失败：地址越界\n");
			break;
		}
		case ERROR_FILE_OPEN_FAILED:
		{
			printf("系统错误：打开文件失败\n");
			break;
		}
		case ERROR_FILE_CLOSE_FAILED:
		{
			printf("系统错误：关闭文件失败\n");
			break;
		}
		case ERROR_FILE_SEEK_FAILED:
		{
			printf("系统错误：文件指针定位失败\n");
			break;
		}
		case ERROR_FILE_READ_FAILED:
		{
			printf("系统错误：读取文件失败\n");
			break;
		}
		case ERROR_FILE_WRITE_FAILED:
		{
			printf("系统错误：写入文件失败\n");
			break;
		}
		default:
		{
			printf("未知错误：没有这个错误代码\n");
		}
	}
}

/* 打印页表 */
void do_print_info()
{
	unsigned int i;
	char str[4];
	int firstNum, secondNum;
	
	printf("页号\t进程\t块号\t装入\t修改\t保护\t计数\t辅存\t访问位\t访问计数\n");

	for (i = 0; i < PAGE_SUM; i++)
	{
		firstNum = i / SECOND_TABLE_SIZE;
		secondNum = i % SECOND_TABLE_SIZE;
		// printf("%u\t%u\t%u\t%u\t%s\t%lu\t%lu\n", i, pageTable[i].blockNum, pageTable[i].filled, 
		// 	pageTable[i].edited, get_proType_str(str, pageTable[i].proType), 
		// 	pageTable[i].count, pageTable[i].auxAddr);
		printf("%u\t%u\t%u\t%u\t%u\t%s\t%lu\t%lu\t%d\t%d%d%d%d%d%d%d%d\n", i, 
		bi_pageTable[firstNum][secondNum].ownerID,
		bi_pageTable[firstNum][secondNum].blockNum, 
		bi_pageTable[firstNum][secondNum].filled,
		bi_pageTable[firstNum][secondNum].edited, 
		get_proType_str(str, bi_pageTable[firstNum][secondNum].proType),
		bi_pageTable[firstNum][secondNum].count,
		bi_pageTable[firstNum][secondNum].auxAddr,
		bi_pageTable[firstNum][secondNum].R,
		bi_pageTable[firstNum][secondNum].counter[0],bi_pageTable[firstNum][secondNum].counter[1],
		bi_pageTable[firstNum][secondNum].counter[2],bi_pageTable[firstNum][secondNum].counter[3],
		bi_pageTable[firstNum][secondNum].counter[4],bi_pageTable[firstNum][secondNum].counter[5],
		bi_pageTable[firstNum][secondNum].counter[6],bi_pageTable[firstNum][secondNum].counter[7]);
	}
	printf("\n");
}

/* 打印实存 */
void do_print_shicun()
{
       int i=0;
       int j=0;
       int m=0;
	printf("页号\t数据\t\n");
	for(i=0;i<BLOCK_SUM;i++)
    {
             // m=0;
        if(i<10)
        {
	      printf("0%d\t",i);
        }
        else 
             printf("%d\t",i);
	      for(j=0;j<PAGE_SIZE;j++)
               {
                       
			printf("%c",actMem[m]);
                         m++;
	       }
		printf("\n");
	}
}

/* 打印辅存 */
void do_print_fucun()
{
    BYTE temp[VIRTUAL_MEMORY_SIZE + 9];
    int i,j,m,k,num;
	k=fseek(ptr_auxMem, 0, SEEK_SET) ;
	if (k < 0)   
	{
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}

       num = fread(temp, sizeof(BYTE), VIRTUAL_MEMORY_SIZE, ptr_auxMem);
	if (num < VIRTUAL_MEMORY_SIZE)
	{
		do_error(ERROR_FILE_READ_FAILED);
		exit(1);
	}
	printf("页号\t数据\t\n");
	m=0;
	for(i=0;i<PAGE_SUM;i++)
	{
	   if(i<10)
		{
		  printf("0%d\t",i);
		}
		else 
		printf("%d\t",i);

		for(j=0;j<PAGE_SIZE;j++)
               {
                       
			printf("%c",temp[m]);
                          m++;
		}
		printf("\n");
	}



}


/* 获取页面保护类型字符串 */
char *get_proType_str(char *str, BYTE type)
{
	if (type & READABLE)
		str[0] = 'r';
	else
		str[0] = '-';
	if (type & WRITABLE)
		str[1] = 'w';
	else
		str[1] = '-';
	if (type & EXECUTABLE)
		str[2] = 'x';
	else
		str[2] = '-';
	str[3] = '\0';
	return str;
}

void init_file()
{
	int i;
	char* key = "0123456789ABCDEFGHIJKLMNOPQRSTUVMWYZabcdefghijklmnopqrstuvwxyz";
	char buffer[VIRTUAL_MEMORY_SIZE + 1];

	if (!(ptr_auxMem = fopen(AUXILIARY_MEMORY, "w+")))
	{
		do_error(ERROR_FILE_OPEN_FAILED);
		exit(1);
	}
	for(i=0; i<VIRTUAL_MEMORY_SIZE-3; i++)
	{
		buffer[i] = key[rand() % 62];
	}
	buffer[VIRTUAL_MEMORY_SIZE-3] = 'y';
	buffer[VIRTUAL_MEMORY_SIZE-2] = 'm';
	buffer[VIRTUAL_MEMORY_SIZE-1] = 'c';
	buffer[VIRTUAL_MEMORY_SIZE] = '\0';

	//随机生成256位字符串
	fwrite(buffer, sizeof(BYTE), VIRTUAL_MEMORY_SIZE, ptr_auxMem);
	/*
	size_t fwrite(const void* buffer, size_t size, size_t count, FILE* stream)
	*/
	fclose(ptr_auxMem);
	printf("系统提示：初始化辅存模拟文件完成\n");
	
}

void init_fifo()
{
	/* 删除FIFO */
	unlink("/tmp/req");
	
	if (mkfifo("/tmp/req", 0666) < 0)
	{
		puts("mkfifo failed");
		exit(1);
	}
	
	/* 在非阻塞模式下打开FIFO */
	if ((fifo = open("/tmp/req", O_RDONLY|O_NONBLOCK)) < 0)
	{
		puts("open fifo failed");
		exit(1);
	}
}
int main(int argc, char* argv[])
{
	char c;
	init_file();
	if (!(ptr_auxMem = fopen(AUXILIARY_MEMORY, "r+")))
	{
		do_error(ERROR_FILE_OPEN_FAILED);
		exit(1);
	}
	do_init();
	do_print_info();
	ptr_memAccReq = (Ptr_MemoryAccessRequest) malloc(REQ_LEN);
	//创建FIFO
	init_fifo();
	/* 在循环中模拟访存请求与处理过程 */
	while (TRUE)
	{
		//printf("count: %d\n",count);
		if (read(fifo,ptr_memAccReq,REQ_LEN) == REQ_LEN)
		{
			do_response();
			printf("按Y打印页表，按其他键不打印...\n");
			if ((c = getchar()) == 'y' || c == 'Y')
				do_print_info();
			while (c != '\n')
				c = getchar();
                printf("按1打印实存，按其他键不打印...\n");
		if ((c = getchar()) == '1' || c == '1')
			do_print_shicun();
		while (c != '\n')
			c = getchar();   
                printf("\n按2打印辅存，按其他键不打印...\n");
		if ((c = getchar()) == '2' || c == '2')
			do_print_fucun();
		while (c != '\n')
			c = getchar();  
			printf("按X退出程序，按其他键继续...\n");
			if ((c = getchar()) == 'x' || c == 'X')
				break;
			while (c != '\n')
				c = getchar();
		}
	}
	close(fifo);
	if (fclose(ptr_auxMem) == EOF)
	{
		do_error(ERROR_FILE_CLOSE_FAILED);
		exit(1);
	}
	return (0);
}
