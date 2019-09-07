
#pragma once

#include "global.h"
#include "../common/thread.h"

class CUDPThread : public CThread
{
public:
  CUDPThread();
  virtual ~CUDPThread();
  virtual void Entry();
public:
  bool        InitThread();
  int         GetUdpListenFD() { return m_udp_listen_fd; }
private:
  int         doCreateListenSocket(int nHostPort);
private:
  int         m_udp_listen_fd;     // UDP监听套接字...
};