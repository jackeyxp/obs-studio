
#pragma once

#include "global.h"

class CTCPThread;
class CUDPThread;
class CApp
{
public:
  CApp();
  ~CApp();
public:
  void        onSignalQuit();
  bool        check_pid_file();
  bool        acquire_pid_file();
  bool        destory_pid_file();
  bool        doStartThread();
  bool        doInitRLimit();
  void        doWaitUdpSocket();
  int         doCreateUdpSocket();
  bool        doProcessCmdLine(int argc, char * argv[]);
public:
  string  &   GetTcpCenterAddr() { return m_strTCPCenterAddr; }
  int         GetTcpCenterPort() { return m_nTCPCenterPort; }
  int         GetUdpListenPort() { return m_nUDPListenPort; }
  int         GetTcpListenPort() { return m_nTCPListenPort; }
  bool        IsDebugMode() { return m_bIsDebugMode; }
  bool        IsSignalQuit() { return m_signal_quit; }
private:
  int         read_pid_file();
  void        doStopSignal();
  void        clearAllSource();
private:
  string            m_strTCPCenterAddr;  // 中心服务器的IP地址...
  int               m_nTCPCenterPort;    // 中心服务器的访问端口...
  int               m_nUDPListenPort;    // UDP监听端口...
  int               m_nTCPListenPort;    // TCP监听端口...
  int               m_udp_listen_fd;     // UDP监听套接字...
  bool              m_signal_quit;       // 信号退出标志...
  bool              m_bIsDebugMode;      // 是否是调试模式 => 只能挂载调试模式的学生端和讲师端...
  CTCPThread   *    m_lpTCPThread;       // TCP监听线程对象...
  CUDPThread   *    m_lpUDPThread;       // UDP数据线程对象...
};