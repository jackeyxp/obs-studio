
#pragma once

#include <util/util.hpp>

#include "qt-display.hpp"
#include "window-main.hpp"
#include "ui_StudentWindow.h"

#include <obs.hpp>
#include <string>
#include <memory>

#include <QNetworkAccessManager>
#include <QPointer>

#include "json.h"

using namespace std;

class CViewCamera;
class CViewTeacher;
class CStudentWindow : public OBSMainWindow
{
    Q_OBJECT
private:
	long disableSaving = 1;
	ConfigFile basicConfig;
	QString m_strSavePath;
	bool m_bIsResetVideo = false;
	bool m_bIsStartOutput = false;
	bool m_bIsSlientClose = false;
	bool m_bIsLoaded = false;
	QPoint m_startMovePos;
	bool m_isPressed = false;
	int m_nOutputTimer = -1;
	int m_nClassTimer = -1;
	int m_nTimeSecond = 0;
	int m_nXPosClock = 0;
	QString m_strClock;
	QPixmap m_QPixClock;
	QPixmap m_QPixUserHead;
	QString m_strUserHeadUrl;
	QString m_strUserNickName;
	QRect m_rcSrcGeometry;
	obs_scene_t  * m_obsScene = nullptr;              // 唯一主场景...
	QPointer<CViewCamera> m_viewSoftCamera = nullptr; // 预览本地摄像头...
	QPointer<CViewTeacher> m_viewTeacher = nullptr;   // 预览老师端画面...
	QNetworkAccessManager  m_objNetManager;	          // QT 网络管理对象...
	std::vector<OBSSignal> signalHandlers;            // 系统信号量集合...
private:
	enum {
		kWebGetUserHead   = 0,
	} m_eNetState;
private slots:
	void OnFinishedLoad();
	void onButtonMinClicked();
	void onButtonMaxClicked();
	void onButtonCloseClicked();
	void onButtonAboutClicked();
	void onButtonUpdateClicked();
	void onButtonCameraClicked();
	void onButtonSystemClicked();
	void onReplyFinished(QNetworkReply *reply);
	void DeferredLoad(const QString &file, int requeueCount);
private slots:
	void onRemoteSmartLogin(int nLiveID);
	void onRemoteCameraPullStart(int nDBCameraID);
	void onRemoteLiveOnLine(int nLiveID, bool bIsLiveOnLine);
	void onRemoteUdpLogout(int nLiveID, int tmTag, int idTag);
	void onRemoteCameraList(Json::Value & value);
	void onRemoteCameraLiveStop(int nDBCameraID);
	void onRemoteCameraLiveStart(int nDBCameraID);
	void onRemoteDeleteExAudioThread();
private slots:
	void StreamingStatus(bool bIsDelete, int nTotalKbps, int nAudioKbps, int nVideoKbps);
	void UpdatedSmartSource(OBSSource source);
	void AddSceneItem(OBSSceneItem item);
	void AddScene(OBSSource source);
private:
	static void SourceCreated(void *data, calldata_t *params);
	static void SourceRemoved(void *data, calldata_t *params);
	static void SourceUpdated(void *data, calldata_t *params);
	static void SceneItemAdded(void *data, calldata_t *params);
private:
	int   doD3DSetup();
	int   ResetVideo();
	bool  ResetAudio();
	void  ResetOutputs();
	bool  InitBasicConfig();
	bool  InitBasicConfigDefaults();
	void  InitBasicConfigDefaults2();
	void  GetFPSCommon(uint32_t &num, uint32_t &den) const;
	void  GetFPSInteger(uint32_t &num, uint32_t &den) const;
	void  GetFPSFraction(uint32_t &num, uint32_t &den) const;
	void  GetFPSNanoseconds(uint32_t &num, uint32_t &den) const;
	void  GetConfigFPS(uint32_t &num, uint32_t &den) const;
	void  CheckForSimpleModeX264Fallback();

	void  LogScenes();
	void  SaveProject();
	void  ClearSceneData();
	void  InitOBSCallbacks();
	void  Load(const char *file);
	void  CreateDefaultScene(bool firstStart);

	void  initWindow();
	void  doWebGetUserHead();
	void  doDrawTimeClock();
	void  doDrawTitle(QPainter & inPainter);
	void  doDrawLeftArea(QPainter & inPainter);
	void  doDrawRightArea(QPainter & inPainter);
	void  loadStyleSheet(const QString &sheetName);
	void  onProcGetUserHead(QNetworkReply *reply);
	float doDShowCheckRatio();
	void  doCheckOutput();
private:
	void timerEvent(QTimerEvent * inEvent);
	virtual void paintEvent(QPaintEvent *event);
	virtual void mousePressEvent(QMouseEvent *event);
	virtual void mouseMoveEvent(QMouseEvent *event);
	virtual void mouseReleaseEvent(QMouseEvent *event);
	virtual void closeEvent(QCloseEvent *event) override;
public:
	inline obs_scene_t * GetObsScene() { return m_obsScene; }
	inline void SetSlientClose(bool bIsSlient) { m_bIsSlientClose = bIsSlient; }
public:
	explicit CStudentWindow(QWidget *parent = NULL);
	virtual ~CStudentWindow();
	virtual void OBSInit() override;
	virtual config_t *Config() const override;
	virtual bool IsLoaded() { return m_bIsLoaded; }
	virtual int GetProfilePath(char *path, size_t size, const char *file) const override;
private:
	std::unique_ptr<Ui::StudentWindow> ui;
};
