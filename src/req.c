#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "vmm.h"

/* 访存请求 */
Ptr_MemoryAccessRequest ptr_memAccReq;
/* FIFO file descriptor */
int fifo;

int main()
{
	if ((fifo=open("/tmp/req",O_WRONLY)) < 0)
		puts("req open fifo failed");
	ptr_memAccReq = (Ptr_MemoryAccessRequest) malloc(REQ_LEN);
	srandom(time(NULL));
	do_request();
	if (write(fifo, ptr_memAccReq, REQ_LEN) < 0)
		puts("req write failed");
	return 0;
}

/* 产生访存请求 */
void do_request()
{
	/* 随机产生请求地址 */
	ptr_memAccReq->virAddr = random() % VIRTUAL_MEMORY_SIZE;
	/* 随机产生请求类型 */
	switch (random() % 3)
	{
		case 0: //读请求
		{
			ptr_memAccReq->reqType = REQUEST_READ;
			printf("产生请求：\n地址：%lu\t类型：读取\n", ptr_memAccReq->virAddr);
			break;
		}
		case 1: //写请求
		{
			ptr_memAccReq->reqType = REQUEST_WRITE;
			/* 随机产生待写入的值 */
			ptr_memAccReq->value = random() % 0xFFu;
			printf("产生请求：\n地址：%lu\t类型：写入\t值：%02X\n", ptr_memAccReq->virAddr, ptr_memAccReq->value);
			break;
		}
		case 2:
		{
			ptr_memAccReq->reqType = REQUEST_EXECUTE;
			printf("产生请求：\n地址：%lu\t类型：执行\n", ptr_memAccReq->virAddr);
			break;
		}
		default:
			break;
	}	
}
