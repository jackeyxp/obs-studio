
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
	int              m_nErrorCode;              // �������...
	bool             m_bIsConnected;			// �Ƿ������ӱ�־...
	QTcpSocket   *   m_TCPSocket;				// TCP�׽���...
	std::string      m_strAddress;				// ���ӵ�ַ...
	std::string      m_strRecv;					// ��������...
	int              m_nPort;					// ���Ӷ˿�...
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
	StorageServer		m_NewStorage;				// ��ǰ��Ч�Ĵ洢������...
	FDFSGroupStat	*	m_lpGroupStat;				// group�б�ͷָ��...
	int					m_nGroupCount;				// group����...
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
		kPackSize = 64 * 1024,			// ���ݰ���С => Խ�󣬷�������Խ��(ÿ�뷢��64��) => 8KB(4Mbps)|64KB(32Mbps)|128KB(64Mbps)
	};
private:
	StorageServer	m_NewStorage;		// ��ǰ��Ч�Ĵ洢������...
	std::string     m_strFilePath;		// ���ڴ�����ļ�ȫ·��...
	std::string     m_strExtend;		// ���ڴ�����ļ���չ��...
	std::string		m_strCurData;		// ��ǰ���ڷ��͵����ݰ�����...
	int64_t         m_llFileSize;		// ���ڴ�����ļ��ܳ���...
	int64_t         m_llLeftSize;		// ʣ�������ܳ���...
	FILE       *    m_lpFile;			// ���ڴ�����ļ����...
	bool            m_bCanReBuild;		// �ܷ�����ؽ���־...
};*/

// ��������ת�ڵ�����������ĻỰ���� => udpserver...
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
	bool          m_bCanReBuild;    // �ܷ�����ؽ���־...
	GM_MapScreen  m_MapScreen;      // ScreenID => string...
};

// �����ķ����������ĻỰ���� => udpcenter...
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
	int         m_nCenterTcpSocketFD;		// ����ӳ����׽���...
	uint32_t    m_uCenterTcpTimeID;         // ���Ĺ�����ʱ���...
};