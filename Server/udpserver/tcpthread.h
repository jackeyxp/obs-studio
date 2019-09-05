
#pragma once

#include "global.h"
#include "../common/thread.h"

class CTCPThread : public CThread
{
public:
  CTCPThread();
  virtual ~CTCPThread();
  virtual void Entry();
public:
  int     GetEpollFD() { return m_epoll_fd; }
  GM_MapTCPConn & GetMapConnect() { return m_MapConnect; }
public:
  bool    InitThread();                         // 初始化并启动线程...
  void    doIncreaseClient(int inSinPort, string & strSinAddr);
  void    doDecreaseClient(int inSinPort, string & strSinAddr);
private:
  int     doCreateListenSocket(int nHostPort);  // 创建TCP监听套接字...
  int     SetNonBlocking(int sockfd);
  int     doHandleWrite(int connfd);
  int     doHandleRead(int connfd);
  void    doHandleTimeout();
  void    doTCPListenEvent();
  void    clearAllClient();
private:
  int                 m_epoll_fd;                // epoll句柄编号...
  int                 m_listen_fd;               // TCP监听套接字...
  int                 m_max_event;               // 当前有效的套接字个数包括监听套接字和中心连接套接字...
  int                 m_accept_count;            // 当前从网络接收到的连接个数，有效连接个数...
  GM_MapTCPConn	      m_MapConnect;              // the global map object...
  struct epoll_event  m_events[MAX_EPOLL_SIZE];  // epoll事件队列...
};
