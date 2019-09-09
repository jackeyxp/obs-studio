
#include "app.h"
#include "room.h"
#include "tcpclient.h"
#include "udpclient.h"

CRoom::CRoom(int inRoomID)
  : m_nRoomID(inRoomID)
  , m_lpTCPTeacher(NULL)
{
  assert(m_nRoomID > 0);
}

CRoom::~CRoom()
{
}

void CRoom::doDumpRoomInfo()
{
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
  return m_lpTCPTeacher ? 1 : 0;
}

int CRoom::GetTcpStudentCount()
{
  return m_MapTCPStudent.size();
}

int CRoom::GetTcpTeacherDBFlowID()
{
  return ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetDBFlowID() : 0);
}

bool CRoom::IsTcpTeacherClientOnLine()
{
  return ((m_lpTCPTeacher != NULL) ? true : false);
}

bool CRoom::IsUdpTeacherPusherOnLine()
{

  CUDPClient * lpUdpPusher = ((m_lpTCPTeacher != NULL) ? m_lpTCPTeacher->GetUdpPusher() : NULL);
  return ((lpUdpPusher != NULL) ? true : false);
}

void CRoom::doTcpCreateTeacher(CTCPClient * lpTeacher)
{
  int nClientType = lpTeacher->GetClientType();
  // ����ն����Ͳ��ǽ�ʦ�ˣ�ֱ�ӷ���...
  if( nClientType != kClientTeacher )
    return;
  // ע�⣺����ֻ�е���ʦ��Ϊ��ʱ���ŷ���֪ͨ�����򣬻���ɼ��������ӣ���ɺ�����ʦ���޷���½������...
  // ֻ�е���ʦ�˶���Ϊ��ʱ����ת���������仯֪ͨ...
  if( m_lpTCPTeacher == NULL ) {
    GetApp()->doTcpRoomCommand(m_nRoomID, kCmd_UdpServer_AddTeacher);
  }
  // ���½�ʦ�����Ӷ���...
  m_lpTCPTeacher = lpTeacher;
}

void CRoom::doTcpDeleteTeacher(CTCPClient * lpTeacher)
{
  // ����Ƿ��������ʦ�ˣ��ÿգ�����...
  if( m_lpTCPTeacher == lpTeacher ) {
    m_lpTCPTeacher = NULL;
    // ��ȡTCP�̶߳���ת���������仯֪ͨ...
    GetApp()->doTcpRoomCommand(kCmd_UdpServer_DelTeacher, m_nRoomID);
    return;
  }
}

// һ����������ж��ѧ���ۿ���...
void CRoom::doTcpCreateStudent(CTCPClient * lpStudent)
{
  int nConnFD = lpStudent->GetConnFD();
  int nClientType = lpStudent->GetClientType();
  // ����ն����Ͳ���ѧ���ۿ��ˣ�ֱ�ӷ���...
  if( nClientType != kClientStudent )
    return;
  // ��ѧ���ۿ������µ��ۿ��б�...
  m_MapTCPStudent[nConnFD] = lpStudent;
  // ��ȡTCP�̶߳���ת���������仯֪ͨ...
  GetApp()->doTcpRoomCommand(m_nRoomID, kCmd_UdpServer_AddStudent);
}

void CRoom::doTcpDeleteStudent(CTCPClient * lpStudent)
{
  int nConnFD = lpStudent->GetConnFD();
  // �ҵ���عۿ�ѧ���˶���ֱ��ɾ������...
  GM_MapTCPConn::iterator itorItem = m_MapTCPStudent.find(nConnFD);
  if( itorItem != m_MapTCPStudent.end() ) {
    m_MapTCPStudent.erase(itorItem);
    // ��ȡTCP�̶߳���ת���������仯֪ͨ...
    GetApp()->doTcpRoomCommand(m_nRoomID, kCmd_UdpServer_DelStudent);
    return;
  }
  // ���ͨ��FD��ʽû���ҵ���ͨ��ָ���������...
  itorItem = m_MapTCPStudent.begin();
  while(itorItem != m_MapTCPStudent.end()) {
    // �ҵ�����ؽڵ� => ɾ���ڵ㣬����...
    if(itorItem->second == lpStudent) {
      m_MapTCPStudent.erase(itorItem);
      // ��ȡTCP�̶߳���ת���������仯֪ͨ...
      GetApp()->doTcpRoomCommand(m_nRoomID, kCmd_UdpServer_DelStudent);
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
  // ����ǽ�ʦ�������߶���...
  if (idTag == ID_TAG_PUSHER) {
    // ����µĽ�ʦ����������ԭ�ж�����ͬ(���ܶ�η�����) => ���߷���������TCP����ѧ���ˣ����Դ��������߳���...
    if( m_lpTCPTeacher->GetUdpPusher() != lpTeacher ) {
      //GetApp()->doUDPTeacherPusherOnLine(m_nRoomID, true);
    }
    // ��UDP�����ն˱��浽��Ӧ��TCP�ն�����...
    m_lpTCPTeacher->doUdpCreatePusher(lpTeacher);
  } else if (idTag == ID_TAG_LOOKER) {
    // ���������ߣ��ѹۿ��߱��浽�����ߵ���...
    CUDPClient * lpUdpPusher = this->doFindUdpPusher(nDBCameraID);
    // ���ѧ�������߲�Ϊ�գ���Ҫ������̽��...
    //if(lpUdpPusher != NULL) { lpUdpPusher->SetCanDetect(true); }
    // ����ǰ�ۿ��߱��浽�����ߵĹۿ����ϵ���...
    if (lpUdpPusher != NULL) {
      lpUdpPusher->doAddUdpLooker(lpTeacher);
    }
  }
}

void CRoom::doUdpCreateStudent(CUDPClient * lpStudent)
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
  // �����ѧ���������߶���...
  if (idTag == ID_TAG_PUSHER) {
    // ����µ�ѧ������������ԭ�ж�����ͬ(���ܶ�η�����) => ���߷���������TCP�նˣ����Դ��������߳���...
    if (lpTcpStudent->GetUdpPusher() != lpStudent) {
      //GetApp()->doUDPStudentPusherOnLine(m_nRoomID, nDBCameraID, true);
    }
    // ��UDP�����ն˱��浽��Ӧ��TCP�ն�����...
    lpTcpStudent->doUdpCreatePusher(lpStudent);
  } else if (idTag == ID_TAG_LOOKER) {
    // ���������ߣ��ѹۿ��߱��浽�����ߵ���...
    CUDPClient * lpUdpPusher = this->doFindUdpPusher(nDBCameraID);
    // ��������߲�Ϊ�գ���Ҫ������̽��...
    // ����ǰ�ۿ��߱��浽�����ߵĹۿ����ϵ���...
    if (lpUdpPusher != NULL) {
      lpUdpPusher->doAddUdpLooker(lpStudent);
    }
  }
}

void CRoom::doUdpDeleteTeacher(CUDPClient * lpTeacher)
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
  // �������ʦ������ => ��������TCP����ѧ���ˣ�����ɾ�������߳���
  if (idTag == ID_TAG_PUSHER) {
    if(m_lpTCPTeacher->GetUdpPusher() == lpTeacher) {
      //GetApp()->doUDPTeacherPusherOnLine(m_nRoomID, false);
    }
    // ��UDP�����ն˴Ӷ�Ӧ��TCP�ն�����ɾ��֮...
    m_lpTCPTeacher->doUdpDeletePusher(lpTeacher);
  } else if (idTag == ID_TAG_LOOKER) {
    // �������ʦ�ۿ��ˣ�֪ͨѧ��������ֹͣ�������ÿգ�����...
    CUDPClient * lpUdpPusher = this->doFindUdpPusher(nDBCameraID);
    // ���ѧ�������߲�Ϊ�գ���Ҫ�ر�����̽��...
    //if(lpUdpPusher != NULL) { lpUdpPusher->SetCanDetect(false); }
    // ����ǰ�ۿ��ߴ������ߵĹۿ����ϵ���ɾ��...
    if (lpUdpPusher != NULL) {
      lpUdpPusher->doDelUdpLooker(lpTeacher);
    }
  }
}

void CRoom::doUdpDeleteStudent(CUDPClient * lpStudent)
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
  if( idTag == ID_TAG_PUSHER ) {
    // �����ѧ�������˷����ɾ����֪ͨ��ʦ�ˣ�����ɾ�������߳���...
    //GetApp()->doUDPStudentPusherOnLine(m_nRoomID, nDBCameraID, false);
    // ��UDP�����ն˴Ӷ�Ӧ��TCP�ն�����ɾ��֮...
    lpTcpStudent->doUdpDeletePusher(lpStudent);
  } else if( idTag == ID_TAG_LOOKER ) {
    // �����ѧ���ۿ��߷����ɾ�����ҵ���Ӧ��������.....
    CUDPClient * lpUdpPusher = this->doFindUdpPusher(nDBCameraID);
    // ����ǰ�ۿ��ߴ������ߵĹۿ����ϵ���ɾ��...
    if (lpUdpPusher != NULL) {
      lpUdpPusher->doDelUdpLooker(lpStudent);
    }
  }  
}
