
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

CCamera * CRoom::doCreateCamera(CTCPClient * lpTCPClient, int inDBCameraID, string & inPCName, string & inCameraName)
{
  // ����ҵ�������ͷ���󣬸�������...
  CCamera * lpCamera = NULL;
  GM_MapCamera::iterator itorItem = m_MapCamera.find(inDBCameraID);
  if( itorItem != m_MapCamera.end() ) {
    lpCamera = itorItem->second;
    lpCamera->m_strPCName = inPCName;
    lpCamera->m_strCameraName = inCameraName;
    lpCamera->m_lpTCPClient = lpTCPClient;
    return lpCamera;
  }
  // ���û���ҵ�����ͷ���󣬴���һ���µĶ���...
  lpCamera = new CCamera(lpTCPClient, m_nRoomID, inDBCameraID, inPCName, inCameraName);
  m_MapCamera[inDBCameraID] = lpCamera;
  return lpCamera;
}

// ɾ������ͷ => ѧ���˷����ֹͣ����...
void CRoom::doDeleteCamera(int inDBCameraID)
{
  // ����ҵ�������ͷ����ֱ��ɾ��֮...
  GM_MapCamera::iterator itorItem = m_MapCamera.find(inDBCameraID);
  if (itorItem != m_MapCamera.end()) {
    delete itorItem->second;
    m_MapCamera.erase(itorItem);
  }
}

void CRoom::doDumpRoomInfo()
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  string strInfo; char szBuffer[256] = { 0 };
  // ��ӡ�����ｲʦ�������߸���������������Ĺۿ��߸���...
  CUDPClient * lpUdpTeacherPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  int nTcpSockID   = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetConnFD() : 0);
  int nLiveID = ((lpUdpTeacherPusher != NULL) ? lpUdpTeacherPusher->GetLiveID() : 0);
  int nLookerCount = ((lpUdpTeacherPusher != NULL) ? lpUdpTeacherPusher->GetLookerCount() : 0);
  sprintf(szBuffer, "TCP-Teacher-ID: %d, UDPPusher-LiveID: %d, Looker-Count: %d\n", nTcpSockID, nLiveID, nLookerCount);
  strInfo.append(szBuffer);
  GM_MapTCPConn::iterator itorItem;
  // ��ӡ������ѧ���������߸���������������Ĺۿ��߸���...
  for(itorItem = m_MapTCPStudent.begin(); itorItem != m_MapTCPStudent.end(); ++itorItem) {
    CTCPClient * lpTcpClient = itorItem->second;
    CUDPClient * lpUdpPusher = lpTcpClient->GetUdpPusher();
    nLiveID = ((lpUdpPusher != NULL) ? lpUdpPusher->GetLiveID() : 0);
    nLookerCount = ((lpUdpPusher != NULL) ? lpUdpPusher->GetLookerCount() : 0);
    sprintf(szBuffer, "TCP-Student-ID: %d, UDPPusher-LiveID: %d, Looker-Count: %d\n", itorItem->first, nLiveID, nLookerCount);
    strInfo.append(szBuffer);
  }
  // ��ӡ���������ɺ�ķ���������Ϣ => ��Ͻ�ʦ�ն˺�ѧ���ն˵���Ϣ...
  log_trace("\n======== RoomID: %d ========\n%s", m_nRoomID, strInfo.c_str());
}

void CRoom::doUdpLogoutToTcp(int nTCPSockFD, int nLiveID, uint8_t tmTag, uint8_t idTag)
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  if (m_lpTCPThread == NULL) return;
  m_lpTCPThread->doUdpLogoutToTcp(nTCPSockFD, nLiveID, tmTag, idTag);
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

int CRoom::GetUdpTeacherLiveID()
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  CUDPClient * lpUdpPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  return ((lpUdpPusher != NULL) ? lpUdpPusher->GetLiveID() : 0);
}

/*bool CRoom::IsTcpTeacherClientOnLine()
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
    // ��������TCP����ѧ���ˣ�����ɾ�������߳���
    CUDPClient * lpUdpPusher = lpTeacher->GetUdpPusher();
    int nLiveID = ((lpUdpPusher != NULL) ? lpUdpPusher->GetLiveID() : 0);
    if( lpUdpPusher != NULL && nLiveID > 0 ) {
      this->doUdpLiveOnLine(m_lpTCPTeacher, nLiveID, false);
    }
    // ��ȡTCP�̶߳���ת���������仯֪ͨ...
    m_lpTCPThread->doRoomCommand(m_nRoomID, kCmd_UdpServer_DelTeacher);
    // ���ý�ʦ�����Ӷ���...
    m_lpTCPTeacher = NULL;
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
  // ���������������ͷ�б�ͨ��ָ�����ƥ��...
  CCamera * lpCamera = NULL;
  GM_MapCamera::iterator itorCamera = m_MapCamera.begin();
  while(itorCamera != m_MapCamera.end()) {
    lpCamera = itorCamera->second;
    // �������ͷ�Ķ����뵱ǰѧ���˵Ķ���һ�£�������һ��...
    if( lpCamera->m_lpTCPClient != lpStudent) {
      ++itorCamera; continue;
    }
    // �׽�����һ�µģ�ɾ������ͷ���б���ɾ��...
    delete lpCamera; lpCamera = NULL;
    m_MapCamera.erase(itorCamera++);
  }
  // FD��ʽ��һ����Ч��ͨ��ָ���������...
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.begin();
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
  log_trace("Can't find TCP-Student in CRoom, ConnFD: %d", lpStudent->GetConnFD());
}

// ͨ������ͷ��Ų��Ҷ�Ӧ�������ն� => ÿ���ն˶�ֻ��һ��������...
CUDPClient * CRoom::doFindUdpPusher(int inLiveID)
{
  //////////////////////////////////////////////////////////////
  // ע�⣺���������������CApp���̵߳ĵ��ã����赥������...
  //////////////////////////////////////////////////////////////

  // �Ȳ��ҽ�ʦ��������������Ƿ�����Ҫ�Ķ���...
  CUDPClient * lpUdpPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  int nCurLiveID = ((lpUdpPusher != NULL) ? lpUdpPusher->GetLiveID() : 0);
  if (nCurLiveID > 0 && nCurLiveID == inLiveID)
    return lpUdpPusher;
  // Ȼ���ٱ������е�ѧ����������������Ƿ�����Ҫ�Ķ���...
  GM_MapTCPConn::iterator itorItem;
  for(itorItem = m_MapTCPStudent.begin(); itorItem != m_MapTCPStudent.end(); ++itorItem) {
    CTCPClient * lpTcpClient = itorItem->second;
    lpUdpPusher = lpTcpClient->GetUdpPusher();
    // ���ҵ�ǰѧ���˶�������������߶�Ӧ������ͷ���...
    nCurLiveID = ((lpUdpPusher != NULL) ? lpUdpPusher->GetLiveID() : 0);
    if (nCurLiveID > 0 && nCurLiveID == inLiveID)
      return lpUdpPusher;
  }
  // ��û���ҵ������ؿ�...
  return NULL;
}

void CRoom::doUdpHeaderSmart(CUDPClient * lpUdpSmart)
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  // ֻ�������߲Żᴦ������ͷ����...
  if (lpUdpSmart->GetIdTag() != ID_TAG_PUSHER)
    return;
  // ���ݲ�ͬ�ն˽��в�ͬ����...
  switch(lpUdpSmart->GetTmTag()) {
    case TM_TAG_TEACHER: this->doUdpHeaderTeacherPusher(lpUdpSmart); break;
    case TM_TAG_STUDENT: this->doUdpHeaderStudentPusher(lpUdpSmart); break;
  }
}

void CRoom::doUdpHeaderTeacherPusher(CUDPClient * lpTeacher)
{
  // ���������Ľ�ʦ��������Ч...
  if (m_lpTCPTeacher == NULL || lpTeacher == NULL)
    return;
  // ����µĽ�ʦ����������ԭ�ж�����ͬ(���ܶ�η�����) => ���߷���������TCP����ѧ���ˣ����Դ��������߳���...
  if( m_lpTCPTeacher->GetUdpPusher() != lpTeacher ) {
    this->doUdpLiveOnLine(m_lpTCPTeacher, lpTeacher->GetLiveID(), true);
  }
  // ��UDP�����ն˱��浽��Ӧ��TCP�ն�����...
  m_lpTCPTeacher->doUdpCreatePusher(lpTeacher);
}

void CRoom::doUdpHeaderStudentPusher(CUDPClient * lpStudent)
{
  // ����UDPѧ���˶����Ӧ��TCPѧ���˶���...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // ��ȡUDPѧ������صı�������...
  CTCPClient * lpTcpStudent = itorItem->second;
  // ����µ�ѧ������������ԭ�ж�����ͬ(���ܶ�η�����) => ���߷���������TCP�նˣ����Դ��������߳���...
  if (lpTcpStudent->GetUdpPusher() != lpStudent) {
    this->doUdpLiveOnLine(lpTcpStudent, lpStudent->GetLiveID(), true);
  }
  // ��UDP�����ն˱��浽��Ӧ��TCP�ն�����...
  lpTcpStudent->doUdpCreatePusher(lpStudent);
}

void CRoom::doUdpCreateSmart(CUDPClient * lpUdpSmart)
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  if (lpUdpSmart->GetTmTag() == TM_TAG_TEACHER) {
    // ���ݽ�ʦ�˲�ͬ�����ͽ��зַ�����...
    switch(lpUdpSmart->GetIdTag()) {
      case ID_TAG_PUSHER: this->doUdpCreateTeacherPusher(lpUdpSmart); break;
      case ID_TAG_LOOKER: this->doUdpCreateTeacherLooker(lpUdpSmart); break;
    }
  } else if (lpUdpSmart->GetTmTag() == TM_TAG_STUDENT) {
    // ����ѧ���˲�ͬ�����ͽ��зַ�����...
    switch(lpUdpSmart->GetIdTag()) {
      case ID_TAG_PUSHER: this->doUdpCreateStudentPusher(lpUdpSmart); break;
      case ID_TAG_LOOKER: this->doUdpCreateStudentLooker(lpUdpSmart); break;
    }
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
  // ע�⣺��������Ѿ��ŵ����ݿ⵱����...
  // �����ǰ��ʦ�����ߵ����������Ч������֮...
  //if (lpTeacher->m_rtp_create.liveID <= 0) {
  //  lpTeacher->m_rtp_create.liveID = ++m_nMaxLiveID;
  //}
  // ע�⣺���������﷢������֪ͨ����Ϊ�ظ��������˵�tagCreate������ܶ�ʧ�����LiveID�ظ�����...
  // ����µĽ�ʦ����������ԭ�ж�����ͬ(���ܶ�η�����) => ���߷���������TCP����ѧ���ˣ����Դ��������߳���...
  /*if( m_lpTCPTeacher->GetUdpPusher() != lpTeacher ) {
    this->doUdpLiveOnLine(m_lpTCPTeacher, lpTeacher->GetLiveID(), true);
  }
  // ��UDP�����ն˱��浽��Ӧ��TCP�ն�����...
  m_lpTCPTeacher->doUdpCreatePusher(lpTeacher);*/
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
  int nLiveID = lpTeacher->GetLiveID();
  // ���������ߣ��ѹۿ��߱��浽�����ߵ���...
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nLiveID);
  if (lpUdpPusher == NULL)
    return;
  // Ŀǰѧ���˻�һֱ��������������ͼ���...
  // ���ѧ�������߲�Ϊ�գ���Ҫ������̽��...
  lpUdpPusher->SetCanDetect(true);
  // ����ǰ�ۿ��߱��浽�����ߵĹۿ����ϵ���...
  lpUdpPusher->doAddUdpLooker(lpTeacher);
}

void CRoom::doUdpCreateStudentPusher(CUDPClient * lpStudent)
{
  // ����UDPѧ���˶����Ӧ��TCPѧ���˶���...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // ע�⣺��������Ѿ��ŵ����ݿ⵱����...
  // �����ǰѧ�������ߵ����������Ч������֮...
  //if (lpStudent->m_rtp_create.liveID <= 0) {
  //  lpStudent->m_rtp_create.liveID = ++m_nMaxLiveID;
  //}
  // ע�⣺���������﷢������֪ͨ����Ϊ�ظ��������˵�tagCreate������ܶ�ʧ�����LiveID�ظ�����...
  // ����µ�ѧ������������ԭ�ж�����ͬ(���ܶ�η�����) => ���߷���������TCP�նˣ����Դ��������߳���...
  /*CTCPClient * lpTcpStudent = itorItem->second;
  if (lpTcpStudent->GetUdpPusher() != lpStudent) {
    this->doUdpLiveOnLine(lpTcpStudent, lpStudent->GetLiveID(), true);
  }
  // ��UDP�����ն˱��浽��Ӧ��TCP�ն�����...
  lpTcpStudent->doUdpCreatePusher(lpStudent);*/
}

void CRoom::doUdpCreateStudentLooker(CUDPClient * lpStudent)
{
  // ����UDPѧ���˶����Ӧ��TCPѧ���˶���...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  // ��ȡUDPѧ������صı�������...
  int nLiveID = lpStudent->GetLiveID();
  // ���������ߣ��ѹۿ��߱��浽�����ߵ���...
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nLiveID);
  if (lpUdpPusher == NULL)
    return;
  // ��������߲�Ϊ�գ���Ҫ������̽��...
  // ����ǰ�ۿ��߱��浽�����ߵĹۿ����ϵ���...
  lpUdpPusher->doAddUdpLooker(lpStudent);
}

void CRoom::doUdpDeleteSmart(CUDPClient * lpUdpSmart)
{
  // ���ù��캯��|�����������л��Ᵽ��...
  OSMutexLocker theLocker(&m_tcp_mutex);
  if (lpUdpSmart->GetTmTag() == TM_TAG_TEACHER) {
    // ���ݽ�ʦ�˲�ͬ�����ͽ��зַ�����...
    switch(lpUdpSmart->GetIdTag()) {
      case ID_TAG_PUSHER: this->doUdpDeleteTeacherPusher(lpUdpSmart); break;
      case ID_TAG_LOOKER: this->doUdpDeleteTeacherLooker(lpUdpSmart); break;
    }
  } else if (lpUdpSmart->GetTmTag() == TM_TAG_STUDENT) {
    // ����ѧ���˲�ͬ�����ͽ��зַ�����...
    switch(lpUdpSmart->GetIdTag()) {
      case ID_TAG_PUSHER: this->doUdpDeleteStudentPusher(lpUdpSmart); break;
      case ID_TAG_LOOKER: this->doUdpDeleteStudentLooker(lpUdpSmart); break;
    }
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
  // ��������TCP����ѧ���ˣ�����ɾ�������߳���
  if(m_lpTCPTeacher->GetUdpPusher() == lpTeacher) {
    this->doUdpLiveOnLine(m_lpTCPTeacher, lpTeacher->GetLiveID(), false);
  }
  // ��UDP�����ն˴Ӷ�Ӧ��TCP�ն�����ɾ��֮...
  m_lpTCPTeacher->doUdpDeletePusher(lpTeacher);
}

void CRoom::doUdpDeleteTeacherLooker(CUDPClient * lpTeacher)
{
  //////////////////////////////////////////////////////////////////
  // ע�⣺�����ж���ô��ȷ�����򣬽�ʦ��ֱ���˳����޷�ɾ��...
  //////////////////////////////////////////////////////////////////
  // ���������Ľ�ʦ��������Ч...
  //if (m_lpTCPTeacher == NULL)
  //  return;
  // �����ʦ���������Sock��UDP���Sock��һ��...
  //int nSrcSockFD = m_lpTCPTeacher->GetConnFD();
  //int nDstSockFD = lpTeacher->GetTCPSockID();
  //if ( nSrcSockFD != nDstSockFD )
  //  return;

  // ��ȡ���뽲ʦ��UDP������ر���...
  int nLiveID = lpTeacher->GetLiveID();
  // �������ʦ�ۿ��ˣ�֪ͨѧ��������ֹͣ�������ÿգ�����...
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nLiveID);
  if (lpUdpPusher == NULL)
    return;
  // Ŀǰѧ���˻�һֱ��������������ͼ���...
  // ���ѧ�������߲�Ϊ�գ���Ҫ�ر�����̽��...
  lpUdpPusher->SetCanDetect(false);
  // ����ǰ�ۿ��ߴ������ߵĹۿ����ϵ���ɾ��...
  lpUdpPusher->doDelUdpLooker(lpTeacher);
}

void CRoom::doUdpDeleteStudentPusher(CUDPClient * lpStudent)
{
  // ����UDPѧ���˶����Ӧ��TCPѧ���˶���...
  int nTcpSockFD = lpStudent->GetTCPSockID();
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nTcpSockFD);
  if (itorItem == m_MapTCPStudent.end())
    return;
  CTCPClient * lpTcpStudent = itorItem->second;
  // �����ѧ�������˷����ɾ����֪ͨ���������նˣ�����ɾ�������߳���...
  if (lpTcpStudent->GetUdpPusher() == lpStudent) {
    this->doUdpLiveOnLine(lpTcpStudent, lpStudent->GetLiveID(), false);
  }
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
  int nLiveID = lpStudent->GetLiveID();
  // �����ѧ���ۿ��߷����ɾ�����ҵ���Ӧ��������.....
  CUDPClient * lpUdpPusher = this->doFindUdpPusher(nLiveID);
  if (lpUdpPusher == NULL)
    return;
  // ����ǰ�ۿ��ߴ������ߵĹۿ����ϵ���ɾ��...
  lpUdpPusher->doDelUdpLooker(lpStudent);
}

void CRoom::doUdpLiveOnLine(CTCPClient * lpTcpExclude, int inLiveID, bool bIsOnLineFlag)
{
  // �����ʦ����Ч�����Ҳ�����ʦ���Լ�����������֪ͨ��ʦ�˿�����ȡ��ɾ��ָ����ŵ���������...
  if( m_lpTCPTeacher != NULL && lpTcpExclude != m_lpTCPTeacher ) {
    m_lpTCPTeacher->doUdpLiveOnLine(inLiveID, bIsOnLineFlag);
  }
  // �����ѧ����������ֻ֪ͨ��ʦ�ˣ���Ҫ֪ͨ�����������ѧ����...
  if( lpTcpExclude->GetClientType() == kClientStudent)
    return;
  // �������������е�ѧ���˶���...
  GM_MapTCPConn::iterator itorItem;
  for(itorItem = m_MapTCPStudent.begin(); itorItem != m_MapTCPStudent.end(); ++itorItem) {
    CTCPClient * lpTcpStudent = itorItem->second;
    // ���ѧ���˶����뵱ǰ��������һ�£�������һ��...
    if (lpTcpStudent == lpTcpExclude) continue;
    // ֪ͨ���ѧ���˶��󣬿�����ȡ��ɾ��ָ����ŵ���������...
    lpTcpStudent->doUdpLiveOnLine(inLiveID, bIsOnLineFlag);
  }  
}
