
#include "app.h"
#include "room.h"
#include "tcpclient.h"
#include "tcpthread.h"
#include "udpclient.h"
#include <json/json.h>

#define MAX_PATH_SIZE           260
#define MAX_LINE_SIZE     64 * 1024   // 2017.07.25 - by jackey => 避免录像任务命令，造成溢出...
#define CLIENT_TIME_OUT      1 * 60   // 客户端超时断开时间1分钟(汇报频率30秒)...

CTCPClient::CTCPClient(CTCPThread * lpTCPThread, int connfd, int nHostPort, string & strSinAddr)
  : m_nRoomID(0)
  , m_nDBFlowID(0)
  , m_lpRoom(NULL)
  , m_nClientType(0)
  , m_nConnFD(connfd)
  , m_lpUdpPusher(NULL)
  , m_nHostPort(nHostPort)
  , m_strSinAddr(strSinAddr)
  , m_lpTCPThread(lpTCPThread)
  , m_nRoleType(kRoleWanRecv)
{
  assert(m_nConnFD > 0 && m_strSinAddr.size() > 0 );
  assert(m_lpTCPThread != NULL);
  m_nStartTime = time(NULL);
  m_epoll_fd = m_lpTCPThread->GetEpollFD();
  m_lpTCPThread->doIncreaseClient(nHostPort, strSinAddr);
}

CTCPClient::~CTCPClient()
{
  // 打印终端退出信息...
  log_trace("TCPClient Delete: %s, From: %s:%d, Socket: %d", get_client_type(m_nClientType), 
            this->m_strSinAddr.c_str(), this->m_nHostPort, this->m_nConnFD);
  /*// 如果是屏幕端，从房间当中删除之...
  if( m_lpRoom != NULL && m_nClientType == kClientScreen ) {
    m_lpRoom->doTcpDeleteScreen(this);
  }*/
  // 如果是学生端，从房间当中删除之...
  if( m_lpRoom != NULL && m_nClientType == kClientStudent ) {
    m_lpRoom->doTcpDeleteStudent(this);
  }
  // 如果是讲师端，从房间当中删除之 => 重置房间流量统计和音频扩展统计...
  if( m_lpRoom != NULL && m_nClientType == kClientTeacher ) {
    //GetApp()->GetUdpThread()->ResetRoomExFlow(m_nRoomID);
    m_lpRoom->doTcpDeleteTeacher(this);
  }
  // 打印终端退出后剩余的链接数量...
  m_lpTCPThread->doDecreaseClient(this->m_nHostPort, this->m_strSinAddr);
}

// 从CRoom调用的更新当前长链接对应的UDP对象...
void CTCPClient::doUdpCreateClient(CUDPClient * lpClient)
{
  int nHostPort = lpClient->GetHostPort();
  uint8_t idTag = lpClient->GetIdTag();
  // 如果是推流者，更新推流对象 => 只有一个推流者...
  if (idTag == ID_TAG_PUSHER) {
    m_lpUdpPusher = lpClient;
  } else if (idTag == ID_TAG_LOOKER) {
    // 如果观看者，放入集合当中 => 有多个观看者...
    m_MapUdpLooker[nHostPort] = lpClient;
  }
}

// 从CRoom调用的更新当前长链接对应的UDP对象...
void CTCPClient::doUdpDeleteClient(CUDPClient * lpClient)
{
  int nHostPort = lpClient->GetHostPort();
  uint8_t idTag = lpClient->GetIdTag();
  // 如果是推流者，直接置空...
  if (idTag == ID_TAG_PUSHER) {
    m_lpUdpPusher = NULL;
  } else if (idTag == ID_TAG_LOOKER) {
    // 如果观看者，从集合中删除之...
    m_MapUdpLooker.erase(nHostPort);
    /*GM_MapUDPConn::iterator itorItem;
    itorItem = m_MapUdpLooker.find(nHostPort);
    if (itorItem != m_MapUdpLooker.end()) {
      m_MapUdpLooker.erase(itorItem);
    }*/
  }
}
//
// 发送网络数据 => 始终设置读事件...
int CTCPClient::ForWrite()
{
  // 如果没有需要发送的数据，直接返回...
  if( m_strSend.size() <= 0 )
    return 0;
  // 发送全部的数据包内容...
  assert( m_strSend.size() > 0 );
  int nWriteLen = write(m_nConnFD, m_strSend.c_str(), m_strSend.size());
  if( nWriteLen <= 0 ) {
    log_trace("TCPClient send command error(%s)", strerror(errno));
    return -1;
  }
  // 每次发送成功，必须清空发送缓存...
  m_strSend.clear();
  // 准备修改事件需要的数据 => 写事件之后，一定是读事件...
  struct epoll_event evClient = {0};
	evClient.data.fd = m_nConnFD;
  evClient.events = EPOLLIN | EPOLLET;
  // 重新修改事件，加入读取事件...
	if( epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, m_nConnFD, &evClient) < 0 ) {
    log_trace("TCPClient mod socket '%d' to epoll failed: %s", m_nConnFD, strerror(errno));
    return -1;
  }
  // 操作成功，返回0...
  return 0;
}
//
// 读取网络数据...
int CTCPClient::ForRead()
{
  // 直接读取网络数据...
	char bufRead[MAX_LINE_SIZE] = {0};
	int  nReadLen = read(m_nConnFD, bufRead, MAX_LINE_SIZE);
  // 读取失败，返回错误，让上层关闭...
  if( nReadLen == 0 ) {
    //log_trace("TCPClient: %s, ForRead: Close, Socket: %d", get_client_type(m_nClientType), this->m_nConnFD);
    return -1;
  }
  // 读取失败，返回错误，让上层关闭...
  if( nReadLen < 0 ) {
		log_trace("TCPClient: %s, read error(%s)", get_client_type(m_nClientType), strerror(errno));
    return -1;
  }
  // 读取数据有效，重置超时时间...
  this->ResetTimeout();
	// 追加读取数据并构造解析头指针...
	m_strRecv.append(bufRead, nReadLen);
	// 这里网络数据会发生粘滞现象，因此，需要循环执行...
	while( m_strRecv.size() > 0 ) {
		// 得到的数据长度不够，直接返回，等待新数据...
    int nCmdLength = sizeof(Cmd_Header);
    if( m_strRecv.size() < nCmdLength )
      return 0;
    // 得到数据的有效长度和指针...
    assert( m_strRecv.size() >= nCmdLength );
    Cmd_Header * lpCmdHeader = (Cmd_Header*)m_strRecv.c_str();
    const char * lpDataPtr = m_strRecv.c_str() + sizeof(Cmd_Header);
    int nDataSize = m_strRecv.size() - sizeof(Cmd_Header);
		// 已获取的数据长度不够，直接返回，等待新数据...
		if( nDataSize < lpCmdHeader->m_pkg_len )
			return 0;
    // 注意：两个命令累加在一起就会发生数据粘滞...
    // 注意：数据粘滞不处理，会造成json解析失败，当数据包有效时，才处理...
    // 注意：数据粘滞的现象，就是字符串的最后一个字符不是\0，造成json解析失败...
    // 方法：将包含命令数据包的请求，进行数据转移，始终保证有效的结束符号\0，这样json解析就不会错误；
    if( lpCmdHeader->m_pkg_len > 0 ) {
      memset(bufRead, 0, MAX_LINE_SIZE);
      memcpy(bufRead, lpDataPtr, lpCmdHeader->m_pkg_len);
      // json数据的头指针使用新的缓存指针...
      lpDataPtr = bufRead;
    }
    // 数据区有效，保存用户类型...
    m_nClientType = lpCmdHeader->m_type;
    assert( nDataSize >= lpCmdHeader->m_pkg_len );
    // 需要提前清空上次解析的结果...
    m_MapJson.clear();
    // 判断是否需要解析JSON数据包，解析错误，直接删除链接...
    int nResult = -1;
    if( lpCmdHeader->m_pkg_len > 0 && lpCmdHeader->m_cmd != kCmd_Screen_Packet) {
      nResult = this->parseJsonData(lpDataPtr, lpCmdHeader->m_pkg_len);
      if( nResult < 0 )
        return nResult;
      assert( nResult >= 0 );
    }
    // 打印调试信息到控制台，播放器类型，命令名称，IP地址端口，套接字...
    // 调试模式 => 只打印，不存盘到日志文件...
    log_trace("TCPClient Command(%s - %s, From: %s:%d, Socket: %d)", 
              get_client_type(m_nClientType), get_command_name(lpCmdHeader->m_cmd),
              this->m_strSinAddr.c_str(), this->m_nHostPort, this->m_nConnFD);
    // 对数据进行用户类型分发...
    switch( m_nClientType )
    {
      case kClientPHP:      nResult = this->doPHPClient(lpCmdHeader, lpDataPtr); break;
      case kClientStudent:  nResult = this->doStudentClient(lpCmdHeader, lpDataPtr); break;
      case kClientTeacher:  nResult = this->doTeacherClient(lpCmdHeader, lpDataPtr); break;
      case kClientScreen:   nResult = this->doScreenClient(lpCmdHeader, lpDataPtr); break;
    }
		// 删除已经处理完毕的数据 => Header + pkg_len...
		m_strRecv.erase(0, lpCmdHeader->m_pkg_len + sizeof(Cmd_Header));
    // 判断是否已经发生了错误...
    if( nResult < 0 )
      return nResult;
    // 如果没有错误，继续执行...
    assert( nResult >= 0 );
  }  
  return 0;
}

// 处理PHP客户端事件...
int CTCPClient::doPHPClient(Cmd_Header * lpHeader, const char * lpJsonPtr)
{
  int nResult = -1;
  /*switch(lpHeader->m_cmd)
  {
    case kCmd_PHP_GetRoomFlow:   nResult = this->doCmdPHPGetRoomFlow(); break;
  }*/
  // 默认全部返回正确...
  return 0;
}

/*// 处理获取指定房间里上行下行的流量统计...
int CTCPClient::doCmdPHPGetRoomFlow()
{
  // 准备反馈需要的变量...
  int nErrCode = ERR_OK;
  // 创建反馈的json数据包...
  json_object * new_obj = json_object_new_object();
  do {
    // 判断传递JSON数据有效性 => 必须包含room_id|flow_id字段信息...
    if(m_MapJson.find("room_id") == m_MapJson.end() || m_MapJson.find("flow_id") == m_MapJson.end()) {
      nErrCode = ERR_NO_PARAM;
      break;
    }
    // 解析已经获取的room_id|flow_id具体内容...
    int nDBRoomID = atoi(m_MapJson["room_id"].c_str());
    int nDBFlowID = atoi(m_MapJson["flow_id"].c_str());
    // 需要根据room_id查找tcp房间对象和udp房间对象...
    if(!m_lpTCPThread->doCheckFlowID(nDBRoomID, nDBFlowID)) {
      nErrCode = ERR_NO_PARAM;
      break;
    }
    // 验证通过之后，获取udp房间里的上行下行流量统计...
    int nUpFlowMB = 0; int nDownFlowMB = 0;
    CUDPThread * lpUdpThread = GetApp()->GetUdpThread();
    lpUdpThread->GetRoomFlow(nDBRoomID, nUpFlowMB, nDownFlowMB);
    json_object_object_add(new_obj, "up_flow", json_object_new_int(nUpFlowMB));
    json_object_object_add(new_obj, "down_flow", json_object_new_int(nDownFlowMB));
  } while( false );
  // 组合错误信息内容...
  json_object_object_add(new_obj, "err_code", json_object_new_int(nErrCode));
  json_object_object_add(new_obj, "err_cmd", json_object_new_int(kCmd_PHP_GetRoomFlow));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendPHPResponse(lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}*/

// PHP专用的反馈接口函数...
int CTCPClient::doSendPHPResponse(const char * lpJsonPtr, int nJsonSize)
{
  // PHP反馈需要的是TrackerHeader结构体...
  TrackerHeader theTracker = {0};
  char szSendBuf[MAX_LINE_SIZE] = {0};
  int nBodyLen = nJsonSize;
  // 组合TrackerHeader包头，方便php扩展使用 => 只需要设置pkg_len，其它置0...
  long2buff(nBodyLen, theTracker.pkg_len);
  memcpy(szSendBuf, &theTracker, sizeof(theTracker));
  memcpy(szSendBuf+sizeof(theTracker), lpJsonPtr, nBodyLen);
  // 将发送数据包缓存起来，等待发送事件到来...
  m_strSend.assign(szSendBuf, nBodyLen+sizeof(theTracker));
  // 向当前终端对象发起发送数据事件...
  epoll_event evClient = {0};
  evClient.data.fd = m_nConnFD;
  evClient.events = EPOLLOUT | EPOLLET;
  // 重新修改事件，加入写入事件...
  if( epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, m_nConnFD, &evClient) < 0 ) {
    log_trace("mod socket '%d' to epoll failed: %s", m_nConnFD, strerror(errno));
    return -1;
  }
  // 返回执行正确...
  return 0;
}

// 处理Student事件...
int CTCPClient::doStudentClient(Cmd_Header * lpHeader, const char * lpJsonPtr)
{
  int nResult = -1;
  switch(lpHeader->m_cmd)
  {
    case kCmd_Student_Login:      nResult = this->doCmdStudentLogin(); break;
    case kCmd_Student_OnLine:     nResult = this->doCmdStudentOnLine(); break;
  }
  // 默认全部返回正确...
  return 0;
}

// 处理Student登录事件...
int CTCPClient::doCmdStudentLogin()
{
  // 处理学生端登录过程 => 判断传递JSON数据有效性...
  if( m_MapJson.find("mac_addr") == m_MapJson.end() ||
    m_MapJson.find("role_type") == m_MapJson.end() ||
    m_MapJson.find("ip_addr") == m_MapJson.end() ||
    m_MapJson.find("room_id") == m_MapJson.end() ||
    m_MapJson.find("pc_name") == m_MapJson.end() ) {
    return -1;
  }
  // 保存解析到的有效JSON数据项...
  m_strMacAddr = m_MapJson["mac_addr"];
  m_strIPAddr  = m_MapJson["ip_addr"];
  m_strRoomID  = m_MapJson["room_id"];
  m_strPCName  = m_MapJson["pc_name"];
  m_nRoomID = atoi(m_strRoomID.c_str());
  m_nRoleType = (ROLE_TYPE)atoi(m_MapJson["role_type"].c_str());
  // 创建或更新房间，更新房间里的学生端...
  m_lpRoom = GetApp()->doCreateRoom(m_nRoomID);
  m_lpRoom->doTcpCreateStudent(this);
  // 当前房间里的TCP讲师端是否在线 和 UDP讲师端是否在线...
  CTCPClient * lpTCPTeacher = m_lpRoom->GetTcpTeacherClient();
  bool bIsTCPTeacherOnLine = ((lpTCPTeacher != NULL) ? true : false);
  CUDPClient * lpUdpPusher = ((lpTCPTeacher != NULL) ? lpTCPTeacher->GetUdpPusher() : NULL);
  bool bIsUDPTeacherOnLine = ((lpUdpPusher != NULL) ? true : false);
  int  nTeacherFlowID = ((lpTCPTeacher != NULL) ? lpTCPTeacher->GetDBFlowID() : 0);
  // 发送反馈命令信息给学生端长链接对象...
  return this->doSendCmdLoginForStudent(bIsTCPTeacherOnLine, bIsUDPTeacherOnLine, nTeacherFlowID);
}

int CTCPClient::doSendCmdLoginForStudent(bool bIsTCPOnLine, bool bIsUDPOnLine, int nTeacherFlowID)
{
  // 构造转发JSON数据块 => 返回套接字|TCP讲师|UDP讲师...
  json_object * new_obj = json_object_new_object();
  json_object_object_add(new_obj, "tcp_socket", json_object_new_int(m_nConnFD));
  json_object_object_add(new_obj, "tcp_teacher", json_object_new_int(bIsTCPOnLine));
  json_object_object_add(new_obj, "udp_teacher", json_object_new_int(bIsUDPOnLine));
  json_object_object_add(new_obj, "flow_teacher", json_object_new_int(nTeacherFlowID));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendCommonCmd(kCmd_Student_Login, lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

int CTCPClient::doCmdStudentOnLine()
{
  // 如果不是学生端对象，直接返回...
  if( m_nClientType != kClientStudent )
    return 0;
  // 当前房间里的TCP讲师端的流量编号...
  CTCPClient * lpTCPTeacher = ((m_lpRoom != NULL) ? m_lpRoom->GetTcpTeacherClient() : NULL);
  int nTeacherFlowID = ((lpTCPTeacher != NULL) ? lpTCPTeacher->GetDBFlowID() : 0);
  // 构造转发JSON数据块 => 返回套TCP讲师流量编号...
  json_object * new_obj = json_object_new_object();
  json_object_object_add(new_obj, "flow_teacher", json_object_new_int(nTeacherFlowID));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendCommonCmd(kCmd_Student_OnLine, lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

// 处理Teacher事件...
int CTCPClient::doScreenClient(Cmd_Header * lpHeader, const char * lpJsonPtr)
{
  /*int nResult = -1;
  switch(lpHeader->m_cmd)
  {
    case kCmd_Screen_Login:      nResult = this->doCmdScreenLogin(); break;
    case kCmd_Screen_OnLine:     nResult = this->doCmdScreenOnLine(); break;
    case kCmd_Screen_Finish:     nResult = this->doCmdScreenFinish(); break;
    case kCmd_Screen_Packet:     nResult = this->doCmdScreenPacket(lpJsonPtr, lpHeader->m_pkg_len); break;
  }*/
  // 默认全部返回正确...
  return 0;
}

/*int CTCPClient::doCmdScreenPacket(const char * lpDataPtr, int nDataSize)
{
  // 如果不是屏幕端，直接返回...
  if( m_nClientType != kClientScreen )
    return 0;
  // 如果数据包无效，直接返回...
  if (lpDataPtr == NULL || nDataSize <= 0)
    return 0;
  // 结束包由专门的命令发送...
  int nResult = this->doTransferScreenPackToTeacher(lpDataPtr, nDataSize);
  // 如果转发给讲师端失败，需要返回0表示不要再继续转发了...
  int nPackSize = ((nResult < 0) ? 0 : nDataSize);
  // 构造转发JSON数据块 => 返回套TCP套接字编号...
  json_object * new_obj = json_object_new_object();
  json_object_object_add(new_obj, "pack_size", json_object_new_int(nPackSize));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  nResult = this->doSendCommonCmd(kCmd_Screen_Packet, lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

int CTCPClient::doTransferScreenPackToTeacher(const char * lpDataPtr, int nDataSize)
{
  // 如果讲师端无效，需要停止转发屏幕端数据包...
  CTCPClient * lpTCPTeacher = m_lpTCPRoom->GetTCPTeacher();
  if (lpTCPTeacher == NULL)
    return -1;
  // 使用统一的通用命令发送接口函数 => 注意：必须是对应的讲师端的对象...
  return lpTCPTeacher->doSendCommonCmd(kCmd_Screen_Packet, lpDataPtr, nDataSize, m_nScreenID);
}

int CTCPClient::doCmdScreenFinish()
{
  // 如果讲师端无效，需要停止转发屏幕端数据包...
  CTCPClient * lpTCPTeacher = m_lpTCPRoom->GetTCPTeacher();
  if (lpTCPTeacher == NULL)
    return -1;
  // 构造转发JSON数据块 => 转发结束命令到讲师端...
  json_object * new_obj = json_object_new_object();
  json_object_object_add(new_obj, "screen_id", json_object_new_int(m_nScreenID));
  json_object_object_add(new_obj, "user_name", json_object_new_string(m_strUserName.c_str()));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = lpTCPTeacher->doSendCommonCmd(kCmd_Screen_Finish, lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

int CTCPClient::doCmdScreenLogin()
{
  if( m_MapJson.find("room_id") == m_MapJson.end() ||
    m_MapJson.find("user_name") == m_MapJson.end() ) {
    return -1;
  }
  // 保存解析到的有效JSON数据项...
  m_strRoomID = m_MapJson["room_id"];
  m_strUserName = m_MapJson["user_name"];
  m_nRoomID = atoi(m_strRoomID.c_str());
  // 创建或更新房间，更新房间里的屏幕端...
  m_lpTCPRoom = m_lpTCPThread->doCreateRoom(m_nRoomID);
  m_lpTCPRoom->doCreateScreen(this);
  // 构造转发JSON数据块 => 返回套TCP套接字编号...
  json_object * new_obj = json_object_new_object();
  json_object_object_add(new_obj, "tcp_socket", json_object_new_int(m_nConnFD));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendCommonCmd(kCmd_Screen_Login, lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

int CTCPClient::doCmdScreenOnLine()
{
  return 0;
}*/

// 处理Teacher事件...
int CTCPClient::doTeacherClient(Cmd_Header * lpHeader, const char * lpJsonPtr)
{
  int nResult = -1;
  switch(lpHeader->m_cmd)
  {
    case kCmd_Teacher_Login:      nResult = this->doCmdTeacherLogin(); break;
    case kCmd_Teacher_OnLine:     nResult = this->doCmdTeacherOnLine(); break;
  }
  // 默认全部返回正确...
  return 0;
}

// 直接转发云台控制命令到正在推流的学生端对象...
/*int CTCPClient::doTransferCameraPTZByTeacher(const char * lpJsonPtr, int nJsonSize)
{
  // 解析命令数据，判断传递JSON数据有效性...
  if( m_MapJson.find("camera_id") == m_MapJson.end() ||
    m_MapJson.find("speed_val") == m_MapJson.end() ||
    m_MapJson.find("cmd_id") == m_MapJson.end() ||
    lpJsonPtr == NULL || nJsonSize <= 0 ||
    m_lpTCPRoom == NULL ) {
    return -1;
  }
  // 获取讲师端传递过来的摄像头通道编号...
  int nDBCameraID = atoi(m_MapJson["camera_id"].c_str());
  // 在房间中查找对应的摄像头对象 => 通过摄像头数据库编号...
  GM_MapTCPCamera & theMapCamera = m_lpTCPRoom->GetMapCamera();
  GM_MapTCPCamera::iterator itorItem = theMapCamera.find(nDBCameraID);
  // 如果没有找到，返回0，不要返回-1，会导致连接被删除...
  if( itorItem == theMapCamera.end() )
    return 0;
  // 查找通道对应的学生端对象...
  GM_MapTCPConn & theMapConn = m_lpTCPThread->GetMapConnect();
  CTCPCamera * lpTCPCamera = itorItem->second;
  int nTCPSockFD = lpTCPCamera->GetTCPSockFD();
  GM_MapTCPConn::iterator itorConn = theMapConn.find(nTCPSockFD);
  // 如果没有找到，直接返回...
  if( itorConn == theMapConn.end() )
    return 0;
  // 对这个找到的学生端进行身份验证...
  CTCPClient * lpStudent = itorConn->second;
  if( lpStudent->GetClientType() != kClientStudent )
    return 0;
  // 使用统一的通用命令发送接口函数 => 注意：必须是对应的学生端的对象...
  return lpStudent->doSendCommonCmd(kCmd_Camera_PTZCommand, lpJsonPtr, nJsonSize);
}

// 处理Teacher发起学生焦点摄像头编号变化事件通知...
int CTCPClient::doCmdTeacherCameraPusherID()
{
  // 解析命令数据，判断传递JSON数据有效性...
  if( m_MapJson.find("camera_id") == m_MapJson.end() )
    return -1;
  // 解析出新的学生端推流焦点摄像头编号...
  int nDBCameraID = atoi(m_MapJson["camera_id"].c_str());
  // 学生推流焦点摄像头编号发生变化需要更新第三方音频编号...
  GetApp()->GetUdpThread()->doTeacherCameraPusherID(m_nRoomID, nDBCameraID);
  // 返回执行结果...
  return 0;
}

// 处理Teacher主动发起停止学生端摄像头推流事件通知...
int CTCPClient::doCmdTeacherCameraLiveStop()
{
  // 解析命令数据，判断传递JSON数据有效性...
  if( m_MapJson.find("camera_id") == m_MapJson.end() )
    return -1;
  int nDBCameraID = atoi(m_MapJson["camera_id"].c_str());
  // 删除UDP房间里的老师观看者对象和学生推流者对象，提前删除避免数据混乱...
  GetApp()->doDeleteForCameraLiveStop(m_nRoomID, nDBCameraID);
  // 将讲师端发起的摄像头停止推流命令转发给摄像头对应的学生端，让学生端发起停止推流命令...
  int nResult = this->doTransferCameraLiveCmdByTeacher(kCmd_Camera_LiveStop, nDBCameraID);
  if (nResult >= 0)
    return nResult;
  // 注意：转发失败后，必须告诉讲师端，才能发起新的开始推流命令...
  assert(nResult < 0);
  // 转发停止通道成功命令到房间里的讲师端...
  json_object * new_obj = json_object_new_object();
  json_object_object_add(new_obj, "camera_id", json_object_new_int(nDBCameraID));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数 => 注意：必须是对应的讲师端的对象...
  nResult = this->doSendCommonCmd(kCmd_Camera_LiveStop, lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

// 处理Teacher发起学生端摄像头开始推流事件通知...
int CTCPClient::doCmdTeacherCameraLiveStart()
{
  // 解析命令数据，判断传递JSON数据有效性...
  if( m_MapJson.find("camera_id") == m_MapJson.end() )
    return -1;
  int nDBCameraID = atoi(m_MapJson["camera_id"].c_str());
  // 将讲师端发起的摄像头推流命令转发给摄像头对应的学生端，让学生端发起推流命令...
  return this->doTransferCameraLiveCmdByTeacher(kCmd_Camera_LiveStart, nDBCameraID);
}

// 处理Teacher发起的让学生端指定的摄像头开始|停止推流的命令...
int CTCPClient::doTransferCameraLiveCmdByTeacher(int nCmdID, int nDBCameraID)
{
  // 有可能房间对象为空...
  if (m_lpTCPRoom == NULL)
    return -1;
  // 在房间中查找对应的摄像头对象 => 通过摄像头数据库编号...
  GM_MapTCPCamera & theMapCamera = m_lpTCPRoom->GetMapCamera();
  GM_MapTCPCamera::iterator itorItem = theMapCamera.find(nDBCameraID);
  // 如果没有找到，直接返回...
  if( itorItem == theMapCamera.end() )
    return -1;
  // 查找通道对应的学生端对象...
  GM_MapTCPConn & theMapConn = m_lpTCPThread->GetMapConnect();
  CTCPCamera * lpTCPCamera = itorItem->second;
  int nTCPSockFD = lpTCPCamera->GetTCPSockFD();
  GM_MapTCPConn::iterator itorConn = theMapConn.find(nTCPSockFD);
  // 如果没有找到，直接返回...
  if( itorConn == theMapConn.end() )
    return -1;
  // 对这个找到的学生端进行身份验证...
  CTCPClient * lpStudent = itorConn->second;
  if( lpStudent->GetClientType() != kClientStudent )
    return -1;
  // 转发开始推流命令到这个学生端...
  json_object * new_obj = json_object_new_object();
  json_object_object_add(new_obj, "camera_id", json_object_new_int(nDBCameraID));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数 => 注意：必须是对应的学生端的对象...
  int nResult = lpStudent->doSendCommonCmd(nCmdID, lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}*/

// 处理Teacher登录事件...
int CTCPClient::doCmdTeacherLogin()
{
  // 处理讲师端登录过程 => 判断传递JSON数据有效性...
  if( m_MapJson.find("mac_addr") == m_MapJson.end() ||
    m_MapJson.find("ip_addr") == m_MapJson.end() ||
    m_MapJson.find("room_id") == m_MapJson.end() ||
    m_MapJson.find("flow_id") == m_MapJson.end() ) {
    return -1;
  }
  // 保存解析到的有效JSON数据项...
  m_strMacAddr = m_MapJson["mac_addr"];
  m_strIPAddr  = m_MapJson["ip_addr"];
  m_strRoomID  = m_MapJson["room_id"];
  m_nRoomID    = atoi(m_strRoomID.c_str());
  m_nDBFlowID  = atoi(m_MapJson["flow_id"].c_str());
  // 创建或更新房间，更新房间里的讲师端...
  m_lpRoom = GetApp()->doCreateRoom(m_nRoomID);
  m_lpRoom->doTcpCreateTeacher(this);
  // 构造转发JSON数据块，只返回套接字...
  json_object * new_obj = json_object_new_object();
  json_object_object_add(new_obj, "tcp_socket", json_object_new_int(m_nConnFD));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendCommonCmd(kCmd_Teacher_Login, lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

// 处理Teacher在线汇报命令...
int CTCPClient::doCmdTeacherOnLine()
{
  return 0;
}

/*// 处理Teacher|Student获取房间里的在线摄像头列表...
int CTCPClient::doCmdCommonCameraOnLineList()
{
  // 有可能房间对象为空...
  if (m_lpTCPRoom == NULL)
    return -1;  
  // 获取当前房间里的在线摄像头通道列表集合...
  GM_MapTCPCamera & theMapCamera = m_lpTCPRoom->GetMapCamera();
  GM_MapTCPCamera::iterator itorItem;
  CTCPCamera * lpTCPCamera = NULL;
  // 构造转发JSON数据块 => 返回在线通道列表...
  json_object * new_obj = json_object_new_object();
  // 先计算在线摄像头总数，放入JSON数据包当中...
  json_object_object_add(new_obj, "list_num", json_object_new_int(theMapCamera.size()));
  // 在线摄像头数大于0，遍历摄像头通道列表集合，组合要返回的数据内容...
  if( theMapCamera.size() > 0 ) {
    json_object * new_array = json_object_new_array();
    for(itorItem = theMapCamera.begin(); itorItem != theMapCamera.end(); ++itorItem) {
      lpTCPCamera = itorItem->second;
      // 每条数据内容 => room_id|camera_id|camera_name|pc_name
      json_object * data_obj = json_object_new_object();
      json_object_object_add(data_obj, "room_id", json_object_new_int(lpTCPCamera->GetRoomID()));
      json_object_object_add(data_obj, "camera_id", json_object_new_int(lpTCPCamera->GetDBCameraID()));
      json_object_object_add(data_obj, "pc_name", json_object_new_string(lpTCPCamera->GetPCName().c_str()));
      json_object_object_add(data_obj, "camera_name", json_object_new_string(lpTCPCamera->GetCameraName().c_str()));
      json_object_array_add(new_array, data_obj);
    }
    // 将数组放入核心对象当中...
    json_object_object_add(new_obj, "list_data", new_array);
  }
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendCommonCmd(kCmd_Camera_OnLineList, lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}*/

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 注意：目前这种通过m_strSend中转发送缓存，存在一定的风险，多个线程同时发送可能就会发生命令丢失的问题...
// 注意：判断的标准是m_strSend是否为空，不为空，说明数据还没有被发走，因此，这里需要改进，改动比较大...
// 注意：epoll_event.data 是union类型，里面的4个变量不能同时使用，只能使用一个，目前我们用的是fd
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 统一的通用命令发送接口函数...
int CTCPClient::doSendCommonCmd(int nCmdID, const char * lpJsonPtr/* = NULL*/, int nJsonSize/* = 0*/, int nSockID/* = 0*/)
{
  // 构造回复结构头信息...
  Cmd_Header theHeader = {0};
  theHeader.m_pkg_len = ((lpJsonPtr != NULL) ? nJsonSize : 0);
  theHeader.m_type = m_nClientType;
  theHeader.m_cmd  = nCmdID;
  theHeader.m_sock = nSockID;
  // 先填充名头头结构数据内容 => 注意是assign重建字符串...
  m_strSend.assign((char*)&theHeader, sizeof(theHeader));
  // 如果传入的数据内容有效，才进行数据的填充...
  if( lpJsonPtr != NULL && nJsonSize > 0 ) {
    m_strSend.append(lpJsonPtr, nJsonSize);
  }
  // 向当前终端对象发起发送数据事件...
  epoll_event evClient = {0};
  evClient.data.fd = m_nConnFD;
  evClient.events = EPOLLOUT | EPOLLET;
  // 重新修改事件，加入写入事件...
  if( epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, m_nConnFD, &evClient) < 0 ) {
    log_trace("mod socket '%d' to epoll failed: %s", m_nConnFD, strerror(errno));
    return -1;
  }
  // 返回执行正确...
  return 0;
}
//
// 统一的JSON解析接口 => 保存到集合对象当中...
int CTCPClient::parseJsonData(const char * lpJsonPtr, int nJsonLength)
{
  // 首先判断输入数据的有效性...
  if( lpJsonPtr == NULL || nJsonLength <= 0 )
    return -1;
  // 解析 JSON 数据包失败，直接返回错误号...
  json_object * new_obj = json_tokener_parse(lpJsonPtr);
  if( new_obj == NULL ) {
    log_trace("parse json data error => %d, %s", nJsonLength, lpJsonPtr);
    return -1;
  }
  // check the json type => must be json_type_object...
  json_type nJsonType = json_object_get_type(new_obj);
  if( nJsonType != json_type_object ) {
    log_trace("parse json data error => %d, %s", nJsonLength, lpJsonPtr);
    json_object_put(new_obj);
    return -1;
  }
  // 解析传递过来的JSON数据包，存入集合当中...
  json_object_object_foreach(new_obj, key, val) {
    m_MapJson[key] = json_object_get_string(val);
  }
  // 解析数据完毕，释放JSON对象...
  json_object_put(new_obj);
  return 0;
}
//
// 检测是否超时...
bool CTCPClient::IsTimeout()
{
  time_t nDeltaTime = time(NULL) - m_nStartTime;
  return ((nDeltaTime >= CLIENT_TIME_OUT) ? true : false);
}
//
// 重置超时时间...
void CTCPClient::ResetTimeout()
{
  m_nStartTime = time(NULL);
}
