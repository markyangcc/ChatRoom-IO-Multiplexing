#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <poll.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>

#define ERR_EXIT(m)         \
	do                      \
	{                       \
		perror(m);          \
		exit(EXIT_FAILURE); \
	} while (0)

ssize_t readn(int fd, void *buf, size_t count)
{						  //ssize_t 有符号整数  size_t 无符号整数
	size_t nleft = count; //剩余的字节数
	ssize_t nread;		  //已接收的字节数
	char *bufp = (char *)buf;

	while (nleft > 0)
	{
		if ((nread = read(fd, bufp, nleft)) < 0)
		{
			if (errno == EINTR) //被信号中断
				continue;
			return -1;
		}
		else if (nread == 0) //对等方关闭了
			return count - nleft;
		bufp += nread;
		nleft -= nread;
	}
	return count;
}

ssize_t writen(int fd, const void *buf, size_t count)
{
	size_t nleft = count; //剩余要发送的字节数
	ssize_t nwritten;	  //已发送的字节数
	char *bufp = (char *)buf;

	while (nleft > 0)
	{
		if ((nwritten = write(fd, bufp, nleft)) < 0)
		{
			if (errno == EINTR) //被信号中断
				continue;
			return -1;
		}
		else if (nwritten == 0)
			continue;
		bufp += nwritten;
		nleft -= nwritten;
	}
	return count;
}

ssize_t recv_peek(int sockfd, void *buf, size_t len)
{ //接收数据后不将数据从缓冲区移除
	while (1)
	{
		int ret = recv(sockfd, buf, len, MSG_PEEK);
		if (ret == -1 && errno == EINTR) //被信号中断
			continue;
		return ret;
	}
}

ssize_t readline(int sockfd, void *buf, size_t maxline)
{ //只能用于套接口
	int ret;
	int nread;
	char *bufp = (char *)buf;
	int nleft = maxline;
	while (1)
	{
		ret = recv_peek(sockfd, bufp, nleft);
		if (ret < 0) //不用再进行中断判断，因为recv_peek函数内部已经进行了
			return ret;
		else if (ret == 0) //对方关闭
			return ret;
		nread = ret;
		int i; //判断已接收到的缓冲区中是否有\n
		for (i = 0; i < nread; ++i)
		{
			if (bufp[i] == '\n')
			{
				ret = readn(sockfd, bufp, i + 1); //将数据从缓冲区移除
				if (ret != i + 1)
					exit(EXIT_FAILURE);
				return ret;
			}
		}

		if (nread > nleft)
			exit(EXIT_FAILURE);

		nleft -= nread;
		ret = readn(sockfd, bufp, nread); //还没遇到\n的数据也从缓冲区移除
		if (ret != nread)
			exit(EXIT_FAILURE);

		bufp += nread;
	}
}

void echo_srv(int conn)
{
	//接收
	char recvbuf[1024];
	while (1)
	{
		memset(recvbuf, 0, sizeof(recvbuf));
		int ret = readline(conn, recvbuf, 1024);
		if (ret == -1)
			ERR_EXIT("readline");
		if (ret == 0)
		{
			printf("client close\n");
			break;
		}

		fputs(recvbuf, stdout);
		writen(conn, recvbuf, strlen(recvbuf));
	}
}

void handle_sigchld(int sig)
{
	//捕获子进程的初始状态
	//wait(NULL);
	while (waitpid(-1, NULL, WNOHANG) > 0)
		; //可以等待所有子进程，大于0表示等待到了一个子进程
}

void handle_sigpipe(int sig)
{
	printf("recv a sig = %d\n", sig);
}

int main()
{
	//signal(SIGPIPE, SIG_IGN);
	signal(SIGPIPE, handle_sigpipe);
	//signal(SIGCHLD, SIG_IGN);
	signal(SIGCHLD, handle_sigchld);
	//创建套接字
	int listenfd;
	if ((listenfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		ERR_EXIT("socket");

	//地址初始化
	struct sockaddr_in servaddr; //IPv4地址结构
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(5188);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY); //绑定本机的任意地址

	int on = 1;
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		ERR_EXIT("setsockopt");

	//绑定
	if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
		ERR_EXIT("bind");

	//监听
	if (listen(listenfd, SOMAXCONN) < 0)
		ERR_EXIT("listen");

	//连接
	struct sockaddr_in peeraddr;
	socklen_t peerlen;
	int conn;

	/*
    struct pollfd {
        int   fd;//文件描述符
        short events;//请求事件/感兴趣事件
        short revents;//返回事件
    };
    */

	int i;
	struct pollfd client[2048]; //保存多个客户端连接信息
	int maxi = 0;				//最大的不空闲的位置

	for (i = 0; i < 2048; ++i)
		client[i].fd = -1; //初始化，-1表示空闲的

	int nready; //检测到的事件个数
	client[0].fd = listenfd;
	client[0].events = POLLIN; //对可读事件感兴趣

	while (1)
	{
		nready = poll(client, maxi + 1, -1);

		if (nready == -1)
		{
			if (errno == EINTR) //被信号中断
				continue;
			ERR_EXIT("poll");
		}
		if (nready == 0) //超时，现在timeout设置为NULL，故不可能发生
			continue;

		if (client[0].revents & POLLIN)
		{
			peerlen = sizeof(peeraddr); //必须设置一个初始值
			conn = accept(listenfd, (struct sockaddr *)&peeraddr, &peerlen);
			if (conn == -1)
				ERR_EXIT("accept");
			//保存到某个空闲位置
			for (i = 0; i < 2048; ++i)
			{
				if (client[i].fd < 0)
				{
					client[i].fd = conn;

					if (i > maxi)
						maxi = i;
					break;
				}
			}
			if (i == 2048)
			{
				fprintf(stderr, "too mang clients\n");
				exit(EXIT_FAILURE);
			}

			printf("ip=%s port=%d\n", inet_ntoa(peeraddr.sin_addr), ntohs(peeraddr.sin_port));

			client[i].events = POLLIN;

			if (--nready <= 0) //检测到的事件已经处理完了，应继续监听，没必要处理下面的代码
				continue;
		}

		//已连接套接口产生了数据
		for (i = 1; i <= maxi; i++)
		{
			conn = client[i].fd;
			if (conn == -1)
				continue;

			if (client[i].revents & POLLIN)
			{
				char recvbuf[1024] = {0};
				int ret = readline(conn, recvbuf, 1024);
				if (ret == -1)
					ERR_EXIT("readline");
				if (ret == 0)
				{
					printf("client close\n");
					client[i].fd = -1;
					close(conn);
				}

				fputs(recvbuf, stdout);

				for (int j = 1; j <= maxi; j++)
				{

					if (client[j].fd != -1 && i != j)
					{
						writen(client[j].fd, recvbuf, strlen(recvbuf));
					}
				}

				if (--nready <= 0)
					break;
			}
		}
	}
}