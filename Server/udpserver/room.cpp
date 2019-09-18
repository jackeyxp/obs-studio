
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
  // ��ʼ���̻߳������ => ����TCP��Դ...
  pthread_mutex_init(&m_tcp_mutex, NULL);
}

CRoom::~CRoom()
{
  // ɾ���̻߳������ => ����TCP��Դ...
  pthread_mutex_destroy(&m_tcp_mutex);
}

void CRoom::doDumpRoomInfo()
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // ��ӡ��������Ϣ...
  log_trace("\n======== RoomID: %d ========\n", m_nRoomID);
  // ��ӡ�����ｲʦ�������߸���������������Ĺۿ��߸���...
  CUDPClient * lpUdpTeacherPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  int nTcpSockID   = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetConnFD() : 0);
  int nPusherCount = ((lpUdpTeacherPusher != NULL) ? 1 : 0);
  int nLookerCount = ((lpUdpTeacherPusher != NULL) ? lpUdpTeacherPusher->GetLookerCount() : 0);
  log_trace("Teacher-ID: %d, Teacher-Pusher: %d, Looker-Count: %d\n", nTcpSockID, nPusherCount, nLookerCount);
  GM_MapTCPConn::iterator itorItem;
  // ��ӡ������ѧ���������߸���������������Ĺۿ��߸���...
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
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  return m_lpTCPTeacher ? 1 : 0;
}

int CRoom::GetTcpStudentCount()
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  return m_MapTCPStudent.size();
}

int CRoom::GetTcpTeacherDBFlowID()
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  return ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetDBFlowID() : 0);
}

bool CRoom::IsTcpTeacherClientOnLine()
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  return ((m_lpTCPTeacher != NULL) ? true : false);
}

bool CRoom::IsUdpTeacherPusherOnLine()
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  CUDPClient * lpUdpPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  return ((lpUdpPusher != NULL) ? true : false);
}

void CRoom::doTcpCreateSmart(CTCPClient * lpSmart)
{
  switch( lpSmart->GetClientType() ) {
    case kClientStudent: this->doTcpCreateStudent(lpSmart); break;
    case kClientTeacher: this->doTcpCreateTeacher(lpSmart); break;
  }
}

void CRoom::doTcpDeleteSmart(CTCPClient * lpSmart)
{
  switch( lpSmart->GetClientType() ) {
    case kClientStudent: this->doTcpDeleteStudent(lpSmart); break;
    case kClientTeacher: this->doTcpDeleteTeacher(lpSmart); break;
  }
}

void CRoom::doTcpCreateTeacher(CTCPClient * lpTeacher)
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // ע�⣺����ֻ�е���ʦ��Ϊ��ʱ���ŷ���֪ͨ�����򣬻���ɼ��������ӣ���ɺ�����ʦ���޷���½������...
  // ֻ�е���ʦ�˶���Ϊ��ʱ����ת���������仯֪ͨ...
  if( m_lpTCPTeacher == NULL && m_lpTCPThread != NULL ) {
    m_lpTCPThread->doRoomCommand(m_nRoomID, kCmd_UdpServer_AddTeacher);
  }
  // ���½�ʦ�����Ӷ���...
  m_lpTCPTeacher = lpTeacher;
}

void CRoom::doTcpDeleteTeacher(CTCPClient * lpTeacher)
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // ����Ƿ��������ʦ�ˣ��ÿգ�����...
  if( m_lpTCPTeacher == lpTeacher ) {
    m_lpTCPTeacher = NULL;
    // ��ȡTCP�̶߳���ת���������仯֪ͨ...
    m_lpTCPThread->doRoomCommand(m_nRoomID, kCmd_UdpServer_DelTeacher);
    return;
  }
}

// һ����������ж��ѧ���ۿ���...
void CRoom::doTcpCreateStudent(CTCPClient * lpStudent)
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // ����ն����Ͳ���ѧ���ۿ��ˣ�ֱ�ӷ���...
  int nConnFD = lpStudent->GetConnFD();
  int nClientType = lpStudent->GetClientType();
  if( nClientType != kClientStudent )
    return;
  // ��ѧ���ۿ������µ��ۿ��б�...
  m_MapTCPStudent[nConnFD] = lpStudent;
  // ��ȡTCP�̶߳���ת���������仯֪ͨ...
  m_lpTCPThread->doRoomCommand(m_nRoomID, kCmd_UdpServer_AddStudent);
}

void CRoom::doTcpDeleteStudent(CTCPClient * lpStudent)
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // �ҵ���عۿ�ѧ���˶���ֱ��ɾ������...
  int nConnFD = lpStudent->GetConnFD();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nConnFD);
  if( itorItem != m_MapTCPStudent.end() ) {
    m_MapTCPStudent.erase(itorItem);
    // ��ȡTCP�̶߳���ת���������仯֪ͨ...
    m_lpTCPThread->doRoomCommand(m_nRoomID, kCmd_UdpServer_DelStudent);
    return;
  }
  // ���ͨ��FD��ʽû���ҵ���ͨ��ָ���������...
  itorItem = m_MapTCPStudent.begin();
  while(itorItem != m_MapTCPStudent.end()) {
    // �ҵ�����ؽڵ� => ɾ���ڵ㣬����...
    if(itorItem->second == lpStudent) {
      m_MapTCPStudent.erase(itorItem);
      // ��ȡTCP�̶߳���ת���������仯֪ͨ...
      m_lpTCPThread->doRoomCommand(m_nRoomID, kCmd_UdpServer_DelStudent);
      return;
    }
    // 2018.09.12 - by jackey => ��ɹ���������...
    // ���û���ҵ���ؽڵ� => ������һ��...
    ++itorItem;
  }
  // ͨ��ָ�����Ҳû���ҵ�����ӡ������Ϣ...
  log_trace("Can't find TCP-Student in CRoom, ConnFD: %d", nConnFD);
}

// ͨ������ͷ��Ų��Ҷ�Ӧ�������ն� => ÿ���ն˶�ֻ��һ��������...
CUDPClient * CRoom::doFindUdpPusher(int inDBCameraID)
{
  //////////////////////////////////////////////////////////////
  // ע�⣺���������������CApp���̵߳ĵ��ã����赥������...
  //////////////////////////////////////////////////////////////

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

void CRoom::doUdpCreateTeacher(CUDPClient * lpTeacher)
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // �����ն����ͽ��зַ�����...
  switch(lpTeacher->GetIdTag())
  {
    case ID_TAG_PUSHER: this->doUdpCreateTeacherPusher(lpTeacher); break;
    case ID_TAG_LOOKER: this->doUdpCreateTeacherLooker(lpTeacher); break;
  }
}

void CRoom::doUdpCreateTeacherPusher(CUDPClient * lpTeacher)
{
  // ���������Ľ�ʦ��������Ч...
  if (m_lpTCPTeacher == NULL)
    return;
  // �����ʦ���������Sock��UDP���Sock��һ��...
  int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  int nDstSockFD = lpTeacher->GetTCPSockID();
  if (nSrcSockFD != nDstSockFD)
    return;
  // ��ȡ���뽲ʦ��UDP������ر���...
  int nDBCameraID = lpTeacher->GetDBCameraID();
  int nHostPort = lpTeacher->GetHostPort();
  uint8_t idTag = lpTeacher->GetIdTag();
  // ������ǽ�ʦ�������߶���...
  if (idTag != ID_TAG_PUSHER)
    return;
  // ����µĽ�ʦ����������ԭ�ж�����ͬ(���ܶ�η�����) => ���߷���������TCP����ѧ���ˣ����Դ��������߳���...
  if( m_lpTCPTeacher->GetUdpPusher() != lpTeacher ) {
    //GetApp()->doUDPTeacherPusherOnLine(m_nRoomID, true);
  }
  // ��UDP�����ն˱��浽��Ӧ��TCP�ն�����...
  m_lpTCPTeacher->doUdpCreatePusher(lpTeacher);
}

void CRoom::doUdpCreateTeacherLooker(CUDPClient * lpTeacher)
{
  // ���������Ľ�ʦ��������Ч...
  if (m_lpTCPTeacher == NULL)
    return;
  // �����ʦ���������Sock��UDP���Sock��һ��...
  int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  int nDstSockFD = lpTeacher->GetTCPSockID();
  if (nSrcSockFD != nDstSockFD)
    return;
  // ��ȡ���뽲ʦ��UDP������ر���...
  int nDBCameraID = lpTeacher->GetDBCameraID();
  int nHostPort = lpTeacher->GetHostPort();
  uint8_t idTag = lpTeacher->GetIdTag();
  // ������ֻ����ʦ�ۿ��ߵ����...
  if (idTag != ID_TAG_LOOKER)
    return;
  // ���������ߣ��ѹۿ��߱��浽�����ߵ���...
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nDBCameraID);
  if (lpUdpPusher == NULL)
    return;
  // ���ѧ�������߲�Ϊ�գ���Ҫ������̽��...
  //lpUdpPusher->SetCanDetect(true);
  // ����ǰ�ۿ��߱��浽�����ߵĹۿ����ϵ���...
  lpUdpPusher->doAddUdpLooker(lpTeacher);
}

void CRoom::doUdpCreateStudent(CUDPClient * lpStudent)
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  switch(lpStudent->GetIdTag())
  {
    case ID_TAG_PUSHER: this->doUdpCreateStudentPusher(lpStudent); break;
    case ID_TAG_LOOKER: this->doUdpCreateStudentLooker(lpStudent); break;
  }
}

void CRoom::doUdpCreateStudentPusher(CUDPClient * lpStudent)
{
  // ����UDPѧ���˶����Ӧ��TCPѧ���˶���...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // ��ȡUDPѧ������صı�������...
  uint8_t idTag = lpStudent->GetIdTag();
  int nDBCameraID = lpStudent->GetDBCameraID();
  CTCPClient * lpTcpStudent = itorItem->second;
  // �������ѧ���������߶���...
  if (idTag != ID_TAG_PUSHER)
    return;
  // ����µ�ѧ������������ԭ�ж�����ͬ(���ܶ�η�����) => ���߷���������TCP�նˣ����Դ��������߳���...
  if (lpTcpStudent->GetUdpPusher() != lpStudent) {
    //GetApp()->doUDPStudentPusherOnLine(m_nRoomID, nDBCameraID, true);
  }
  // ��UDP�����ն˱��浽��Ӧ��TCP�ն�����...
  lpTcpStudent->doUdpCreatePusher(lpStudent);
}

void CRoom::doUdpCreateStudentLooker(CUDPClient * lpStudent)
{
  // ����UDPѧ���˶����Ӧ��TCPѧ���˶���...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // ��ȡUDPѧ������صı�������...
  uint8_t idTag = lpStudent->GetIdTag();
  int nDBCameraID = lpStudent->GetDBCameraID();
  // �������ѧ���˹ۿ��߶���...
  if (idTag != ID_TAG_LOOKER)
    return;
  // ���������ߣ��ѹۿ��߱��浽�����ߵ���...
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nDBCameraID);
  if (lpUdpPusher == NULL)
    return;
  // ��������߲�Ϊ�գ���Ҫ������̽��...
  // ����ǰ�ۿ��߱��浽�����ߵĹۿ����ϵ���...
  lpUdpPusher->doAddUdpLooker(lpStudent);
}

void CRoom::doUdpDeleteTeacher(CUDPClient * lpTeacher)
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  switch(lpTeacher->GetIdTag())
  {
    case ID_TAG_PUSHER: this->doUdpDeleteTeacherPusher(lpTeacher); break;
    case ID_TAG_LOOKER: this->doUdpDeleteTeacherLooker(lpTeacher); break;
  }
}

void CRoom::doUdpDeleteTeacherPusher(CUDPClient * lpTeacher)
{
  // ���������Ľ�ʦ��������Ч...
  if (m_lpTCPTeacher == NULL)
    return;
  // �����ʦ���������Sock��UDP���Sock��һ��...
  int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  int nDstSockFD = lpTeacher->GetTCPSockID();
  if ( nSrcSockFD != nDstSockFD )
    return;
  // ��ȡ���뽲ʦ��UDP������ر���...
  int nDBCameraID = lpTeacher->GetDBCameraID();
  int nHostPort = lpTeacher->GetHostPort();
  uint8_t idTag = lpTeacher->GetIdTag();
  // ���������ʦ�����߶���...
  if (idTag != ID_TAG_PUSHER)
    return;
  // ��������TCP����ѧ���ˣ�����ɾ�������߳���
  if(m_lpTCPTeacher->GetUdpPusher() == lpTeacher) {
    //GetApp()->doUDPTeacherPusherOnLine(m_nRoomID, false);
  }
  // ��UDP�����ն˴Ӷ�Ӧ��TCP�ն�����ɾ��֮...
  m_lpTCPTeacher->doUdpDeletePusher(lpTeacher);
}

void CRoom::doUdpDeleteTeacherLooker(CUDPClient * lpTeacher)
{
  // ���������Ľ�ʦ��������Ч...
  if (m_lpTCPTeacher == NULL)
    return;
  // �����ʦ���������Sock��UDP���Sock��һ��...
  int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  int nDstSockFD = lpTeacher->GetTCPSockID();
  if ( nSrcSockFD != nDstSockFD )
    return;
  // ��ȡ���뽲ʦ��UDP������ر���...
  int nDBCameraID = lpTeacher->GetDBCameraID();
  int nHostPort = lpTeacher->GetHostPort();
  uint8_t idTag = lpTeacher->GetIdTag();
  // ������ǽ�ʦ�ۿ��߶���...
  if (idTag != ID_TAG_LOOKER)
    return;
  // �������ʦ�ۿ��ˣ�֪ͨѧ��������ֹͣ�������ÿգ�����...
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nDBCameraID);
  if (lpUdpPusher == NULL)
    return;
  // ���ѧ�������߲�Ϊ�գ���Ҫ�ر�����̽��...
  //lpUdpPusher->SetCanDetect(false); }
  // ����ǰ�ۿ��ߴ������ߵĹۿ����ϵ���ɾ��...
  lpUdpPusher->doDelUdpLooker(lpTeacher);
}

void CRoom::doUdpDeleteStudent(CUDPClient * lpStudent)
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  switch(lpStudent->GetIdTag())
  {
    case ID_TAG_PUSHER: this->doUdpDeleteStudentPusher(lpStudent); break;
    case ID_TAG_LOOKER: this->doUdpDeleteStudentLooker(lpStudent); break;
  }
}

void CRoom::doUdpDeleteStudentPusher(CUDPClient * lpStudent)
{
  // ����UDPѧ���˶����Ӧ��TCPѧ���˶���...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // ��ȡUDPѧ������صı�������...
  uint8_t idTag = lpStudent->GetIdTag();
  int nDBCameraID = lpStudent->GetDBCameraID();
  CTCPClient * lpTcpStudent = itorItem->second;
  // �������ѧ�������߶���...
  if( idTag != ID_TAG_PUSHER )
    return;
  // �����ѧ�������˷����ɾ����֪ͨ��ʦ�ˣ�����ɾ�������߳���...
  //GetApp()->doUDPStudentPusherOnLine(m_nRoomID, nDBCameraID, false);
  // ��UDP�����ն˴Ӷ�Ӧ��TCP�ն�����ɾ��֮...
  lpTcpStudent->doUdpDeletePusher(lpStudent);
}

void CRoom::doUdpDeleteStudentLooker(CUDPClient * lpStudent)
{
  // ����UDPѧ���˶����Ӧ��TCPѧ���˶���...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // ��ȡUDPѧ������صı�������...
  uint8_t idTag = lpStudent->GetIdTag();
  int nDBCameraID = lpStudent->GetDBCameraID();
  // �������ѧ���ۿ��߶���...
  if (idTag != ID_TAG_LOOKER)
    return;
  // �����ѧ���ۿ��߷����ɾ�����ҵ���Ӧ��������.....
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nDBCameraID);
  if (lpUdpPusher == NULL)
    return;
  // ����ǰ�ۿ��ߴ������ߵĹۿ����ϵ���ɾ��...
  lpUdpPusher->doDelUdpLooker(lpStudent);
}
