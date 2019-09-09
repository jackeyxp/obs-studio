
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
  void        doWaitForExit();
  bool        doProcessCmdLine(int argc, char * argv[]);
  int         doTcpRoomCommand(int inRoomID, int inCmdID);
public:
  bool        onRecvEvent(uint32_t inHostAddr, uint16_t inHostPort, char * lpBuffer, int inBufSize);
public:
  string      GetAllRoomList();
  CRoom   *   doCreateRoom(int inRoomID);
  void        doAddLoseForLooker(CUDPClient * lpLooker);
  void        doDelLoseForLooker(CUDPClient * lpLooker);
  void        doAddSupplyForPusher(CUDPClient * lpPusher);
  void        doDelSupplyForPusher(CUDPClient * lpPusher);
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
  int64_t           m_next_check_ns;     // 下次超时检测的时间戳 => 纳秒 => 每隔10秒检测一次...
  int64_t           m_next_detect_ns;	   // 下次发送探测包的时间戳 => 纳秒 => 每隔1秒发送一次...
  circlebuf         m_circle;            // 网络环形队列...
  os_sem_t     *    m_sem_t;             // 辅助线程信号量...
  CTCPThread   *    m_lpTCPThread;       // TCP监听线程对象...
  CUDPThread   *    m_lpUDPThread;       // UDP数据线程对象...
  GM_MapRoom        m_MapRoom;           // 房间集合对象...
  GM_MapUDPConn     m_MapUdpConn;        // UDP网络对象列表...
  GM_ListPusher     m_ListPusher;        // 有补包的推流者列表 => 学生推流者|老师推流者...
  GM_ListLooker     m_ListLooker;        // 有丢包的观看者列表 => 学生观看者|老师观看者...
  pthread_mutex_t   m_room_mutex;        // 房间资源互斥对象...
  pthread_mutex_t   m_buff_mutex;        // 缓存资源互斥对象...
};