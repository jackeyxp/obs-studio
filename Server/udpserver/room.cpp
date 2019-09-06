
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
  // ע�⣺����ֻ�е���ʦ��Ϊ��ʱ���ŷ���֪ͨ�����򣬻���ɼ��������ӣ���ɺ�����ʦ���޷���½������...
  // ֻ�е���ʦ�˶���Ϊ��ʱ����ת���������仯֪ͨ...
  if( m_lpTCPTeacher == NULL ) {
    GetApp()->doTCPRoomCommand(kCmd_UdpServer_AddTeacher, m_nRoomID);
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
  GetApp()->doTCPRoomCommand(kCmd_UdpServer_AddStudent, m_nRoomID);
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
  GetApp()->doTCPRoomCommand(kCmd_UdpServer_DelTeacher, m_nRoomID);
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
    GetApp()->doTCPRoomCommand(kCmd_UdpServer_DelStudent, m_nRoomID);
    return 0;
  }
  // ���ͨ��FD��ʽû���ҵ���ͨ��ָ���������...
  itorItem = m_MapTCPStudent.begin();
  while(itorItem != m_MapTCPStudent.end()) {
    // �ҵ�����ؽڵ� => ɾ���ڵ㣬����...
    if(itorItem->second == lpClient) {
      m_MapTCPStudent.erase(itorItem);
      // ��ȡTCP�̶߳���ת���������仯֪ͨ...
      GetApp()->doTCPRoomCommand(kCmd_UdpServer_DelStudent, m_nRoomID);
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

/*int CRoom::doUdpClientCreate(CUDPClient * lpClient)
{
  
}

int CRoom::doUdpClientDelete(CUDPClient * lpClient)
{
  
}*/