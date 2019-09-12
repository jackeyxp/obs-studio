
#pragma once

#include "json.h"
#include "ui_LoginMini.h"

#include <string>
#include <functional>
#include <QThread>
#include <QPointer>
#include <QNetworkAccessManager>

using namespace std;

class QMovie;
class QLabel;
class CCenterSession;
class CLoginMini : public QDialog
{
    Q_OBJECT
public:
	CLoginMini(QWidget *parent = NULL);
    ~CLoginMini();
public:
	int  GetDBRoomID() { return m_nDBRoomID; }
	int  GetDBUserID() { return m_nDBUserID; }
signals:
	void doTriggerMiniSuccess();
protected:
	void loadStyleSheet(const QString &sheetName);
private:
	virtual void paintEvent(QPaintEvent *event);
	virtual void mousePressEvent(QMouseEvent *event);
	virtual void mouseMoveEvent(QMouseEvent *event);
	virtual void mouseReleaseEvent(QMouseEvent *event);
private slots:
	void updateCheckFinished();
	void onTriggerTcpConnect();
	void onButtonMinClicked();
	void onButtonCloseClicked();
	void onReplyFinished(QNetworkReply *reply);
	void onTriggerBindMini(int nUserID, int nBindCmd, int nRoomID);
private:
	void initWindow();
	void doCheckAppMode();
	bool doCheckOnLine();
	void doCheckSession();
	void doWebGetCenterAddr();
	void doTcpConnCenterAddr();
	void doWebGetMiniToken();
	void doWebGetMiniQRCode();
	void doWebGetMiniUserInfo();
	void doWebGetMiniLoginRoom();
	void timerEvent(QTimerEvent * inEvent);
	void onProcCenterAddr(QNetworkReply *reply);
	void onProcMiniToken(QNetworkReply *reply);
	void onProcMiniQRCode(QNetworkReply *reply);
	void onProcMiniUserInfo(QNetworkReply *reply);
	void onProcMiniLoginRoom(QNetworkReply *reply);
	bool parseJson(string & inData, Json::Value & outValue, bool bIsWeiXin);
	void CheckForUpdates(bool manualUpdate);
	void TimedCheckForUpdates();
private:
	enum {
		kQRCodeWidth = 280,	// 小程序二维码宽度
	};
	enum LOGIN_CMD {
		kNoneCmd        = 0,	// 没有命令...
		kScanCmd        = 1,	// 扫码成功...
		kSaveCmd        = 2,	// 确认登录...
		kCancelCmd      = 3,	// 取消登录...
	} m_eLoginCmd;
	enum {
		kCenterAddr     = 0,  // 获取中心服务器的TCP地址和端口...
		kCenterConn     = 1,  // 连接中心服务器的TCP地址和端口...
		kMiniToken      = 2,  // 连接微信服务器获取access_token...
		kMiniQRCode     = 3,  // 链接微信服务器获取并显示小程序码...
		kMiniLoginRoom  = 4,  // 正式登录指定房间号码...
		kMiniUserInfo   = 5,  // 获取登录用户的详细信息...
	} m_eMiniState;
private:
	Ui::LoginMini  *  ui;
	QPoint	          m_startMovePos;
	bool	          m_isPressed = false;
	string            m_strCenterTcpAddr;       // 中心服务器的TCP地址...
	int               m_nCenterTcpPort;         // 中心服务器的TCP端口...
	int               m_nTcpSocketFD;           // 中心服务器上的套接字...
	uint32_t          m_uTcpTimeID;             // 中心服务器上的时间标识...
	int               m_nDBUserID;              // 中心服务器上的数据库用户编号...
	int               m_nDBRoomID;              // 中心服务器上的房间编号...
	int               m_nOnLineTimer;           // 中心服务器在线检测时钟...
	int               m_nCenterTimer;           // 中心会话对象重建检测时钟...
	string            m_strMiniToken;			// 获取到的access_token值...
	string            m_strMiniPath;			// 获取到的小程序响应页面...
	QMovie     *      m_lpMovieGif;             // GIF动画对象...
	QLabel     *      m_lpLoadBack;             // GIF动画背景...
	QPixmap           m_QPixQRCode;				// 获取到的小程序码图片数据...
	QString           m_strQRNotice;            // 提示状态信息字符串内容...
	QString           m_strScan;                // 扫码过程中显示的信息...
	QString           m_strVer;                 // 显示版本信息内容文字...
	QNetworkAccessManager    m_objNetManager;	// QT 网络管理对象...
	QPointer<CCenterSession> m_CenterSession;   // For UDP-Center
	QPointer<QThread> updateCheckThread = NULL; // 升级检测线程...
};
