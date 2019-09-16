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

using namespace std;

std::string CurrentTimeString();
std::string CurrentDateTimeString();
std::string GenerateTimeDateFilename(const char *extension, bool noSpace = false);
std::string GenerateSpecifiedFilename(const char *extension, bool noSpace, const char *format);
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
	std::string                    m_strWebCenterAddr;         // 中心网站服务器地址 => 可以带上端口号...
	string                         m_strCenterTcpAddr;         // 中心服务器的TCP地址...
	int                            m_nCenterTcpPort;           // 中心服务器的TCP端口...
	std::string                    m_strTrackerAddr;           // FDFS-Tracker的IP地址...
	int                            m_nTrackerPort;             // FDFS-Tracker的端口地址...
	std::string                    m_strRemoteAddr;            // 远程UDPServer的TCP地址...
	int                            m_nRemotePort;              // 远程UDPServer的TCP端口...
	std::string                    m_strUdpAddr;               // 远程UDPServer的UDP地址...
	int                            m_nUdpPort;                 // 远程UDPServer的UDP端口...
	int                            m_nDBFlowID;                // 从服务器获取到的流量统计数据库编号...
	int                            m_nDBUserID;                // 已登录用户的数据库编号...
	std::string                    m_strUserName;              // 已登录用户的用户名称...
	std::string                    m_strRoomID;                // 登录的房间号...
	std::string                    m_strMacAddr;               // 本机MAC地址...
	std::string                    m_strIPAddr;                // 本机IP地址...
	int                            m_nFastTimer;               // 分布式存储、中转链接检测时钟...
	int                            m_nFlowTimer;               // 流量统计检测时钟...
	int                            m_nOnLineTimer;             // 中转服务器在线检测时钟...
	int                            m_nRtpTCPSockFD;            // CRemoteSession在服务器端的套接字号码...
	CLIENT_TYPE                    m_nClientType;              // 当前终端的类型 => Smart
	bool                           m_bIsDebugMode;             // 是否是调试模式 => 挂载到调试服务器...
	bool                           m_bIsMiniMode;              // 是否是小程序模式 => 挂载到阿里云服务器...
	std::string                    locale;
	std::string                    theme;
	ConfigFile                     globalConfig;
	TextLookup                     textLookup;
	OBSContext                     obsContext;
	QPointer<OBSMainWindow>        mainWindow;                 // 主窗口对象
	QPointer<CLoginMini>           m_LoginMini;                // 小程序登录窗口
	QPointer<CRemoteSession>       m_RemoteSession;            // For UDP-Server
	QNetworkAccessManager          m_objNetManager;            // QT 网络管理对象...
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
	void AddExtraThemeColor(QPalette &pal, int group, const char *name,	uint32_t color);
public:
	static string getJsonString(Json::Value & inValue);
	static char * GetServerDNSName();
public:
	bool     IsMiniMode() { return m_bIsMiniMode; }
	bool     IsDebugMode() { return m_bIsDebugMode; }
	int      GetRtpTCPSockFD() { return m_nRtpTCPSockFD; }
	int      GetDBFlowID() { return m_nDBFlowID; }
	int      GetDBUserID() { return m_nDBUserID; }
	string & GetLocalIPAddr() { return m_strIPAddr; }
	string & GetLocalMacAddr() { return m_strMacAddr; }
	string & GetRoomIDStr() { return m_strRoomID; }
	string & GetUserNameStr() { return m_strUserName; }
	CLIENT_TYPE GetClientType() { return m_nClientType; }

	string & GetWebCenterAddr() { return m_strWebCenterAddr; }

	string & GetTcpCenterAddr() { return m_strCenterTcpAddr; }
	int      GetTcpCenterPort() { return m_nCenterTcpPort; }
	string & GetTrackerAddr() { return m_strTrackerAddr; }
	int		 GetTrackerPort() { return m_nTrackerPort; }
	string & GetRemoteAddr() { return m_strRemoteAddr; }
	int		 GetRemotePort() { return m_nRemotePort; }
	string & GetUdpAddr() { return m_strUdpAddr; }
	int		 GetUdpPort() { return m_nUdpPort; }

	void     SetDBFlowID(int nDBFlowID) { m_nDBFlowID = nDBFlowID; }
	void     SetRtpTCPSockFD(int nTCPSockFD) { m_nRtpTCPSockFD = nTCPSockFD; }
	void	 SetUdpAddr(const string & strAddr) { m_strUdpAddr = strAddr; }
	void     SetUdpPort(int nPort) { m_nUdpPort = nPort; }
	void	 SetRemoteAddr(const string & strAddr) { m_strRemoteAddr = strAddr; }
	void     SetRemotePort(int nPort) { m_nRemotePort = nPort; }
	void	 SetTrackerAddr(const string & strAddr) { m_strTrackerAddr = strAddr; }
	void     SetTrackerPort(int nPort) { m_nTrackerPort = nPort; }
	void	 SetTcpCenterAddr(const string & strAddr) { m_strCenterTcpAddr = strAddr; }
	void     SetTcpCenterPort(int nPort) { m_nCenterTcpPort = nPort; }
	void     SetClientType(CLIENT_TYPE inType) { m_nClientType = inType; }
public:
	OBSApp(int &argc, char **argv, profiler_name_store_t *store);
	~OBSApp();

	void AppInit();
	bool OBSInit();

	void doLoginInit();
	void doLogoutEvent();
	void doProcessCmdLine(int argc, char * argv[]);

	void doCheckFDFS();
	void doCheckRemote();
	void doCheckOnLine();
	void doCheckRoomFlow();

	void timerEvent(QTimerEvent * inEvent);

	void UpdateHotkeyFocusSetting(bool reset = true);
	void DisableHotkeys();

	inline bool HotkeysEnabledInFocus() const {
		return enableHotkeysInFocus;
	}

	inline CLoginMini *GetLoginMini() const;
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
	const char *InteractRtpSource() const;

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
