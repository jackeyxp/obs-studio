
#include "app.h"
#include "room.h"
#include "tcpthread.h"
#include "tcpclient.h"
#include "udpclient.h"

CRoom::CRoom(int inRoomID, CTCPThread * lpTcpThread)
  : m_lpTCPThread(lpTcpThread)
  , m_lpTCPTeacher(NULL)
  , m_nRoomID(inRoomID)
  , m_nMaxLiveID(0)
{
  assert(m_nRoomID > 0);
  assert(m_lpTCPThread != NULL);
  // 初始化线程互斥对象 => 互斥TCP资源...
  pthread_mutex_init(&m_tcp_mutex, NULL);
}

CRoom::~CRoom()
{
  // 删除线程互斥对象 => 互斥TCP资源...
  pthread_mutex_destroy(&m_tcp_mutex);
}

void CRoom::doDumpRoomInfo()
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // 打印房间编号信息...
  log_trace("\n======== RoomID: %d ========\n", m_nRoomID);
  // 打印房间里讲师端推流者个数，推流者下面的观看者个数...
  CUDPClient * lpUdpTeacherPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  int nTcpSockID   = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetConnFD() : 0);
  int nPusherCount = ((lpUdpTeacherPusher != NULL) ? 1 : 0);
  int nLookerCount = ((lpUdpTeacherPusher != NULL) ? lpUdpTeacherPusher->GetLookerCount() : 0);
  log_trace("Teacher-ID: %d, Teacher-Pusher: %d, Looker-Count: %d\n", nTcpSockID, nPusherCount, nLookerCount);
  GM_MapTCPConn::iterator itorItem;
  // 打印房间里学生端推流者个数，推流者下面的观看者个数...
  for(itorItem = m_MapTCPStudent.begin(); itorItem != m_MapTCPStudent.end(); ++itorItem) {
    CTCPClient * lpTcpClient = itorItem->second;
    CUDPClient * lpUdpPusher = lpTcpClient->GetUdpPusher();
    nPusherCount = ((lpUdpPusher != NULL) ? 1 : 0);
    nLookerCount = ((lpUdpPusher != NULL) ? lpUdpPusher->GetLookerCount() : 0);
    log_trace("Student-ID: %d, Student-Pusher: %d, Looker-Count: %d\n", itorItem->first, nPusherCount, nLookerCount);
  }  
}

int CRoom::GetTcpTeacherCount()
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  return m_lpTCPTeacher ? 1 : 0;
}

int CRoom::GetTcpStudentCount()
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  return m_MapTCPStudent.size();
}

int CRoom::GetTcpTeacherDBFlowID()
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  return ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetDBFlowID() : 0);
}

bool CRoom::IsTcpTeacherClientOnLine()
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  return ((m_lpTCPTeacher != NULL) ? true : false);
}

bool CRoom::IsUdpTeacherPusherOnLine()
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  CUDPClient * lpUdpPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  return ((lpUdpPusher != NULL) ? true : false);
}

void CRoom::doTcpCreateSmart(CTCPClient * lpTcpSmart)
{
  switch( lpTcpSmart->GetClientType() ) {
    case kClientStudent: this->doTcpCreateStudent(lpTcpSmart); break;
    case kClientTeacher: this->doTcpCreateTeacher(lpTcpSmart); break;
  }
}

void CRoom::doTcpDeleteSmart(CTCPClient * lpTcpSmart)
{
  switch( lpTcpSmart->GetClientType() ) {
    case kClientStudent: this->doTcpDeleteStudent(lpTcpSmart); break;
    case kClientTeacher: this->doTcpDeleteTeacher(lpTcpSmart); break;
  }
}

void CRoom::doTcpCreateTeacher(CTCPClient * lpTeacher)
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // 注意：这里只有当讲师端为空时，才发送通知，否则，会造成计数器增加，造成后续讲师端无法登陆的问题...
  // 只有当讲师端对象为空时，才转发计数器变化通知...
  if( m_lpTCPTeacher == NULL && m_lpTCPThread != NULL ) {
    m_lpTCPThread->doRoomCommand(m_nRoomID, kCmd_UdpServer_AddTeacher);
  }
  // 更新讲师端连接对象...
  m_lpTCPTeacher = lpTeacher;
}

void CRoom::doTcpDeleteTeacher(CTCPClient * lpTeacher)
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // 如果是房间里的老师端，置空，返回...
  if( m_lpTCPTeacher == lpTeacher ) {
    m_lpTCPTeacher = NULL;
    // 获取TCP线程对象，转发计数器变化通知...
    m_lpTCPThread->doRoomCommand(m_nRoomID, kCmd_UdpServer_DelTeacher);
    return;
  }
}

// 一个房间可以有多个学生观看端...
void CRoom::doTcpCreateStudent(CTCPClient * lpStudent)
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // 如果终端类型不是学生观看端，直接返回...
  int nConnFD = lpStudent->GetConnFD();
  int nClientType = lpStudent->GetClientType();
  if( nClientType != kClientStudent )
    return;
  // 将学生观看到更新到观看列表...
  m_MapTCPStudent[nConnFD] = lpStudent;
  // 获取TCP线程对象，转发计数器变化通知...
  m_lpTCPThread->doRoomCommand(m_nRoomID, kCmd_UdpServer_AddStudent);
}

void CRoom::doTcpDeleteStudent(CTCPClient * lpStudent)
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // 找到相关观看学生端对象，直接删除返回...
  int nConnFD = lpStudent->GetConnFD();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nConnFD);
  if( itorItem != m_MapTCPStudent.end() ) {
    m_MapTCPStudent.erase(itorItem);
    // 获取TCP线程对象，转发计数器变化通知...
    m_lpTCPThread->doRoomCommand(m_nRoomID, kCmd_UdpServer_DelStudent);
    return;
  }
  // 如果通过FD方式没有找到，通过指针遍历查找...
  itorItem = m_MapTCPStudent.begin();
  while(itorItem != m_MapTCPStudent.end()) {
    // 找到了相关节点 => 删除节点，返回...
    if(itorItem->second == lpStudent) {
      m_MapTCPStudent.erase(itorItem);
      // 获取TCP线程对象，转发计数器变化通知...
      m_lpTCPThread->doRoomCommand(m_nRoomID, kCmd_UdpServer_DelStudent);
      return;
    }
    // 2018.09.12 - by jackey => 造成过严重问题...
    // 如果没有找到相关节点 => 继续下一个...
    ++itorItem;
  }
  // 通过指针遍历也没有找到，打印错误信息...
  log_trace("Can't find TCP-Student in CRoom, ConnFD: %d", nConnFD);
}

// 通过摄像头编号查找对应的推流终端 => 每个终端都只有一个推流者...
CUDPClient * CRoom::doFindUdpPusher(int inLiveID)
{
  //////////////////////////////////////////////////////////////
  // 注意：这个函数都是来自CApp主线程的调用，无需单独互斥...
  //////////////////////////////////////////////////////////////

  // 先查找讲师端里面的推流者是否是想要的对象...
  CUDPClient * lpUdpPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  int nCurLiveID = ((lpUdpPusher != NULL) ? lpUdpPusher->GetLiveID() : 0);
  if (nCurLiveID > 0 && nCurLiveID == inLiveID)
    return lpUdpPusher;
  // 然后，再遍历所有的学生端里面的推流者是否是想要的对象...
  GM_MapTCPConn::iterator itorItem;
  for(itorItem = m_MapTCPStudent.begin(); itorItem != m_MapTCPStudent.end(); ++itorItem) {
    CTCPClient * lpTcpClient = itorItem->second;
    lpUdpPusher = lpTcpClient->GetUdpPusher();
    // 查找当前学生端对象下面的推流者对应的摄像头编号...
    nCurLiveID = ((lpUdpPusher != NULL) ? lpUdpPusher->GetLiveID() : 0);
    if (nCurLiveID > 0 && nCurLiveID == inLiveID)
      return lpUdpPusher;
  }
  // 都没有找到，返回空...
  return NULL;
}

void CRoom::doUdpCreateSmart(CUDPClient * lpUdpSmart)
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  if (lpUdpSmart->GetTmTag() == TM_TAG_TEACHER) {
    // 根据讲师端不同的类型进行分发处理...
    switch(lpUdpSmart->GetIdTag()) {
      case ID_TAG_PUSHER: this->doUdpCreateTeacherPusher(lpUdpSmart); break;
      case ID_TAG_LOOKER: this->doUdpCreateTeacherLooker(lpUdpSmart); break;
    }
  } else if (lpUdpSmart->GetTmTag() == TM_TAG_STUDENT) {
    // 根据学生端不同的类型进行分发处理...
    switch(lpUdpSmart->GetIdTag()) {
      case ID_TAG_PUSHER: this->doUdpCreateStudentPusher(lpUdpSmart); break;
      case ID_TAG_LOOKER: this->doUdpCreateStudentLooker(lpUdpSmart); break;
    }
  }
}

void CRoom::doUdpCreateTeacherPusher(CUDPClient * lpTeacher)
{
  // 如果房间里的讲师长链接无效...
  if (m_lpTCPTeacher == NULL)
    return;
  // 如果讲师长链接里的Sock与UDP里的Sock不一致...
  int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  int nDstSockFD = lpTeacher->GetTCPSockID();
  if (nSrcSockFD != nDstSockFD)
    return;
  // 如果当前讲师推流者的推流编号无效，更新之...
  if (lpTeacher->m_rtp_create.liveID <= 0) {
    lpTeacher->m_rtp_create.liveID = ++m_nMaxLiveID;
  }
  // 如果新的讲师推流对象与原有对象不相同(可能多次发命令) => 告诉房间里所有TCP在线学生端，可以创建拉流线程了...
  if( m_lpTCPTeacher->GetUdpPusher() != lpTeacher ) {
    //GetApp()->doUDPTeacherPusherOnLine(m_nRoomID, true);
  }
  // 将UDP推流终端保存到对应的TCP终端里面...
  m_lpTCPTeacher->doUdpCreatePusher(lpTeacher);
}

void CRoom::doUdpCreateTeacherLooker(CUDPClient * lpTeacher)
{
  // 如果房间里的讲师长链接无效...
  if (m_lpTCPTeacher == NULL)
    return;
  // 如果讲师长链接里的Sock与UDP里的Sock不一致...
  int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  int nDstSockFD = lpTeacher->GetTCPSockID();
  if (nSrcSockFD != nDstSockFD)
    return;
  // 获取输入讲师端UDP连接相关变量...
  int nLiveID = lpTeacher->GetLiveID();
  int nHostPort = lpTeacher->GetHostPort();
  uint8_t idTag = lpTeacher->GetIdTag();
  // 本函数只处理讲师观看者的情况...
  if (idTag != ID_TAG_LOOKER)
    return;
  // 查找推流者，把观看者保存到推流者当中...
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nLiveID);
  if (lpUdpPusher == NULL)
    return;
  // 如果学生推流者不为空，需要打开网络探测...
  //lpUdpPusher->SetCanDetect(true);
  // 将当前观看者保存到推流者的观看集合当中...
  lpUdpPusher->doAddUdpLooker(lpTeacher);
}

void CRoom::doUdpCreateStudentPusher(CUDPClient * lpStudent)
{
  // 查找UDP学生端对象对应的TCP学生端对象...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // 如果当前学生推流者的推流编号无效，更新之...
  if (lpStudent->m_rtp_create.liveID <= 0) {
    lpStudent->m_rtp_create.liveID = ++m_nMaxLiveID;
  }
  // 获取UDP学生端相关的变量内容...
  CTCPClient * lpTcpStudent = itorItem->second;
  // 如果新的学生推流对象与原有对象不相同(可能多次发命令) => 告诉房间里所有TCP终端，可以创建拉流线程了...
  if (lpTcpStudent->GetUdpPusher() != lpStudent) {
    //GetApp()->doUDPStudentPusherOnLine(m_nRoomID, nDBCameraID, true);
  }
  // 将UDP推流终端保存到对应的TCP终端里面...
  lpTcpStudent->doUdpCreatePusher(lpStudent);
}

void CRoom::doUdpCreateStudentLooker(CUDPClient * lpStudent)
{
  // 查找UDP学生端对象对应的TCP学生端对象...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // 获取UDP学生端相关的变量内容...
  uint8_t idTag = lpStudent->GetIdTag();
  int nLiveID = lpStudent->GetLiveID();
  // 如果不是学生端观看者对象...
  if (idTag != ID_TAG_LOOKER)
    return;
  // 查找推流者，把观看者保存到推流者当中...
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nLiveID);
  if (lpUdpPusher == NULL)
    return;
  // 如果推流者不为空，需要打开网络探测...
  // 将当前观看者保存到推流者的观看集合当中...
  lpUdpPusher->doAddUdpLooker(lpStudent);
}

void CRoom::doUdpDeleteSmart(CUDPClient * lpUdpSmart)
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  if (lpUdpSmart->GetTmTag() == TM_TAG_TEACHER) {
    // 根据讲师端不同的类型进行分发处理...
    switch(lpUdpSmart->GetIdTag()) {
      case ID_TAG_PUSHER: this->doUdpDeleteTeacherPusher(lpUdpSmart); break;
      case ID_TAG_LOOKER: this->doUdpDeleteTeacherLooker(lpUdpSmart); break;
    }
  } else if (lpUdpSmart->GetTmTag() == TM_TAG_STUDENT) {
    // 根据学生端不同的类型进行分发处理...
    switch(lpUdpSmart->GetIdTag()) {
      case ID_TAG_PUSHER: this->doUdpDeleteStudentPusher(lpUdpSmart); break;
      case ID_TAG_LOOKER: this->doUdpDeleteStudentLooker(lpUdpSmart); break;
    }
  }
}

void CRoom::doUdpDeleteTeacherPusher(CUDPClient * lpTeacher)
{
  // 如果房间里的讲师长链接无效...
  if (m_lpTCPTeacher == NULL)
    return;
  // 如果讲师长链接里的Sock与UDP里的Sock不一致...
  int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  int nDstSockFD = lpTeacher->GetTCPSockID();
  if ( nSrcSockFD != nDstSockFD )
    return;
  // 获取输入讲师端UDP连接相关变量...
  int nLiveID = lpTeacher->GetLiveID();
  int nHostPort = lpTeacher->GetHostPort();
  uint8_t idTag = lpTeacher->GetIdTag();
  // 如果不是老师推流者对象...
  if (idTag != ID_TAG_PUSHER)
    return;
  // 告诉所有TCP在线学生端，可以删除拉流线程了
  if(m_lpTCPTeacher->GetUdpPusher() == lpTeacher) {
    //GetApp()->doUDPTeacherPusherOnLine(m_nRoomID, false);
  }
  // 将UDP推流终端从对应的TCP终端里面删除之...
  m_lpTCPTeacher->doUdpDeletePusher(lpTeacher);
}

void CRoom::doUdpDeleteTeacherLooker(CUDPClient * lpTeacher)
{
  // 如果房间里的讲师长链接无效...
  if (m_lpTCPTeacher == NULL)
    return;
  // 如果讲师长链接里的Sock与UDP里的Sock不一致...
  int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  int nDstSockFD = lpTeacher->GetTCPSockID();
  if ( nSrcSockFD != nDstSockFD )
    return;
  // 获取输入讲师端UDP连接相关变量...
  int nLiveID = lpTeacher->GetLiveID();
  int nHostPort = lpTeacher->GetHostPort();
  uint8_t idTag = lpTeacher->GetIdTag();
  // 如果不是讲师观看者对象...
  if (idTag != ID_TAG_LOOKER)
    return;
  // 如果是老师观看端，通知学生推流端停止推流，置空，返回...
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nLiveID);
  if (lpUdpPusher == NULL)
    return;
  // 如果学生推流者不为空，需要关闭网络探测...
  //lpUdpPusher->SetCanDetect(false); }
  // 将当前观看者从推流者的观看集合当中删除...
  lpUdpPusher->doDelUdpLooker(lpTeacher);
}

void CRoom::doUdpDeleteStudentPusher(CUDPClient * lpStudent)
{
  // 查找UDP学生端对象对应的TCP学生端对象...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // 获取UDP学生端相关的变量内容...
  uint8_t idTag = lpStudent->GetIdTag();
  int nLiveID = lpStudent->GetLiveID();
  CTCPClient * lpTcpStudent = itorItem->second;
  // 如果不是学生推流者对象...
  if( idTag != ID_TAG_PUSHER )
    return;
  // 如果是学生推流端发起的删除，通知讲师端，可以删除拉流线程了...
  //GetApp()->doUDPStudentPusherOnLine(m_nRoomID, nLiveID, false);
  // 将UDP推流终端从对应的TCP终端里面删除之...
  lpTcpStudent->doUdpDeletePusher(lpStudent);
}

void CRoom::doUdpDeleteStudentLooker(CUDPClient * lpStudent)
{
  // 查找UDP学生端对象对应的TCP学生端对象...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // 获取UDP学生端相关的变量内容...
  uint8_t idTag = lpStudent->GetIdTag();
  int nLiveID = lpStudent->GetLiveID();
  // 如果不是学生观看者对象...
  if (idTag != ID_TAG_LOOKER)
    return;
  // 如果是学生观看者发起的删除，找到对应的推流者.....
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nLiveID);
  if (lpUdpPusher == NULL)
    return;
  // 将当前观看者从推流者的观看集合当中删除...
  lpUdpPusher->doDelUdpLooker(lpStudent);
}
