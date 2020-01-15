
#pragma once

#include "global.h"

class CTCPThread;
class CRoom
{
public:
  CRoom(int inRoomID, CTCPThread * lpTcpThread);
  ~CRoom();
public:
  int          GetTcpTeacherCount();
  int          GetTcpStudentCount();
  int          GetUdpTeacherLiveID();
  int          GetTcpTeacherDBFlowID();
  CTCPClient * GetTCPTeacher() { return m_lpTCPTeacher; }
  GM_MapCamera & GetMapCamera() { return m_MapCamera; }
public:
  void         doDumpRoomInfo();
  CUDPClient * doFindUdpPusher(int inLiveID);
  void         doTcpCreateSmart(CTCPClient * lpTcpSmart);
  void         doTcpDeleteSmart(CTCPClient * lpTcpSmart);
  void         doUdpCreateSmart(CUDPClient * lpUdpSmart);
  void         doUdpDeleteSmart(CUDPClient * lpUdpSmart);
  void         doUdpHeaderSmart(CUDPClient * lpUdpSmart);
  void         doUdpLogoutToTcp(int nTCPSockFD, int nLiveID, uint8_t tmTag, uint8_t idTag);
  CCamera   *  doCreateCamera(CTCPClient * lpTCPClient, int inDBCameraID, string & inPCName, string & inCameraName);
  void         doDeleteCamera(int inDBCameraID);
private:
  void         doTcpCreateTeacher(CTCPClient * lpTeacher);
  void         doTcpCreateStudent(CTCPClient * lpStudent);
  void         doTcpDeleteTeacher(CTCPClient * lpTeacher);
  void         doTcpDeleteStudent(CTCPClient * lpStudent);

  void         doUdpHeaderTeacherPusher(CUDPClient * lpTeacher);
  void         doUdpHeaderStudentPusher(CUDPClient * lpStudent);
  void         doUdpCreateTeacherPusher(CUDPClient * lpTeacher);
  void         doUdpCreateTeacherLooker(CUDPClient * lpTeacher);
  void         doUdpCreateStudentPusher(CUDPClient * lpStudent);
  void         doUdpCreateStudentLooker(CUDPClient * lpStudent);
  void         doUdpDeleteTeacherPusher(CUDPClient * lpTeacher);
  void         doUdpDeleteTeacherLooker(CUDPClient * lpTeacher);
  void         doUdpDeleteStudentPusher(CUDPClient * lpStudent);
  void         doUdpDeleteStudentLooker(CUDPClient * lpStudent);
  
  void         doUdpLiveOnLine(CTCPClient * lpTcpExclude, int inLiveID, bool bIsOnLineFlag);
private:
  int              m_nRoomID;           // 房间标识号码...
  CTCPThread   *   m_lpTCPThread;       // 房间管理的TCP线程...
  CTCPClient   *   m_lpTCPTeacher;      // 只有一个老师端长链接...
  GM_MapCamera     m_MapCamera;         // 房间关联的所有摄像头列表 => live_id => CCamera*...
  GM_MapTCPConn    m_MapTCPStudent;     // 多个学生端长链接...
  pthread_mutex_t  m_tcp_mutex;         // TCP资源互斥对象...
};

class CCamera
{
public:
  CCamera(CTCPClient * lpTCPClient, int inDBRoomID, int inDBCameraID, string & inPCName, string & inCameraName) {
    m_lpTCPClient = lpTCPClient; m_nDBRoomID =inDBRoomID, m_nDBCameraID = inDBCameraID;
    m_strPCName = inPCName; m_strCameraName = inCameraName;
  }
  ~CCamera() {}
public:
  int          m_nDBRoomID;         // 房间标识号码...
  int          m_nDBCameraID;       // 摄像头编号 => live_id
  string       m_strPCName;         // 通道所在主机名称
  string       m_strCameraName;     // 摄像头通道名称
  CTCPClient * m_lpTCPClient;       // 通道关联的TCP连接
};
