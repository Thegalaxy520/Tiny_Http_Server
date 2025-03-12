#include <stdio.h>
#include "server.h"
#include <stdlib.h>
#include <unistd.h>
int main(int argc ,char * argv[])
{
	if (argc <3)
	{
		printf("请输入端口号\n例如:./a.out 8899 path/\n");
		return 0;
	}
	//切换软件的工作目录
	chdir(argv[2]);
	unsigned short port = atoi(argv[1]);
	epoll_run(port);
	return 0;
}