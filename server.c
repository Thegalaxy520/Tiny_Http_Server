#include "server.h"

#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <asm-generic/errno-base.h>
#include <sys/stat.h>

int initSockFD(unsigned short port)
{
	//创建通信的套接字
	int sfd = socket(AF_INET,SOCK_STREAM, 0);
	if (sfd < 0)
	{
		perror("Socket Error ");
		exit(-1);
	}
	//设置端口复用
	int opt = 1;
	if (setsockopt(sfd,SOL_SOCKET,SO_REUSEPORT, &opt, sizeof(opt)) < 0)
	{
		perror("set Sock Option Error");
		exit(-1);
	}
	//绑定
	struct sockaddr_in addr;
	addr.sin_port = htons(port);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
	{
		perror("bind Error");
		exit(-1);
	}
	//设置监听
	if (listen(sfd, SOMAXCONN) == -1)
	{
		perror("Listen Error");
		exit(-1);
	}
	return sfd;
}

int epoll_run(unsigned short port)
{
	//1.创建epoll模型
	int ep = epoll_create(128);
	if (ep < 0)
	{
		perror("epoll Create Error");
		exit(-1);
	}
	int sfd = initSockFD(port); //获得通信套接字
	struct epoll_event event;
	event.data.fd = sfd;
	event.events = EPOLLIN;
	if (epoll_ctl(ep,EPOLL_CTL_ADD, sfd, &event) < 0)
	{
		perror("Epoll Control ERROR");
		exit(-1);
	}

	struct epoll_event events[1024];
	int len = sizeof(events) / sizeof(events[0]);
	int flag = 0;
	while (1)
	{
		if (flag)
		{
			break;
		}
		int num = epoll_wait(ep, events, len, -1);
		for (int i = 0; i < num; i++)
		{
			int curfd = events[i].data.fd;
			if (curfd == sfd)
			{
				//建立通信
				int ret = accept_conn(sfd, ep);
				if (ret == -1)
				{
					flag = 1;
					break;
				}
			}

			else
			{
				int ret = recvHttpRequest(curfd, ep);
				//通信-》先接受数据，然后回复数据
			}
		}
	}

	return ep;
}

int accept_conn(int sfd, int ep)
{
	int cfd = accept(sfd,NULL,NULL);
	if (cfd == -1)
	{
		perror("Accept Error");
		return -1;
	}
	//设置文件描述符为非阻塞
	int flag = fcntl(cfd,F_GETFL);
	fcntl(cfd,F_SETFL, flag | O_NONBLOCK);
	//将通信的文件描述符加入到epoll中
	struct epoll_event event;
	event.data.fd = cfd;
	event.events = EPOLLIN | EPOLLET; //边沿模式
	if (epoll_ctl(ep,EPOLL_CTL_ADD, cfd, &event) == -1)
	{
		perror("EPOLL Error ");
		return -1;
	}
	return 0;
}

int recvHttpRequest(int cfd, int ep)
{
	char buf[4096], tmp[1024];
	int len;
	int total = 0;
	while ((len = recv(cfd, tmp, sizeof(tmp), 0)) > 0)
	{
		if (total + len < sizeof(buf))
		{
			memcpy(buf, tmp, total + len);
		}
		total += len;
	}
	//循环结束说明读完了
	if (len == -1 && errno == EAGAIN)
	{
		praseRequireLine(cfd,buf);
		//将数据从解析的请求行中拿出来
		char *pt = strstr(buf, "\r\n");
		const int reqlen = pt - buf;
		buf[reqlen] = '\0';
	}
	else if (len == 0)
	{
		printf("客户端断开了连接....\n");
		//服务器和客户端断开连接,epoll中删除文件描述符
		disconnect(cfd, ep);
	}
	else
	{
		perror("recv Error");
		return -1;
	}
	return 0;
}

int praseRequireLine(int cfd, const char *reqLine)
{
	char method[6];
	char path[1024];
	char *file = NULL;
	// printf("%s\n",reqLine);
	sscanf(reqLine, "%[^ ] %[^ ]", method, path);
	//判断用户发送的是不是get请求
	if (strcasecmp(method, "GET") != 0)
	{
		printf("用户发送的不是get请求，忽略。。。。\n");
		return -1;
	}
	// 如果文件名中有中文, 需要还原
	decodeMsg(path, path);
	if (strcmp(path, "/") == 0)
	{
		file = "./";
	}
	else
	{
		file = path + 1;
	}

	//属性判断
	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)
	{
		//获取文件属性失败==》没有这个文件
		//给客户端发送404
		sendHeadMessage(cfd, 404, "Not Found", getFileType(".html"), -1);
		sendFile(cfd,"404.html");
	}
	if (S_ISREG(st.st_mode))
	{
		//如果是普通文件就把内容发送给客户端
		sendHeadMessage(cfd,200,"OK",getFileType(file),st.st_size);
		sendFile(cfd,file);

	}
	else
	{
		//如果是目录就遍历目录，把目录内容发送给客户端
		sendHeadMessage(cfd,200,"OK",getFileType(".html"),-1);
		sendDir(cfd,file);
		//发送目录函数
	}
	return 0;
}
int sendHeadMessage(int cfd, int status, const char *description, const char *type, int length)
{
	char buf[4096];
	//http/1.1 200 ok
	sprintf(buf, "http1.1 %d %s\r\n", status, description);
	sprintf(buf + strlen(buf), "Content-Type: %s\r\n", type);
	sprintf(buf + strlen(buf), "Content-Length:%d\r\n\r\n", length);
	int ret = send(cfd, buf, strlen(buf), 0);
	if (ret < 0)
	{
		perror("send error");
		return -1;
	}

	return 0;
}

int disconnect(int fd, int epfd)
{
	int ret = epoll_ctl(epfd,EPOLL_CTL_DEL, fd,NULL);
	if (ret == -1)
	{
		close(fd);
		perror("Epoll CTL ERROR");
		return -1;
	}
	close(fd);
	return 0;
}

int sendFile(int cfd, const char* Filename)
{
	//1.打开文件
	int fd = open(Filename,O_RDONLY);
	//循环读文件
	while (1)
	{
		char buf[1024] = {0};
		int len = read(fd,buf,sizeof(buf));
		if (len > 0)
		{
			//发送读出的数据
			send(cfd, buf ,len ,0);
			//发送端发太快会导致接收端的显示有异常
			usleep(10);
		}
		else if (len == 0)//文件读完了
		{
			break;
		}
		else
		{
			perror("读取文件失败...");
			return -1;
		}
	}
	return 0;
}

int sendDir(int cfd, const char *Path)
{
	char buf[4096];
	sprintf(buf,"<html><head><title>%s</title></head><body><table>", Path);
	//遍历目录文件
	//将目录中的所有文件发给客户端
	struct dirent **namelist;

	int num =scandir(Path,&namelist,NULL,alphasort);

	for (int i = 0 ; i < num;i++)
	{
		//取出文件名
		char * name = namelist[i]->d_name;
		//拼接当前文件在资源文件中的相对路径
		char subpath[1024];
		sprintf(subpath,"%s/%s",Path,name);
		struct stat st;
		stat(subpath,&st);
		if (S_ISDIR(st.st_mode))
		{
			// 如果是目录, 超链接的跳转路径文件名后边加 /
			sprintf(buf + strlen(buf),
				"<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>",
				name, name, (long)st.st_size);
		}
		else
		{
			sprintf(buf + strlen(buf),
				"<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
				name, name, (long)st.st_size);
		}
		// 发送数据
		send(cfd, buf, strlen(buf), 0);
		// 清空数组
		memset(buf, 0, sizeof(buf));
		// 释放资源 namelist[i] 这个指针指向一块有效的内存
		free(namelist[i]);
	}
	return 0;
}

const char *getFileType(const char *name)
{
	// a.jpg a.mp4 a.html
	// 自右向左查找‘.’字符, 如不存在返回NULL
	const char *dot = strrchr(name, '.');
	if (dot == NULL)
		return "text/plain; charset=utf-8"; // 纯文本
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
		return "text/html; charset=utf-8";
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcmp(dot, ".png") == 0)
		return "image/png";
	if (strcmp(dot, ".css") == 0)
		return "text/css";
	if (strcmp(dot, ".au") == 0)
		return "audio/basic";
	if (strcmp(dot, ".wav") == 0)
		return "audio/wav";
	if (strcmp(dot, ".avi") == 0)
		return "video/x-msvideo";
	if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
		return "video/quicktime";
	if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
		return "video/mpeg";
	if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
		return "model/vrml";
	if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
		return "audio/midi";
	if (strcmp(dot, ".mp3") == 0)
		return "audio/mpeg";
	if (strcmp(dot, ".ogg") == 0)
		return "application/ogg";
	if (strcmp(dot, ".pac") == 0)
		return "application/x-ns-proxy-autoconfig";

	return "text/plain; charset=utf-8";
}


//
void decodeMsg(char *to, char *from)
{
	for (; *from != '\0'; ++to, ++from)
	{
		// isxdigit -> 判断字符是不是16进制格式
		// Linux%E5%86%85%E6%A0%B8.jpg
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
		{
			// 将16进制的数 -> 十进制 将这个数值赋值给了字符 int -> char
			// A1 == 161
			*to = hexit(from[1]) * 16 + hexit(from[2]);

			from += 2;
		}
		else
		{
			// 不是特殊字符字节赋值
			*to = *from;
		}
	}
	*to = '\0';
}

// 最终得到10进制的整数
int hexit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	return 0;

}
