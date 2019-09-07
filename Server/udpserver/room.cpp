
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
  // ע�⣺����ֻ�е���ʦ��Ϊ��ʱ���ŷ���֪ͨ�����򣬻���ɼ��������ӣ���ɺ�����ʦ���޷���½������...
  // ֻ�е���ʦ�˶���Ϊ��ʱ����ת���������仯֪ͨ...
  if( m_lpTCPTeacher == NULL ) {
    GetApp()->doTCPRoomCommand(m_nRoomID, kCmd_UdpServer_AddTeacher);
  }
  // ���½�ʦ�����Ӷ���...
  m_lpTCPTeacher = lpClient;
}

int CRoom::doTcpCreateStudent(CTCPClient * lpClient)
{
  // ��ѧ���ۿ������µ��ۿ��б�...
  int nConnFD = lpClient->GetConnFD();
  m_MapTCPStudent[nConnFD] = lpClient;
  // ��ȡTCP�̶߳���ת���������仯֪ͨ...
  GetApp()->doTCPRoomCommand(m_nRoomID, kCmd_UdpServer_AddStudent);
  // ��ȡ��ǰ������Ľ�ʦ��״̬...
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
  // ����Ƿ��������ʦ�ˣ��ÿգ�����...
  m_lpTCPTeacher = NULL;
  // ��ȡTCP�̶߳���ת���������仯֪ͨ...
  GetApp()->doTCPRoomCommand(m_nRoomID, kCmd_UdpServer_DelTeacher);
  return 0;
}

int CRoom::doTcpDeleteStudent(CTCPClient * lpClient)
{
  int nConnFD = lpClient->GetConnFD();
  // �ҵ���عۿ�ѧ���˶���ֱ��ɾ������...
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nConnFD);
  if( itorItem != m_MapTCPStudent.end() ) {
    m_MapTCPStudent.erase(itorItem);
    // ��ȡTCP�̶߳���ת���������仯֪ͨ...
    GetApp()->doTCPRoomCommand(m_nRoomID, kCmd_UdpServer_DelStudent);
    return 0;
  }
  // ���ͨ��FD��ʽû���ҵ���ͨ��ָ���������...
  itorItem = m_MapTCPStudent.begin();
  while(itorItem != m_MapTCPStudent.end()) {
    // �ҵ�����ؽڵ� => ɾ���ڵ㣬����...
    if(itorItem->second == lpClient) {
      m_MapTCPStudent.erase(itorItem);
      // ��ȡTCP�̶߳���ת���������仯֪ͨ...
      GetApp()->doTCPRoomCommand(m_nRoomID, kCmd_UdpServer_DelStudent);
      return 0;
    }
    // 2018.09.12 - by jackey => ��ɹ���������...
    // ���û���ҵ���ؽڵ� => ������һ��...
    ++itorItem;
  }
  // ͨ��ָ�����Ҳû���ҵ�����ӡ������Ϣ...
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
  // ���������Ľ�ʦ��������Ч...
  if (m_lpTCPTeacher == NULL)
    return nResult;
  // �����ʦ���������Sock��UDP���Sock��һ��...
  int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  int nDstSockFD = lpClient->GetTCPSockID();
  if ( nSrcSockFD != nDstSockFD )
    return nResult;
  int nDBCameraID = lpClient->GetDBCameraID();
  int nHostPort = lpClient->GetHostPort();
  uint8_t idTag = lpClient->GetIdTag();
  // ����ǽ�ʦ�����ߣ���Ҫ���������ж�...
  if (idTag == ID_TAG_PUSHER) {
    // ����µĽ�ʦ����������ԭ�ж�����ͬ(���ܶ�η�����) => ���߷���������TCP����ѧ���ˣ����Դ��������߳���...
    if( m_lpTCPTeacher->GetUdpPusher() != lpClient ) {
      //GetApp()->doUDPTeacherPusherOnLine(m_nRoomID, true);
    }
    // �ظ���ʦ������ => �����Ѿ������ɹ�����Ҫ�ٷ�����������...
    nResult = this->doUdpCreateForPusher(lpClient, lpBuffer, inBufSize);
  } else if (idTag == ID_TAG_LOOKER) {
    // ���Ҷ�Ӧ��ѧ�������ߣ�����������̽�⣬Ȼ�󣬰ѽ�ʦ�ۿ����ü��ϱ���...
    //CUDPClient * lpStudentPusher = this->GetStudentPusher(nDBCameraID);
    // ���ѧ�������߲�Ϊ�գ���Ҫ������̽��...
    //if(lpStudentPusher != NULL) { lpStudentPusher->SetCanDetect(true); }
    // �ظ���ʦ�ۿ��� => ��ѧ�������˵�����ͷת������ʦ�ۿ��� => ����û��P2Pģʽ���ۿ��˲��÷���׼����������...
    nResult = this->doUdpCreateForLooker(lpClient, lpBuffer, inBufSize);
  }
  // ��UDP�ն˱��浽��Ӧ��TCP�ն����� => ������������|�ۿ���...
  return m_lpTCPTeacher->doUdpCreateClient(lpClient);
}

int CRoom::doUdpCreateForPusher(CUDPClient * lpClient, char * lpBuffer, int inBufSize)
{
  // ���췴�������ݰ�...
  rtp_hdr_t rtpHdr = {0};
  rtpHdr.tm = TM_TAG_SERVER;
  rtpHdr.id = ID_TAG_SERVER;
  rtpHdr.pt = PT_TAG_CREATE;
  // �ظ���ʦ������ => �����Ѿ������ɹ�����Ҫ�ٷ�����������...
  return lpClient->doTransferToFrom((char*)&rtpHdr, sizeof(rtpHdr));
}

// ͨ������ͷ��Ų��Ҷ�Ӧ�������ն� => ÿ���ն˶�ֻ��һ��������...
CUDPClient * CRoom::FindUdpPusher(int inDBCameraID)
{
  // �Ȳ��ҽ�ʦ��������������Ƿ�����Ҫ�Ķ���...
  CUDPClient * lpUdpPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  int nCurDBCameraID = ((lpUdpPusher != NULL) ? lpUdpPusher->GetDBCameraID() : 0);
  if (nCurDBCameraID > 0 && nCurDBCameraID == inDBCameraID)
    return lpUdpPusher;
  // Ȼ���ٱ������е�ѧ����������������Ƿ�����Ҫ�Ķ���...
  GM_MapTCPConn::iterator itorItem;
  for(itorItem = m_MapTCPStudent.begin(); itorItem != m_MapTCPStudent.end(); ++itorItem) {
    CTCPClient * lpTcpClient = itorItem->second;
    lpUdpPusher = lpTcpClient->GetUdpPusher();
    // ���ҵ�ǰѧ���˶�������������߶�Ӧ������ͷ���...
    nCurDBCameraID = ((lpUdpPusher != NULL) ? lpUdpPusher->GetDBCameraID() : 0);
    if (nCurDBCameraID > 0 && nCurDBCameraID == inDBCameraID)
      return lpUdpPusher;
  }
  // ��û���ҵ������ؿ�...
  return NULL;
}

int CRoom::doUdpCreateForLooker(CUDPClient * lpClient, char * lpBuffer, int inBufSize)
{
  // ��ȡ������ָ����ŵ������߶��� => �������ߣ�ֱ�ӷ���...
  int nDBCameraID = lpClient->GetDBCameraID();
  CUDPClient * lpUdpPusher = this->FindUdpPusher(nDBCameraID);
  if( lpUdpPusher == NULL )
    return false;
  // ��ȡѧ�������ߵ�����ͷ��Ϣ => ����ͷΪ�գ�ֱ�ӷ���...
  string & strSeqHeader = lpUdpPusher->GetSeqHeader();
  if( strSeqHeader.size() <= 0 )
    return false;
  // �ظ��ۿ��� => �������˵�����ͷת�����ۿ���...
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
  // �����ѧ�������ߣ���Ҫ����������...
  if( idTag == ID_TAG_PUSHER ) {
    // ����µ�ѧ������������ԭ�ж�����ͬ(���ܶ�η�����) => ���߷���������TCP�նˣ����Դ��������߳���...
    if( lpTcpStudent->GetUdpPusher() != lpClient ) {
      //GetApp()->doUDPStudentPusherOnLine(m_nRoomID, nDBCameraID, true);
    }
    nResult = this->doUdpCreateForPusher(lpClient, lpBuffer, inBufSize);
  } else if( idTag == ID_TAG_LOOKER ) {
    nResult = this->doUdpCreateForLooker(lpClient, lpBuffer, inBufSize);
  }
  // ��UDP�ն˱��浽��Ӧ��TCP�ն����� => ������������|�ۿ���...
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
  // ���������Ľ�ʦ��������Ч...
  if (m_lpTCPTeacher == NULL)
    return nResult;
  // �����ʦ���������Sock��UDP���Sock��һ��...
  int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  int nDstSockFD = lpClient->GetTCPSockID();
  if ( nSrcSockFD != nDstSockFD )
    return nResult;
  uint8_t idTag = lpClient->GetIdTag();
  if(idTag == ID_TAG_PUSHER) {
    // �������ʦ������ => ��������TCP����ѧ���ˣ�����ɾ�������߳���
    if(m_lpTCPTeacher->GetUdpPusher() == lpClient) {
      //GetApp()->doUDPTeacherPusherOnLine(m_nRoomID, false);
    }
  } else if( idTag == ID_TAG_LOOKER ) {
    // �������ʦ�ۿ��ˣ�֪ͨѧ��������ֹͣ�������ÿգ�����...
    //CStudent * lpStudentPusher = this->GetStudentPusher(nDBCameraID);
    // ���ѧ�������߲�Ϊ�գ���Ҫ�ر�̽��...
    //if(lpStudentPusher != NULL) { lpStudentPusher->SetCanDetect(false); }
    // ע�⣺���ǵڶ��ַ��� => ֱ�ӷ���ֹͣ���� => ͨ���л�ʱ������ظ�ֹͣ...
    //GetApp()->doUDPTeacherLookerDelete(m_nRoomID, lpTeacher->GetDBCameraID());
  }
  // ��UDP�ն˴Ӷ�Ӧ��TCP�ն�ɾ�� => ������������|�ۿ���...
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
    // �����ѧ���˷����ɾ����֪ͨ��ʦ�ˣ�����ɾ�������߳���...
    //GetApp()->doUDPStudentPusherOnLine(m_nRoomID, nDBCameraID, false);
  } else if( idTag == ID_TAG_LOOKER ) {
    // ѧ���ۿ���.....
  }
  // ��UDP�ն˴Ӷ�Ӧ��TCP�ն�ɾ�� => ������������|�ۿ���...
  return lpTcpStudent->doUdpDeleteClient(lpClient);
}
