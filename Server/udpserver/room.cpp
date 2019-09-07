
#include "app.h"
#include "room.h"
#include "tcpclient.h"
#include "udpclient.h"

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
    GetApp()->doTCPRoomCommand(m_nRoomID, kCmd_UdpServer_AddTeacher);
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
  GetApp()->doTCPRoomCommand(m_nRoomID, kCmd_UdpServer_AddStudent);
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
  GetApp()->doTCPRoomCommand(m_nRoomID, kCmd_UdpServer_DelTeacher);
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
    GetApp()->doTCPRoomCommand(m_nRoomID, kCmd_UdpServer_DelStudent);
    return 0;
  }
  // 如果通过FD方式没有找到，通过指针遍历查找...
  itorItem = m_MapTCPStudent.begin();
  while(itorItem != m_MapTCPStudent.end()) {
    // 找到了相关节点 => 删除节点，返回...
    if(itorItem->second == lpClient) {
      m_MapTCPStudent.erase(itorItem);
      // 获取TCP线程对象，转发计数器变化通知...
      GetApp()->doTCPRoomCommand(m_nRoomID, kCmd_UdpServer_DelStudent);
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

int CRoom::doUdpClientCreate(CUDPClient * lpClient, char * lpBuffer, int inBufSize)
{
  int nResult = -1;
  switch( lpClient->GetTmTag() )
  {
    case TM_TAG_STUDENT: nResult = this->doUdpCreateStudent(lpClient, lpBuffer, inBufSize); break;
    case TM_TAG_TEACHER: nResult = this->doUdpCreateTeacher(lpClient, lpBuffer, inBufSize); break;
  }
  return nResult;
}

int CRoom::doUdpCreateTeacher(CUDPClient * lpClient, char * lpBuffer, int inBufSize)
{
  int nResult = -1;
  // 如果房间里的讲师长链接无效...
  if (m_lpTCPTeacher == NULL)
    return nResult;
  // 如果讲师长链接里的Sock与UDP里的Sock不一致...
  int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  int nDstSockFD = lpClient->GetTCPSockID();
  if ( nSrcSockFD != nDstSockFD )
    return nResult;
  int nDBCameraID = lpClient->GetDBCameraID();
  int nHostPort = lpClient->GetHostPort();
  uint8_t idTag = lpClient->GetIdTag();
  // 如果是讲师推流者，需要进行特殊判断...
  if (idTag == ID_TAG_PUSHER) {
    // 如果新的讲师推流对象与原有对象不相同(可能多次发命令) => 告诉房间里所有TCP在线学生端，可以创建拉流线程了...
    if( m_lpTCPTeacher->GetUdpPusher() != lpClient ) {
      //GetApp()->doUDPTeacherPusherOnLine(m_nRoomID, true);
    }
    // 回复老师推流端 => 房间已经创建成功，不要再发创建命令了...
    nResult = this->doUdpCreateForPusher(lpClient, lpBuffer, inBufSize);
  } else if (idTag == ID_TAG_LOOKER) {
    // 查找对应的学生推流者，并开启网络探测，然后，把讲师观看者用集合保存...
    //CUDPClient * lpStudentPusher = this->GetStudentPusher(nDBCameraID);
    // 如果学生推流者不为空，需要打开网络探测...
    //if(lpStudentPusher != NULL) { lpStudentPusher->SetCanDetect(true); }
    // 回复老师观看端 => 将学生推流端的序列头转发给老师观看端 => 由于没有P2P模式，观看端不用发送准备就绪命令...
    nResult = this->doUdpCreateForLooker(lpClient, lpBuffer, inBufSize);
  }
  // 将UDP终端保存到对应的TCP终端里面 => 可能是推流者|观看者...
  return m_lpTCPTeacher->doUdpCreateClient(lpClient);
}

int CRoom::doUdpCreateForPusher(CUDPClient * lpClient, char * lpBuffer, int inBufSize)
{
  // 构造反馈的数据包...
  rtp_hdr_t rtpHdr = {0};
  rtpHdr.tm = TM_TAG_SERVER;
  rtpHdr.id = ID_TAG_SERVER;
  rtpHdr.pt = PT_TAG_CREATE;
  // 回复老师推流端 => 房间已经创建成功，不要再发创建命令了...
  return lpClient->doTransferToFrom((char*)&rtpHdr, sizeof(rtpHdr));
}

// 通过摄像头编号查找对应的推流终端 => 每个终端都只有一个推流者...
CUDPClient * CRoom::FindUdpPusher(int inDBCameraID)
{
  // 先查找讲师端里面的推流者是否是想要的对象...
  CUDPClient * lpUdpPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  int nCurDBCameraID = ((lpUdpPusher != NULL) ? lpUdpPusher->GetDBCameraID() : 0);
  if (nCurDBCameraID > 0 && nCurDBCameraID == inDBCameraID)
    return lpUdpPusher;
  // 然后，再遍历所有的学生端里面的推流者是否是想要的对象...
  GM_MapTCPConn::iterator itorItem;
  for(itorItem = m_MapTCPStudent.begin(); itorItem != m_MapTCPStudent.end(); ++itorItem) {
    CTCPClient * lpTcpClient = itorItem->second;
    lpUdpPusher = lpTcpClient->GetUdpPusher();
    // 查找当前学生端对象下面的推流者对应的摄像头编号...
    nCurDBCameraID = ((lpUdpPusher != NULL) ? lpUdpPusher->GetDBCameraID() : 0);
    if (nCurDBCameraID > 0 && nCurDBCameraID == inDBCameraID)
      return lpUdpPusher;
  }
  // 都没有找到，返回空...
  return NULL;
}

int CRoom::doUdpCreateForLooker(CUDPClient * lpClient, char * lpBuffer, int inBufSize)
{
  // 获取房间里指定编号的推流者对象 => 无推流者，直接返回...
  int nDBCameraID = lpClient->GetDBCameraID();
  CUDPClient * lpUdpPusher = this->FindUdpPusher(nDBCameraID);
  if( lpUdpPusher == NULL )
    return false;
  // 获取学生推流者的序列头信息 => 序列头为空，直接返回...
  string & strSeqHeader = lpUdpPusher->GetSeqHeader();
  if( strSeqHeader.size() <= 0 )
    return false;
  // 回复观看端 => 将推流端的序列头转发给观看端...
  return lpClient->doTransferToFrom((char*)strSeqHeader.c_str(), strSeqHeader.size());
}

int CRoom::doUdpCreateStudent(CUDPClient * lpClient, char * lpBuffer, int inBufSize)
{
  int nResult = -1;
  int nTcpSockFD = lpClient->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return nResult;
  uint8_t idTag = lpClient->GetIdTag();
  int nDBCameraID = lpClient->GetDBCameraID();
  CTCPClient * lpTcpStudent = itorItem->second;
  // 如果是学生推流者，需要进行特殊检测...
  if( idTag == ID_TAG_PUSHER ) {
    // 如果新的学生推流对象与原有对象不相同(可能多次发命令) => 告诉房间里所有TCP终端，可以创建拉流线程了...
    if( lpTcpStudent->GetUdpPusher() != lpClient ) {
      //GetApp()->doUDPStudentPusherOnLine(m_nRoomID, nDBCameraID, true);
    }
    nResult = this->doUdpCreateForPusher(lpClient, lpBuffer, inBufSize);
  } else if( idTag == ID_TAG_LOOKER ) {
    nResult = this->doUdpCreateForLooker(lpClient, lpBuffer, inBufSize);
  }
  // 将UDP终端保存到对应的TCP终端里面 => 可能是推流者|观看者...
  return lpTcpStudent->doUdpCreateClient(lpClient);
}

int CRoom::doUdpClientDelete(CUDPClient * lpClient)
{
  int nResult = -1;
  switch( lpClient->GetTmTag() )
  {
    case TM_TAG_STUDENT: nResult = this->doUdpDeleteStudent(lpClient); break;
    case TM_TAG_TEACHER: nResult = this->doUdpDeleteTeacher(lpClient); break;
  }
  return nResult;
}

int CRoom::doUdpDeleteTeacher(CUDPClient * lpClient)
{
  int nResult = -1;
  // 如果房间里的讲师长链接无效...
  if (m_lpTCPTeacher == NULL)
    return nResult;
  // 如果讲师长链接里的Sock与UDP里的Sock不一致...
  int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  int nDstSockFD = lpClient->GetTCPSockID();
  if ( nSrcSockFD != nDstSockFD )
    return nResult;
  uint8_t idTag = lpClient->GetIdTag();
  if(idTag == ID_TAG_PUSHER) {
    // 如果是老师推流端 => 告诉所有TCP在线学生端，可以删除拉流线程了
    if(m_lpTCPTeacher->GetUdpPusher() == lpClient) {
      //GetApp()->doUDPTeacherPusherOnLine(m_nRoomID, false);
    }
  } else if( idTag == ID_TAG_LOOKER ) {
    // 如果是老师观看端，通知学生推流端停止推流，置空，返回...
    //CStudent * lpStudentPusher = this->GetStudentPusher(nDBCameraID);
    // 如果学生推流者不为空，需要关闭探测...
    //if(lpStudentPusher != NULL) { lpStudentPusher->SetCanDetect(false); }
    // 注意：这是第二种方案 => 直接发送停止推流 => 通道切换时会造成重复停止...
    //GetApp()->doUDPTeacherLookerDelete(m_nRoomID, lpTeacher->GetDBCameraID());
  }
  // 将UDP终端从对应的TCP终端删除 => 可能是推流者|观看者...
  return m_lpTCPTeacher->doUdpDeleteClient(lpClient);
}

int CRoom::doUdpDeleteStudent(CUDPClient * lpClient)
{
  int nResult = -1;
  int nTcpSockFD = lpClient->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return nResult;
  uint8_t idTag = lpClient->GetIdTag();
  int nDBCameraID = lpClient->GetDBCameraID();
  CTCPClient * lpTcpStudent = itorItem->second;
  if( idTag == ID_TAG_PUSHER ) {
    // 如果是学生端发起的删除，通知讲师端，可以删除拉流线程了...
    //GetApp()->doUDPStudentPusherOnLine(m_nRoomID, nDBCameraID, false);
  } else if( idTag == ID_TAG_LOOKER ) {
    // 学生观看者.....
  }
  // 将UDP终端从对应的TCP终端删除 => 可能是推流者|观看者...
  return lpTcpStudent->doUdpDeleteClient(lpClient);
}
