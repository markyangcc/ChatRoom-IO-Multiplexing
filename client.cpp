#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

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

void echo_cli(int sock)
{
	fd_set rset;
	FD_ZERO(&rset);

	//循环检测是否产生了可读事件
	int nready;
	int maxfd;
	int fd_stdin = fileno(stdin); //标准输入的文件描述符
	if (fd_stdin > sock)
		maxfd = fd_stdin;
	else
		maxfd = sock;

	char sendbuf[1024] = {0};
	char recvbuf[1024] = {0};

	int stdineof = 0;

	while (1)
	{
		if (stdineof == 0)
			FD_SET(fd_stdin, &rset);
		FD_SET(sock, &rset);
		nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
		if (nready == -1)
			ERR_EXIT("select");
		if (nready == 0)
			continue;
		if (FD_ISSET(sock, &rset))
		{
			int ret = readline(sock, recvbuf, sizeof(recvbuf));
			if (ret == -1)
				ERR_EXIT("readline");
			else if (ret == 0)
			{
				printf("server close\n");
				break;
			}

			fputs(recvbuf, stdout);
			memset(recvbuf, 0, sizeof(recvbuf));
		}
		if (FD_ISSET(fd_stdin, &rset))
		{
			if (fgets(sendbuf, sizeof(sendbuf), stdin) == NULL)
			{
				stdineof = 1;
				break;
			}
			writen(sock, sendbuf, strlen(sendbuf));
			memset(sendbuf, 0, sizeof(sendbuf));
		}
	}
	close(sock);
}

int main()
{
	//创建套接字
	int sock;
	if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		ERR_EXIT("socket");

	//地址初始化
	struct sockaddr_in servaddr; //IPv4地址结构
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(5188);
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (connect(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
		ERR_EXIT("connect");

	struct sockaddr_in localaddr;
	socklen_t addrlen = sizeof(localaddr);
	if ((getsockname(sock, (struct sockaddr *)&localaddr, &addrlen)) < 0)
		ERR_EXIT("getsockname");
	printf("ip = %s  port = %d\n", inet_ntoa(localaddr.sin_addr), ntohs(localaddr.sin_port));

	echo_cli(sock);

	return 0;
}