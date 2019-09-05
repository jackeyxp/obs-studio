
#pragma once

#include "global.h"

class CTCPThread;
class CTCPCenter
{
public:
  CTCPCenter(CTCPThread * lpTCPThread);
  ~CTCPCenter();
public:
  int          InitTCPCenter();
  int          doHandleTimeout();
  int          doEpollEvent(int nEvent);
  int          GetConnFD() { return m_center_fd; }
private:
  bool         IsTimeout();
  void         ResetTimeout();
  int          doHandleWrite();
  int          doHandleRead();
  int          SetNonBlocking(int sockfd);
  int          doCreateTCPSocket(const char * lpInAddr, int nHostPort);
  int          doSendCommonCmd(int nCmdID, const char * lpJsonPtr = NULL, int nJsonSize = 0);
private:
  int          m_epoll_fd;         // epoll句柄编号...
  int          m_center_fd;        // TCP中心套接字...
  int          m_nClientType;      // 客户端类型...
  time_t       m_nStartTime;       // 超时检测起点...
  string       m_strSend;          // 数据发送缓存...
  CTCPThread * m_lpTCPThread;      // TCP线程对象...
};
