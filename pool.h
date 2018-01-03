#ifndef POOL_H_
#define POOL_H_

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

/*����*/
class process
{
	public:
		process() : m_pid(-1)
		{
		}
	public:
		pid_t m_pid;
		int m_pipefd[2];
};

template <typename T>
class processpool
{
	public:
		//����ģʽ
		static processpool<T>* create(int listenfd, int process_number = 8)
		{
			if(!m_instance)
			{
				m_instance = new processpool<T>(listenfd, process_number);
			}
			return m_instance;
		}
		~processpool()
		{
			delete [] m_sub_process;
		}
		//�����߳�
		void run();

	private:
		//�����캯������Ϊ˽�еģ��������ֻ��ͨ�������create��̬����������processpoolʵ��
		processpool(int listenfd, int process_number = 8);
		//���и�����
		void run_parent();
		//�����ӽ���
		void run_child();
		//�ܵ�����
		void setup_sig_pipe();

	private:
		//���̳����е�����ӽ�������
		static const int MAX_PROCESS_NUMBER = 16;
		//ÿ���ӽ�������ܴ���Ŀͻ�����
		static const int USER_PER_PROCESS = 65536;
		//epoll����ܴ�����¼���
		static const int MAX_EVENT_NUMBER = 10000;
		//ÿ�����̶���һ��epoll�ں��¼�����m_epollfd��ʶ
		int m_epollfd;
		//�����̼���������
		int m_listenfd;
		//���������ӽ��̵�������Ϣ
		process * m_sub_process;
		//���̳��еĽ�������
		int m_process_number;
		//�ӽ����ڳ��е���ţ���0��ʼ
		int m_idx;
		//�ӽ���ͨ��m_stop�������Ƿ�ֹͣ����
		int m_stop;
		//��̬���̳�ʵ��
		static processpool<T >* m_instance;
};

template<typename T> processpool<T>* processpool<T>::m_instance = NULL;

//���ڴ����źŵĹܵ�����ʵ��ͳһ�¼�Դ�������֮Ϊ�źŹܵ���
static int sig_pipefd[2];

//�Ը����ļ��������趨Ϊ������
static int setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}

//����ļ���������epollfd��
static void addfd(int epollfd, int fd)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}

//��epollfd��ʶ��epoll�ں��¼�����ɾ��fd�ϵ�����ע���¼�
static void removefd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

//�źŴ�����
static void sig_handler(int sig)
{
	int save_errno = errno;
	int msg = sig;
	send(sig_pipefd[1], (char *)&msg, 1, 0);
	errno = save_errno;
}

//����ź�
static void addsig(int sig, void(handler)(int), bool restart = true)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = handler;
	if(restart)
	{
		sa.sa_flags |= SA_RESTART;
	}
	sigfillset(&sa.sa_mask);
	sigaction(sig, &sa, NULL);
}

//���̳ع��캯��������listenfd�Ǽ���socket���������ڴ������̳�֮ǰ�������������ӽ����޷�ֱ����������
template< typename T>
processpool< T >::processpool(int listenfd, int process_number):m_listenfd(listenfd), m_process_number(process_number), m_idx(-1),m_stop(false)
{
	if((process_number <= 0) && (process_number > MAX_PROCESS_NUMBER))
	{
		PRINT_ERROR("check param\n");
		return;
	}

	m_sub_process = new process[process_number];
	if(NULL == m_sub_process)
	{
		PRINT_ERROR("new process fail\n");
		return;
	}
	//����process_number���ӽ��̣����������Ǻ͸�����֮��Ĺܵ�
	for(int i = 0; i < process_number; ++i)
	{
		int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
		if(ret != 0)
		{
			PRINT_ERROR("socketpair fail\n");
			return;
		}
		m_sub_process[i].m_pid = fork();
		if(m_sub_process[i].m_pid > 0)
		{
			//fork���ش���0 ������
			close(m_sub_process[i].m_pipefd[1]); //�ļ����������������һ�ݣ�����ر�һ��
			continue;
		}
		else
		{	//�ӽ���
			close(m_sub_process[i].m_pipefd[0]);
			m_idx = i;
			break;
		}
	}
}

template< typename T>
void processpool< T >::setup_sig_pipe()
{
	//����epoll �¼���������źŹܵ�
	m_epollfd = epoll_create(5);
	if(m_epollfd == -1)
	{
		PRINT_ERROR("epoll_create fail\n");
		return;
	}

	int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
	if(ret == -1)
	{
		PRINT_ERROR("socketpair fail\n");
		return;
	}
	setnonblocking(sig_pipefd[1]);
	addfd(m_epollfd, sig_pipefd[0]);

	addsig(SIGCHLD, sig_handler);
	addsig(SIGTERM, sig_handler);
	addsig(SIGINT, sig_handler);
	addsig(SIGPIPE, SIG_IGN);
}

template< typename T>
void processpool< T >::run()
{
	if(m_idx != -1)
	{
		run_child();
		return;
	}
	run_parent();
}

template<typename T>
void processpool<T>::run_child()
{
	setup_sig_pipe();
	//ÿ���ӽ��̶�ͨ�����ڽ��̳��е����ֵm_idx�ҵ��븸����ͨ�ŵĹܵ�
	int pipefd = m_sub_process[m_idx].m_pipefd[1];
	addfd(m_epollfd, pipefd);

	epoll_event events[MAX_EVENT_NUMBER];
	T* users = new T[USER_PER_PROCESS];
	if(NULL == users)
	{
		PRINT_ERROR("new fail\n");
		return;
	}
	int number = 0;
	int ret = -1;

	while(!m_stop)
	{
		number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
		if((number < 0) && (errno != EINTR))
		{
			PRINT_ERROR("epoll failure\n");
			break;
		}
		for(int i = 0; i < number; i++)
		{
			int sockfd = events[i].data.fd;
			if((sockfd == pipefd) && (events[i].events & EPOLLIN))
			{
				int client = 0;
				//�Ӹ�-�ӽ��̼�Ĺܵ���ȡ���ݣ�������������ڱ���client�С������ȡ�ɹ������ʾ���¿ͻ����ӵ���
				ret = recv(sockfd, (char *)&client, sizeof(client), 0);
				if(((ret < 0) && (errno != EAGAIN)) || ret == 0)
				{
					continue;
				}
				else
				{
					struct sockaddr_in client_address;
					socklen_t client_addrlength = sizeof(client_address);
					int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
					if(connfd < 0)
					{
						PRINT_ERROR("errno is: %d\n", errno);
						continue;
					}
					addfd(m_epollfd, connfd);

					users[connfd].init(m_epollfd, connfd, client_address);
				}
			}
			else if((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN))
			{
				char signals[1024];
				ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
				if(ret <= 0)
				{
					continue;
				}
				else
				{
					for(int i = 0; i < ret; ++i)
					{
						switch (signals[i])
						{
							case SIGCHLD:
								pid_t pid;
								int stat;
								while((pid = waitpid(-1, &stat, WNOHANG)) > 0)
								{
									continue;
								}
								break;
							case SIGTERM:
							case SIGINT:
							{
								m_stop = true;
								break;
							}
							default:
								break;
						}

					}
				}
			}
			else if(events[i].events & EPOLLIN)
			{
				users[sockfd].process();
			}
			else
			{
				continue;
			}
		}
	}
	delete []users;
	users = NULL;
	close(m_epollfd);
}

template<typename T>
void processpool< T >::run_parent()
{
	setup_sig_pipe();

	//�����̼���m_listenfd;
	addfd(m_epollfd, m_listenfd);

	epoll_event events[MAX_EVENT_NUMBER];
	int sub_process_counter = 0;
	int new_conn = 1;
	int number = 0;
	int ret = -1;

	while(!m_stop)
	{
		number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
		if((number < 0) && (errno != EINTR))
		{
			PRINT_ERROR("epoll failure\n");
			break;
		}

		for(int i = 0; i < number; i++)
		{
			int sockfd = events[i].data.fd;
			if(sockfd == m_listenfd)
			{
				//����������ӵ������Ͳ���RR��ʽ��������һ���ӽ��̴���
				int i = sub_process_counter;
				do
				{
					if(m_sub_process[i].m_pid != -1)
					{
						break;
					}
					i = (i + 1)% m_process_number;
				}while(i != sub_process_counter);

				if(m_sub_process[i].m_pid == -1)
				{
					m_stop = true;
					break;
				}
				sub_process_counter = (i + 1) % m_process_number;
				send(m_sub_process[i].m_pipefd[0], (char *)&new_conn, sizeof(new_conn), 0);
				PRINT_INFO("send request to child %d\n", i);
			}
			else if((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN))
			{
				char signals[1024];
				ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
				if(ret <= 0)
				{
					continue;
				}
				else
				{
					for(int i = 0; i < ret; ++i)
					{
						switch (signals[i])
						{
							case SIGCHLD:
								pid_t pid;
								int stat;
								while((pid = waitpid(-1, &stat, WNOHANG)) > 0)
								{
									for(int i = 0; i < m_process_number; ++i)
									{
										/* ������̳��е�i���ӽ����˳��ˣ��������̹ر���Ӧ��ͨ�Źܵ���
										 * ��������Ӧ��m_pidΪ-1���Ա�Ǹ��ӽ����Ѿ��˳�*/
										if(m_sub_process[i].m_pid == pid)
										{
											PRINT_ERROR("child %d join\n", i);
											close(m_sub_process[i].m_pipefd[0]);
											m_sub_process[i].m_pid = -1;
										}
									}
								}
								//��������ӽ��̶��Ѿ��˳��ˣ��򸸽���Ҳ�˳�
								m_stop = true;
								for(int i = 0; i < m_process_number; ++i)
								{
									if(m_sub_process[i].m_pid != -1)
									{
										m_stop = false;
									}
								}
								break;
							case SIGTERM:
							case SIGINT:
							{
								PRINT_ERROR("kill all the child now\n");
								for(int i = 0; i < m_process_number; ++i)
								{
									int pid = m_sub_process[i].m_pid;
									if(pid != -1)
									{
										kill(pid, SIGTERM);
									}
								}
								break;
							}
							default:
								break;
						}
					}
				}

			}
			else
			{
				continue;
			}
		}

	}
	close(m_epollfd);
}
#endif /* POOL_H_ */
