//
// Created by moon on 24-12-16.
//

#ifndef SERVER_H
#define SERVER_H
#include <sys/epoll.h>
//初始化套接字头文件
int initSockFD(unsigned short port);
//启动epoll模型
int epoll_run(unsigned short port);
//建立连接
int accept_conn(int sfd,int ep);
//接受请求数据
int recvHttpRequest(int cfd,int ep);
//解析请求行
int praseRequireLine(int cfd, const char* reqLine);
//发送头信息
int sendHeadMessage(int cfd,int status,const char * description,const char * type,int length);
//读文件内容
int sendFile(int cfd, const char* Filename);
//发送目录
int sendDir(int cfd,const char * Path);
//断开连接
int disconnect(int fd,int epfd);
//通过文件名得到文件的content-type
const char *getFileType(const char *name);
#endif //SERVER_H
