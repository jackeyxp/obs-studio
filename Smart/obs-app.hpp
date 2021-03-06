/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

#include <QNetworkAccessManager>
#include <QApplication>
#include <QTranslator>
#include <QPointer>
#include <obs.hpp>
#include <util/lexer.h>
#include <util/profiler.h>
#include <util/util.hpp>
#include <util/platform.h>
#include <obs-frontend-api.h>
#include <functional>
#include <string>
#include <memory>
#include <vector>
#include <deque>
#include "json.h"

#include "HYDefine.h"
#include "window-main.hpp"
#include "window-login-mini.h"

using namespace std;

std::string CurrentTimeString();
std::string CurrentDateTimeString();
std::string GenerateTimeDateFilename(const char *extension, bool noSpace = false);
std::string GenerateSpecifiedFilename(bool autoRemux, const char *extension, bool noSpace, const char *format);
QObject *CreateShortcutFilter();

struct BaseLexer {
	lexer lex;
public:
	inline BaseLexer() { lexer_init(&lex); }
	inline ~BaseLexer() { lexer_free(&lex); }
	operator lexer *() { return &lex; }
};

class OBSTranslator : public QTranslator {
	Q_OBJECT
public:
	virtual bool isEmpty() const override { return false; }
	virtual QString translate(const char *context, const char *sourceText,
				  const char *disambiguation, int n) const override;
};

typedef std::function<void()> VoidFunc;

class CLoginMini;
class CRemoteSession;
class OBSApp : public QApplication {
	Q_OBJECT
private:
	GM_MapNodeCamera               m_MapNodeCamera;				    // 监控通道配置信息(数据库CameraID）
	std::string                    m_strWebCenterAddr;              // 中心网站服务器地址 => 可以带上端口号...
	string                         m_strCenterTcpAddr;              // 中心服务器的TCP地址...
	int                            m_nCenterTcpPort = 0;            // 中心服务器的TCP端口...
	std::string                    m_strTrackerAddr;                // FDFS-Tracker的IP地址...
	int                            m_nTrackerPort = 0;              // FDFS-Tracker的端口地址...
	std::string                    m_strRemoteAddr;                 // 远程UDPServer的TCP地址...
	int                            m_nRemotePort = 0;               // 远程UDPServer的TCP端口...
	int                            m_nRemoteTcpSockFD = 0;          // CRemoteSession在服务器端的套接字号码...
	std::string                    m_strUdpAddr;                    // 远程UDPServer的UDP地址...
	int                            m_nUdpPort = 0;                  // 远程UDPServer的UDP端口...
	int                            m_nDBFlowTeacherID = 0;          // 学生端与讲师端关联的流量记录编号...
	int                            m_nDBFlowID = 0;                 // 从服务器获取到的流量统计数据库编号...
	int                            m_nDBUserID = 0;                 // 已登录用户的数据库编号...
	int                            m_nDBSmartID = 0;                // 本机在网站端数据库中的编号...
	int                            m_nDBSoftCameraID = 0;           // 本机唯一软编码摄像头的数据库编号...
	int                            m_nDBTeacherCameraID = 0;        // 本机唯一讲师端摄像头的数据库编号...
	std::string                    m_strRoomID;                     // 登录的房间号...
	std::string                    m_strMacAddr;                    // 本机MAC地址...
	std::string                    m_strIPAddr;                     // 本机IP地址...
	int                            m_nFastTimer = -1;               // 分布式存储、中转链接检测时钟...
	int                            m_nFlowTimer = -1;               // 流量统计检测时钟...
	int                            m_nOnLineTimer = -1;             // 中转服务器在线检测时钟...
	CLIENT_TYPE                    m_nClientType = kClientStudent;  // 默认当前终端的类型 => Student
	bool                           m_bIsDebugMode = false;          // 是否是调试模式 => 挂载到调试服务器...
	bool                           m_bIsMiniMode = true;            // 是否是小程序模式 => 挂载到阿里云服务器...
	uint64_t                       m_nUpFlowByte = 0;               // 终端上行流量...
	uint64_t                       m_nDownFlowByte = 0;             // 终端下行流量...
	Json::Value                    m_JsonUser;                      // 用户信息列表...
	std::string                    locale;
	std::string                    theme;
	ConfigFile                     globalConfig;
	TextLookup                     textLookup;
	OBSContext                     obsContext;
	QNetworkAccessManager          m_objNetManager;                 // QT 网络管理对象...
	QPointer<OBSMainWindow>        mainWindow = NULL;               // 主窗口对象
	QPointer<CLoginMini>           m_LoginMini = NULL;              // 小程序登录窗口
	QPointer<CRemoteSession>       m_RemoteSession = NULL;          // For UDP-Server
	profiler_name_store_t     *    profilerNameStore = nullptr;

	os_inhibit_t *sleepInhibitor = nullptr;
	int sleepInhibitRefs = 0;

	bool enableHotkeysInFocus = true;
	bool enableHotkeysOutOfFocus = true;
	std::deque<obs_frontend_translate_ui_cb> translatorHooks;
	QPalette defaultPalette;
private:
	bool InitTheme();
	bool InitLocale();
	bool InitMacIPAddr();
	bool InitGlobalConfig();
	bool InitGlobalConfigDefaults();
	inline void ResetHotkeyState(bool inFocus);
	void ParseExtraThemeData(const char *path);
	bool UpdatePre22MultiviewLayout(const char *layout);
	void AddExtraThemeColor(QPalette &pal, int group, const char *name, uint32_t color);
public:
	static int EncodeURI(const char* inSrc, int inSrcLen, char* ioDest, int inDestLen);
	static string getJsonString(Json::Value & inValue);
	static char * GetServerDNSName();
	static char * GetServerOS();
	static string GetSystemVer();
public:
	bool     IsMiniMode() { return m_bIsMiniMode; }
	bool     IsDebugMode() { return m_bIsDebugMode; }
	int      GetDBFlowID() { return m_nDBFlowID; }
	int      GetDBUserID() { return m_nDBUserID; }
	int      GetDBSmartID() { return m_nDBSmartID; }
	int      GetDBSoftCameraID() { return m_nDBSoftCameraID; }
	int      GetDBTeacherCameraID() { return m_nDBTeacherCameraID; }
	string & GetLocalIPAddr() { return m_strIPAddr; }
	string & GetLocalMacAddr() { return m_strMacAddr; }
	string & GetRoomIDStr() { return m_strRoomID; }
	CLIENT_TYPE GetClientType() { return m_nClientType; }
	QString  GetClientTypeName();

	string & GetWebCenterAddr() { return m_strWebCenterAddr; }

	string & GetTcpCenterAddr() { return m_strCenterTcpAddr; }
	int      GetTcpCenterPort() { return m_nCenterTcpPort; }
	string & GetTrackerAddr() { return m_strTrackerAddr; }
	int		 GetTrackerPort() { return m_nTrackerPort; }
	int      GetRemoteTcpSockFD() { return m_nRemoteTcpSockFD; }
	string & GetRemoteAddr() { return m_strRemoteAddr; }
	int		 GetRemotePort() { return m_nRemotePort; }
	string & GetUdpAddr() { return m_strUdpAddr; }
	int		 GetUdpPort() { return m_nUdpPort; }
	int      GetDBFlowTeacherID() { return m_nDBFlowTeacherID; }

	string   GetUserRealPhone() { return OBSApp::getJsonString(m_JsonUser["real_phone"]); }
	string   GetUserRealName() { return OBSApp::getJsonString(m_JsonUser["real_name"]); }
	string   GetUserNickName() { return OBSApp::getJsonString(m_JsonUser["wx_nickname"]); }
	string   GetUserHeadUrl() { return OBSApp::getJsonString(m_JsonUser["wx_headurl"]); }

	void     doAddDownFlowByte(int nDownSize) { m_nDownFlowByte += nDownSize; }
	void     doAddUpFlowByte(int nUpSize) { m_nUpFlowByte += nUpSize; }
	void     SetDBFlowTeacherID(int nFlowTeacherID) { m_nDBFlowTeacherID = nFlowTeacherID; }
	void     SetDBFlowID(int nDBFlowID) { m_nDBFlowID = nDBFlowID; }
	void	 SetUdpAddr(const string & strAddr) { m_strUdpAddr = strAddr; }
	void     SetUdpPort(int nPort) { m_nUdpPort = nPort; }
	void     SetRemoteTcpSockFD(int nTCPSockFD) { m_nRemoteTcpSockFD = nTCPSockFD; }
	void	 SetRemoteAddr(const string & strAddr) { m_strRemoteAddr = strAddr; }
	void     SetRemotePort(int nPort) { m_nRemotePort = nPort; }
	void	 SetTrackerAddr(const string & strAddr) { m_strTrackerAddr = strAddr; }
	void     SetTrackerPort(int nPort) { m_nTrackerPort = nPort; }
	void	 SetTcpCenterAddr(const string & strAddr) { m_strCenterTcpAddr = strAddr; }
	void     SetTcpCenterPort(int nPort) { m_nCenterTcpPort = nPort; }
	void     SetClientType(CLIENT_TYPE inType) { m_nClientType = inType; }
	void     SetDBTeacherCameraID(int nDBCameraID) { m_nDBTeacherCameraID = nDBCameraID; }
	void     SetDBSoftCameraID(int nDBCameraID) { m_nDBSoftCameraID = nDBCameraID; }
	void     SetDBSmartID(int nDBSmartID) { m_nDBSmartID = nDBSmartID; }
	void     SetJsonUser(Json::Value & inUser) { m_JsonUser = inUser; }

	string   GetCameraSName(int nDBCameraID);
	QString  GetCameraQName(int nDBCameraID);

	void	 SetCamera(int nDBCameraID, GM_MapData & inMapData) { m_MapNodeCamera[nDBCameraID] = inMapData; }
	void	 GetCamera(int nDBCameraID, GM_MapData & outMapData) { outMapData = m_MapNodeCamera[nDBCameraID]; }
	GM_MapNodeCamera & GetNodeCamera() { return m_MapNodeCamera; }
public:
	OBSApp(int &argc, char **argv, profiler_name_store_t *store);
	~OBSApp();

	void AppInit();
	bool OBSInit();

	void doLoginInit();
	void doLogoutEvent();
	void doProcessCmdLine(int argc, char * argv[]);

	bool doSendCameraOnLineListCmd();
	bool doSendCameraPullStartCmd(int nDBCameraID);
	bool doSendCameraPullStopCmd(int nDBCameraID);
	bool doSendCameraLiveStopCmd(int nDBCameraID);
	bool doSendCameraLiveStartCmd(int nDBCameraID);
	//bool doSendCameraPusherIDCmd(int nDBCameraID);
	//bool doSendCameraPTZCmd(int nDBCameraID, int nCmdID, int nSpeedVal);

	void doCheckFDFS();
	void doCheckRemote();
	void doCheckOnLine();
	void doCheckSmartFlow();
	void doCheckRtpSource();

	void timerEvent(QTimerEvent * inEvent);

	void UpdateHotkeyFocusSetting(bool reset = true);
	void DisableHotkeys();

	inline bool HotkeysEnabledInFocus() const {
		return enableHotkeysInFocus;
	}

	inline CLoginMini *GetLoginMini() const { return m_LoginMini.data(); }
	inline QMainWindow *GetMainWindow() const { return mainWindow.data(); }
	profiler_name_store_t *GetProfilerNameStore() const { return profilerNameStore; }
	inline config_t *GlobalConfig() const { return globalConfig; }
	inline const char *GetLocale() const { return locale.c_str(); }
	inline const char *GetTheme() const { return theme.c_str(); }
	bool SetTheme(std::string name, std::string path = "");
	inline lookup_t *GetTextLookup() const { return textLookup; }
	inline const char *GetString(const char *lookupVal) const {
		return textLookup.GetString(lookupVal);
	}

	bool TranslateString(const char *lookupVal, const char **out) const;

	const char *GetLastLog() const;
	const char *GetCurrentLog() const;
	const char *GetLastCrashLog() const;
	std::string GetVersionString() const;
	bool IsPortableMode();

	const char *GetNSFilter() const;
	const char *DShowInputSource() const;
	const char *InputAudioSource() const;
	const char *OutputAudioSource() const;
	const char *InteractSmartSource() const;

	const char *GetRenderModule() const;

	inline void IncrementSleepInhibition()
	{
		if (!sleepInhibitor)
			return;
		if (sleepInhibitRefs++ == 0)
			os_inhibit_sleep_set_active(sleepInhibitor, true);
	}

	inline void DecrementSleepInhibition()
	{
		if (!sleepInhibitor)
			return;
		if (sleepInhibitRefs == 0)
			return;
		if (--sleepInhibitRefs == 0)
			os_inhibit_sleep_set_active(sleepInhibitor, false);
	}

	inline void PushUITranslation(obs_frontend_translate_ui_cb cb)
	{
		translatorHooks.emplace_front(cb);
	}

	inline void PopUITranslation() { translatorHooks.pop_front(); }

public slots:
	void Exec(VoidFunc func);
	void onTriggerMiniSuccess();
	void onReplyFinished(QNetworkReply *reply);
signals:
	void StyleChanged();
};

int GetConfigPath(char *path, size_t size, const char *name);
char *GetConfigPathPtr(const char *name);

int GetProgramDataPath(char *path, size_t size, const char *name);
char *GetProgramDataPathPtr(const char *name);

inline OBSApp *App() { return static_cast<OBSApp *>(qApp); }

inline config_t *GetGlobalConfig() { return App()->GlobalConfig(); }

std::vector<std::pair<std::string, std::string>> GetLocaleNames();
inline const char *Str(const char *lookup) { return App()->GetString(lookup); }
#define QTStr(lookupVal) QString::fromUtf8(Str(lookupVal))

bool GetFileSafeName(const char *name, std::string &file);
bool GetClosestUnusedFileName(std::string &path, const char *extension);

bool WindowPositionValid(QRect rect);

static inline int GetProfilePath(char *path, size_t size, const char *file) {
	OBSMainWindow *window = reinterpret_cast<OBSMainWindow *>(App()->GetMainWindow());
	return window->GetProfilePath(path, size, file);
}

extern bool portable_mode;
extern bool remuxAfterRecord;
extern std::string remuxFilename;

extern bool opt_start_streaming;
extern bool opt_start_recording;
extern bool opt_start_replaybuffer;
extern bool opt_minimize_tray;
extern bool opt_studio_mode;
extern bool opt_allow_opengl;
extern bool opt_always_on_top;
extern std::string opt_starting_scene;
