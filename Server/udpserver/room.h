
#pragma once

#include "global.h"

class CRoom
{
public:
  CRoom(int inRoomID);
  ~CRoom();
public:
  int          GetTcpTeacherCount() { return m_lpTCPTeacher ? 1 : 0; }
  int          GetTcpStudentCount() { return m_MapTCPStudent.size(); }
  CTCPClient * GetTcpTeacherClient() { return m_lpTCPTeacher; }
public:
  void         doDumpRoomInfo();
  CUDPClient * doFindUdpPusher(int inDBCameraID);
  void         doTcpCreateTeacher(CTCPClient * lpTeacher);
  void         doTcpCreateStudent(CTCPClient * lpStudent);
  void         doTcpDeleteTeacher(CTCPClient * lpTeacher);
  void         doTcpDeleteStudent(CTCPClient * lpStudent);

  void         doUdpCreateTeacher(CUDPClient * lpTeacher);
  void         doUdpCreateStudent(CUDPClient * lpStudent);
  void         doUdpDeleteTeacher(CUDPClient * lpTeacher);
  void         doUdpDeleteStudent(CUDPClient * lpStudent);
private:
  int              m_nRoomID;              // �����ʶ����...
  CTCPClient   *   m_lpTCPTeacher;         // ֻ��һ����ʦ�˳�����...
  GM_MapTCPConn    m_MapTCPStudent;        // ���ѧ���˳�����...
};