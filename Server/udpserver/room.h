
#pragma once

#include "global.h"

class CRoom
{
public:
  CRoom(int inRoomID);
  ~CRoom();
public:
  int         GetTcpTeacherCount() { return m_lpTCPTeacher ? 1 : 0; }
  int         GetTcpStudentCount() { return m_MapTCPStudent.size(); }
public:
  int         GetTeacherDBFlowID();
  int         doTcpClientCreate(CTCPClient * lpClient);
  int         doTcpClientDelete(CTCPClient * lpClient);
//int         doUdpClientCreate(CUDPClient * lpClient);
//int         doUdpClientDelete(CUDPClient * lpClient);
private:
  int         doTcpCreateTeacher(CTCPClient * lpClient);
  int         doTcpCreateStudent(CTCPClient * lpClient);
  int         doTcpDeleteTeacher(CTCPClient * lpClient);
  int         doTcpDeleteStudent(CTCPClient * lpClient);
private:
  int              m_nRoomID;          // �����ʶ����...
  CTCPClient   *   m_lpTCPTeacher;     // ֻ��һ����ʦ���������������ѧ����...
  GM_MapTCPConn    m_MapTCPStudent;    // ѧ���˹ۿ����б���������ʦ������...
};