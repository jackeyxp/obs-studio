
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
  int          GetTcpTeacherDBFlowID();
  bool         IsTcpTeacherClientOnLine();
  bool         IsUdpTeacherPusherOnLine();
public:
  void         doDumpRoomInfo();
  CUDPClient * doFindUdpPusher(int inDBCameraID);
  void         doTcpCreateSmart(CTCPClient * lpSmart);
  void         doTcpDeleteSmart(CTCPClient * lpSmart);

  void         doUdpCreateTeacher(CUDPClient * lpTeacher);
  void         doUdpCreateStudent(CUDPClient * lpStudent);
  void         doUdpDeleteTeacher(CUDPClient * lpTeacher);
  void         doUdpDeleteStudent(CUDPClient * lpStudent);
private:
  void         doTcpCreateTeacher(CTCPClient * lpTeacher);
  void         doTcpCreateStudent(CTCPClient * lpStudent);
  void         doTcpDeleteTeacher(CTCPClient * lpTeacher);
  void         doTcpDeleteStudent(CTCPClient * lpStudent);

  void         doUdpCreateTeacherPusher(CUDPClient * lpTeacher);
  void         doUdpCreateTeacherLooker(CUDPClient * lpTeacher);
  void         doUdpCreateStudentPusher(CUDPClient * lpStudent);
  void         doUdpCreateStudentLooker(CUDPClient * lpStudent);
  void         doUdpDeleteTeacherPusher(CUDPClient * lpTeacher);
  void         doUdpDeleteTeacherLooker(CUDPClient * lpTeacher);
  void         doUdpDeleteStudentPusher(CUDPClient * lpStudent);
  void         doUdpDeleteStudentLooker(CUDPClient * lpStudent);
private:
  int              m_nRoomID;           // �����ʶ����...
  CTCPThread   *   m_lpTCPThread;       // ��������TCP�߳�...
  CTCPClient   *   m_lpTCPTeacher;      // ֻ��һ����ʦ�˳�����...
  GM_MapTCPConn    m_MapTCPStudent;     // ���ѧ���˳�����...
  pthread_mutex_t  m_tcp_mutex;         // TCP��Դ�������...
};