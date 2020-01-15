
#pragma once

#include <fastdfs.h>
#include <QTcpSocket>
#include <string>
#include "json.h"
#include <map>

using namespace std;

typedef map<int, string> GM_MapScreen;

class CFastSession : public QObject {
	Q_OBJECT
public:
	CFastSession();
	virtual ~CFastSession();
public:
	int  GetErrCode() { return m_nErrorCode; }
	bool IsConnected() { return m_bIsConnected; }
	bool InitSession(const char * lpszAddr, int nPort);
protected:
	void closeSocket();
protected slots:
	virtual void onConnected() = 0;
	virtual void onReadyRead() = 0;
	virtual void onDisConnected() = 0;
	virtual void onBytesWritten(qint64 nBytes) = 0;
	virtual void onError(QAbstractSocket::SocketError nError) = 0;
protected:
	int              m_nErrorCode;              // 错误号码...
	bool             m_bIsConnected;			// 是否已连接标志...
	QTcpSocket   *   m_TCPSocket;				// TCP套接字...
	std::string      m_strAddress;				// 链接地址...
	std::string      m_strRecv;					// 网络数据...
	int              m_nPort;					// 链接端口...
};

/*class CTrackerSession : public CFastSession {
	Q_OBJECT
public:
	CTrackerSession();
	virtual ~CTrackerSession();
public:
	StorageServer   &   GetStorageServer() { return m_NewStorage; }
protected slots:
	void onConnected() override;
	void onReadyRead() override;
	void onDisConnected() override;
	void onBytesWritten(qint64 nBytes) override;
	void onError(QAbstractSocket::SocketError nError) override;
private:
	void SendCmd(char inCmd);
	void doStorageWanAddr();
private:
	TrackerHeader		m_TrackerCmd;				// Tracker-Header-Cmd...
	StorageServer		m_NewStorage;				// 当前有效的存储服务器...
	FDFSGroupStat	*	m_lpGroupStat;				// group列表头指针...
	int					m_nGroupCount;				// group数量...
};

class CStorageSession : public CFastSession {
	Q_OBJECT
public:
	CStorageSession();
	virtual ~CStorageSession();
public:
	bool IsCanReBuild() { return m_bCanReBuild; }
	bool ReBuildSession(StorageServer * lpStorage, const char * lpszFilePath);
protected slots:
	void onConnected() override;
	void onReadyRead() override;
	void onDisConnected() override;
	void onBytesWritten(qint64 nBytes) override;
	void onError(QAbstractSocket::SocketError nError) override;
private:
	bool    SendNextPacket(int64_t inLastBytes);
	bool	SendCmdHeader();
	void    CloseUpFile();
private:
	enum {
		kPackSize = 64 * 1024,			// 数据包大小 => 越大，发送码流越高(每秒发送64次) => 8KB(4Mbps)|64KB(32Mbps)|128KB(64Mbps)
	};
private:
	StorageServer	m_NewStorage;		// 当前有效的存储服务器...
	std::string     m_strFilePath;		// 正在处理的文件全路径...
	std::string     m_strExtend;		// 正在处理的文件扩展名...
	std::string		m_strCurData;		// 当前正在发送的数据包内容...
	int64_t         m_llFileSize;		// 正在处理的文件总长度...
	int64_t         m_llLeftSize;		// 剩余数据总长度...
	FILE       *    m_lpFile;			// 正在处理的文件句柄...
	bool            m_bCanReBuild;		// 能否进行重建标志...
};*/

// 与命令中转节点服务器交互的会话对象 => udpserver...
class CRemoteSession : public CFastSession {
	Q_OBJECT
public:
	CRemoteSession();
	virtual ~CRemoteSession();
signals:
	void doTriggerSmartLogin(int nLiveID);
	void doTriggerCameraPullStart(int nDBCameraID);
	void doTriggerLiveOnLine(int nLiveID, bool bIsLiveOnLine);
	void doTriggerUdpLogout(int nLiveID, int tmTag, int idTag);
	void doTriggerCameraList(Json::Value & value);
	void doTriggerCameraLiveStop(int nDBCameraID);
	void doTriggerCameraLiveStart(int nDBCameraID);
	void doTriggerDeleteExAudioThread();
	//void doTriggerScreenFinish(int nScreenID, QString strQUser, QString strQFile);
public:
	bool IsCanReBuild() { return m_bCanReBuild; }
	bool doSendOnLineCmd();
	bool doSendCameraOnLineListCmd();
	bool doSendCameraPullStartCmd(int nDBCameraID);
	bool doSendCameraPullStopCmd(int nDBCameraID);
	bool doSendCameraLiveStopCmd(int nDBCameraID);
	bool doSendCameraLiveStartCmd(int nDBCameraID);
	//bool doSendCameraPusherIDCmd(int nDBCameraID);
	//bool doSendCameraPTZCmd(int nDBCameraID, int nCmdID, int nSpeedVal);
protected slots:
	void onConnected() override;
	void onReadyRead() override;
	void onDisConnected() override;
	void onBytesWritten(qint64 nBytes) override;
	void onError(QAbstractSocket::SocketError nError) override;
private:
	bool doSendCommonCmd(int nCmdID, const char * lpJsonPtr = NULL, int nJsonSize = 0);
	bool doParseJson(const char * lpData, int nSize, Json::Value & outValue);
	bool doCmdSmartLogin(const char * lpData, int nSize);
	bool doCmdSmartOnLine(const char * lpData, int nSize);
	bool doCmdLiveOnLine(const char * lpData, int nSize);
	bool doCmdUdpLogout(const char * lpData, int nSize);
	bool doCmdCameraPullStart(const char * lpData, int nSize);
	bool doCmdCameraList(const char * lpData, int nSize);
	bool doCmdCameraLiveStop(const char * lpData, int nSize);
	bool doCmdCameraLiveStart(const char * lpData, int nSize);
	//bool doCmdScreenPacket(const char * lpData, Cmd_Header * lpCmdHeader);
	//bool doCmdScreenFinish(const char * lpData, Cmd_Header * lpCmdHeader);
	bool SendData(const char * lpDataPtr, int nDataSize);
	bool SendLoginCmd();
private:
	bool          m_bCanReBuild;    // 能否进行重建标志...
	GM_MapScreen  m_MapScreen;      // ScreenID => string...
};

// 与中心服务器交互的会话对象 => udpcenter...
class CCenterSession : public CFastSession {
	Q_OBJECT
public:
	CCenterSession();
	virtual ~CCenterSession();
public:
	uint32_t  GetTcpTimeID() { return m_uCenterTcpTimeID; }
	int       GetTcpSocketFD() { return m_nCenterTcpSocketFD; }
	bool      doSendOnLineCmd();
signals:
	void doTriggerTcpConnect();
	void doTriggerBindMini(int nUserID, int nBindCmd, int nRoomID);
protected slots:
	void onConnected() override;
	void onReadyRead() override;
	void onDisConnected() override;
	void onBytesWritten(qint64 nBytes) override;
	void onError(QAbstractSocket::SocketError nError) override;
private:
	bool doSendCommonCmd(int nCmdID, const char * lpJsonPtr = NULL, int nJsonSize = 0);
	bool doParseJson(const char * lpData, int nSize, Json::Value & outValue);
	bool doCmdSmartLogin(const char * lpData, int nSize);
	bool doCmdSmartOnLine(const char * lpData, int nSize);
	bool doCmdPHPBindMini(const char * lpData, int nSize);
	bool SendData(const char * lpDataPtr, int nDataSize);
private:
	int         m_nCenterTcpSocketFD;		// 中心映射的套接字...
	uint32_t    m_uCenterTcpTimeID;         // 中心关联的时间戳...
};