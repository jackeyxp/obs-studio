
#include "app.h"
#include "room.h"
#include "tcpthread.h"
#include "tcpclient.h"
#include "udpclient.h"

CRoom::CRoom(int inRoomID, CTCPThread * lpTcpThread)
  : m_lpTCPThread(lpTcpThread)
  , m_lpTCPTeacher(NULL)
  , m_nRoomID(inRoomID)
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

CCamera * CRoom::doCreateCamera(CTCPClient * lpTCPClient, int inDBCameraID, string & inPCName, string & inCameraName)
{
  // 如果找到了摄像头对象，更新数据...
  CCamera * lpCamera = NULL;
  GM_MapCamera::iterator itorItem = m_MapCamera.find(inDBCameraID);
  if( itorItem != m_MapCamera.end() ) {
    lpCamera = itorItem->second;
    lpCamera->m_strPCName = inPCName;
    lpCamera->m_strCameraName = inCameraName;
    lpCamera->m_lpTCPClient = lpTCPClient;
    return lpCamera;
  }
  // 如果没有找到摄像头对象，创建一个新的对象...
  lpCamera = new CCamera(lpTCPClient, m_nRoomID, inDBCameraID, inPCName, inCameraName);
  m_MapCamera[inDBCameraID] = lpCamera;
  return lpCamera;
}

// 删除摄像头 => 学生端发起的停止命令...
void CRoom::doDeleteCamera(int inDBCameraID)
{
  // 如果找到了摄像头对象，直接删除之...
  GM_MapCamera::iterator itorItem = m_MapCamera.find(inDBCameraID);
  if (itorItem != m_MapCamera.end()) {
    delete itorItem->second;
    m_MapCamera.erase(itorItem);
  }
}

void CRoom::doDumpRoomInfo()
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  string strInfo; char szBuffer[256] = { 0 };
  // 打印房间里讲师端推流者个数，推流者下面的观看者个数...
  CUDPClient * lpUdpTeacherPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  int nTcpSockID   = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetConnFD() : 0);
  int nLiveID = ((lpUdpTeacherPusher != NULL) ? lpUdpTeacherPusher->GetLiveID() : 0);
  int nLookerCount = ((lpUdpTeacherPusher != NULL) ? lpUdpTeacherPusher->GetLookerCount() : 0);
  sprintf(szBuffer, "TCP-Teacher-ID: %d, UDPPusher-LiveID: %d, Looker-Count: %d\n", nTcpSockID, nLiveID, nLookerCount);
  strInfo.append(szBuffer);
  GM_MapTCPConn::iterator itorItem;
  // 打印房间里学生端推流者个数，推流者下面的观看者个数...
  for(itorItem = m_MapTCPStudent.begin(); itorItem != m_MapTCPStudent.end(); ++itorItem) {
    CTCPClient * lpTcpClient = itorItem->second;
    CUDPClient * lpUdpPusher = lpTcpClient->GetUdpPusher();
    nLiveID = ((lpUdpPusher != NULL) ? lpUdpPusher->GetLiveID() : 0);
    nLookerCount = ((lpUdpPusher != NULL) ? lpUdpPusher->GetLookerCount() : 0);
    sprintf(szBuffer, "TCP-Student-ID: %d, UDPPusher-LiveID: %d, Looker-Count: %d\n", itorItem->first, nLiveID, nLookerCount);
    strInfo.append(szBuffer);
  }
  // 打印最终组合完成后的房间内容信息 => 组合讲师终端和学生终端的信息...
  log_trace("\n======== RoomID: %d ========\n%s", m_nRoomID, strInfo.c_str());
}

void CRoom::doUdpLogoutToTcp(int nTCPSockFD, int nLiveID, uint8_t tmTag, uint8_t idTag)
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  if (m_lpTCPThread == NULL) return;
  m_lpTCPThread->doUdpLogoutToTcp(nTCPSockFD, nLiveID, tmTag, idTag);
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

int CRoom::GetUdpTeacherLiveID()
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  CUDPClient * lpUdpPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  return ((lpUdpPusher != NULL) ? lpUdpPusher->GetLiveID() : 0);
}

/*bool CRoom::IsTcpTeacherClientOnLine()
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
}*/

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
    // 告诉所有TCP在线学生端，可以删除拉流线程了
    CUDPClient * lpUdpPusher = lpTeacher->GetUdpPusher();
    int nLiveID = ((lpUdpPusher != NULL) ? lpUdpPusher->GetLiveID() : 0);
    if( lpUdpPusher != NULL && nLiveID > 0 ) {
      this->doUdpLiveOnLine(m_lpTCPTeacher, nLiveID, false);
    }
    // 获取TCP线程对象，转发计数器变化通知...
    m_lpTCPThread->doRoomCommand(m_nRoomID, kCmd_UdpServer_DelTeacher);
    // 重置讲师端连接对象...
    m_lpTCPTeacher = NULL;
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
  // 遍历房间里的摄像头列表，通过指针进行匹配...
  CCamera * lpCamera = NULL;
  GM_MapCamera::iterator itorCamera = m_MapCamera.begin();
  while(itorCamera != m_MapCamera.end()) {
    lpCamera = itorCamera->second;
    // 如果摄像头的对象与当前学生端的对象不一致，继续下一个...
    if( lpCamera->m_lpTCPClient != lpStudent) {
      ++itorCamera; continue;
    }
    // 套接字是一致的，删除摄像头，列表中删除...
    delete lpCamera; lpCamera = NULL;
    m_MapCamera.erase(itorCamera++);
  }
  // FD方式不一定有效，通过指针遍历查找...
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.begin();
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
  log_trace("Can't find TCP-Student in CRoom, ConnFD: %d", lpStudent->GetConnFD());
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

void CRoom::doUdpHeaderSmart(CUDPClient * lpUdpSmart)
{
  // 利用构造函数|析构函数进行互斥保护...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // 只有推流者才会处理序列头命令...
  if (lpUdpSmart->GetIdTag() != ID_TAG_PUSHER)
    return;
  // 根据不同终端进行不同处理...
  switch(lpUdpSmart->GetTmTag()) {
    case TM_TAG_TEACHER: this->doUdpHeaderTeacherPusher(lpUdpSmart); break;
    case TM_TAG_STUDENT: this->doUdpHeaderStudentPusher(lpUdpSmart); break;
  }
}

void CRoom::doUdpHeaderTeacherPusher(CUDPClient * lpTeacher)
{
  // 如果房间里的讲师长链接无效...
  if (m_lpTCPTeacher == NULL || lpTeacher == NULL)
    return;
  // 如果新的讲师推流对象与原有对象不相同(可能多次发命令) => 告诉房间里所有TCP在线学生端，可以创建拉流线程了...
  if( m_lpTCPTeacher->GetUdpPusher() != lpTeacher ) {
    this->doUdpLiveOnLine(m_lpTCPTeacher, lpTeacher->GetLiveID(), true);
  }
  // 将UDP推流终端保存到对应的TCP终端里面...
  m_lpTCPTeacher->doUdpCreatePusher(lpTeacher);
}

void CRoom::doUdpHeaderStudentPusher(CUDPClient * lpStudent)
{
  // 查找UDP学生端对象对应的TCP学生端对象...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // 获取UDP学生端相关的变量内容...
  CTCPClient * lpTcpStudent = itorItem->second;
  // 如果新的学生推流对象与原有对象不相同(可能多次发命令) => 告诉房间里所有TCP终端，可以创建拉流线程了...
  if (lpTcpStudent->GetUdpPusher() != lpStudent) {
    this->doUdpLiveOnLine(lpTcpStudent, lpStudent->GetLiveID(), true);
  }
  // 将UDP推流终端保存到对应的TCP终端里面...
  lpTcpStudent->doUdpCreatePusher(lpStudent);
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
  // 注意：推流编号已经放到数据库当中了...
  // 如果当前讲师推流者的推流编号无效，更新之...
  //if (lpTeacher->m_rtp_create.liveID <= 0) {
  //  lpTeacher->m_rtp_create.liveID = ++m_nMaxLiveID;
  //}
  // 注意：不能在这里发送拉流通知，因为回复给推流端的tagCreate命令可能丢失，造成LiveID重复创建...
  // 如果新的讲师推流对象与原有对象不相同(可能多次发命令) => 告诉房间里所有TCP在线学生端，可以创建拉流线程了...
  /*if( m_lpTCPTeacher->GetUdpPusher() != lpTeacher ) {
    this->doUdpLiveOnLine(m_lpTCPTeacher, lpTeacher->GetLiveID(), true);
  }
  // 将UDP推流终端保存到对应的TCP终端里面...
  m_lpTCPTeacher->doUdpCreatePusher(lpTeacher);*/
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
  // 查找推流者，把观看者保存到推流者当中...
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nLiveID);
  if (lpUdpPusher == NULL)
    return;
  // 目前学生端会一直保持推流，这里就简化了...
  // 如果学生推流者不为空，需要打开网络探测...
  lpUdpPusher->SetCanDetect(true);
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
  // 注意：推流编号已经放到数据库当中了...
  // 如果当前学生推流者的推流编号无效，更新之...
  //if (lpStudent->m_rtp_create.liveID <= 0) {
  //  lpStudent->m_rtp_create.liveID = ++m_nMaxLiveID;
  //}
  // 注意：不能在这里发送拉流通知，因为回复给推流端的tagCreate命令可能丢失，造成LiveID重复创建...
  // 如果新的学生推流对象与原有对象不相同(可能多次发命令) => 告诉房间里所有TCP终端，可以创建拉流线程了...
  /*CTCPClient * lpTcpStudent = itorItem->second;
  if (lpTcpStudent->GetUdpPusher() != lpStudent) {
    this->doUdpLiveOnLine(lpTcpStudent, lpStudent->GetLiveID(), true);
  }
  // 将UDP推流终端保存到对应的TCP终端里面...
  lpTcpStudent->doUdpCreatePusher(lpStudent);*/
}

void CRoom::doUdpCreateStudentLooker(CUDPClient * lpStudent)
{
  // 查找UDP学生端对象对应的TCP学生端对象...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // 获取UDP学生端相关的变量内容...
  int nLiveID = lpStudent->GetLiveID();
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
  // 告诉所有TCP在线学生端，可以删除拉流线程了
  if(m_lpTCPTeacher->GetUdpPusher() == lpTeacher) {
    this->doUdpLiveOnLine(m_lpTCPTeacher, lpTeacher->GetLiveID(), false);
  }
  // 将UDP推流终端从对应的TCP终端里面删除之...
  m_lpTCPTeacher->doUdpDeletePusher(lpTeacher);
}

void CRoom::doUdpDeleteTeacherLooker(CUDPClient * lpTeacher)
{
  //////////////////////////////////////////////////////////////////
  // 注意：不用判断那么精确，否则，讲师端直接退出，无法删除...
  //////////////////////////////////////////////////////////////////
  // 如果房间里的讲师长链接无效...
  //if (m_lpTCPTeacher == NULL)
  //  return;
  // 如果讲师长链接里的Sock与UDP里的Sock不一致...
  //int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  //int nDstSockFD = lpTeacher->GetTCPSockID();
  //if ( nSrcSockFD != nDstSockFD )
  //  return;

  // 获取输入讲师端UDP连接相关变量...
  int nLiveID = lpTeacher->GetLiveID();
  // 如果是老师观看端，通知学生推流端停止推流，置空，返回...
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nLiveID);
  if (lpUdpPusher == NULL)
    return;
  // 目前学生端会一直保持推流，这里就简化了...
  // 如果学生推流者不为空，需要关闭网络探测...
  lpUdpPusher->SetCanDetect(false);
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
  CTCPClient * lpTcpStudent = itorItem->second;
  // 如果是学生推流端发起的删除，通知所有拉流终端，可以删除拉流线程了...
  if (lpTcpStudent->GetUdpPusher() == lpStudent) {
    this->doUdpLiveOnLine(lpTcpStudent, lpStudent->GetLiveID(), false);
  }
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
  int nLiveID = lpStudent->GetLiveID();
  // 如果是学生观看者发起的删除，找到对应的推流者.....
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nLiveID);
  if (lpUdpPusher == NULL)
    return;
  // 将当前观看者从推流者的观看集合当中删除...
  lpUdpPusher->doDelUdpLooker(lpStudent);
}

void CRoom::doUdpLiveOnLine(CTCPClient * lpTcpExclude, int inLiveID, bool bIsOnLineFlag)
{
  // 如果老师端有效，并且不是老师端自己的推流对象，通知老师端可以拉取或删除指定编号的推流者了...
  if( m_lpTCPTeacher != NULL && lpTcpExclude != m_lpTCPTeacher ) {
    m_lpTCPTeacher->doUdpLiveOnLine(inLiveID, bIsOnLineFlag);
  }
  // 如果是学生端推流，只通知老师端，不要通知房间里的其它学生端...
  if( lpTcpExclude->GetClientType() == kClientStudent)
    return;
  // 遍历房间里所有的学生端对象...
  GM_MapTCPConn::iterator itorItem;
  for(itorItem = m_MapTCPStudent.begin(); itorItem != m_MapTCPStudent.end(); ++itorItem) {
    CTCPClient * lpTcpStudent = itorItem->second;
    // 如果学生端对象与当前推流对象一致，继续下一个...
    if (lpTcpStudent == lpTcpExclude) continue;
    // 通知这个学生端对象，可以拉取或删除指定编号的推流者了...
    lpTcpStudent->doUdpLiveOnLine(inLiveID, bIsOnLineFlag);
  }  
}
