
#include "app.h"
#include "tcpclient.h"
#include "tcpthread.h"
#include "tcpcenter.h"

#define WAIT_TIME_OUT     10 * 1000   // 全局超时检测10秒...

CTCPThread::CTCPThread()
  : m_lpTCPCenter(NULL)
  , m_accept_count(0)
  , m_max_event(0)
  , m_listen_fd(0)
  , m_epoll_fd(0)
{
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
  // 先关闭epoll句柄对象...
  if( m_epoll_fd > 0 ) {
    close(m_epoll_fd);
    m_epoll_fd = 0;
  }
  // 关闭TCP中心套接字对象...
  if( m_lpTCPCenter != NULL ) {
    delete m_lpTCPCenter;
    m_lpTCPCenter = NULL;
  }
  // 删除所有的TCP连接对象...
  this->clearAllClient();
}

int CTCPThread::doRoomCommand(int inRoomID, int inCmdID)
{
  if( m_lpTCPCenter == NULL )
    return -1;
  // 中心套接字有效，转发计数器变化通知...
  return m_lpTCPCenter->doRoomCommand(inRoomID, inCmdID);
}

bool CTCPThread::InitThread()
{
  // 创建TCP服务器监听套接字，并加入到epoll队列当中...
  int nHostPort = GetApp()->GetTcpListenPort();
  if( this->doCreateListenSocket(nHostPort) < 0 )
    return false;
  
  // 打印中心服务器正在监听的端口信息...
  log_trace("[UDPServer] tcp listen port => %d", nHostPort);
  
  // 重新创建TCP中心套接字管理对象...
  if( m_lpTCPCenter != NULL ) {
    delete m_lpTCPCenter;
    m_lpTCPCenter = NULL;
  }
  
  // 创建新的TCP中心套接字管理对象...
  m_lpTCPCenter = new CTCPCenter(this);

  // 注意：为了保证正常工作，失败之后，仍然继续运行...
  // 创建连接UDP中心服务器的套接字，并加入到epoll队列当中...
  m_lpTCPCenter->InitTCPCenter();
  // 累加进入epoll队列套接字个数...
  ++m_max_event;
  // 启动tcp服务器监听线程...
  this->Start();
  return true;
}

// 创建TCP服务器监听套接字，并加入到epoll队列当中...
int CTCPThread::doCreateListenSocket(int nHostPort)
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
  m_epoll_fd = epoll_create(MAX_EPOLL_SIZE);
  assert( m_epoll_fd > 0 );
  // 注意：epoll写事件，只要投递就会触发，读事件则会等待底层激发...
  // 加入epoll队列当中，只加入读取事件......
  struct epoll_event evListen = {0};
  evListen.data.fd = listen_fd;
  evListen.events = EPOLLIN | EPOLLET;
  // EPOLLEF模式下，accept时必须用循环来接收链接，防止链接丢失...
  if( epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, listen_fd, &evListen) < 0 ) {
    log_trace("epoll set insertion error: fd=%d", listen_fd);
    return -1;
  }
  // 累加进入epoll队列套接字个数...
  ++m_max_event;
  // 返回已经绑定完毕的TCP套接字...
  m_listen_fd = listen_fd;
  return m_listen_fd;
}

int CTCPThread::SetNonBlocking(int sockfd)
{
  // 对套接字进行异步状态设置...
	if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFD, 0)|O_NONBLOCK) == -1) {
		return -1;
	}
	return 0;
}

void CTCPThread::doTCPCenterEvent(int nEvent)
{
  // 响应中心套接字事件，成功，直接返回...
  if( m_lpTCPCenter->doEpollEvent(nEvent) >= 0 )
    return;
  // 删除对象，等待重建...
  delete m_lpTCPCenter;
  m_lpTCPCenter = NULL;
}

void CTCPThread::doTCPListenEvent()
{
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
    if( m_max_event >= MAX_EPOLL_SIZE ) {
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
    // 新建客户端之前，先删除相同套接字的对象...
    if( m_MapConnect.find(connfd) != m_MapConnect.end() ) {
        delete m_MapConnect[connfd]; m_MapConnect.erase(connfd);
        log_trace("=== delete same socket(%d) before create ===", connfd);
    }
    int nSinPort = ntohs(cliaddr.sin_port);
    string strSinAddr = inet_ntoa(cliaddr.sin_addr);
    // 创建客户端对象,并保存到集合当中...
    CTCPClient * lpTCPClient = new CTCPClient(this, connfd, nSinPort, strSinAddr);
    m_MapConnect[connfd] = lpTCPClient;
  }
}

void CTCPThread::doIncreaseClient(int inSinPort, string & strSinAddr)
{
  // 累加最大事件数和已连接用户数，并打印出来...
  ++m_accept_count; ++m_max_event;
  log_trace("client accept-count(%d), max-event(%d) - increase, accept from %s:%d",
             m_accept_count, m_max_event, strSinAddr.c_str(), inSinPort);
}

void CTCPThread::doDecreaseClient(int inSinPort, string & strSinAddr)
{
  // 打印已连接用户数和最大事件数...
  --m_accept_count; --m_max_event;
  log_trace("client accept-count(%d), max-event(%d) - decrease, accept from %s:%d",
             m_accept_count, m_max_event, strSinAddr.c_str(), inSinPort);
}

void CTCPThread::Entry()
{
  // 打印TCP监听线程正常启动提示信息...
  log_trace("tcp-thread startup, tcp-port %d, max-connection is %d, backlog is %d", GetApp()->GetTcpListenPort(), MAX_EPOLL_SIZE, MAX_LISTEN_SIZE);
  // 进入epoll线程循环过程...
  time_t myStartTime = time(NULL);
  while( !this->IsStopRequested() ) {
    // 注意：m_max_event 必须大于0，否则会发生 EINVAL 错误，导致线程退出...
    // 等待epoll事件，直到超时，设定为1秒超时...
    int nfds = epoll_wait(m_epoll_fd, m_events, m_max_event, 1000);
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
      // 处理主监听套接字事件...
      if( nCurEventFD == m_listen_fd ) {
        this->doTCPListenEvent();
        continue;
      }
      // 响应中心套接字事件的处理过程...
      if( m_lpTCPCenter != NULL && nCurEventFD == m_lpTCPCenter->GetConnFD() ) {
        this->doTCPCenterEvent(m_events[n].events);
        continue;
      }
      // 注意：套接字的读写命令，没有用线程保护...
      // 注意：目的是为了防止UDP删除命令先到达造成的互锁问题...
      int nRetValue = -1;
      if( m_events[n].events & EPOLLIN ) {
        nRetValue = this->doHandleRead(nCurEventFD);
      } else if( m_events[n].events & EPOLLOUT ) {
        nRetValue = this->doHandleWrite(nCurEventFD);
      }
      // 判断事件处理结果...
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
        // 关闭套接字..
        close(nCurEventFD);
      }
    }
  }
  // 打印TCP线程退出信息...
  log_trace("tcp-thread exit.");
  ////////////////////////////////////////////////////////////////////
  // 注意：房间对象和终端对象的删除，放在了析构函数当中...
  ////////////////////////////////////////////////////////////////////
}
//
// 删除所有的客户端连接...
void CTCPThread::clearAllClient()
{
  GM_MapTCPConn::iterator itorItem;
  for(itorItem = m_MapConnect.begin(); itorItem != m_MapConnect.end(); ++itorItem) {
    CTCPClient * lpTCPClient = itorItem->second;
    delete lpTCPClient; lpTCPClient = NULL;
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
  // 打印信息，提示重建中心对象完毕...
  if( m_lpTCPCenter == NULL ) {
    m_lpTCPCenter = new CTCPCenter(this);
    m_lpTCPCenter->InitTCPCenter();
    log_trace("TCP-Center has been rebuild.");
  }
  // 查看中心套接字的连接超时状态...
  if( m_lpTCPCenter != NULL ) {
    m_lpTCPCenter->doHandleTimeout();
  }
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
      int nCurEventFD = lpTCPClient->GetConnFD();
      struct epoll_event evDelete = {0};
      evDelete.data.fd = nCurEventFD;
      evDelete.events = EPOLLIN | EPOLLET;
      epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, nCurEventFD, &evDelete);
      // 打印删除消息，删除对象...
      log_trace("handleTimeout: %s, Socket(%d) be killed", get_client_type(lpTCPClient->GetClientType()), nCurEventFD);
      delete lpTCPClient; lpTCPClient = NULL;
      m_MapConnect.erase(itorConn++);
      // 关闭套接字...
      close(nCurEventFD);
    } else {
      ++itorConn;
    }
  }
}
