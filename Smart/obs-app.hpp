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
	std::string                    m_strWebCenterAddr;              // ������վ��������ַ => ���Դ��϶˿ں�...
	string                         m_strCenterTcpAddr;              // ���ķ�������TCP��ַ...
	int                            m_nCenterTcpPort = 0;            // ���ķ�������TCP�˿�...
	std::string                    m_strTrackerAddr;                // FDFS-Tracker��IP��ַ...
	int                            m_nTrackerPort = 0;              // FDFS-Tracker�Ķ˿ڵ�ַ...
	std::string                    m_strRemoteAddr;                 // Զ��UDPServer��TCP��ַ...
	int                            m_nRemotePort = 0;               // Զ��UDPServer��TCP�˿�...
	int                            m_nRemoteTcpSockFD = 0;          // CRemoteSession�ڷ������˵��׽��ֺ���...
	std::string                    m_strUdpAddr;                    // Զ��UDPServer��UDP��ַ...
	int                            m_nUdpPort = 0;                  // Զ��UDPServer��UDP�˿�...
	int                            m_nDBFlowTeacherID = 0;          // ѧ�����뽲ʦ�˹�����������¼���...
	int                            m_nDBFlowID = 0;                 // �ӷ�������ȡ��������ͳ�����ݿ���...
	int                            m_nDBUserID = 0;                 // �ѵ�¼�û������ݿ���...
	int                            m_nDBSmartID = 0;                // ��������վ�����ݿ��еı��...
	std::string                    m_strUserName;                   // �ѵ�¼�û����û�����...
	std::string                    m_strRoomID;                     // ��¼�ķ����...
	std::string                    m_strMacAddr;                    // ����MAC��ַ...
	std::string                    m_strIPAddr;                     // ����IP��ַ...
	int                            m_nFastTimer = -1;               // �ֲ�ʽ�洢����ת���Ӽ��ʱ��...
	int                            m_nFlowTimer = -1;               // ����ͳ�Ƽ��ʱ��...
	int                            m_nOnLineTimer = -1;             // ��ת���������߼��ʱ��...
	CLIENT_TYPE                    m_nClientType = kClientStudent;  // Ĭ�ϵ�ǰ�ն˵����� => Student
	bool                           m_bIsDebugMode = false;          // �Ƿ��ǵ���ģʽ => ���ص����Է�����...
	bool                           m_bIsMiniMode = true;            // �Ƿ���С����ģʽ => ���ص������Ʒ�����...
	uint64_t                       m_nUpFlowByte = 0;               // �ն���������...
	uint64_t                       m_nDownFlowByte = 0;             // �ն���������...
	std::string                    locale;
	std::string                    theme;
	ConfigFile                     globalConfig;
	TextLookup                     textLookup;
	OBSContext                     obsContext;
	QNetworkAccessManager          m_objNetManager;                 // QT ����������...
	QPointer<OBSMainWindow>        mainWindow = NULL;               // �����ڶ���
	QPointer<CLoginMini>           m_LoginMini = NULL;              // С�����¼����
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
	int      GetRemoteTcpSockFD() { return m_nRemoteTcpSockFD; }
	string & GetRemoteAddr() { return m_strRemoteAddr; }
	int		 GetRemotePort() { return m_nRemotePort; }
	string & GetUdpAddr() { return m_strUdpAddr; }
	int		 GetUdpPort() { return m_nUdpPort; }
	int      GetDBFlowTeacherID() { return m_nDBFlowTeacherID; }

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
	void     SetDBSmartID(int nDBSmartID) { m_nDBSmartID = nDBSmartID; }
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
	void doCheckSmartFlow();

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
