
#pragma once

#include "global.h"

class CUDPClient;
class CTCPClient;
class CTCPThread;
class CUDPThread;
class CApp
{
public:
  CApp();
  ~CApp();
public: /* 启动时的公共接口... */
  void        onSignalQuit();
  bool        check_pid_file();
  bool        acquire_pid_file();
  bool        destory_pid_file();
  bool        doStartThread();
  bool        doInitRLimit();
  void        doWaitForExit();
  bool        doProcessCmdLine(int argc, char * argv[]);
public: /* 针对CUDPThread线程的数据接口... */
  bool        onRecvEvent(uint32_t inHostAddr, uint16_t inHostPort, char * lpBuffer, int inBufSize);
public: /* 针对CRoom的房间接口... */
  string      GetAllRoomList();
  int         GetTeacherDBFlowID(int inRoomID);
  int         doTCPRoomCommand(int inRoomID, int inCmdID);
  int         doTcpClientCreate(int inRoomID, CTCPClient * lpClient);
  int         doTcpClientDelete(int inRoomID, CTCPClient * lpClient);
  int         doUdpClientDelete(int inRoomID, CUDPClient * lpClient);
  int         doUdpClientCreate(int inRoomID, CUDPClient * lpClient, char * lpBuffer, int inBufSize);
public: /* 公共通用接口 */
  string  &   GetTcpCenterAddr() { return m_strTCPCenterAddr; }
  int         GetTcpCenterPort() { return m_nTCPCenterPort; }
  int         GetUdpListenPort() { return m_nUDPListenPort; }
  int         GetTcpListenPort() { return m_nTCPListenPort; }
  bool        IsDebugMode() { return m_bIsDebugMode; }
  bool        IsSignalQuit() { return m_signal_quit; }
private:
  int         read_pid_file();
  void        doStopSignal();
  void        doCheckTimeout();
  void        doSendDetectCmd();
  void        doSendSupplyCmd();
  void        doSendLoseCmd();
  void        doRecvPacket();
  void        clearAllRoom();
  void        clearAllSource();
  void        clearAllUdpClient();
  void        doTagDelete(int nHostPort);
  bool        doProcSocket(uint32_t nHostSinAddr, uint16_t nHostSinPort, char * lpBuffer, int inBufSize);
private:
  string            m_strTCPCenterAddr;  // 中心服务器的IP地址...
  int               m_nTCPCenterPort;    // 中心服务器的访问端口...
  int               m_nUDPListenPort;    // UDP监听端口...
  int               m_nTCPListenPort;    // TCP监听端口...
  bool              m_signal_quit;       // 信号退出标志...
  bool              m_bIsDebugMode;      // 是否是调试模式 => 只能挂载调试模式的学生端和讲师端...
  circlebuf         m_circle;            // 网络环形队列...
  os_sem_t     *    m_sem_t;             // 辅助线程信号量...
  CTCPThread   *    m_lpTCPThread;       // TCP监听线程对象...
  CUDPThread   *    m_lpUDPThread;       // UDP数据线程对象...
  GM_MapRoom        m_MapRoom;           // 房间集合对象...
  GM_MapUDPConn     m_MapUdpConn;        // UDP网络对象列表...
  pthread_mutex_t   m_room_mutex;        // 房间资源互斥对象...
  pthread_mutex_t   m_buff_mutex;        // 缓存资源互斥对象...
};