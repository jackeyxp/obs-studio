
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
  int              m_nRoomID;              // 房间标识号码...
  CTCPClient   *   m_lpTCPTeacher;         // 只有一个老师端长链接...
  GM_MapTCPConn    m_MapTCPStudent;        // 多个学生端长链接...
};