
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
  bool    InitThread();                     // 初始化并启动线程...
private:
  int     doCreateSocket(int nHostPort);    // 创建TCP监听套接字...
  int     SetNonBlocking(int sockfd);
  int     doHandleWrite(int connfd);
  int     doHandleRead(int connfd);
  void    doHandleTimeout();
  void    clearAllClient();
private:
  int                 m_epoll_fd;           // epoll句柄编号...
  int                 m_listen_fd;          // TCP监听套接字...
  GM_MapTCPConn	      m_MapConnect;         // the global map object...
  pthread_mutex_t     m_mutex;              // 线程互斥对象...
  struct epoll_event  m_events[MAX_EPOLL_SIZE]; // epoll事件队列...
};
