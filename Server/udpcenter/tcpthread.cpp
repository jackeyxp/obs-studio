
#include "tcpthread.h"
#include "tcpclient.h"

#define WAIT_TIME_OUT     10 * 1000   // 全局超时检测10秒...

CTCPThread::CTCPThread()
  : m_listen_fd(0)
  , m_epoll_fd(0)
{
  // 初始化线程互斥对象...
  pthread_mutex_init(&m_mutex, NULL);
  // 重置epoll事件队列列表...
  memset(m_events, 0, sizeof(epoll_event) * MAX_EPOLL_SIZE);
}

CTCPThread::~CTCPThread()
{
  // 等待线程退出...
  this->StopAndWaitForThread();
  // 关闭套接字，阻止网络数据到达...
  if( m_listen_fd > 0 ) {
    close(m_listen_fd);
    m_listen_fd = 0;
  }
  // 删除所有的客户端连接...
  this->clearAllClient();
  // 删除线程互斥对象...
  pthread_mutex_destroy(&m_mutex);  
}

bool CTCPThread::InitThread()
{
  // 创建tcp服务器套接字...
  int nHostPort = DEF_CENTER_PORT;
  if( this->doCreateSocket(nHostPort) < 0 )
    return false;
  // 打印中心服务器正在监听的端口信息...
  log_trace("[UDPCenter] tcp listen port => %d", nHostPort);
  // 启动tcp服务器监听线程...
  this->Start();
  return true;
}
//
// 创建TCP监听套接字...
int CTCPThread::doCreateSocket(int nHostPort)
{
  // 创建TCP监听套接字...
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0); 
  if( listen_fd < 0 ) {
    log_trace("can't create tcp socket");
    return -1;
  }
 	int opt = 1;
  // 设置地址重用 => 失败，关闭套接字...
  if( setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0 ) {
    log_trace("SO_REUSEADDR error: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }
  // 设置端口重用 => 失败，关闭套接字...
  if( setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) != 0 ) {
    log_trace("SO_REUSEPORT error: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }
  // 设置异步套接字 => 失败，关闭套接字...
  if( this->SetNonBlocking(listen_fd) < 0 ) {
    log_trace("SetNonBlocking error: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }
  // 设定发送和接收缓冲最大值...
  int nRecvMaxLen = 64 * 1024;
  int nSendMaxLen = 64 * 1024;
  // 设置接收缓冲区...
  if( setsockopt(listen_fd, SOL_SOCKET, SO_RCVBUF, &nRecvMaxLen, sizeof(nRecvMaxLen)) != 0 ) {
    log_trace("SO_RCVBUF error: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }
  // 设置发送缓冲区...
  if( setsockopt(listen_fd, SOL_SOCKET, SO_SNDBUF, &nSendMaxLen, sizeof(nSendMaxLen)) != 0 ) {
    log_trace("SO_SNDBUF error: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }
  // 准备绑定地址结构体...
	struct sockaddr_in tcpAddr = {0};
	bzero(&tcpAddr, sizeof(tcpAddr));
	tcpAddr.sin_family = AF_INET; 
	tcpAddr.sin_port = htons(nHostPort);
	tcpAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  // 绑定监听端口...
	if( bind(listen_fd, (struct sockaddr *)&tcpAddr, sizeof(struct sockaddr)) == -1 ) {
    log_trace("bind tcp port: %d, error: %s", nHostPort, strerror(errno));
    close(listen_fd);
		return -1;
	}
  // 启动监听队列...
	if( listen(listen_fd, MAX_LISTEN_SIZE) == -1 ) {
    log_trace("listen error: %s", strerror(errno));
    close(listen_fd);
		return -1;
	}
  // create epoll handle, add socket to epoll events...
  // EPOLLEF模式下，accept时必须用循环来接收链接，防止链接丢失...
  struct epoll_event evListen = {0};
	m_epoll_fd = epoll_create(MAX_EPOLL_SIZE);
	evListen.data.fd = listen_fd;
	evListen.events = EPOLLIN | EPOLLET;
	if( epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, listen_fd, &evListen) < 0 ) {
    log_trace("epoll set insertion error: fd=%d", listen_fd);
		return -1;
	}
  // 返回已经绑定完毕的TCP套接字...
  m_listen_fd = listen_fd;
  return m_listen_fd;
}

int CTCPThread::SetNonBlocking(int sockfd)
{
	if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFD, 0)|O_NONBLOCK) == -1) {
		return -1;
	}
	return 0;
}

void CTCPThread::Entry()
{
  // 打印TCP监听线程正常启动提示信息...
  log_trace("tcp-server startup, port %d, max-connection is %d, backlog is %d", DEF_CENTER_PORT, MAX_EPOLL_SIZE, MAX_LISTEN_SIZE);
  // 进入epoll线程循环过程...
  time_t myStartTime = time(NULL);
	int curfds = 1, acceptCount = 0;
  while( !this->IsStopRequested() ) {
    // 等待epoll事件，直到超时...
    int nfds = epoll_wait(m_epoll_fd, m_events, curfds, WAIT_TIME_OUT);
    // 发生错误，EINTR这个错误，不能退出...
		if( nfds == -1 ) {
      log_trace("epoll_wait error(code:%d, %s)", errno, strerror(errno));
      // is EINTR, continue...
      if( errno == EINTR ) 
        continue;
      // not EINTR, break...
      assert( errno != EINTR );
      break;
		}
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // 处理超时的情况 => 释放一些已经死掉的资源...
    // 注意：这里的超时，是一种时钟，每隔10秒自动执行一次，不能只处理事件超时，要自行处理...
    // 以前的写法会造成只要2个以上有效用户就永远无法处理超时过程，因为还没超时，就有新事件到达了...
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    time_t nDeltaTime = time(NULL) - myStartTime;
    if( nDeltaTime >= WAIT_TIME_OUT/1000 ) {
      myStartTime = time(NULL);
      this->doHandleTimeout();
    }
    // 刚好是超时事件，继续下次等待...
    if( nfds == 0 )
      continue;
    // 处理正确返回值的情况...
    for(int n = 0; n < nfds; ++n) {
      // 处理服务器socket事件...
      int nCurEventFD = m_events[n].data.fd;
			if( nCurEventFD == m_listen_fd ) {
        // 这里要循环accept链接，可能会有多个链接同时到达...
        while( true ) {
          // 收到客户端连接的socket...
          struct sockaddr_in cliaddr = {0};
          socklen_t socklen = sizeof(struct sockaddr_in);
          int connfd = accept(m_listen_fd, (struct sockaddr *)&cliaddr, &socklen);
          // 发生错误，跳出循环...
          if( connfd < 0 )
            break;
          // eqoll队列超过最大值，关闭，继续...
          if( curfds >= MAX_EPOLL_SIZE ) {
            log_trace("too many connection, more than %d", MAX_EPOLL_SIZE);
            close(connfd);
            break; 
          }
          // set none blocking for the new client socket => error not close... 
          if( this->SetNonBlocking(connfd) < 0 ) {
            log_trace("client SetNonBlocking error fd: %d", connfd);
          }
          // 添加新socket到epoll事件池 => 只加入读取事件...
          struct epoll_event evClient = {0};
          evClient.events = EPOLLIN | EPOLLET;
          evClient.data.fd = connfd;
          // 添加失败，记录，继续...
          if( epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, connfd, &evClient) < 0 ) {
            log_trace("add socket '%d' to epoll failed: %s", connfd, strerror(errno));
            close(connfd);
            break;
          }
          // 全部成功，打印信息，引用计数增加...
          ++curfds; ++acceptCount;
          int nSinPort = ntohs(cliaddr.sin_port);
          string strSinAddr = inet_ntoa(cliaddr.sin_addr);
          //log_trace("client count(%d) - increase, accept from %s:%d", acceptCount, strSinAddr.c_str(), nSinPort);
          // 进入线程互斥保护...
          pthread_mutex_lock(&m_mutex);
          // 创建客户端对象,并保存到集合当中...
          CTCPClient * lpTCPClient = new CTCPClient(this, connfd, nSinPort, strSinAddr);
          m_MapConnect[connfd] = lpTCPClient;
          // 退出线程互斥保护...
          pthread_mutex_unlock(&m_mutex);  
        }
      } else {
        // 进入线程互斥保护...
        pthread_mutex_lock(&m_mutex);
        // 处理客户端socket事件...
        int nRetValue = -1;
        if( m_events[n].events & EPOLLIN ) {
          nRetValue = this->doHandleRead(nCurEventFD);
        } else if( m_events[n].events & EPOLLOUT ) {
          nRetValue = this->doHandleWrite(nCurEventFD);
        }
        // 判断处理结果...
        if( nRetValue < 0 ) {
          // 处理失败，从epoll队列中删除...
          struct epoll_event evDelete = {0};
          evDelete.data.fd = nCurEventFD;
          evDelete.events = EPOLLIN | EPOLLET;
          epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, nCurEventFD, &evDelete);
          // 删除对应的客户端连接对象...
          if( m_MapConnect.find(nCurEventFD) != m_MapConnect.end() ) {
            delete m_MapConnect[nCurEventFD];
            m_MapConnect.erase(nCurEventFD);
          }
          // 关闭连接，减少引用，打印事件...
          close(nCurEventFD);
          --curfds; --acceptCount;
          //log_trace("client count(%d) - decrease", acceptCount);
        }
        // 退出线程互斥保护...
        pthread_mutex_unlock(&m_mutex);  
      }
    }
  }
  // clear all the connected client...
  this->clearAllClient();
  // close listen socket and exit...
  log_trace("tcp-thread exit.");
  close(m_listen_fd);
  m_listen_fd = 0;
}
//
// 删除所有的客户端连接...
void CTCPThread::clearAllClient()
{
  GM_MapTCPConn::iterator itorItem;
  for(itorItem = m_MapConnect.begin(); itorItem != m_MapConnect.end(); ++itorItem) {
    delete itorItem->second;
  }
  m_MapConnect.clear();
}
//
// 处理客户端socket读取事件...
int CTCPThread::doHandleRead(int connfd)
{
  // 在集合中查找客户端对象...
  GM_MapTCPConn::iterator itorConn = m_MapConnect.find(connfd);
  if( itorConn == m_MapConnect.end() ) {
    log_trace("can't find client connection(%d)", connfd);
		return -1;
  }
  // 获取对应的客户端对象，执行读取操作...
  CTCPClient * lpTCPClient = itorConn->second;
  return lpTCPClient->ForRead();  
}
//
// 处理客户端socket写入事件...
int CTCPThread::doHandleWrite(int connfd)
{
  // 在集合中查找客户端对象...
  GM_MapTCPConn::iterator itorConn = m_MapConnect.find(connfd);
  if( itorConn == m_MapConnect.end() ) {
    log_trace("can't find client connection(%d)", connfd);
		return -1;
  }
  // 获取对应的客户端对象，执行写入操作...
  CTCPClient * lpTCPClient = itorConn->second;
  return lpTCPClient->ForWrite();
}
//
// 处理epoll超时事件...
void CTCPThread::doHandleTimeout()
{
  // 2017.07.26 - by jackey => 根据连接状态删除客户端...
  // 2017.12.16 - by jackey => 去掉gettcpstate，使用超时机制...
  // 遍历所有的连接，判断连接是否超时，超时直接删除...
  CTCPClient * lpTCPClient = NULL;
  GM_MapTCPConn::iterator itorConn;
  itorConn = m_MapConnect.begin();
  while( itorConn != m_MapConnect.end() ) {
    lpTCPClient = itorConn->second;
    if( lpTCPClient->IsTimeout() ) {
      // 发生超时，从epoll队列中删除...
      int nCurEventFD = lpTCPClient->m_nConnFD;
      struct epoll_event evDelete = {0};
      evDelete.data.fd = nCurEventFD;
      evDelete.events = EPOLLIN | EPOLLET;
      epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, nCurEventFD, &evDelete);
      // 打印删除消息，删除对象...
      log_trace("handleTimeout: %s, Socket(%d) be killed", get_client_type(lpTCPClient->m_nClientType), nCurEventFD);
      delete lpTCPClient; lpTCPClient = NULL;
      m_MapConnect.erase(itorConn++);
      // 关闭套接字...
      close(nCurEventFD);
    } else {
      ++itorConn;
    }
  }  
}
