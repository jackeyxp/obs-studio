
#pragma once

#include "global.h"

class CTCPCenter;
class CTCPThread : public CThread
{
public:
  CTCPThread();
  virtual ~CTCPThread();
  virtual void Entry();
public:
  int     GetEpollFD() { return m_epoll_fd; }
public:
  bool    InitThread();
  int     doRoomCommand(int inRoomID, int inCmdID);
  void    doIncreaseClient(int inSinPort, string & strSinAddr);
  void    doDecreaseClient(int inSinPort, string & strSinAddr);
  void    doUdpLogoutToTcp(int nTCPSockFD, int nLiveID, uint8_t tmTag, uint8_t idTag);
private:
  int     doCreateListenSocket(int nHostPort);
  void    doTCPCenterEvent(int nEvent);
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
  CTCPCenter    *     m_lpTCPCenter;             // 连接中心服务器对象...
  GM_MapTCPConn	      m_MapConnect;              // the global map object...
  struct epoll_event  m_events[MAX_EPOLL_SIZE];  // epoll事件队列...
};
