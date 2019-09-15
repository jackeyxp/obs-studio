
#include "app.h"
#include "tcpclient.h"
#include "tcpthread.h"
#include <json/json.h>

#define MAX_PATH_SIZE           260
#define MAX_LINE_SIZE          4096   // 最大命令缓存长度，以前是64K，太长了...
#define CLIENT_TIME_OUT      1 * 60   // 客户端超时断开时间1分钟(汇报频率30秒)...

CTCPClient::CTCPClient(CTCPThread * lpTCPThread, int connfd, int nHostPort, string & strSinAddr)
  : m_nClientType(0)
  , m_nConnFD(connfd)
  , m_nHostPort(nHostPort)
  , m_strSinAddr(strSinAddr)
  , m_lpTCPThread(lpTCPThread)
{
  assert(m_nConnFD > 0 && m_strSinAddr.size() > 0 );
  assert(m_lpTCPThread != NULL);
  m_nStartTime = time(NULL);
  m_epoll_fd = m_lpTCPThread->GetEpollFD();
  m_uTcpTimeID = GetApp()->GetRefCounterID();
}

CTCPClient::~CTCPClient()
{
  // 打印终端退出信息...
  log_trace("Client Delete: %s, From: %s:%d, Socket: %d", get_client_type(m_nClientType), 
            this->m_strSinAddr.c_str(), this->m_nHostPort, this->m_nConnFD);
  // 终端退出时，需要删除服务器对象...
  GetApp()->doDeleteUdpServer(m_nConnFD);
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
    log_trace("transmit command error(%s)", strerror(errno));
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
    log_trace("mod socket '%d' to epoll failed: %s", m_nConnFD, strerror(errno));
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
    //log_trace("Client: %s, ForRead: Close, Socket: %d", get_client_type(m_nClientType), this->m_nConnFD);
    return -1;
  }
  // 读取失败，返回错误，让上层关闭...
  if( nReadLen < 0 ) {
		log_trace("Client: %s, read error(%s)", get_client_type(m_nClientType), strerror(errno));
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
    if( lpCmdHeader->m_pkg_len > 0 ) {
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
      case kClientPHP:       nResult = this->doPHPClient(lpCmdHeader, lpDataPtr); break;
      case kClientStudent:   nResult = this->doSmartClient(lpCmdHeader, lpDataPtr); break;
      case kClientTeacher:   nResult = this->doSmartClient(lpCmdHeader, lpDataPtr); break;
      case kClientUdpServer: nResult = this->doUdpServerClient(lpCmdHeader, lpDataPtr); break;
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

// 处理Smart事件...
int CTCPClient::doSmartClient(Cmd_Header * lpHeader, const char * lpJsonPtr)
{
  int nResult = -1;
  switch(lpHeader->m_cmd)
  {
    case kCmd_Smart_Login:  nResult = this->doCmdSmartLogin(); break;
    case kCmd_Smart_OnLine: nResult = this->doCmdSmartOnLine(); break;
  }
  // 默认全部返回正确...
  return 0;
}

// 处理Smart登录事件...
int CTCPClient::doCmdSmartLogin()
{
  // 构造转发JSON数据块 => 返回套接字+时间戳...
  json_object * new_obj = json_object_new_object();
  json_object_object_add(new_obj, "tcp_socket", json_object_new_int(m_nConnFD));
  json_object_object_add(new_obj, "tcp_time", json_object_new_int(m_uTcpTimeID));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendCommonCmd(kCmd_Smart_Login, lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

// 处理Smart在线汇报命令...
int CTCPClient::doCmdSmartOnLine()
{
  return 0;
}

// 处理PHP客户端事件...
int CTCPClient::doPHPClient(Cmd_Header * lpHeader, const char * lpJsonPtr)
{
  int nResult = -1;
  switch(lpHeader->m_cmd)
  {
    case kCmd_PHP_GetUdpServer:   nResult = this->doCmdPHPGetUdpServer(); break;
    case kCmd_PHP_GetAllServer:   nResult = this->doCmdPHPGetAllServer(); break;
    case kCmd_PHP_GetAllClient:   nResult = this->doCmdPHPGetAllClient(); break;
    case kCmd_PHP_GetRoomList:    nResult = this->doCmdPHPGetRoomList(); break;
    case kCmd_PHP_GetPlayerList:  nResult = this->doCmdPHPGetPlayerList(); break;
    case kCmd_PHP_Bind_Mini:      nResult = this->doTransferBindMini(lpJsonPtr, lpHeader->m_pkg_len); break;
  }
  // 默认全部返回正确...
  return 0;
}

// 处理PHP发送的小程序绑定登录命令...
int CTCPClient::doTransferBindMini(const char * lpJsonPtr, int nJsonSize)
{
  // 准备反馈需要的变量...
  int nErrCode = ERR_OK;
  // 创建反馈的json数据包...
  json_object * new_obj = json_object_new_object();
  do {
    // 判断传递JSON数据有效性 => 必须包含client_type|tcp_socket|tcp_time|room_id字段信息...
    if( m_MapJson.find("client_type") == m_MapJson.end() || 
        m_MapJson.find("tcp_socket") == m_MapJson.end() ||
        m_MapJson.find("tcp_time") == m_MapJson.end() ||
        m_MapJson.find("room_id") == m_MapJson.end() ) {
      nErrCode = ERR_NO_PARAM;
      break;
    }
    // 获取传递过来的反向终端套接字编号|用户类型|时间戳...
    int nRoomID = atoi(m_MapJson["room_id"].c_str());
    int nTypeID = atoi(m_MapJson["client_type"].c_str());
    int nSocketFD = atoi(m_MapJson["tcp_socket"].c_str());
    uint32_t uTcpTimeID = (uint32_t)atoi(m_MapJson["tcp_time"].c_str());
    CTCPThread * lpTCPThread = GetApp()->GetTCPThread();
    GM_MapTCPConn & theMapConn = lpTCPThread->GetMapConnect();
    GM_MapTCPConn::iterator itorItem = theMapConn.find(nSocketFD);
    if (itorItem == theMapConn.end()) {
      nErrCode = ERR_NO_TERMINAL;
      break;
    }
    // 得到当前终端的对象，终端类型不匹配，返回错误...
    CTCPClient * lpTCPClient = itorItem->second;
    if (nTypeID != lpTCPClient->GetClientType()) {
      nErrCode = ERR_TYPE_MATCH;
      break;
    }
    // 如果时间标识符不一致，直接返回错误...
    if (uTcpTimeID != lpTCPClient->GetTcpTimeID()) {
      nErrCode = ERR_TIME_MATCH;
      break;
    }
    // 如果是讲师端登录，需要验证当前选择的房间是否已经有讲师登录...
    /*CTCPRoom * lpTCPRoom = GetApp()->doFindTCPRoom(nRoomID);
    if (nTypeID == kClientTeacher && lpTCPRoom != NULL && lpTCPRoom->GetTeacherCount() > 0) {
      nErrCode = ERR_HAS_TEACHER;
      break;
    }*/
    // 直接调用该有效终端的转发命令接口...
    int nReturn = lpTCPClient->doSendCommonCmd(kCmd_PHP_Bind_Mini, lpJsonPtr, nJsonSize);
  } while ( false );
  // 组合错误信息内容...
  json_object_object_add(new_obj, "err_code", json_object_new_int(nErrCode));
  json_object_object_add(new_obj, "err_cmd", json_object_new_int(kCmd_PHP_Bind_Mini));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendPHPResponse(lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;  
}

// 处理PHP发送的查询指定服务器的房间列表...
int CTCPClient::doCmdPHPGetRoomList()
{
  // 准备反馈需要的变量...
  int nErrCode = ERR_OK;
  // 创建反馈的json数据包...
  json_object * new_obj = json_object_new_object();
  do {
    // 判断传递JSON数据有效性 => 必须包含server_id字段信息...
    if( m_MapJson.find("server_id") == m_MapJson.end() ) {
      nErrCode = ERR_NO_SERVER;
      break;
    }
    // 获取传递过来的服务器套接字编号...
    int nSocketFD = atoi(m_MapJson["server_id"].c_str());
    // 通过套接字编号直接查找直播服务器...
    CUdpServer * lpUdpServer = GetApp()->doFindUdpServer(nSocketFD);
    if( lpUdpServer == NULL ) {
      nErrCode = ERR_NO_SERVER;
      break;
    }
    // 先计算在线房间总数，放入JSON数据包当中...
    json_object_object_add(new_obj, "list_num", json_object_new_int(lpUdpServer->GetRoomCount()));
    // 遍历在线的房间列表，组合成记录，发送给PHP客户端...
    GM_MapRoom & theMapRoom = lpUdpServer->m_MapRoom;
    GM_MapRoom::iterator itorItem;
    CTCPRoom * lpTCPRoom = NULL;
    if( theMapRoom.size() > 0 ) {
      json_object * new_array = json_object_new_array();
      for(itorItem = theMapRoom.begin(); itorItem != theMapRoom.end(); ++itorItem) {
        lpTCPRoom = itorItem->second;
        // 每条数据内容 => room_id|teacher|student
        json_object * data_obj = json_object_new_object();
        json_object_object_add(data_obj, "room_id", json_object_new_int(lpTCPRoom->GetRoomID()));
        json_object_object_add(data_obj, "teacher", json_object_new_int(lpTCPRoom->GetTeacherCount()));
        json_object_object_add(data_obj, "student", json_object_new_int(lpTCPRoom->GetStudentCount()));
        json_object_array_add(new_array, data_obj);
      }
      // 将数组放入核心对象当中...
      json_object_object_add(new_obj, "list_data", new_array);
    }
  } while( false );
  // 组合错误信息内容...
  json_object_object_add(new_obj, "err_code", json_object_new_int(nErrCode));
  json_object_object_add(new_obj, "err_cmd", json_object_new_int(kCmd_PHP_GetRoomList));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendPHPResponse(lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

// 处理PHP发送的查询指定房间的用户列表...
int CTCPClient::doCmdPHPGetPlayerList()
{
  // 准备反馈需要的变量...
  int nErrCode = ERR_OK;
  // 创建反馈的json数据包...
  json_object * new_obj = json_object_new_object();
  // 组合错误信息内容...
  json_object_object_add(new_obj, "err_code", json_object_new_int(nErrCode));
  json_object_object_add(new_obj, "err_cmd", json_object_new_int(kCmd_PHP_GetPlayerList));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendPHPResponse(lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

// 处理PHP发送的查询所有在线用户列表...
int CTCPClient::doCmdPHPGetAllClient()
{
  // 准备反馈需要的变量...
  int nErrCode = ERR_OK;
  // 创建反馈的json数据包...
  CTCPClient * lpTCPClient = NULL;
  GM_MapTCPConn::iterator itorItem;
  json_object * new_obj = json_object_new_object();
  CTCPThread * lpTCPThread = GetApp()->GetTCPThread();
  GM_MapTCPConn & theMapConn = lpTCPThread->GetMapConnect();
  // 先计算在线客户端总数，放入JSON数据包当中...
  json_object_object_add(new_obj, "list_num", json_object_new_int(theMapConn.size()));
  // 遍历在线的客户端列表，组合成记录，发送给PHP客户端...
  if( theMapConn.size() > 0 ) {
    json_object * new_array = json_object_new_array();
    for(itorItem = theMapConn.begin(); itorItem != theMapConn.end(); ++itorItem) {
      lpTCPClient = itorItem->second;
      // 每条数据内容 => client_type|client_addr|client_port
      json_object * data_obj = json_object_new_object();
      json_object_object_add(data_obj, "client_type", json_object_new_string(get_client_type(lpTCPClient->m_nClientType)));
      json_object_object_add(data_obj, "client_addr", json_object_new_string(lpTCPClient->m_strSinAddr.c_str()));
      json_object_object_add(data_obj, "client_port", json_object_new_int(lpTCPClient->m_nHostPort));
      json_object_array_add(new_array, data_obj);
    }
    // 将数组放入核心对象当中...
    json_object_object_add(new_obj, "list_data", new_array);    
  }
  // 组合错误信息内容...
  json_object_object_add(new_obj, "err_code", json_object_new_int(nErrCode));
  json_object_object_add(new_obj, "err_cmd", json_object_new_int(kCmd_PHP_GetAllClient));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendPHPResponse(lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

// 处理PHP发送的查询所有在线的UDP服务器的事件...
int CTCPClient::doCmdPHPGetAllServer()
{
  // 准备反馈需要的变量...
  int nErrCode = ERR_OK;
  // 创建反馈的json数据包...
  CUdpServer * lpUdpServer = NULL;
  GM_MapServer::iterator itorItem;
  json_object * new_obj = json_object_new_object();
  GM_MapServer & theMapServer = GetApp()->GetMapServer();
  // 先计算在线UDP服务器总数，放入JSON数据包当中...
  json_object_object_add(new_obj, "list_num", json_object_new_int(theMapServer.size()));
  // 遍历在线的UDP服务器列表，组合成记录，发送给PHP客户端...
  if( theMapServer.size() > 0 ) {
    json_object * new_array = json_object_new_array();
    for(itorItem = theMapServer.begin(); itorItem != theMapServer.end(); ++itorItem) {
      lpUdpServer = itorItem->second;
      // 每条数据内容 => remote_addr|remote_port|udp_addr|udp_port|room_num|server
      json_object * data_obj = json_object_new_object();
      json_object_object_add(data_obj, "remote_addr", json_object_new_string(lpUdpServer->m_strRemoteAddr.c_str()));
      json_object_object_add(data_obj, "remote_port", json_object_new_int(lpUdpServer->m_nRemotePort));
      json_object_object_add(data_obj, "udp_addr", json_object_new_string(lpUdpServer->m_strUdpAddr.c_str()));
      json_object_object_add(data_obj, "udp_port", json_object_new_int(lpUdpServer->m_nUdpPort));
      json_object_object_add(data_obj, "debug_mode", json_object_new_int(lpUdpServer->m_bIsDebugMode));
      json_object_object_add(data_obj, "room_num", json_object_new_int(lpUdpServer->GetRoomCount()));
      json_object_object_add(data_obj, "server_id", json_object_new_int(itorItem->first));
      json_object_array_add(new_array, data_obj);
    }
    // 将数组放入核心对象当中...
    json_object_object_add(new_obj, "list_data", new_array);
  }
  // 组合错误信息内容...
  json_object_object_add(new_obj, "err_code", json_object_new_int(nErrCode));
  json_object_object_add(new_obj, "err_cmd", json_object_new_int(kCmd_PHP_GetAllServer));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendPHPResponse(lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

// 处理PHP发送的根据房间号查询UDP服务器的事件...
int CTCPClient::doCmdPHPGetUdpServer()
{
  // 准备反馈需要的变量...
  int nErrCode = ERR_OK;
  // 创建反馈的json数据包...
  json_object * new_obj = json_object_new_object();
  do {
    // 判断传递JSON数据有效性 => 必须包含room_id字段信息...
    if( m_MapJson.find("room_id") == m_MapJson.end() ) {
      nErrCode = ERR_NO_ROOM;
      break;
    }
    // 获取传递过来的房间编号和终端运行模式...
    CApp * lpApp = GetApp();
    int nRoomID = atoi(m_MapJson["room_id"].c_str());
    bool bIsDebugMode = ((m_MapJson.find("debug_mode") == m_MapJson.end()) ? false : atoi(m_MapJson["debug_mode"].c_str()));
    // 通过房间号查找房间的方式，间接查找直播服务器...
    CTCPRoom * lpTCPRoom = lpApp->doFindTCPRoom(nRoomID);
    CUdpServer * lpUdpServer = ((lpTCPRoom != NULL) ? lpTCPRoom->GetUdpServer() : NULL);
    // 如果直播服务器有效，查看运行模式是否与终端一致，不一致，返回不匹配...
    if( lpUdpServer != NULL && lpUdpServer->m_bIsDebugMode != bIsDebugMode ) {
      nErrCode = ERR_MODE_MATCH;
      break;
    }
    // 如果直播服务器无效，查找挂载量最小的直播服务器...
    if( lpUdpServer == NULL ) {
      lpUdpServer = (bIsDebugMode ? lpApp->doFindDebugUdpServer() : lpApp->doFindMinUdpServer());
    }
    // 如果最终还是没有找到直播服务器，设置错误编号...
    if( lpUdpServer == NULL ) {
      nErrCode = ERR_NO_SERVER;
      break;
    }
    // 组合PHP需要的直播服务器结果信息...
    json_object_object_add(new_obj, "remote_addr", json_object_new_string(lpUdpServer->m_strRemoteAddr.c_str()));
    json_object_object_add(new_obj, "remote_port", json_object_new_int(lpUdpServer->m_nRemotePort));
    json_object_object_add(new_obj, "udp_addr", json_object_new_string(lpUdpServer->m_strUdpAddr.c_str()));
    json_object_object_add(new_obj, "udp_port", json_object_new_int(lpUdpServer->m_nUdpPort));
    // 组合PHP需要的查找通道的内容信息...
    int nTeacherCount = ((lpTCPRoom != NULL) ? lpTCPRoom->GetTeacherCount() : 0);
    int nStudentCount = ((lpTCPRoom != NULL) ? lpTCPRoom->GetStudentCount() : 0);
    json_object_object_add(new_obj, "room_id", json_object_new_int(nRoomID));
    json_object_object_add(new_obj, "teacher", json_object_new_int(nTeacherCount));
    json_object_object_add(new_obj, "student", json_object_new_int(nStudentCount));
  } while( false );
  // 组合错误信息内容...
  json_object_object_add(new_obj, "err_code", json_object_new_int(nErrCode));
  json_object_object_add(new_obj, "err_cmd", json_object_new_int(kCmd_PHP_GetUdpServer));
  // 转换成json字符串，获取字符串长度...
  char * lpNewJson = (char*)json_object_to_json_string(new_obj);
  // 使用统一的通用命令发送接口函数...
  int nResult = this->doSendPHPResponse(lpNewJson, strlen(lpNewJson));
  // json对象引用计数减少...
  json_object_put(new_obj);
  // 返回执行结果...
  return nResult;
}

// 处理UdpServer事件...
int CTCPClient::doUdpServerClient(Cmd_Header * lpHeader, const char * lpJsonPtr)
{
  int nResult = -1;
  switch(lpHeader->m_cmd)
  {
    case kCmd_UdpServer_Login:      nResult = this->doCmdUdpServerLogin(); break;
    case kCmd_UdpServer_OnLine:     nResult = this->doCmdUdpServerOnLine(); break;
    case kCmd_UdpServer_AddTeacher: nResult = this->doCmdUdpServerAddTeacher(); break;
    case kCmd_UdpServer_DelTeacher: nResult = this->doCmdUdpServerDelTeacher(); break;
    case kCmd_UdpServer_AddStudent: nResult = this->doCmdUdpServerAddStudent(); break;
    case kCmd_UdpServer_DelStudent: nResult = this->doCmdUdpServerDelStudent(); break;
  }
  // 默认全部返回正确...
  return 0;
}

// 处理UdpServer的登录事件...
int CTCPClient::doCmdUdpServerLogin()
{
  // 判断传递JSON数据有效性 => 必须包含服务器地址|端口字段信息...
  if( m_MapJson.find("remote_addr") == m_MapJson.end() ||
    m_MapJson.find("remote_port") == m_MapJson.end() ||
    m_MapJson.find("udp_addr") == m_MapJson.end() ||
    m_MapJson.find("udp_port") == m_MapJson.end() ) {
    return -1;
  }
  // 创建或更新服务器对象，创建成功，更新信息...
  CUdpServer * lpUdpServer = GetApp()->doCreateUdpServer(m_nConnFD);
  if( lpUdpServer != NULL ) {
    // 注意：阿里云专有网络无法获取外网地址，ECS绑定了公网地址，TCP链接获取的地址就是这个公网地址...
    // 注意：UDPServer的远程地址和UDP地址都是相同的，通过TCP链接获取到的公网地址...
    // 注意：UDPServer传递的参数remote_addr和udp_addr，都是空地址...
    lpUdpServer->m_bIsDebugMode = ((m_MapJson.find("debug_mode") == m_MapJson.end()) ? false : atoi(m_MapJson["debug_mode"].c_str()));
    lpUdpServer->m_strRemoteAddr = this->m_strSinAddr; //m_MapJson["remote_addr"];
    lpUdpServer->m_strUdpAddr = this->m_strSinAddr; //m_MapJson["udp_addr"];
    lpUdpServer->m_nUdpPort = atoi(m_MapJson["udp_port"].c_str());
    lpUdpServer->m_nRemotePort = atoi(m_MapJson["remote_port"].c_str());
    log_trace("[UdpServer] Mode => %s, UdpAddr => %s:%d, RemoteAddr => %s:%d",
              lpUdpServer->m_bIsDebugMode ? "Debug" : "Release",
              lpUdpServer->m_strUdpAddr.c_str(), lpUdpServer->m_nUdpPort,
              lpUdpServer->m_strRemoteAddr.c_str(), lpUdpServer->m_nRemotePort);
  }
  return 0;
}

static int8_t sDigitMask[] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //0-9
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //10-19 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //20-29
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //30-39
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, //40-49 //stop on every character except a number
	1, 1, 1, 1, 1, 1, 1, 1, 0, 0, //50-59
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //60-69 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //70-79
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //80-89
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //90-99
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //100-109
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //110-119
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //120-129
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //130-139
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //140-149
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //150-159
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //160-169
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //170-179
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //180-189
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //190-199
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //200-209
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //210-219
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //220-229
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //230-239
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //240-249
	0, 0, 0, 0, 0, 0              //250-255
};

int CTCPClient::doParseInt(char * inList, int & outSize)
{
  int nNumVal = 0; outSize = 0;
	uint8_t * fStartGet = (uint8_t*)inList;
	uint8_t * fEndGet = fStartGet + strlen(inList);
	// 遍历字符串，直到遇到数字就停止遍历...
	while ((fStartGet < fEndGet) && (!sDigitMask[*fStartGet])) {
		fStartGet++;
	}
	// 如果开始大于或等于结束，说明没有找到数字，返回...
	if (fStartGet >= fEndGet)
    return -1;
	// 从当前位置开始，解析出数字内容...
	while ((fStartGet < fEndGet) && (*fStartGet >= '0') && (*fStartGet <= '9')) {
		nNumVal = (nNumVal * 10) + (*fStartGet - '0');
		fStartGet++;
	}
  // 返回有效数值和字符串长度...
  outSize = fStartGet - (uint8_t*)inList;
  return nNumVal;
}

void CTCPClient::doParseRoom(char * inList)
{
  int nOutSize = 0;
  int nRoomID = doParseInt(inList, nOutSize);
  inList += nOutSize;
  int nTeacherCount = doParseInt(inList, nOutSize);
  inList += nOutSize;
  int nStudentCount = doParseInt(inList, nOutSize);
  inList += nOutSize;
  if (nRoomID < 0 || nTeacherCount < 0 || nStudentCount < 0)
    return;
  // 如果讲师数|学生数都是零，直接删除对应房间号...
  if (nTeacherCount <= 0 && nStudentCount <= 0) {
    GetApp()->doDeleteRoom(nRoomID);
    return;
  }
  // 创建或更新房间对象，创建成功，更新信息...
  CUdpServer * lpUdpServer = GetApp()->doFindUdpServer(m_nConnFD);
  CTCPRoom * lpRoom = GetApp()->doCreateRoom(nRoomID, lpUdpServer);
  if( lpRoom != NULL ) {
    lpRoom->m_nTeacherCount = nTeacherCount;
    lpRoom->m_nStudentCount = nStudentCount;
  }
  // 将房间号码添加到临时房间列表当中...
  lpUdpServer->m_MapInt[nRoomID] = nRoomID;
}

// 处理UdpServer的在线汇报事件...
int CTCPClient::doCmdUdpServerOnLine()
{
  // 通过套接字编号查找服务器对象，没有找到，直接返回...
  CUdpServer * lpUdpServer = GetApp()->doFindUdpServer(m_nConnFD);
  // 如果没有包含room_list字段 => 清除当前服务器下所有的房间...
  if( m_MapJson.find("room_list") == m_MapJson.end() ) {
    GetApp()->doDeleteRoom(lpUdpServer);
    return -1;
  }
  // 如果没有找到服务器，返回...
  if( lpUdpServer == NULL )
    return -1;
  // 先清理临时房间列表...
  lpUdpServer->m_MapInt.clear();
  // 获取房间列表字符串内容...
  string strRoomList = m_MapJson["room_list"];
  char * lpList = (char*)strRoomList.c_str();
  char * ptrLine = strtok(lpList, "|");
  while (ptrLine != NULL) {
    this->doParseRoom(ptrLine);
    ptrLine = strtok(NULL, "|");
  }
  // 调用服务器接口，删除已经死亡的房间对象...
  lpUdpServer->doEarseDeadRoom();
  return 0;
}

// 处理UdpServer汇报的添加老师命令...
int CTCPClient::doCmdUdpServerAddTeacher()
{
  // 判断传递JSON数据有效性 => 必须包含room_id字段信息...
  if( m_MapJson.find("room_id") == m_MapJson.end() ) {
    return -1;
  }
  // 通过套接字编号查找服务器对象，没有找到，直接返回...
  CUdpServer * lpUdpServer = GetApp()->doFindUdpServer(m_nConnFD);
  if( lpUdpServer == NULL )
    return -1;
  // 通知服务器对象，指定房间里的教师引用计数增加...
  int nRoomID = atoi(m_MapJson["room_id"].c_str());
  lpUdpServer->doAddTeacher(nRoomID);
  return 0;
}

// 处理UdpServer汇报的删除老师命令...
int CTCPClient::doCmdUdpServerDelTeacher()
{
  // 判断传递JSON数据有效性 => 必须包含room_id字段信息...
  if( m_MapJson.find("room_id") == m_MapJson.end() ) {
    return -1;
  }
  // 通过套接字编号查找服务器对象，没有找到，直接返回...
  CUdpServer * lpUdpServer = GetApp()->doFindUdpServer(m_nConnFD);
  if( lpUdpServer == NULL )
    return -1;
  // 通知服务器对象，指定房间里的教师引用计数减少...
  int nRoomID = atoi(m_MapJson["room_id"].c_str());
  lpUdpServer->doDelTeacher(nRoomID);
  return 0;
}

// 处理UdpServer汇报的添加学生命令...
int CTCPClient::doCmdUdpServerAddStudent()
{
  // 判断传递JSON数据有效性 => 必须包含room_id字段信息...
  if( m_MapJson.find("room_id") == m_MapJson.end() ) {
    return -1;
  }
  // 通过套接字编号查找服务器对象，没有找到，直接返回...
  CUdpServer * lpUdpServer = GetApp()->doFindUdpServer(m_nConnFD);
  if( lpUdpServer == NULL )
    return -1;
  // 通知服务器对象，指定房间里的学生引用计数增加...
  int nRoomID = atoi(m_MapJson["room_id"].c_str());
  lpUdpServer->doAddStudent(nRoomID);
  return 0;
}

// 处理UdpServer汇报的删除学生命令...
int CTCPClient::doCmdUdpServerDelStudent()
{
  // 判断传递JSON数据有效性 => 必须包含room_id字段信息...
  if( m_MapJson.find("room_id") == m_MapJson.end() ) {
    return -1;
  }
  // 通过套接字编号查找服务器对象，没有找到，直接返回...
  CUdpServer * lpUdpServer = GetApp()->doFindUdpServer(m_nConnFD);
  if( lpUdpServer == NULL )
    return -1;
  // 通知服务器对象，指定房间里的学生引用计数减少...
  int nRoomID = atoi(m_MapJson["room_id"].c_str());
  lpUdpServer->doDelStudent(nRoomID);
  return 0;
}

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
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 注意：目前这种通过m_strSend中转发送缓存，存在一定的风险，多个线程同时发送可能就会发生命令丢失的问题...
// 注意：判断的标准是m_strSend是否为空，不为空，说明数据还没有被发走，因此，这里需要改进，改动比较大...
// 注意：epoll_event.data 是union类型，里面的4个变量不能同时使用，只能使用一个，目前我们用的是fd
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 统一的通用命令发送接口函数...
int CTCPClient::doSendCommonCmd(int nCmdID, const char * lpJsonPtr/* = NULL*/, int nJsonSize/* = 0*/)
{
  // 构造回复结构头信息...
  Cmd_Header theHeader = {0};
  theHeader.m_pkg_len = ((lpJsonPtr != NULL) ? nJsonSize : 0);
  theHeader.m_type = m_nClientType;
  theHeader.m_cmd  = nCmdID;
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
    log_trace("parse json data error");
    return -1;
  }
  // check the json type => must be json_type_object...
  json_type nJsonType = json_object_get_type(new_obj);
  if( nJsonType != json_type_object ) {
    log_trace("parse json data error");
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
