
#include "app.h"
#include "room.h"
#include "tcpclient.h"

CRoom::CRoom(int inRoomID)
  : m_nRoomID(inRoomID)
  , m_lpTCPTeacher(NULL)
{
}

CRoom::~CRoom()
{
}

int CRoom::GetTeacherDBFlowID()
{
  return ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetDBFlowID() : 0);
}

int CRoom::doTcpClientCreate(CTCPClient * lpClient)
{
  int nResult = -1;
  switch( lpClient->GetClientType() )
  {
    case kClientTeacher: nResult = this->doTcpCreateTeacher(lpClient); break;
    case kClientStudent: nResult = this->doTcpCreateStudent(lpClient); break;
  }
  return nResult;
}

int CRoom::doTcpCreateTeacher(CTCPClient * lpClient)
{
  // 注意：这里只有当讲师端为空时，才发送通知，否则，会造成计数器增加，造成后续讲师端无法登陆的问题...
  // 只有当讲师端对象为空时，才转发计数器变化通知...
  if( m_lpTCPTeacher == NULL ) {
    GetApp()->doTCPRoomCommand(kCmd_UdpServer_AddTeacher, m_nRoomID);
  }
  // 更新讲师端连接对象...
  m_lpTCPTeacher = lpClient;
}

int CRoom::doTcpCreateStudent(CTCPClient * lpClient)
{
  // 将学生观看到更新到观看列表...
  int nConnFD = lpClient->GetConnFD();
  m_MapTCPStudent[nConnFD] = lpClient;
  // 获取TCP线程对象，转发计数器变化通知...
  GetApp()->doTCPRoomCommand(kCmd_UdpServer_AddStudent, m_nRoomID);
  // 获取当前房间里的讲师端状态...
  bool bIsUDPTeacherOnLine = false;
  bool bIsTCPTeacherOnLine = ((m_lpTCPTeacher != NULL) ? true : false);
  int  nTeacherFlowID = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetDBFlowID() : 0);
  return lpClient->doSendCmdLoginForStudent(bIsTCPTeacherOnLine, bIsUDPTeacherOnLine, nTeacherFlowID);
}

int CRoom::doTcpClientDelete(CTCPClient * lpClient)
{
  int nResult = -1;
  switch( lpClient->GetClientType() )
  {
    case kClientTeacher: nResult = this->doTcpDeleteTeacher(lpClient); break;
    case kClientStudent: nResult = this->doTcpDeleteStudent(lpClient); break;
  }
  return nResult;
}

int CRoom::doTcpDeleteTeacher(CTCPClient * lpClient)
{
  if (m_lpTCPTeacher != lpClient) {
    log_trace("CRoom::doTcpDeleteTeacher");
    return -1;
  }
  // 如果是房间里的老师端，置空，返回...
  m_lpTCPTeacher = NULL;
  // 获取TCP线程对象，转发计数器变化通知...
  GetApp()->doTCPRoomCommand(kCmd_UdpServer_DelTeacher, m_nRoomID);
  return 0;
}

int CRoom::doTcpDeleteStudent(CTCPClient * lpClient)
{
  int nConnFD = lpClient->GetConnFD();
  // 找到相关观看学生端对象，直接删除返回...
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nConnFD);
  if( itorItem != m_MapTCPStudent.end() ) {
    m_MapTCPStudent.erase(itorItem);
    // 获取TCP线程对象，转发计数器变化通知...
    GetApp()->doTCPRoomCommand(kCmd_UdpServer_DelStudent, m_nRoomID);
    return 0;
  }
  // 如果通过FD方式没有找到，通过指针遍历查找...
  itorItem = m_MapTCPStudent.begin();
  while(itorItem != m_MapTCPStudent.end()) {
    // 找到了相关节点 => 删除节点，返回...
    if(itorItem->second == lpClient) {
      m_MapTCPStudent.erase(itorItem);
      // 获取TCP线程对象，转发计数器变化通知...
      GetApp()->doTCPRoomCommand(kCmd_UdpServer_DelStudent, m_nRoomID);
      return 0;
    }
    // 2018.09.12 - by jackey => 造成过严重问题...
    // 如果没有找到相关节点 => 继续下一个...
    ++itorItem;
  }
  // 通过指针遍历也没有找到，打印错误信息...
  log_trace("Can't find TCP-Student in CRoom, ConnFD: %d", nConnFD);
  return -1;
}

/*int CRoom::doUdpClientCreate(CUDPClient * lpClient)
{
  
}

int CRoom::doUdpClientDelete(CUDPClient * lpClient)
{
  
}*/