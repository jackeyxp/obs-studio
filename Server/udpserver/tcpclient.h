
#pragma once

#include "global.h"

class CRoom;
class CTCPThread;
class CTCPClient
{
public:
  CTCPClient(CTCPThread * lpTCPThread, int connfd, int nHostPort, string & strSinAddr);
  ~CTCPClient();
public:
  int       ForRead();            // 读取网络数据
  int       ForWrite();           // 发送网络数据
  bool      IsTimeout();          // 检测是否超时
  void      ResetTimeout();       // 重置超时时间
public:
  int       GetConnFD() { return m_nConnFD; }
  int       GetDBFlowID() { return m_nDBFlowID; }
  int       GetClientType() { return m_nClientType; }
  string &  GetMacAddr() { return m_strMacAddr; }
  CUDPClient * GetUdpPusher() { return m_lpUdpPusher; }
public:
  void      doUdpCreatePusher(CUDPClient * lpPusher);
  void      doUdpDeletePusher(CUDPClient * lpPusher);
  int       doUdpLiveOnLine(int inLiveID, bool bIsOnLineFlag);
  int       doUdpLogoutToTcp(int nLiveID, uint8_t tmTag, uint8_t idTag);
private:
  int       parseJsonData(const char * lpJsonPtr, int nJsonLength);          // 统一的JSON解析接口...
  int       doPHPClient(Cmd_Header * lpHeader, const char * lpJsonPtr);      // 处理PHP客户端事件...
  int       doStudentClient(Cmd_Header * lpHeader, const char * lpJsonPtr);  // 处理Student事件...
  int       doTeacherClient(Cmd_Header * lpHeader, const char * lpJsonPtr);  // 处理Teacher事件...
  int       doScreenClient(Cmd_Header * lpHeader, const char * lpJsonPtr);   // 处理Screen事件...
private:
  int       doCmdSmartLogin();
  int       doCmdSmartOnLine();
  int       doSendCmdLoginForStudent(int nTeacherFlowID, int nTeacherLiveID);
  int       doSendCommonCmd(int nCmdID, const char * lpJsonPtr = NULL, int nJsonSize = 0, int nSockID = 0);
  int       doSendPHPResponse(const char * lpJsonPtr, int nJsonSize);
private:
  int           m_epoll_fd;         // epoll句柄编号...
  int           m_nConnFD;          // 连接socket...
  int           m_nRoomID;          // 记录房间编号...
  int           m_nDBFlowID;        // 记录流量编号...
  int           m_nHostPort;        // 连接端口...
  int           m_nClientType;      // 客户端类型...
  time_t        m_nStartTime;       // 超时检测起点...
  string        m_strUserName;      // 屏幕端用户名...
  string        m_strSinAddr;       // 连接映射地址...
  string        m_strMacAddr;       // 记录登录MAC地址...
  string        m_strIPAddr;        // 记录登录IP地址...
  string        m_strRoomID;        // 记录房间编号...
  string        m_strPCName;        // 记录终端名称...
  string        m_strSend;          // 数据发送缓存...
  string        m_strRecv;          // 数据读取缓存...
  GM_MapJson    m_MapJson;          // 终端传递过来的JSON数据...
  CRoom      *  m_lpRoom;           // 房间对象指针...
  CTCPThread *  m_lpTCPThread;      // TCP线程对象...
  CUDPClient *  m_lpUdpPusher;      // 唯一UDP推流者... 
};