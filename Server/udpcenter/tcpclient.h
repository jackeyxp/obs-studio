
#pragma once

#include "global.h"

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
  int       GetClientType() { return m_nClientType; }
  uint32_t  GetTcpTimeID() { return m_uTcpTimeID; }
private:
  void      doParseRoom(char * inList);
  int       doParseInt(char * inList, int & outSize);
  int       parseJsonData(const char * lpJsonPtr, int nJsonLength);           // 统一的JSON解析接口...
  int       doPHPClient(Cmd_Header * lpHeader, const char * lpJsonPtr);       // 处理PHP客户端事件...
  int       doStudentClient(Cmd_Header * lpHeader, const char * lpJsonPtr);   // 处理Student事件...
  int       doTeacherClient(Cmd_Header * lpHeader, const char * lpJsonPtr);   // 处理Teacher事件...
  int       doUdpServerClient(Cmd_Header * lpHeader, const char * lpJsonPtr); // 处理UdpServer事件...
private:
  int       doCmdStudentLogin();
  int       doCmdStudentOnLine();
  int       doCmdTeacherLogin();
  int       doCmdTeacherOnLine();
  int       doCmdPHPGetPlayerList();
  int       doCmdPHPGetRoomList();
  int       doCmdPHPGetAllClient();
  int       doCmdPHPGetAllServer();
  int       doCmdPHPGetUdpServer();
  int       doCmdUdpServerLogin();
  int       doCmdUdpServerOnLine();
  int       doCmdUdpServerAddTeacher();
  int       doCmdUdpServerDelTeacher();
  int       doCmdUdpServerAddStudent();
  int       doCmdUdpServerDelStudent();
  int       doTransferBindMini(const char * lpJsonPtr, int nJsonSize);
  int       doSendPHPResponse(const char * lpJsonPtr, int nJsonSize);
  int       doSendCommonCmd(int nCmdID, const char * lpJsonPtr = NULL, int nJsonSize = 0);
private:
  int          m_epoll_fd;         // epoll句柄编号...
  int          m_nConnFD;          // 连接socket...
  int          m_nHostPort;        // 连接端口...
  int          m_nClientType;      // 客户端类型...
  time_t       m_nStartTime;       // 超时检测起点...
  uint32_t     m_uTcpTimeID;       // 时间标识(毫秒)...
  string       m_strSinAddr;       // 连接映射地址...
  string       m_strSend;          // 数据发送缓存...
  string       m_strRecv;          // 数据读取缓存...
  GM_MapJson   m_MapJson;          // 终端传递过来的JSON数据...
  CTCPThread * m_lpTCPThread;      // TCP线程对象...
  
  friend class CTCPThread;
};