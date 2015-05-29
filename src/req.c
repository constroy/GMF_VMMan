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
	if(!do_request())
		return 0 ;
	if (write(fifo, ptr_memAccReq, REQ_LEN) < 0)
		puts("req write failed");
	return 0;
}

/* 产生访存请求 */
BOOL do_request()
{
	char type[80];
	int  add,req_value;
	char req_type; 
	printf("选择输入请求模式,random/nonrandom\n");
	scanf("%s",type);
	if(strcmp(type,"random")==0){
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
	else if(strcmp(type,"nonrandom")==0){
		printf("输入请求格式,地址-模式 (写入值)\n");
		scanf("%d-%c",&add,&req_type);
		if(add>=VIRTUAL_MEMORY_SIZE){
		printf("请求地址超界,请小于%d\n",VIRTUAL_MEMORY_SIZE);
		return FALSE;		
		}
		ptr_memAccReq->virAddr=add;
		switch (req_type)
		{
		case 'r': //读请求
		{
			ptr_memAccReq->reqType = REQUEST_READ;
			printf("产生请求：\n地址：%lu\t类型：读取\n", ptr_memAccReq->virAddr);
			break;
		}
		case 'w': //写请求
		{
			ptr_memAccReq->reqType = REQUEST_WRITE;
			/* 随机产生待写入的值 */
			//ptr_memAccReq->value = random() % 0xFFu;
			scanf("-%d",&req_value);
			if(req_value>=0xFFu){
				printf("写入值应小于%02X\n",0xFFu);
				return FALSE;			
			}
			ptr_memAccReq->value=req_value % 0xFFu;
			printf("产生请求：\n地址：%lu\t类型：写入\t值：%02X\n", ptr_memAccReq->virAddr, ptr_memAccReq->value);
			break;
		}
		case 'e':
		{
			ptr_memAccReq->reqType = REQUEST_EXECUTE;
			printf("产生请求：\n地址：%lu\t类型：执行\n", ptr_memAccReq->virAddr);
			break;
		}
		default:
			break;
	}
	
	}else{
	printf("没有选择正确模式\n");		
	return FALSE;
	}return TRUE;
}
