
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
	// �������ͬʱ�ȵ���close()�ٵ���disconnect���������ڴ�й©...
	if (m_TCPSocket != NULL) {
		m_TCPSocket->close();
		m_TCPSocket->disconnectFromHost();
		delete m_TCPSocket;
		m_TCPSocket = NULL;
	}
	// ���������ӱ�־...
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
	// �ȹر��׽���...
	this->closeSocket();
	// �ٹرշ���Ķ���...
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
	// ����ȡ��Storage��IP��ַ����ת���ж�...
	uint32_t nHostAddr = ntohl(inet_addr(m_NewStorage.ip_addr));
	// ����Ƿ�����������������ַ...
	// A�ࣺ10.0.0.0 ~ 10.255.255.255
	// B�ࣺ172.16.0.0 ~ 172.31.255.255
	// C�ࣺ192.168.0.0 ~ 192.168.255.255
	if ((nHostAddr >= 0x0A000000 && nHostAddr <= 0x0AFFFFFF) ||
		(nHostAddr >= 0xAC100000 && nHostAddr <= 0xAC1FFFFF) ||
		(nHostAddr >= 0xC0A80000 && nHostAddr <= 0xC0A8FFFF))
	{
		// �����������ַ���滻��tracker��ַ...
		strcpy(m_NewStorage.ip_addr, m_strAddress.c_str());
		blog(LOG_INFO, "Adjust => Group = %s, Storage = %s:%d, PathIndex = %d", 
			 m_NewStorage.group_name, m_NewStorage.ip_addr, m_NewStorage.port,
			 m_NewStorage.store_path_index);
	}
}

void CTrackerSession::onReadyRead()
{
	// ��������ȡ���еĻ�������������������������...
	QByteArray theBuffer = m_TCPSocket->readAll();
	m_strRecv.append(theBuffer.toStdString());
	// �õ������ݳ��Ȳ�����ֱ�ӷ��أ��ȴ�������...
	int nCmdLength = sizeof(TrackerHeader);
	if (m_strRecv.size() < nCmdLength)
		return;
	// ������ͷ���зַ�����...
	if (m_TrackerCmd.cmd == TRACKER_PROTO_CMD_SERVICE_QUERY_STORE_WITHOUT_GROUP_ONE) {
		// �õ������ݳ��Ȳ�����ֱ�ӱ����ȴ��µ����ӽ��룬Storage���ߺ󲻻������㱨...
		if (m_strRecv.size() < (nCmdLength + TRACKER_QUERY_STORAGE_STORE_BODY_LEN))
			return;
		// �Ի�ȡ�����ݽ���ת�ƴ���...
		char * in_buff = (char*)m_strRecv.c_str() + nCmdLength;
		memset(&m_NewStorage, 0, sizeof(m_NewStorage));
		memcpy(m_NewStorage.group_name, in_buff, FDFS_GROUP_NAME_MAX_LEN);
		memcpy(m_NewStorage.ip_addr, in_buff + FDFS_GROUP_NAME_MAX_LEN, IP_ADDRESS_SIZE - 1);
		m_NewStorage.port = (int)buff2long(in_buff + FDFS_GROUP_NAME_MAX_LEN + IP_ADDRESS_SIZE - 1);
		m_NewStorage.store_path_index = (int)(*(in_buff + FDFS_GROUP_NAME_MAX_LEN + IP_ADDRESS_SIZE - 1 + FDFS_PROTO_PKG_LEN_SIZE)); // �ڴ���ֻ��һ���ֽ�...
		blog(LOG_INFO, "Source => Group = %s, Storage = %s:%d, PathIndex = %d", m_NewStorage.group_name, m_NewStorage.ip_addr, m_NewStorage.port, m_NewStorage.store_path_index);
		// �������ж�Storage��ַ�Ƿ���������ַ�������������ַ������tracker��ַ�滻֮...
		this->doStorageWanAddr();
		// �����������м��ٴ���...
		m_strRecv.erase(0, nCmdLength + TRACKER_QUERY_STORAGE_STORE_BODY_LEN);
		// �����µ����� => ��ѯ���е����б�...
		return this->SendCmd(TRACKER_PROTO_CMD_SERVER_LIST_ALL_GROUPS);
	} else if (m_TrackerCmd.cmd == TRACKER_PROTO_CMD_SERVER_LIST_ALL_GROUPS) {
		// �õ������ݳ��Ȳ�����ֱ�ӷ��أ��ȴ�������...
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
		// ���������ӱ�־...
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
	// ������������δ���ӱ�־...
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
	// �ȹر��׽���...
	this->closeSocket();
	// �ٹر��ļ�����...
	this->CloseUpFile();
	// ��ӡ��ǰ�����ϴ����ļ���λ��...
	// û���ϴ���ϵ��ļ���fdfs-storage���Զ�(�ع�)ɾ���������˸������´����ϴ�ʱ�����ϴ�������
	// STORAGE_PROTO_CMD_UPLOAD_FILE �� STORAGE_PROTO_CMD_APPEND_FILE ������...
	if (m_llLeftSize > 0) {
		blog(LOG_INFO, "~CStorageSession: File = %s, Left = %I64d, Size = %I64d", m_strFilePath.c_str(), m_llLeftSize, m_llFileSize);
	}
}
//
// �ؽ��Ự...
bool CStorageSession::ReBuildSession(StorageServer * lpStorage, const char * lpszFilePath)
{
	// �ж���������Ƿ���Ч...
	if (lpStorage == NULL || lpszFilePath == NULL) {
		blog(LOG_INFO, "ReBuildSession: input param is NULL");
		return false;
	}
	// �ر��׽��ֺ��ļ�...
	this->closeSocket();
	this->CloseUpFile();
	// �������ݣ����ļ�...
	m_NewStorage = *lpStorage;
	m_strFilePath.assign(lpszFilePath);
	m_strExtend.assign(strrchr(lpszFilePath, '.') + 1);
	m_lpFile = os_fopen(m_strFilePath.c_str(), "rb");
	if (m_lpFile == NULL) {
		blog(LOG_INFO, "ReBuildSession: os_fopen is NULL");
		return false;
	}
	// �����ļ��ܳ���...
	m_llFileSize = os_fgetsize(m_lpFile);
	m_llLeftSize = m_llFileSize;
	// ��ʼ���ϴ��Ự����ʧ�ܣ���ԭ�ؽ���־...
	if (!this->InitSession(m_NewStorage.ip_addr, m_NewStorage.port)) {
		this->m_bCanReBuild = true;
		return false;
	}
	// �ؽ��ɹ�����������...
	return true;
}
//
// ����ָ����Ϣͷ���ݰ�...
bool CStorageSession::SendCmdHeader()
{
	// ׼����Ҫ�ı���...
	const char * lpszExt = m_strExtend.c_str();
	int64_t  llSize = m_llFileSize;
	qint64	 nReturn = 0;
	qint64   nLength = 0;
	char  *  lpBuf = NULL;
	char     szBuf[_MAX_PATH] = { 0 };
	int      n_pkg_len = 1 + sizeof(int64_t) + FDFS_FILE_EXT_NAME_MAX_LEN;
	// ָ������� => Ŀ¼���(1) + �ļ�����(8) + �ļ���չ��(6)

	// ���ָ��ͷ => ָ���� + ״̬ + ָ�������(����ָ��ͷ)...
	// STORAGE_PROTO_CMD_UPLOAD_FILE �� STORAGE_PROTO_CMD_APPEND_FILE ������...
	// û���ϴ���ϵ��ļ���fdfs-storage���Զ�(�ع�)ɾ���������˸������´����ϴ�ʱ�����ϴ�������
	TrackerHeader * lpHeader = (TrackerHeader*)szBuf;
	lpHeader->cmd = STORAGE_PROTO_CMD_UPLOAD_FILE;
	lpHeader->status = 0;
	long2buff(llSize + n_pkg_len, lpHeader->pkg_len);

	// ���ָ�������...
	lpBuf = szBuf + sizeof(TrackerHeader);							// ��ָ�붨λ��������...
	lpBuf[0] = m_NewStorage.store_path_index;						// Ŀ¼���   => 0 - 1  => 1���ֽ�
	long2buff(llSize, lpBuf + 1);									// �ļ�����   => 1 - 9  => 8���ֽ� ULONGLONG
	memcpy(lpBuf + 1 + sizeof(int64_t), lpszExt, strlen(lpszExt));	// �ļ���չ�� => 9 - 15 => 6���ֽ� FDFS_FILE_EXT_NAME_MAX_LEN

	// �����ϴ�ָ��ͷ��ָ������� => ����ͷ + ���ݳ���...
	nLength = sizeof(TrackerHeader) + n_pkg_len;
	nReturn = m_TCPSocket->write(szBuf, nLength);
	// ��ǰ���ڷ��͵Ļ�������С...
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
	// ����Ѿ������ؽ�״̬��ֱ�ӷ���...
	if (m_bCanReBuild) return;
	// ��������ȡ���еĻ�������������������������...
	QByteArray theBuffer = m_TCPSocket->readAll();
	m_strRecv.append(theBuffer.toStdString());
	// �õ������ݳ��Ȳ������ȴ��µĽ�������...
	int nCmdLength = sizeof(TrackerHeader);
	if (m_strRecv.size() < nCmdLength)
		return;
	int nDataSize = m_strRecv.size() - nCmdLength;
	const char * lpszDataBuf = m_strRecv.c_str() + nCmdLength;
	TrackerHeader * lpHeader = (TrackerHeader*)m_strRecv.c_str();
	// �жϷ��������ص�״̬�Ƿ���ȷ...
	if (lpHeader->status != 0) {
		blog(LOG_INFO, "onReadyRead: status error");
		m_bCanReBuild = true;
		return;
	}
	// �жϷ��ص������������Ƿ���ȷ => ������� FDFS_GROUP_NAME_MAX_LEN
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
	// �ر��ϴ��ļ����...
	this->CloseUpFile();
	// ����վ�㱨������FDFS�ļ���¼...
	App()->doWebSaveFDFS((char*)m_strFilePath.c_str(), szFileFDFS, m_llFileSize);
	// ��ӡ�ϴ������ɾ�����ϴ��ļ�...
	blog(LOG_INFO, "Local = %s, Remote = %s, Size = %I64d\n", m_strFilePath.c_str(), szFileFDFS, m_llFileSize);
	if (os_unlink(m_strFilePath.c_str()) != 0) {
		blog(LOG_INFO, "DeleteFile failed!");
	}
	// ���ý��ջ��������ȴ��µ�����...
	m_strRecv.clear();
	// ���и�λ�������ȴ��µĽ�ͼ����...
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
	// ����Ѿ������ؽ�״̬��ֱ�ӷ���...
	if (m_bCanReBuild) return;
	// ����������ݰ�ʧ�ܣ����и�λ�������ȴ��µĽ�ͼ����...
	if (!this->SendNextPacket(nBytes)) {
		m_bCanReBuild = true;
	}
}
//
// ����һ����Ч���ݰ�...
bool CStorageSession::SendNextPacket(int64_t inLastBytes)
{
	// û������Ҫ������...
	if (m_llLeftSize <= 0)
		return true;
	// ����ļ�����Ѿ��ر���...
	if (m_lpFile == NULL) {
		blog(LOG_INFO, "SendNextPacket: File = %s, Left = %I64d, Size = %I64d", m_strFilePath.c_str(), m_llLeftSize, m_llFileSize);
		return false;
	}
	// ��ɾ���Ѿ����ͳɹ��Ļ�����...
	m_strCurData.erase(0, inLastBytes);
	m_llLeftSize -= inLastBytes;
	// ����Ѿ�������ϣ�ֱ�ӷ��أ��ȴ�֪ͨ...
	if (m_llLeftSize <= 0)
		return true;
	// ���ļ��ж�ȡһ�����ݰ���������...
	char szBuffer[kPackSize] = { 0 };
	int nReadSize = fread(szBuffer, 1, kPackSize, m_lpFile);
	// ����ļ���ȡʧ�ܣ����ش���...
	if (nReadSize <= 0) {
		blog(LOG_INFO, "SendNextPacket: fread error");
		return false;
	}
	// ���滺����������Ҫ���͵ĳ���...
	m_strCurData.append(szBuffer, nReadSize);
	int nCurSize = m_strCurData.size();
	int nSendSize = ((nCurSize >= kPackSize) ? kPackSize : nCurSize);
	// �����µķ�������...
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
	// �������ӳɹ���־...
	m_bIsConnected = true;
	// ���ӳɹ����������͵�¼���� => Cmd_Header + JSON...
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
	// ����Ѿ������ؽ�״̬��ֱ�ӷ���...
	if (m_bCanReBuild)
		return;
	// ��������ȡ���еĻ�������������������������...
	QByteArray theBuffer = m_TCPSocket->readAll();
	m_strRecv.append(theBuffer.toStdString());
	// �����������ݻᷢ��ճ��������ˣ���Ҫѭ��ִ��...
	while (m_strRecv.size() > 0) {
		// �õ������ݳ��Ȳ�����ֱ�ӷ��أ��ȴ�������...
		int nCmdLength = sizeof(Cmd_Header);
		if (m_strRecv.size() < nCmdLength)
			return;
		ASSERT(m_strRecv.size() >= nCmdLength);
		Cmd_Header * lpCmdHeader = (Cmd_Header*)m_strRecv.c_str();
		const char * lpDataPtr = m_strRecv.c_str() + sizeof(Cmd_Header);
		int nDataSize = m_strRecv.size() - sizeof(Cmd_Header);
		// �ѻ�ȡ�����ݳ��Ȳ�����ֱ�ӷ��أ��ȴ�������...
		if (nDataSize < lpCmdHeader->m_pkg_len)
			return;
		ASSERT(nDataSize >= lpCmdHeader->m_pkg_len);
		// ��ӡԶ�̿��ƻỰ������յ���TCP����������Ϣ...
		blog(LOG_INFO, "[RemoteSession] Command-Recv: %s", get_command_name(lpCmdHeader->m_cmd));
		// ��ʼ������ת����������������...
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
		// ɾ���Ѿ�������ϵ����� => Header + pkg_len...
		m_strRecv.erase(0, lpCmdHeader->m_pkg_len + sizeof(Cmd_Header));
		// ����������ݣ��������������...
	}
}

bool CRemoteSession::doCmdSmartLogin(const char * lpData, int nSize)
{
	Json::Value value;
	// ����Json���ݰ������ݽ���...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CRemoteSession::doParseJson Error!");
		return false;
	}
	// ��ȡԶ�������ڷ������˵�TCP�׽��� => ��ʦ��|ѧ���˶����յ�...
	int nTCPSocketFD = atoi(OBSApp::getJsonString(value["tcp_socket"]).c_str());
	// ����ȡ��TCP�׽��ָ��µ�ϵͳ�������� => �ڴ���UDP����ʱ���õ�...
	if (App()->GetRemoteTcpSockFD() != nTCPSocketFD) {
		App()->SetRemoteTcpSockFD(nTCPSocketFD);
	}
	// ��ӡ��ȡ����Զ��tcp�׽��ֵı��...
	blog(LOG_INFO, "[RemoteSession] doCmdSmartLogin => tcp_socket: %d", nTCPSocketFD);
	// ����ǽ�ʦ�ˣ�����ֱ�ӷ�����...
	if (App()->GetClientType() == kClientTeacher)
		return true;
	// ���ѧ���ˣ���Ҫ��ȡ�������Ϣ => TCP��ʦ�˺�UDP��ʦ���Ƿ�����...
	bool bIsTCPTeacherOnLine = atoi(OBSApp::getJsonString(value["tcp_teacher"]).c_str());
	bool bIsUDPTeacherOnLine = atoi(OBSApp::getJsonString(value["udp_teacher"]).c_str());
	int  nDBFlowTeacherID = atoi(OBSApp::getJsonString(value["flow_teacher"]).c_str());
	// ��������Ľ�ʦ������¼��� => ��һ�£�������Чʱ�Ÿ���...
	if (nDBFlowTeacherID > 0 && App()->GetDBFlowTeacherID() != nDBFlowTeacherID) {
		App()->SetDBFlowTeacherID(nDBFlowTeacherID);
	}
	return true;

	/*int nDBCameraID = atoi(OBSApp::getJsonString(value["camera_id"]).c_str());
	bool bIsCameraOnLine = atoi(OBSApp::getJsonString(value["udp_camera"]).c_str());
	// ��ӡ�����������Ϣ => ֻ������ͷͨ����Ŵ���0ʱ������Ҫ����������������������������...
	blog(LOG_INFO, "[RemoteSession] doCmdSmartLogin => tcp_socket: %d, CameraID: %d, OnLine: %d", nTCPSocketFD, nDBCameraID, bIsCameraOnLine);
	// ��������ͷ����״̬������rtp_source��Դ�����̵߳Ĵ�����ɾ��...
	if (nDBCameraID > 0) {
		emit this->doTriggerRtpSource(nDBCameraID, bIsCameraOnLine);
	}return true;*/
}

bool CRemoteSession::doCmdSmartOnLine(const char * lpData, int nSize)
{
	// ����ǽ�ʦ�ˣ�����ֱ�ӷ�����...
	if (App()->GetClientType() == kClientTeacher)
		return true;
	// ���ѧ���ˣ���Ҫ��ȡ�������Ϣ...
	Json::Value value;
	// ����Json���ݰ������ݽ���...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CRemoteSession::doParseJson Error!");
		return false;
	}
	// ��ȡ������TCP��ʦ�˵��������...
	int  nDBFlowTeacherID = atoi(OBSApp::getJsonString(value["flow_teacher"]).c_str());
	// ��������Ľ�ʦ������¼��� => ��һ�£�������Чʱ�Ÿ���...
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
	// ���ͨ����Ļ��ţ��ҵ��˶�Ӧ�����ݿ飬ֱ��׷�Ӹ���...
	GM_MapScreen::iterator itorItem = m_MapScreen.find(nScreenID);
	if (itorItem != m_MapScreen.end()) {
		string & strData = itorItem->second;
		strData.append(lpData, nDataSize);
		return true;
	}
	// ���û���ҵ�������һ���¼�¼...
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
	// ����Json���ݰ������ݽ���...
	if (!this->doParseJson(lpData, nDataSize, value)) {
		blog(LOG_INFO, "CRemoteSession::doParseJson Error!");
		return false;
	}
	// ��ȡ���������͹�����������Ϣ...
	int nScreenID = atoi(OBSApp::getJsonString(value["screen_id"]).c_str());
	string strUserName = OBSApp::getJsonString(value["user_name"]);
	// �����ݼ����в�����Ҫ����Ļ��ţ�û���ҵ���ֱ�ӷ���...
	GM_MapScreen::iterator itorItem = m_MapScreen.find(nScreenID);
	if (itorItem == m_MapScreen.end())
		return false;
	// �����ݿ�������֮������ɾ����ӦԪ��...
	string strData = itorItem->second;
	m_MapScreen.erase(nScreenID);
	// ����ȡ����ͼ������ֱ�ӽ��д��̲���...
	char path[512] = { 0 };
	if (GetConfigPath(path, sizeof(path), "obs-teacher/screen") <= 0)
		return false;
	// ����jpg�����ļ�ȫ·��...
	QString strQFileName = QString("%1/%2_%3.jpg").arg(path).arg(strUserName.c_str()).arg(nScreenID);
	QFile theFile(strQFileName);
	// ��jpg�ļ���� => ʧ�ܣ�ֱ�ӷ���...
	if (!theFile.open(QIODevice::WriteOnly))
		return false;
	// ֱ�Ӵ��̣�Ȼ��ر� => QFile���ļ�����������...
	theFile.write(strData.c_str(), strData.size());
	theFile.close();
	// ��Ҫͨ���źŲۣ�֪ͨ����㣬����ѧ����Ļ��������Դ...
	emit doTriggerScreenFinish(nScreenID, QString("%1").arg(strUserName.c_str()), strQFileName);
	return true;
}

// �����ɷ��������ص�ѧ����ָ��ͨ��ֹͣ�����ɹ����¼�֪ͨ...
bool CRemoteSession::doCmdTeacherCameraLiveStop(const char * lpData, int nSize)
{
	Json::Value value;
	// ����Json���ݰ������ݽ���...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CRemoteSession::doParseJson Error!");
		return false;
	}
	// ��ȡ���������͹�����������Ϣ...
	int nDBCameraID = atoi(OBSApp::getJsonString(value["camera_id"]).c_str());
	// ֪ͨ�����ڽ���㣬ָֹͣ��ͨ���ɹ����...
	emit this->doTriggerCameraLiveStop(nDBCameraID);
	return true;
}

// �����ɷ��������صĽ�ʦ�����ڷ������������ͷ�б�...
bool CRemoteSession::doCmdTeacherCameraList(const char * lpData, int nSize)
{
	Json::Value value;
	// ����Json���ݰ������ݽ���...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CRemoteSession::doParseJson Error!");
		return false;
	}
	// ��һ����������ͷ�б�����...
	int nListNum = atoi(OBSApp::getJsonString(value["list_num"]).c_str());
	if (nListNum <= 0) {
		blog(LOG_INFO, "=== No OnLine Camera ===");
		return false;
	}
	// ֪ͨ�����ڽ��棬�ѻ�ȡ����ͷ�����б�ɹ�...
	emit this->doTriggerCameraList(value["list_data"]);
	return true;
}

bool CRemoteSession::doCmdUdpLogout(const char * lpData, int nSize)
{
	Json::Value value;
	// ����Json���ݰ������ݽ���...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CRemoteSession::doParseJson Error!");
		return false;
	}
	// ��ȡ���������͹�����������Ϣ...
	int tmTag = atoi(OBSApp::getJsonString(value["tm_tag"]).c_str());
	int idTag = atoi(OBSApp::getJsonString(value["id_tag"]).c_str());
	int nDBCameraID = atoi(OBSApp::getJsonString(value["camera_id"]).c_str());
	// ֪ͨ�����ڽ���㣬UDP�ն˷����˳��¼�...
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

// ͨ����ת��������ѧ���˷���ֹͣͨ����������...
/*bool CRemoteSession::doSendCameraLiveStopCmd(int nDBCameraID)
{
	// û�д�������״̬��ֱ�ӷ���...
	if (!m_bIsConnected)
		return false;
	// ���������Ҫ��JSON���ݰ� => ͨ�����|������Դ���...
	string strJson;	Json::Value root;
	char szDataBuf[32] = { 0 };
	sprintf(szDataBuf, "%d", nDBCameraID);
	root["camera_id"] = szDataBuf;
	//sprintf(szDataBuf, "%d", nSceneItemID);
	//root["sitem_id"] = szDataBuf;
	strJson = root.toStyledString();
	// ��ӡjson���ݰ�������ʹ��...
	//blog(LOG_INFO, "Json => %d, %s", strJson.size(), strJson.c_str());
	// ����ͳһ�Ľӿڽ����������ݵķ��Ͳ���...
	return this->doSendCommonCmd(kCmd_Camera_LiveStop, strJson.c_str(), strJson.size());
}

// ͨ����ת��������ѧ���˷��Ϳ���ͨ����������...
bool CRemoteSession::doSendCameraLiveStartCmd(int nDBCameraID)
{
	// û�д�������״̬��ֱ�ӷ���...
	if (!m_bIsConnected)
		return false;
	// ���������Ҫ��JSON���ݰ� => ͨ�����|������Դ���...
	string strJson;	Json::Value root;
	char szDataBuf[32] = { 0 };
	sprintf(szDataBuf, "%d", nDBCameraID);
	root["camera_id"] = szDataBuf;
	//sprintf(szDataBuf, "%d", nSceneItemID);
	//root["sitem_id"] = szDataBuf;
	strJson = root.toStyledString();
	// ����ͳһ�Ľӿڽ����������ݵķ��Ͳ���...
	return this->doSendCommonCmd(kCmd_Camera_LiveStart, strJson.c_str(), strJson.size());
}

bool CRemoteSession::doSendCameraPTZCmd(int nDBCameraID, int nCmdID, int nSpeedVal)
{
	// û�д�������״̬��ֱ�ӷ���...
	if (!m_bIsConnected)
		return false;
	// ���������Ҫ��JSON���ݰ� => ͨ�����|������Դ���...
	string strJson;	Json::Value root;
	char szDataBuf[32] = { 0 };
	sprintf(szDataBuf, "%d", nDBCameraID);
	root["camera_id"] = szDataBuf;
	sprintf(szDataBuf, "%d", nCmdID);
	root["cmd_id"] = szDataBuf;
	sprintf(szDataBuf, "%d", nSpeedVal);
	root["speed_val"] = szDataBuf;
	strJson = root.toStyledString();
	// ����ͳһ�Ľӿڽ����������ݵķ��Ͳ���...
	return this->doSendCommonCmd(kCmd_Camera_PTZCommand, strJson.c_str(), strJson.size());
}

bool CRemoteSession::doSendCameraPusherIDCmd(int nDBCameraID)
{
	// û�д�������״̬��ֱ�ӷ���...
	if (!m_bIsConnected)
		return false;
	// ���������Ҫ��JSON���ݰ� => ͨ�����|������Դ���...
	string strJson;	Json::Value root;
	char szDataBuf[32] = { 0 };
	sprintf(szDataBuf, "%d", nDBCameraID);
	root["camera_id"] = szDataBuf;
	strJson = root.toStyledString();
	// ����ͳһ�Ľӿڽ����������ݵķ��Ͳ���...
	return this->doSendCommonCmd(kCmd_Camera_PusherID, strJson.c_str(), strJson.size());
}

// ��ȡ��ʦ�����ڷ����������������ͷ�б�...
bool CRemoteSession::doSendCameraOnLineListCmd()
{
	// û�д�������״̬��ֱ�ӷ���...
	if (!m_bIsConnected)
		return false;
	ASSERT(m_bIsConnected);
	// ����ͳһ�Ľӿڽ����������ݵķ��Ͳ���...
	return this->doSendCommonCmd(kCmd_Camera_OnLineList);
}*/

// ÿ��30�뷢�����������...
bool CRemoteSession::doSendOnLineCmd()
{
	// û�д�������״̬��ֱ�ӷ���...
	if (!m_bIsConnected)
		return false;
	ASSERT(m_bIsConnected);
	// ����ͳһ�Ľӿڽ����������ݵķ��Ͳ���...
	return this->doSendCommonCmd(kCmd_Smart_OnLine);
}

// ���ӳɹ�֮�󣬷��͵�¼����...
bool CRemoteSession::SendLoginCmd()
{
	// û�д�������״̬��ֱ�ӷ���...
	if (!m_bIsConnected)
		return false;
	// ���Login������Ҫ��JSON���ݰ�...
	char szDataBuf[32] = { 0 };
	string strJson;	Json::Value root;
	// ����������|mac��ַ|ip��ַ|������|��������...
	sprintf(szDataBuf, "%d", App()->GetDBFlowID());
	root["flow_id"] = szDataBuf;
	root["mac_addr"] = App()->GetLocalMacAddr();
	root["ip_addr"] = App()->GetLocalIPAddr();
	root["room_id"] = App()->GetRoomIDStr();
	root["pc_name"] = OBSApp::GetServerDNSName();
	strJson = root.toStyledString();
	// ����ͳһ�Ľӿڽ����������ݵķ��Ͳ���...
	return this->doSendCommonCmd(kCmd_Smart_Login, strJson.c_str(), strJson.size());
}

// ͨ�õ�����ͽӿ�...
bool CRemoteSession::doSendCommonCmd(int nCmdID, const char * lpJsonPtr/* = NULL*/, int nJsonSize/* = 0*/)
{
	// ��ӡԶ�̿��ƻỰ�����͵�TCP����������Ϣ...
	blog(LOG_INFO, "[RemoteSession] Command-Send: %s", get_command_name(nCmdID));
	// ��������ͷ => ���ݳ��� | �ն����� | ������
	string     strBuffer;
	Cmd_Header theHeader = { 0 };
	theHeader.m_pkg_len = ((lpJsonPtr != NULL) ? nJsonSize : 0);
	theHeader.m_type = App()->GetClientType();
	theHeader.m_cmd = nCmdID;
	// ׷�������ͷ���������ݰ�����...
	strBuffer.append((char*)&theHeader, sizeof(theHeader));
	// ������������������Ч���Ž������ݵ����...
	if (lpJsonPtr != NULL && nJsonSize > 0) {
		strBuffer.append(lpJsonPtr, nJsonSize);
	}
	// ����ͳһ�ķ��ͽӿ�...
	return this->SendData(strBuffer.c_str(), strBuffer.size());
}

// ͳһ�ķ��ͽӿ�...
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
	// �������ӳɹ���־...
	m_bIsConnected = true;
	// ���ӳɹ����������͵�¼���� => ����tcp_socket...
	this->doSendCommonCmd(kCmd_Smart_Login);
}

void CCenterSession::onReadyRead()
{
	// ��������ȡ���еĻ�������������������������...
	QByteArray theBuffer = m_TCPSocket->readAll();
	m_strRecv.append(theBuffer.toStdString());
	// �����������ݻᷢ��ճ��������ˣ���Ҫѭ��ִ��...
	while (m_strRecv.size() > 0) {
		// �õ������ݳ��Ȳ�����ֱ�ӷ��أ��ȴ�������...
		int nCmdLength = sizeof(Cmd_Header);
		if (m_strRecv.size() < nCmdLength)
			return;
		ASSERT(m_strRecv.size() >= nCmdLength);
		Cmd_Header * lpCmdHeader = (Cmd_Header*)m_strRecv.c_str();
		const char * lpDataPtr = m_strRecv.c_str() + sizeof(Cmd_Header);
		int nDataSize = m_strRecv.size() - sizeof(Cmd_Header);
		// �ѻ�ȡ�����ݳ��Ȳ�����ֱ�ӷ��أ��ȴ�������...
		if (nDataSize < lpCmdHeader->m_pkg_len)
			return;
		ASSERT(nDataSize >= lpCmdHeader->m_pkg_len);
		// ��ӡԶ�̿��ƻỰ������յ���TCP����������Ϣ...
		blog(LOG_INFO, "[CenterSession] Command-Recv: %s", get_command_name(lpCmdHeader->m_cmd));
		// ��ʼ������ת����������������...
		bool bResult = false;
		switch (lpCmdHeader->m_cmd)
		{
		case kCmd_Smart_Login:     bResult = this->doCmdSmartLogin(lpDataPtr, lpCmdHeader->m_pkg_len); break;
		case kCmd_Smart_OnLine:    bResult = this->doCmdSmartOnLine(lpDataPtr, lpCmdHeader->m_pkg_len); break;
		case kCmd_PHP_Bind_Mini:   bResult = this->doCmdPHPBindMini(lpDataPtr, lpCmdHeader->m_pkg_len); break;
		}
		// ɾ���Ѿ�������ϵ����� => Header + pkg_len...
		m_strRecv.erase(0, lpCmdHeader->m_pkg_len + sizeof(Cmd_Header));
		// ����������ݣ��������������...
	}
}

bool CCenterSession::doCmdPHPBindMini(const char * lpData, int nSize)
{
	Json::Value value;
	// ����Json���ݰ������ݽ���...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CCenterSession::doParseJson Error!");
		return false;
	}
	// ��ȡ������������� => 1(Scan),2(Save),3(Cancel)
	int nUserID = atoi(OBSApp::getJsonString(value["user_id"]).c_str());
	int nRoomID = atoi(OBSApp::getJsonString(value["room_id"]).c_str());
	int nBindCmd = atoi(OBSApp::getJsonString(value["bind_cmd"]).c_str());
	blog(LOG_INFO, "[CenterSession] doCmdPHPBindMini => user_id: %d, bind_cmd: %d, room_id: %d", nUserID, nBindCmd, nRoomID);
	// �����֪ͨС����ͨ��PHPת��������ɨ��󶨵�¼����...
	emit this->doTriggerBindMini(nUserID, nBindCmd, nRoomID);
	return true;
}

bool CCenterSession::doCmdSmartLogin(const char * lpData, int nSize)
{
	Json::Value value;
	// ����Json���ݰ������ݽ���...
	if (!this->doParseJson(lpData, nSize, value)) {
		blog(LOG_INFO, "CCenterSession::doParseJson Error!");
		return false;
	}
	// ��ȡԶ�������ڷ������˵�TCP�׽��֣�����ӡ����...
	m_nCenterTcpSocketFD = atoi(OBSApp::getJsonString(value["tcp_socket"]).c_str());
	m_uCenterTcpTimeID = (uint32_t)atoi(OBSApp::getJsonString(value["tcp_time"]).c_str());
	blog(LOG_INFO, "[CenterSession] doCmdSmartLogin => tcp_socket: %d, tcp_time: %lu", m_nCenterTcpSocketFD, m_uCenterTcpTimeID);
	// �����֪ͨ��ȡtcp_socket״̬�ɹ�...
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

// ÿ��30�뷢�����������...
bool CCenterSession::doSendOnLineCmd()
{
	// û�д�������״̬��ֱ�ӷ���...
	if (!m_bIsConnected)
		return false;
	ASSERT(m_bIsConnected);
	// ����ͳһ�Ľӿڽ����������ݵķ��Ͳ���...
	return this->doSendCommonCmd(kCmd_Smart_OnLine);
}

// ͨ�õ�����ͽӿ�...
bool CCenterSession::doSendCommonCmd(int nCmdID, const char * lpJsonPtr/* = NULL*/, int nJsonSize/* = 0*/)
{
	// ��ӡԶ�̿��ƻỰ�����͵�TCP����������Ϣ...
	blog(LOG_INFO, "[CenterSession] Command-Send: %s", get_command_name(nCmdID));
	// ��������ͷ => ���ݳ��� | �ն����� | ������
	string     strBuffer;
	Cmd_Header theHeader = { 0 };
	theHeader.m_pkg_len = ((lpJsonPtr != NULL) ? nJsonSize : 0);
	theHeader.m_type = App()->GetClientType();
	theHeader.m_cmd = nCmdID;
	// ׷�������ͷ���������ݰ�����...
	strBuffer.append((char*)&theHeader, sizeof(theHeader));
	// ������������������Ч���Ž������ݵ����...
	if (lpJsonPtr != NULL && nJsonSize > 0) {
		strBuffer.append(lpJsonPtr, nJsonSize);
	}
	// ����ͳһ�ķ��ͽӿ�...
	return this->SendData(strBuffer.c_str(), strBuffer.size());
}

// ͳһ�ķ��ͽӿ�...
bool CCenterSession::SendData(const char * lpDataPtr, int nDataSize)
{
	int nReturn = m_TCPSocket->write(lpDataPtr, nDataSize);
	return ((nReturn > 0) ? true : false);
}