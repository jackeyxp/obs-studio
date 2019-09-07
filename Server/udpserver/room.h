
#pragma once

#include "global.h"

class CRoom
{
public:
  CRoom(int inRoomID);
  ~CRoom();
public:
  int         GetTcpTeacherDBFlowID();
  int         GetTcpTeacherCount() { return m_lpTCPTeacher ? 1 : 0; }
  int         GetTcpStudentCount() { return m_MapTCPStudent.size(); }
public:
  int         doTcpClientCreate(CTCPClient * lpClient);
  int         doTcpClientDelete(CTCPClient * lpClient);
  int         doUdpClientDelete(CUDPClient * lpClient);
  int         doUdpClientCreate(CUDPClient * lpClient, char * lpBuffer, int inBufSize);
private:
  int         doTcpCreateTeacher(CTCPClient * lpClient);
  int         doTcpCreateStudent(CTCPClient * lpClient);
  int         doTcpDeleteTeacher(CTCPClient * lpClient);
  int         doTcpDeleteStudent(CTCPClient * lpClient);
  int         doUdpDeleteTeacher(CUDPClient * lpClient);
  int         doUdpDeleteStudent(CUDPClient * lpClient);
  int         doUdpCreateTeacher(CUDPClient * lpClient, char * lpBuffer, int inBufSize);
  int         doUdpCreateStudent(CUDPClient * lpClient, char * lpBuffer, int inBufSize);
  int         doUdpCreateForPusher(CUDPClient * lpClient, char * lpBuffer, int inBufSize);
  int         doUdpCreateForLooker(CUDPClient * lpClient, char * lpBuffer, int inBufSize);
private:
  CUDPClient * FindUdpPusher(int inDBCameraID);
private:
  int              m_nRoomID;              // 房间标识号码...
  CTCPClient   *   m_lpTCPTeacher;         // 只有一个老师端长链接...
  GM_MapTCPConn    m_MapTCPStudent;        // 多个学生端长链接...
};