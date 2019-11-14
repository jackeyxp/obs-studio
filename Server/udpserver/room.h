
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
public:
  void         doDumpRoomInfo();
  CUDPClient * doFindUdpPusher(int inLiveID);
  void         doTcpCreateSmart(CTCPClient * lpTcpSmart);
  void         doTcpDeleteSmart(CTCPClient * lpTcpSmart);
  void         doUdpCreateSmart(CUDPClient * lpUdpSmart);
  void         doUdpDeleteSmart(CUDPClient * lpUdpSmart);
  void         doUdpHeaderSmart(CUDPClient * lpUdpSmart);
  void         doUdpLogoutToTcp(int nTCPSockFD, int nLiveID, uint8_t tmTag, uint8_t idTag);
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
  int              m_nRoomID;           // �����ʶ����...
  uint32_t         m_nMaxLiveID;        // ��ǰ�����������...
  CTCPThread   *   m_lpTCPThread;       // ��������TCP�߳�...
  CTCPClient   *   m_lpTCPTeacher;      // ֻ��һ����ʦ�˳�����...
  GM_MapTCPConn    m_MapTCPStudent;     // ���ѧ���˳�����...
  pthread_mutex_t  m_tcp_mutex;         // TCP��Դ�������...
};