#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "debug.h"
#include "pool.h"

//公共的网关接口
class cgi_conn
{
public:
	cgi_conn(){};
	~cgi_conn(){};

	void init(int epollfd, int sockfd, const sockaddr_in& client_addr)
	{
		m_epollfd = epollfd;
		m_sockfd = sockfd;
		m_address = client_addr;
		memset(m_buf, '\0', BUFFER_SIZE);
		m_read_idx = 0;
	}

	void process()
	{
		int idx = 0;
		int ret = -1;

		while(true)
		{
			idx = m_read_idx;
			ret = recv(m_sockfd, m_buf + idx, BUFFER_SIZE - 1 -idx, 0);
			if(ret < 0)
			{
				if(errno != EAGAIN)
				{
					removefd(m_epollfd, m_sockfd);
				}
				break;
			}
			else if(ret == 0)
			{
				/* 如果对方关闭连接，则服务器也关闭连接 */
				removefd(m_epollfd, m_sockfd);
				break;
			}
			else
			{
				m_read_idx += ret;
				PRINT_INFO("user content is: %s\n", m_buf);
				/*如果遇到字符\r\n,则开始处理客户请求*/
				for( ; idx < m_read_idx; ++idx)
				{
					if((idx >=1 ) && (m_buf[idx - 1] == '\n') && (m_buf[idx] == '\n'))
					{
						break;
					}
				}
				/* 如果没有遇到字符 "\r\n"，则需要读取更多客户数据*/
				if(idx == m_read_idx)
				{
					continue;
				}
				m_buf[idx-1] = '\0';

				char * file_name = m_buf;
				if(access(file_name, F_OK) == -1)
				{
					removefd(m_epollfd, m_sockfd);
					break;
				}
				/*创建子进程来执行CGI程序*/
				ret = fork();
				if(ret == -1)
				{
					removefd(m_epollfd, m_sockfd);
					break;
				}
				else if(ret > 0)
				{
					/* 父进程只需关闭连接 */
					removefd(m_epollfd, m_sockfd);
					break;
				}
				else
				{
					/* 子进程将标准输出定向到m_socketfd,并执行CGI程序*/
					close(STDOUT_FILENO);
					dup(m_sockfd);
					execl(m_buf, m_buf, NULL);
					exit(0);
				}

			}
		}
	}

private:
	static const int BUFFER_SIZE = 1024;
	static int m_epollfd;
	int m_sockfd;
	sockaddr_in m_address;
	char m_buf[BUFFER_SIZE];
	int m_read_idx;
};

int cgi_conn::m_epollfd = -1;

int main(int argc, char **argv)
{
	if(argc <=2)
	{
		PRINT_INFO("usage: %s ip_address port_number\n", basename(argv[0]));
		return 1;
	}
	const char *ip = argv[1];
	int port = atoi(argv[2]);

	int listenfd = socket(PF_INET, SOCK_STREAM, 0);
	if(listenfd < 0)
	{
		PRINT_ERROR("socket failure\n");
		return -1;
	}

	int ret = 0;
	struct sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &address.sin_addr);
	address.sin_port = htons(port);

	PRINT_INFO("bind %s %d\n", ip, port);

	ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
	if(ret == -1)
	{
		PRINT_ERROR("bind failure\n");
		return -1;
	}

	ret = listen(listenfd, 5);
	if(ret == -1)
	{
		PRINT_ERROR("listen failure\n");
		return -1;
	}

	processpool< cgi_conn>* pool = processpool<cgi_conn>::create(listenfd);
	if(pool)
	{
		pool->run();
		delete pool;
	}
	close(listenfd);
	return 0;
}
