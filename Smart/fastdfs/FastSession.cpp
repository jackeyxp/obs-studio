
#include "obs-app.hpp"
#include "FastSession.h"
#include <winsock2.h>
#include <QFile>

#pragma comment(lib, "WS2_32.Lib")

void long2buff(int64_t n, char *buff)
{
	unsigned char *p;
	p = (unsigned char *)buff;
	*p++ = (n >> 56) & 0xFF;
	*p++ = (n >> 48) & 0xFF;
	*p++ = (n >> 40) & 0xFF;
	*p++ = (n >> 32) & 0xFF;
	*p++ = (n >> 24) & 0xFF;
	*p++ = (n >> 16) & 0xFF;
	*p++ = (n >> 8) & 0xFF;
	*p++ = n & 0xFF;
}

int64_t buff2long(const char *buff)
{
	unsigned char *p;
	p = (unsigned char *)buff;
	return  (((int64_t)(*p)) << 56) | \
		(((int64_t)(*(p + 1))) << 48) | \
		(((int64_t)(*(p + 2))) << 40) | \
		(((int64_t)(*(p + 3))) << 32) | \
		(((int64_t)(*(p + 4))) << 24) | \
		(((int64_t)(*(p + 5))) << 16) | \
		(((int64_t)(*(p + 6))) << 8) | \
		((int64_t)(*(p + 7)));
}

const char * get_command_name(int inCmd)
{
	switch (inCmd)
	{
	case kCmd_Smart_Login:          return "Smart_Login";
	case kCmd_Smart_OnLine:         return "Smart_OnLine";
	case kCmd_UdpServer_Login:      return "UdpServer_Login";
	case kCmd_UdpServer_OnLine:     return "UdpServer_OnLine";
	case kCmd_UdpServer_AddTeacher: return "UdpServer_AddTeacher";
	case kCmd_UdpServer_DelTeacher: return "UdpServer_DelTeacher";
	case kCmd_UdpServer_AddStudent: return "UdpServer_AddStudent";
	case kCmd_UdpServer_DelStudent: return "UdpServer_DelStudent";
	case kCmd_PHP_GetUdpServer:     return "PHP_GetUdpServer";
	case kCmd_PHP_GetAllServer:     return "PHP_GetAllServer";
	case kCmd_PHP_GetAllClient:     return "PHP_GetAllClient";
	case kCmd_PHP_GetRoomList:      return "PHP_GetRoomList";
	case kCmd_PHP_GetPlayerList:    return "PHP_GetPlayerList";
	case kCmd_PHP_Bind_Mini:        return "PHP_Bind_Mini";
	case kCmd_PHP_GetRoomFlow:      return "PHP_GetRoomFlow";
	case kCmd_Screen_Login:         return "Screen_Login";
	case kCmd_Screen_OnLine:        return "Screen_OnLine";
	case kCmd_Screen_Packet:        return "Screen_Packet";
	case kCmd_Screen_Finish:        return "Screen_Finish";
	}
	return "unknown";
}

CFastSession::CFastSession()
  : m_bIsConnected(false)
  , m_TCPSocket(NULL)
  , m_nErrorCode(-1)
  , m_nPort(0)
{
}

CFastSession::~CFastSession()
{
	this->closeSocket();
}

void CFastSession::closeSocket()
{
	// 这里必须同时先调用close()再调用disconnect，否则有内存泄漏...
	if (m_TCPSocket != NULL) {
		m_TCPSocket->close();
		m_TCPSocket->disconnectFromHost();
		delete m_TCPSocket;
		m_TCPSocket = NULL;
	}
	// 重置已链接标志...
	m_bIsConnected = false;
}

bool CFastSession::InitSession(const char * lpszAddr, int nPort)
{
	if (m_TCPSocket != NULL) {
		delete m_TCPSocket;
		m_TCPSocket = NULL;
	}
	m_nPort = nPort;
	m_strAddress.assign(lpszAddr);
	m_TCPSocket = new QTcpSocket();
	connect(m_TCPSocket, SIGNAL(connected()), this, SLOT(onConnected()));
	connect(m_TCPSocket, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
	connect(m_TCPSocket, SIGNAL(disconnected()), this, SLOT(onDisConnected()));
	connect(m_TCPSocket, SIGNAL(bytesWritten(qint64)), this, SLOT(onBytesWritten(qint64)));
	connect(m_TCPSocket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onError(QAbstractSocket::SocketError)));
	m_TCPSocket->connectToHost(QString::fromUtf8(lpszAddr), nPort);
	return true;
}

/*CTrackerSession::CTrackerSession()
{
	m_nGroupCount = 0;
	m_lpGroupStat = NULL;
	memset(&m_NewStorage, 0, sizeof(m_NewStorage));
	memset(&m_TrackerCmd, 0, sizeof(m_TrackerCmd));
}

CTrackerSession::~CTrackerSession()
{
	// 先关闭套接字...
	this->closeSocket();
	// 再关闭分配的对象...
	if (m_lpGroupStat != NULL) {
		delete[] m_lpGroupStat;
		m_lpGroupStat = NULL;
		m_nGroupCount = 0;
	}
}

void CTrackerSession::SendCmd(char inCmd)
{
	int nSendSize = sizeof(m_TrackerCmd);
	memset(&m_TrackerCmd, 0, nSendSize);
	m_TrackerCmd.cmd = inCmd;
	m_TCPSocket->write((char*)&m_TrackerCmd, nSendSize);
}

void CTrackerSession::onConnected()
{
	this->SendCmd(TRACKER_PROTO_CMD_SERVICE_QUERY_STORE_WITHOUT_GROUP_ONE);
}

void CTrackerSession::doStorageWanAddr()
{
	// 将获取的Storage的IP地址进行转换判断...
	uint32_t nHostAddr = ntohl(inet_addr(m_NewStorage.ip_addr));
	// 检查是否是以下三类内网地址...
	// A类：10.0.0.0 ~ 10.255.255.255
	// B类：172.16.0.0 ~ 172.31.255.255
	// C类：192.168.0.0 ~ 192.168.255.255
	if ((nHostAddr >= 0x0A000000 && nHostAddr <= 0x0AFFFFFF) ||
		(nHostAddr >= 0xAC100000 && nHostAddr <= 0xAC1FFFFF) ||
		(nHostAddr >= 0xC0A80000 && nHostAddr <= 0xC0A8FFFF))
	{
		// 如果是内网地址，替换成tracker地址...
		strcpy(m_NewStorage.ip_addr, m_strAddress.c_str());
		blog(LOG_INFO, "Adjust => Group = %s, Storage = %s:%d, PathIndex = %d", 
			 m_NewStorage.group_name, m_NewStorage.ip_addr, m_NewStorage.port,
			 m_NewStorage.store_path_index);
	}
}

void CTrackerSession::onReadyRead()
{
	// 从网络层读取所有的缓冲区，并将缓冲区连接起来...
	QByteArray theBuffer = m_TCPSocket->readAll();
	m_strRecv.append(theBuffer.toStdString());
	// 得到的数据长度不够，直接返回，等待新数据...
	int nCmdLength = sizeof(TrackerHeader);
	if (m_strRecv.size() < nCmdLength)
		return;
	// 对命令头进行分发处理...
	if (m_TrackerCmd.cmd == TRACKER_PROTO_CMD_SERVICE_QUERY_STORE_WITHOUT_GROUP_ONE) {
		// 得到的数据长度不够，直接报错，等待新的链接接入，Storage上线后不会主动汇报...
		if (m_strRecv.size() < (nCmdLength + TRACKER_QUERY_STORAGE_STORE_BODY_LEN))
			return;
		// 对获取的数据进行转移处理...
		char * in_buff = (char*)m_strRecv.c_str() + nCmdLength;
		memset(&m_NewStorage, 0, sizeof(m_NewStorage));
		memcpy(m_NewStorage.group_name, in_buff, FDFS_GROUP_NAME_MAX_LEN);
		memcpy(m_NewStorage.ip_addr, in_buff + FDFS_GROUP_NAME_MAX_LEN, IP_ADDRESS_SIZE - 1);
		m_NewStorage.port = (int)buff2long(in_buff + FDFS_GROUP_NAME_MAX_LEN + IP_ADDRESS_SIZE - 1);
		m_NewStorage.store_path_index = (int)(*(in_buff + FDFS_GROUP_NAME_MAX_LEN + IP_ADDRESS_SIZE - 1 + FDFS_PROTO_PKG_LEN_SIZE)); // 内存中只有一个字节...
		blog(LOG_INFO, "Source => Group = %s, Storage = %s:%d, PathIndex = %d", m_NewStorage.group_name, m_NewStorage.ip_addr, m_NewStorage.port, m_NewStorage.store_path_index);
		// 分析并判断Storage地址是否是内网地址，如果是内网地址，就用tracker地址替换之...
		this->doStorageWanAddr();
		// 将缓冲区进行减少处理...
		m_strRecv.erase(0, nCmdLength + TRACKER_QUERY_STORAGE_STORE_BODY_LEN);
		// 发起新的命令 => 查询所有的组列表...
		return this->SendCmd(TRACKER_PROTO_CMD_SERVER_LIST_ALL_GROUPS);
	} else if (m_TrackerCmd.cmd == TRACKER_PROTO_CMD_SERVER_LIST_ALL_GROUPS) {
		// 得到的数据长度不够，直接返回，等待新数据...
		int in_bytes = m_strRecv.size() - nCmdLength;
		if (in_bytes % sizeof(TrackerGroupStat) != 0)
			return;
		m_nGroupCount = in_bytes / sizeof(TrackerGroupStat);
		m_lpGroupStat = new FDFSGroupStat[m_nGroupCount];
		memset(m_lpGroupStat, 0, sizeof(FDFSGroupStat) * m_nGroupCount);
		TrackerGroupStat * pSrc = (TrackerGroupStat*)(m_strRecv.c_str() + nCmdLength);
		TrackerGroupStat * pEnd = pSrc + m_nGroupCount;
		FDFSGroupStat * pDest = m_lpGroupStat;
		for (; pSrc < pEnd; pSrc++)
		{
			memcpy(pDest->group_name, pSrc->group_name, FDFS_GROUP_NAME_MAX_LEN);
			pDest->total_mb = buff2long(pSrc->sz_total_mb);
			pDest->free_mb = buff2long(pSrc->sz_free_mb);
			pDest->trunk_free_mb = buff2long(pSrc->sz_trunk_free_mb);
			pDest->count = buff2long(pSrc->sz_count);
			pDest->storage_port = buff2long(pSrc->sz_storage_port);
			pDest->storage_http_port = buff2long(pSrc->sz_storage_http_port);
			pDest->active_count = buff2long(pSrc->sz_active_count);
			pDest->current_write_server = buff2long(pSrc->sz_current_write_server);
			pDest->store_path_count = buff2long(pSrc->sz_store_path_count);
			pDest->subdir_count_per_path = buff2long(pSrc->sz_subdir_count_per_path);
			pDest->current_trunk_file_id = buff2long(pSrc->sz_current_trunk_file_id);

			pDest++;
		}
		// 设置已连接标志...
		m_bIsConnected = true;
	}
}

void CTrackerSession::onDisConnected()
{
	m_bIsConnected = false;
}

void CTrackerSession::onBytesWritten(qint64 nBytes)
{
}

void CTrackerSession::onError(QAbstractSocket::SocketError nError)
{
	// 发生错误，设置未连接标志...
	m_bIsConnected = false;
	m_nErrorCode = nError;
}

CStorageSession::CStorageSession()
  : m_bCanReBuild(false)
  , m_llFileSize(0)
  , m_llLeftSize(0)
  , m_lpFile(NULL)
{
	memset(&m_NewStorage, 0, sizeof(m_NewStorage));
}

void CStorageSession::CloseUpFile()
{
	if (m_lpFile != NULL) {
		fclose(m_lpFile);
		m_lpFile = NULL;
	}
}

CStorageSession::~CStorageSession()
{
	// 先关闭套接字...
	this->closeSocket();
	// 再关闭文件对象...
	this->CloseUpFile();
	// 打印当前正在上传的文件和位置...
	// 没有上传完毕的文件，fdfs-storage会自动(回滚)删除服务器端副本，下次再上传时重新上传。。。
	// STORAGE_PROTO_CMD_UPLOAD_FILE 与 STORAGE_PROTO_CMD_APPEND_FILE 无区别...
	if (m_llLeftSize > 0) {
		blog(LOG_INFO, "~CStorageSession: File = %s, Left = %I64d, Size = %I64d", m_strFilePath.c_str(), m_llLeftSize, m_llFileSize);
	}
}
//
// 重建会话...
bool CStorageSession::ReBuildSession(StorageServer * lpStorage, const char * lpszFilePath)
{
	// 判断输入参数是否有效...
	if (lpStorage == NULL || lpszFilePath == NULL) {
		blog(LOG_INFO, "ReBuildSession: input param is NULL");
		return false;
	}
	// 关闭套接字和文件...
	this->closeSocket();
	this->CloseUpFile();
	// 保存数据，打开文件...
	m_NewStorage = *lpStorage;
	m_strFilePath.assign(lpszFilePath);
	m_strExtend.assign(strrchr(lpszFilePath, '.') + 1);
	m_lpFile = os_fopen(m_strFilePath.c_str(), "rb");
	if (m_lpFile == NULL) {
		blog(LOG_INFO, "ReBuildSession: os_fopen is NULL");
		return false;
	}
	// 保存文件总长度...
	m_llFileSize = os_fgetsize(m_lpFile);
	m_llLeftSize = m_llFileSize;
	// 初始化上传会话对象失败，复原重建标志...
	if (!this->InitSession(m_NewStorage.ip_addr, m_NewStorage.port)) {
		this->m_bCanReBuild = true;
		return false;
	}
	// 重建成功，返回正常...
	return true;
}
//
// 发送指令信息头数据包...
bool CStorageSession::SendCmdHeader()
{
	// 准备需要的变量...
	const char * lpszExt = m_strExtend.c_str();
	int64_t  llSize = m_llFileSize;
	qint64	 nReturn = 0;
	qint64   nLength = 0;
	char  *  lpBuf = NULL;
	char     szBuf[_MAX_PATH] = { 0 };
	int      n_pkg_len = 1 + sizeof(int64_t) + FDFS_FILE_EXT_NAME_MAX_LEN;
	// 指令包长度 => 目录编号(1) + 文件长度(8) + 文件扩展名(6)

	// 填充指令头 => 指令编号 + 状态 + 指令包长度(不含指令头)...
	// STORAGE_PROTO_CMD_UPLOAD_FILE 与 STORAGE_PROTO_CMD_APPEND_FILE 无区别...
	// 没有上传完毕的文件，fdfs-storage会自动(回滚)删除服务器端副本，下次再上传时重新上传。。。
	TrackerHeader * lpHeader = (TrackerHeader*)szBuf;
	lpHeader->cmd = STORAGE_PROTO_CMD_UPLOAD_FILE;
	lpHeader->status = 0;
	long2buff(llSize + n_pkg_len, lpHeader->pkg_len);

	// 填充指令包数据...
	lpBuf = szBuf + sizeof(TrackerHeader);							// 将指针定位到数据区...
	lpBuf[0] = m_NewStorage.store_path_index;						// 目录编号   => 0 - 1  => 1个字节
	long2buff(llSize, lpBuf + 1);									// 文件长度   => 1 - 9  => 8个字节 ULONGLONG
	memcpy(lpBuf + 1 + sizeof(int64_t), lpszExt, strlen(lpszExt));	// 文件扩展名 => 9 - 15 => 6个字节 FDFS_FILE_EXT_NAME_MAX_LEN

	// 发送上传指令头和指令包数据 => 命令头 + 数据长度...
	nLength = sizeof(TrackerHeader) + n_pkg_len;
	nReturn = m_TCPSocket->write(szBuf, nLength);
	// 当前正在发送的缓冲区大小...
	m_strCurData.append(szBuf, nLength);
	m_llLeftSize += nLength;
	return true;
}

void CStorageSession::onConnected()
{
	m_bIsConnected = true;
	this->SendCmdHeader();
}

void CStorageSession::onReadyRead()
{
	// 如果已经处于重建状态，直接返回...
	if (m_bCanReBuild) return;
	// 从网络层读取所有的缓冲区，并将缓冲区连接起来...
	QByteArray theBuffer = m_TCPSocket->readAll();
	m_strRecv.append(theBuffer.toStdString());
	// 得到的数据长度不够，等待新的接收命令...
	int nCmdLength = sizeof(TrackerHeader);
	if (m_strRecv.size() < nCmdLength)
		return;
	int nDataSize = m_strRecv.size() - nCmdLength;
	const char * lpszDataBuf = m_strRecv.c_str() + nCmdLength;
	TrackerHeader * lpHeader = (TrackerHeader*)m_strRecv.c_str();
	// 判断服务器返回的状态是否正确...
	if (lpHeader->status != 0) {
		blog(LOG_INFO, "onReadyRead: status error");
		m_bCanReBuild = true;
		return;
	}
	// 判断返回的数据区长度是否正确 => 必须大于 FDFS_GROUP_NAME_MAX_LEN
	if (nDataSize <= FDFS_GROUP_NAME_MAX_LEN) {
		blog(LOG_INFO, "onReadyRead: GROUP NAME error");
		m_bCanReBuild = true;
		return;
	}
	char szFileFDFS[_MAX_PATH] = { 0 };
	char group_name[FDFS_GROUP_NAME_MAX_LEN + 1] = { 0 };
	char remote_filename[FDFS_REMOTE_NAME_MAX_SIZE] = { 0 };
	memcpy(group_name, lpszDataBuf, FDFS_GROUP_NAME_MAX_LEN);
	memcpy(remote_filename, lpszDataBuf + FDFS_GROUP_NAME_MAX_LEN, nDataSize - FDFS_GROUP_NAME_MAX_LEN + 1);
	sprintf(szFileFDFS, "%s/%s", group_name, remote_filename);
	// 关闭上传文件句柄...
	this->CloseUpFile();
	// 向网站汇报并保存FDFS文件记录...
	App()->doWebSaveFDFS((char*)m_strFilePath.c_str(), szFileFDFS, m_llFileSize);
	// 打印上传结果，删除已上传文件...
	blog(LOG_INFO, "Local = %s, Remote = %s, Size = %I64d\n", m_strFilePath.c_str(), szFileFDFS, m_llFileSize);
	if (os_unlink(m_strFilePath.c_str()) != 0) {
		blog(LOG_INFO, "DeleteFile failed!");
	}
	// 重置接收缓冲区，等待新的数据...
	m_strRecv.clear();
	// 进行复位操作，等待新的截图到达...
	m_bCanReBuild = true;
}

void CStorageSession::onDisConnected()
{
	m_bCanReBuild = true;
	m_bIsConnected = false;
	blog(LOG_INFO, "[CStorageSession] - onDisConnected");
}

void CStorageSession::onBytesWritten(qint64 nBytes)
{
	// 如果已经处于重建状态，直接返回...
	if (m_bCanReBuild) return;
	// 如果发送数据包失败，进行复位操作，等待新的截图到达...
	if (!this->SendNextPacket(nBytes)) {
		m_bCanReBuild = true;
	}
}
//
// 发送一个有效数据包...
bool CStorageSession::SendNextPacket(int64_t inLastBytes)
{
	// 没有数据要发送了...
	if (m_llLeftSize <= 0)
		return true;
	// 如果文件句柄已经关闭了...
	if (m_lpFile == NULL) {
		blog(LOG_INFO, "SendNextPacket: File = %s, Left = %I64d, Size = %I64d", m_strFilePath.c_str(), m_llLeftSize, m_llFileSize);
		return false;
	}
	// 先删除已经发送成功的缓冲区...
	m_strCurData.erase(0, inLastBytes);
	m_llLeftSize -= inLastBytes;
	// 如果已经发送完毕，直接返回，等待通知...
	if (m_llLeftSize <= 0)
		return true;
	// 从文件中读取一个数据包的数据量...
	char szBuffer[kPackSize] = { 0 };
	int nReadSize = fread(szBuffer, 1, kPackSize, m_lpFile);
	// 如果文件读取失败，返回错误...
	if (nReadSize <= 0) {
		blog(LOG_INFO, "SendNextPacket: fread error");
		return false;
	}
	// 保存缓冲区，计算要发送的长度...
	m_strCurData.append(szBuffer, nReadSize);
	int nCurSize = m_strCurData.size();
	int nSendSize = ((nCurSize >= kPackSize) ? kPackSize : nCurSize);
	// 发起新的发送任务...
	int nReturn = m_TCPSocket->write(m_strCurData.c_str(), nSendSize);
	return true;
}

void CStorageSession::onError(QAbstractSocket::SocketError nError)
{
	m_nErrorCode = nError;
	m_bCanReBuild = true;
	m_bIsConnected = false;
	blog(LOG_INFO, "[CStorageSession] - onError: %d", nError);
}*/

CRemoteSession::CRemoteSession()
  : m_bCanReBuild(false)
{
}

CRemoteSession::~CRemoteSession()
{
	blog(LOG_INFO, "[~CRemoteSession] - Exit");
}

void CRemoteSession::onConnected()
{
	// 设置链接成功标志...
	m_bIsConnected = true;
	// 链接成功，立即发送登录命令 => Cmd_Header + JSON...
	this->SendLoginCmd();
}

bool CRemoteSession::doParseJson(const char * lpData, int nSize, Json::Value & outValue)
{
	if (nSize <= 0 || lpData == NULL)
		return false;
	string strUTF8Data;
	Json::Reader reader;
	strUTF8Data.assign(lpData, nSize);
	return reader.parse(strUTF8Data, outValue);
}

void CRemoteSession::onReadyRead()
{
	// 如果已经处于重建状态，直接返回...
	if (m_bCanReBuild)
		return;
	// 从网络层读取所有的缓冲区，并将缓冲区连接起来...
	QByteArray theBuffer = m_TCPSocket->readAll();
	m_strRecv.append(theBuffer.toStdString());
	// 这里网络数据会发生粘滞现象，因此，需要循环执行...
	while (m_strRecv.size() > 0) {
		// 得到的数据长度不够，直接返回，等待新数据...
		int nCmdLength = sizeof(Cmd_Header);
		if (m_strRecv.size() < nCmdLength)
			return;
		ASSERT(m_strRecv.size() >= nCmdLength);
		Cmd_Header * lpCmdHeader = (Cmd_Header*)m_strRecv.c_str();
		const char * lpDataPtr = m_strRecv.c_str() + sizeof(Cmd_Header);
		int nDataSize = m_strRecv.size() - sizeof(Cmd_Header);
		// 已获取的数据长度不够，直接返回，等待新数据...
		if (nDataSize < lpCmdHeader->m_pkg_len)
			return;
		ASSERT(nDataSize >= lpCmdHeader->m_pkg_len);
		// 打印远程控制会话对象接收到的TCP网络命令信息...
		blog(LOG_INFO, "[RemoteSession] Command-Recv: %s", get_command_name(lpCmdHeader->m_cmd));
		// 开始处理中转服务器发来的命令...
		bool bResult = false;
		switch(lpCmdHeader->m_cmd)
		{
		case kCmd_Smart_Login:         bResult = this->doCmdSmartLogin(lpDataPtr, lpCmdHeader->m_pkg_len); break;
		case kCmd_Smart_OnLine:        bResult = this->doCmdSmartOnLine(lpDataPtr, lpCmdHeader->m_pkg_len); break;
		//case kCmd_UDP_Logout:        bResult = this->doCmdUdpLogout(lpDataPtr, lpCmdHeader->m_pkg_len); break;
		//case kCmd_Camera_LiveStop:   bResult = this->doCmdTeacherCameraLiveStop(lpDataPtr, lpCmdHeader->m_pkg_len); break;
		//case kCmd_Camera_OnLineList: bResult = this->doCmdTeacherCameraList(lpDataPtr, lpCmdHeader->m_pkg_len); break;
		//case kCmd_Screen_Packet:     bResult = this->doCmdScreenPacket(lpDataPtr, lpCmdHeader); break;
		//case kCmd_Screen_Finish:     bResult = this->doCmdScreenFinish(lpDataPtr, lpCmdHeader); break;
		}
		// 删除已经处理完毕的数据 => Header + pkg_len...
		m_strRecv.erase(0, lpCmdHeader->m_pkg_len + sizeof(Cmd_Header));
		// 如果还有数据，则继续解析命令...
	}
}

bool CRemoteSession::doCmdSmartLogin(const char * lpData, int nSize)
{
	Json::Value value;
	// 进行Json数据包的内容解析...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CRemoteSession::doParseJson Error!");
		return false;
	}
	// 获取远程连接在服务器端的TCP套接字 => 讲师端|学生端都会收到...
	int nTCPSocketFD = atoi(OBSApp::getJsonString(value["tcp_socket"]).c_str());
	// 将获取的TCP套接字更新到系统变量当中 => 在创建UDP连接时会用到...
	if (App()->GetRemoteTcpSockFD() != nTCPSocketFD) {
		App()->SetRemoteTcpSockFD(nTCPSocketFD);
	}
	// 打印获取到的远程tcp套接字的编号...
	blog(LOG_INFO, "[RemoteSession] doCmdSmartLogin => tcp_socket: %d", nTCPSocketFD);
	// 如果是讲师端，可以直接返回了...
	if (App()->GetClientType() == kClientTeacher)
		return true;
	// 针对学生端，需要获取更多的信息 => TCP讲师端和UDP讲师端是否在线...
	bool bIsTCPTeacherOnLine = atoi(OBSApp::getJsonString(value["tcp_teacher"]).c_str());
	bool bIsUDPTeacherOnLine = atoi(OBSApp::getJsonString(value["udp_teacher"]).c_str());
	int  nDBFlowTeacherID = atoi(OBSApp::getJsonString(value["flow_teacher"]).c_str());
	// 保存关联的讲师流量记录编号 => 不一致，并且有效时才更新...
	if (nDBFlowTeacherID > 0 && App()->GetDBFlowTeacherID() != nDBFlowTeacherID) {
		App()->SetDBFlowTeacherID(nDBFlowTeacherID);
	}
	return true;

	/*int nDBCameraID = atoi(OBSApp::getJsonString(value["camera_id"]).c_str());
	bool bIsCameraOnLine = atoi(OBSApp::getJsonString(value["udp_camera"]).c_str());
	// 打印命令反馈详情信息 => 只有摄像头通道编号大于0时，才需要反馈给主界面进行拉流或断流操作...
	blog(LOG_INFO, "[RemoteSession] doCmdSmartLogin => tcp_socket: %d, CameraID: %d, OnLine: %d", nTCPSocketFD, nDBCameraID, bIsCameraOnLine);
	// 根据摄像头在线状态，进行rtp_source资源拉流线程的创建或删除...
	if (nDBCameraID > 0) {
		emit this->doTriggerRtpSource(nDBCameraID, bIsCameraOnLine);
	}return true;*/
}

bool CRemoteSession::doCmdSmartOnLine(const char * lpData, int nSize)
{
	// 如果是讲师端，可以直接返回了...
	if (App()->GetClientType() == kClientTeacher)
		return true;
	// 针对学生端，需要获取更多的信息...
	Json::Value value;
	// 进行Json数据包的内容解析...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CRemoteSession::doParseJson Error!");
		return false;
	}
	// 获取关联的TCP讲师端的流量编号...
	int  nDBFlowTeacherID = atoi(OBSApp::getJsonString(value["flow_teacher"]).c_str());
	// 保存关联的讲师流量记录编号 => 不一致，并且有效时才更新...
	if (nDBFlowTeacherID > 0 && App()->GetDBFlowTeacherID() != nDBFlowTeacherID) {
		App()->SetDBFlowTeacherID(nDBFlowTeacherID);
	}
	return true;
}

/*bool CRemoteSession::doCmdScreenPacket(const char * lpData, Cmd_Header * lpCmdHeader)
{
	if (lpData == NULL || lpCmdHeader == NULL)
		return false;
	int nScreenID = lpCmdHeader->m_sock;
	int nDataSize = lpCmdHeader->m_pkg_len;
	// 如果通过屏幕编号，找到了对应的数据块，直接追加更新...
	GM_MapScreen::iterator itorItem = m_MapScreen.find(nScreenID);
	if (itorItem != m_MapScreen.end()) {
		string & strData = itorItem->second;
		strData.append(lpData, nDataSize);
		return true;
	}
	// 如果没有找到，增加一条新纪录...
	string strValue(lpData, nDataSize);
	m_MapScreen[nScreenID] = strValue;
	return true;
}

bool CRemoteSession::doCmdScreenFinish(const char * lpData, Cmd_Header * lpCmdHeader)
{
	if (lpData == NULL || lpCmdHeader == NULL)
		return false;
	int nDataSize = lpCmdHeader->m_pkg_len;
	Json::Value value;
	// 进行Json数据包的内容解析...
	if (!this->doParseJson(lpData, nDataSize, value)) {
		blog(LOG_INFO, "CRemoteSession::doParseJson Error!");
		return false;
	}
	// 获取服务器发送过来的数据信息...
	int nScreenID = atoi(OBSApp::getJsonString(value["screen_id"]).c_str());
	string strUserName = OBSApp::getJsonString(value["user_name"]);
	// 在数据集合中查找需要的屏幕编号，没有找到，直接返回...
	GM_MapScreen::iterator itorItem = m_MapScreen.find(nScreenID);
	if (itorItem == m_MapScreen.end())
		return false;
	// 将数据拷贝出来之后，立即删除对应元素...
	string strData = itorItem->second;
	m_MapScreen.erase(nScreenID);
	// 将获取到的图像数据直接进行存盘操作...
	char path[512] = { 0 };
	if (GetConfigPath(path, sizeof(path), "obs-teacher/screen") <= 0)
		return false;
	// 构造jpg存盘文件全路径...
	QString strQFileName = QString("%1/%2_%3.jpg").arg(path).arg(strUserName.c_str()).arg(nScreenID);
	QFile theFile(strQFileName);
	// 打开jpg文件句柄 => 失败，直接返回...
	if (!theFile.open(QIODevice::WriteOnly))
		return false;
	// 直接存盘，然后关闭 => QFile的文件名不会乱码...
	theFile.write(strData.c_str(), strData.size());
	theFile.close();
	// 需要通过信号槽，通知界面层，更新学生屏幕分享数据源...
	emit doTriggerScreenFinish(nScreenID, QString("%1").arg(strUserName.c_str()), strQFileName);
	return true;
}

// 处理由服务器返回的学生端指定通道停止推流成功的事件通知...
bool CRemoteSession::doCmdTeacherCameraLiveStop(const char * lpData, int nSize)
{
	Json::Value value;
	// 进行Json数据包的内容解析...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CRemoteSession::doParseJson Error!");
		return false;
	}
	// 获取服务器发送过来的数据信息...
	int nDBCameraID = atoi(OBSApp::getJsonString(value["camera_id"]).c_str());
	// 通知主窗口界面层，停止指定通道成功完成...
	emit this->doTriggerCameraLiveStop(nDBCameraID);
	return true;
}

// 处理由服务器返回的讲师端所在房间的在线摄像头列表...
bool CRemoteSession::doCmdTeacherCameraList(const char * lpData, int nSize)
{
	Json::Value value;
	// 进行Json数据包的内容解析...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CRemoteSession::doParseJson Error!");
		return false;
	}
	// 进一步解析摄像头列表数组...
	int nListNum = atoi(OBSApp::getJsonString(value["list_num"]).c_str());
	if (nListNum <= 0) {
		blog(LOG_INFO, "=== No OnLine Camera ===");
		return false;
	}
	// 通知主窗口界面，已获取摄像头在线列表成功...
	emit this->doTriggerCameraList(value["list_data"]);
	return true;
}

bool CRemoteSession::doCmdUdpLogout(const char * lpData, int nSize)
{
	Json::Value value;
	// 进行Json数据包的内容解析...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CRemoteSession::doParseJson Error!");
		return false;
	}
	// 获取服务器发送过来的数据信息...
	int tmTag = atoi(OBSApp::getJsonString(value["tm_tag"]).c_str());
	int idTag = atoi(OBSApp::getJsonString(value["id_tag"]).c_str());
	int nDBCameraID = atoi(OBSApp::getJsonString(value["camera_id"]).c_str());
	// 通知主窗口界面层，UDP终端发生退出事件...
	emit this->doTriggerUdpLogout(tmTag, idTag, nDBCameraID);
	return true;
}*/

void CRemoteSession::onDisConnected()
{
	m_bCanReBuild = true;
	m_bIsConnected = false;
	blog(LOG_INFO, "[RemoteSession] - onDisConnected");
}

void CRemoteSession::onBytesWritten(qint64 nBytes)
{
}

void CRemoteSession::onError(QAbstractSocket::SocketError nError)
{
	m_nErrorCode = nError;
	m_bCanReBuild = true;
	m_bIsConnected = false;
	blog(LOG_INFO, "[RemoteSession] - onError: %d", nError);
}

// 通过中转服务器向学生端发送停止通道推流工作...
/*bool CRemoteSession::doSendCameraLiveStopCmd(int nDBCameraID)
{
	// 没有处于链接状态，直接返回...
	if (!m_bIsConnected)
		return false;
	// 组合命令需要的JSON数据包 => 通道编号|场景资源编号...
	string strJson;	Json::Value root;
	char szDataBuf[32] = { 0 };
	sprintf(szDataBuf, "%d", nDBCameraID);
	root["camera_id"] = szDataBuf;
	//sprintf(szDataBuf, "%d", nSceneItemID);
	//root["sitem_id"] = szDataBuf;
	strJson = root.toStyledString();
	// 打印json数据包，测试使用...
	//blog(LOG_INFO, "Json => %d, %s", strJson.size(), strJson.c_str());
	// 调用统一的接口进行命令数据的发送操作...
	return this->doSendCommonCmd(kCmd_Camera_LiveStop, strJson.c_str(), strJson.size());
}

// 通过中转服务器向学生端发送开启通道推流工作...
bool CRemoteSession::doSendCameraLiveStartCmd(int nDBCameraID)
{
	// 没有处于链接状态，直接返回...
	if (!m_bIsConnected)
		return false;
	// 组合命令需要的JSON数据包 => 通道编号|场景资源编号...
	string strJson;	Json::Value root;
	char szDataBuf[32] = { 0 };
	sprintf(szDataBuf, "%d", nDBCameraID);
	root["camera_id"] = szDataBuf;
	//sprintf(szDataBuf, "%d", nSceneItemID);
	//root["sitem_id"] = szDataBuf;
	strJson = root.toStyledString();
	// 调用统一的接口进行命令数据的发送操作...
	return this->doSendCommonCmd(kCmd_Camera_LiveStart, strJson.c_str(), strJson.size());
}

bool CRemoteSession::doSendCameraPTZCmd(int nDBCameraID, int nCmdID, int nSpeedVal)
{
	// 没有处于链接状态，直接返回...
	if (!m_bIsConnected)
		return false;
	// 组合命令需要的JSON数据包 => 通道编号|场景资源编号...
	string strJson;	Json::Value root;
	char szDataBuf[32] = { 0 };
	sprintf(szDataBuf, "%d", nDBCameraID);
	root["camera_id"] = szDataBuf;
	sprintf(szDataBuf, "%d", nCmdID);
	root["cmd_id"] = szDataBuf;
	sprintf(szDataBuf, "%d", nSpeedVal);
	root["speed_val"] = szDataBuf;
	strJson = root.toStyledString();
	// 调用统一的接口进行命令数据的发送操作...
	return this->doSendCommonCmd(kCmd_Camera_PTZCommand, strJson.c_str(), strJson.size());
}

bool CRemoteSession::doSendCameraPusherIDCmd(int nDBCameraID)
{
	// 没有处于链接状态，直接返回...
	if (!m_bIsConnected)
		return false;
	// 组合命令需要的JSON数据包 => 通道编号|场景资源编号...
	string strJson;	Json::Value root;
	char szDataBuf[32] = { 0 };
	sprintf(szDataBuf, "%d", nDBCameraID);
	root["camera_id"] = szDataBuf;
	strJson = root.toStyledString();
	// 调用统一的接口进行命令数据的发送操作...
	return this->doSendCommonCmd(kCmd_Camera_PusherID, strJson.c_str(), strJson.size());
}

// 获取讲师端所在房间的所有在线摄像头列表...
bool CRemoteSession::doSendCameraOnLineListCmd()
{
	// 没有处于链接状态，直接返回...
	if (!m_bIsConnected)
		return false;
	ASSERT(m_bIsConnected);
	// 调用统一的接口进行命令数据的发送操作...
	return this->doSendCommonCmd(kCmd_Camera_OnLineList);
}*/

// 每隔30秒发送在线命令包...
bool CRemoteSession::doSendOnLineCmd()
{
	// 没有处于链接状态，直接返回...
	if (!m_bIsConnected)
		return false;
	ASSERT(m_bIsConnected);
	// 调用统一的接口进行命令数据的发送操作...
	return this->doSendCommonCmd(kCmd_Smart_OnLine);
}

// 链接成功之后，发送登录命令...
bool CRemoteSession::SendLoginCmd()
{
	// 没有处于链接状态，直接返回...
	if (!m_bIsConnected)
		return false;
	// 组合Login命令需要的JSON数据包...
	char szDataBuf[32] = { 0 };
	string strJson;	Json::Value root;
	// 填充流量编号|mac地址|ip地址|房间编号|机器名称...
	sprintf(szDataBuf, "%d", App()->GetDBFlowID());
	root["flow_id"] = szDataBuf;
	root["mac_addr"] = App()->GetLocalMacAddr();
	root["ip_addr"] = App()->GetLocalIPAddr();
	root["room_id"] = App()->GetRoomIDStr();
	root["pc_name"] = OBSApp::GetServerDNSName();
	strJson = root.toStyledString();
	// 调用统一的接口进行命令数据的发送操作...
	return this->doSendCommonCmd(kCmd_Smart_Login, strJson.c_str(), strJson.size());
}

// 通用的命令发送接口...
bool CRemoteSession::doSendCommonCmd(int nCmdID, const char * lpJsonPtr/* = NULL*/, int nJsonSize/* = 0*/)
{
	// 打印远程控制会话对象发送的TCP网络命令信息...
	blog(LOG_INFO, "[RemoteSession] Command-Send: %s", get_command_name(nCmdID));
	// 组合命令包头 => 数据长度 | 终端类型 | 命令编号
	string     strBuffer;
	Cmd_Header theHeader = { 0 };
	theHeader.m_pkg_len = ((lpJsonPtr != NULL) ? nJsonSize : 0);
	theHeader.m_type = App()->GetClientType();
	theHeader.m_cmd = nCmdID;
	// 追加命令包头和命令数据包内容...
	strBuffer.append((char*)&theHeader, sizeof(theHeader));
	// 如果传入的数据内容有效，才进行数据的填充...
	if (lpJsonPtr != NULL && nJsonSize > 0) {
		strBuffer.append(lpJsonPtr, nJsonSize);
	}
	// 调用统一的发送接口...
	return this->SendData(strBuffer.c_str(), strBuffer.size());
}

// 统一的发送接口...
bool CRemoteSession::SendData(const char * lpDataPtr, int nDataSize)
{
	int nReturn = m_TCPSocket->write(lpDataPtr, nDataSize);
	return ((nReturn > 0) ? true : false);
}

CCenterSession::CCenterSession()
{
	m_nCenterTcpSocketFD = -1;
	m_uCenterTcpTimeID = 0;
}

CCenterSession::~CCenterSession()
{
	blog(LOG_INFO, "[~CCenterSession] - Exit");
}

void CCenterSession::onConnected()
{
	// 设置链接成功标志...
	m_bIsConnected = true;
	// 链接成功，立即发送登录命令 => 返回tcp_socket...
	this->doSendCommonCmd(kCmd_Smart_Login);
}

void CCenterSession::onReadyRead()
{
	// 从网络层读取所有的缓冲区，并将缓冲区连接起来...
	QByteArray theBuffer = m_TCPSocket->readAll();
	m_strRecv.append(theBuffer.toStdString());
	// 这里网络数据会发生粘滞现象，因此，需要循环执行...
	while (m_strRecv.size() > 0) {
		// 得到的数据长度不够，直接返回，等待新数据...
		int nCmdLength = sizeof(Cmd_Header);
		if (m_strRecv.size() < nCmdLength)
			return;
		ASSERT(m_strRecv.size() >= nCmdLength);
		Cmd_Header * lpCmdHeader = (Cmd_Header*)m_strRecv.c_str();
		const char * lpDataPtr = m_strRecv.c_str() + sizeof(Cmd_Header);
		int nDataSize = m_strRecv.size() - sizeof(Cmd_Header);
		// 已获取的数据长度不够，直接返回，等待新数据...
		if (nDataSize < lpCmdHeader->m_pkg_len)
			return;
		ASSERT(nDataSize >= lpCmdHeader->m_pkg_len);
		// 打印远程控制会话对象接收到的TCP网络命令信息...
		blog(LOG_INFO, "[CenterSession] Command-Recv: %s", get_command_name(lpCmdHeader->m_cmd));
		// 开始处理中转服务器发来的命令...
		bool bResult = false;
		switch (lpCmdHeader->m_cmd)
		{
		case kCmd_Smart_Login:     bResult = this->doCmdSmartLogin(lpDataPtr, lpCmdHeader->m_pkg_len); break;
		case kCmd_Smart_OnLine:    bResult = this->doCmdSmartOnLine(lpDataPtr, lpCmdHeader->m_pkg_len); break;
		case kCmd_PHP_Bind_Mini:   bResult = this->doCmdPHPBindMini(lpDataPtr, lpCmdHeader->m_pkg_len); break;
		}
		// 删除已经处理完毕的数据 => Header + pkg_len...
		m_strRecv.erase(0, lpCmdHeader->m_pkg_len + sizeof(Cmd_Header));
		// 如果还有数据，则继续解析命令...
	}
}

bool CCenterSession::doCmdPHPBindMini(const char * lpData, int nSize)
{
	Json::Value value;
	// 进行Json数据包的内容解析...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CCenterSession::doParseJson Error!");
		return false;
	}
	// 获取绑定命令的子命令 => 1(Scan),2(Save),3(Cancel)
	int nUserID = atoi(OBSApp::getJsonString(value["user_id"]).c_str());
	int nRoomID = atoi(OBSApp::getJsonString(value["room_id"]).c_str());
	int nBindCmd = atoi(OBSApp::getJsonString(value["bind_cmd"]).c_str());
	blog(LOG_INFO, "[CenterSession] doCmdPHPBindMini => user_id: %d, bind_cmd: %d, room_id: %d", nUserID, nBindCmd, nRoomID);
	// 向外层通知小程序通过PHP转发反馈的扫描绑定登录命令...
	emit this->doTriggerBindMini(nUserID, nBindCmd, nRoomID);
	return true;
}

bool CCenterSession::doCmdSmartLogin(const char * lpData, int nSize)
{
	Json::Value value;
	// 进行Json数据包的内容解析...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CCenterSession::doParseJson Error!");
		return false;
	}
	// 获取远程连接在服务器端的TCP套接字，并打印出来...
	m_nCenterTcpSocketFD = atoi(OBSApp::getJsonString(value["tcp_socket"]).c_str());
	m_uCenterTcpTimeID = (uint32_t)atoi(OBSApp::getJsonString(value["tcp_time"]).c_str());
	blog(LOG_INFO, "[CenterSession] doCmdSmartLogin => tcp_socket: %d, tcp_time: %lu", m_nCenterTcpSocketFD, m_uCenterTcpTimeID);
	// 向外层通知获取tcp_socket状态成功...
	emit this->doTriggerTcpConnect();
	return true;
}

bool CCenterSession::doCmdSmartOnLine(const char * lpData, int nSize)
{
	return true;
}

bool CCenterSession::doParseJson(const char * lpData, int nSize, Json::Value & outValue)
{
	if (nSize <= 0 || lpData == NULL)
		return false;
	string strUTF8Data;
	Json::Reader reader;
	strUTF8Data.assign(lpData, nSize);
	return reader.parse(strUTF8Data, outValue);
}

void CCenterSession::onDisConnected()
{
	m_bIsConnected = false;
	emit this->doTriggerTcpConnect();
	blog(LOG_INFO, "[CCenterSession] - onDisConnected");
}

void CCenterSession::onBytesWritten(qint64 nBytes)
{
}

void CCenterSession::onError(QAbstractSocket::SocketError nError)
{
	m_nErrorCode = nError;
	m_bIsConnected = false;
	emit this->doTriggerTcpConnect();
	blog(LOG_INFO, "[CCenterSession] - onError: %d", nError);
}

// 每隔30秒发送在线命令包...
bool CCenterSession::doSendOnLineCmd()
{
	// 没有处于链接状态，直接返回...
	if (!m_bIsConnected)
		return false;
	ASSERT(m_bIsConnected);
	// 调用统一的接口进行命令数据的发送操作...
	return this->doSendCommonCmd(kCmd_Smart_OnLine);
}

// 通用的命令发送接口...
bool CCenterSession::doSendCommonCmd(int nCmdID, const char * lpJsonPtr/* = NULL*/, int nJsonSize/* = 0*/)
{
	// 打印远程控制会话对象发送的TCP网络命令信息...
	blog(LOG_INFO, "[CenterSession] Command-Send: %s", get_command_name(nCmdID));
	// 组合命令包头 => 数据长度 | 终端类型 | 命令编号
	string     strBuffer;
	Cmd_Header theHeader = { 0 };
	theHeader.m_pkg_len = ((lpJsonPtr != NULL) ? nJsonSize : 0);
	theHeader.m_type = App()->GetClientType();
	theHeader.m_cmd = nCmdID;
	// 追加命令包头和命令数据包内容...
	strBuffer.append((char*)&theHeader, sizeof(theHeader));
	// 如果传入的数据内容有效，才进行数据的填充...
	if (lpJsonPtr != NULL && nJsonSize > 0) {
		strBuffer.append(lpJsonPtr, nJsonSize);
	}
	// 调用统一的发送接口...
	return this->SendData(strBuffer.c_str(), strBuffer.size());
}

// 统一的发送接口...
bool CCenterSession::SendData(const char * lpDataPtr, int nDataSize)
{
	int nReturn = m_TCPSocket->write(lpDataPtr, nDataSize);
	return ((nReturn > 0) ? true : false);
}