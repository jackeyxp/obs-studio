
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

using namespace std;

class CViewCamera;
class CStudentWindow : public OBSMainWindow
{
    Q_OBJECT
private:
	long disableSaving = 1;
	ConfigFile basicConfig;
	QString m_strSavePath;
	bool m_bIsSlientClose = false;
	bool m_bIsLoaded = false;
	QPoint m_startMovePos;
	bool m_isPressed = false;
	int m_nClassTimer = -1;
	int m_nTimeSecond = 0;
	int m_nXPosClock = 0;
	QString m_strClock;
	QPixmap m_QPixClock;
	QPixmap m_QPixUserHead;
	QString m_strUserHeadUrl;
	QString m_strUserNickName;
	QRect m_rcSrcGeometry;
	obs_scene_t  * m_obsScene = nullptr;             // 唯一主场景...
	obs_sceneitem_t * m_dshowSceneItem = nullptr;    // 本地摄像头...
	obs_sceneitem_t * m_teacherSceneItem = nullptr;  // 远程老师端...
	QPointer<CViewCamera> m_viewCamera = nullptr;    // 预览本地摄像头...
	QNetworkAccessManager  m_objNetManager;	         // QT 网络管理对象...
	std::vector<OBSSignal> signalHandlers;
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
    void doDShowResetVideo(int sourceCX, int sourceCY);
	void AddSceneItem(OBSSceneItem item);
	void AddScene(OBSSource source);
private:
	static void SourceCreated(void *data, calldata_t *params);
	static void SourceRemoved(void *data, calldata_t *params);
	static void SceneItemAdded(void *data, calldata_t *params);
private:
	int   doD3DSetup();
	int   ResetVideo();
	bool  ResetAudio();
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
	void  RefreshSceneCollections();
	void  CreateDefaultScene(bool firstStart);
	void  ResetAudioDevice(const char *sourceId, const char *deviceId, const char *deviceDesc, int channel);

	void  initWindow();
	void  doWebGetUserHead();
	void  doDrawTimeClock();
	void  doDrawTitle(QPainter & inPainter);
	void  doDrawLeftArea(QPainter & inPainter);
	void  doDrawRightArea(QPainter & inPainter);
	void  loadStyleSheet(const QString &sheetName);
	void  onProcGetUserHead(QNetworkReply *reply);
	float doDShowCheckRatio();
private:
	void timerEvent(QTimerEvent * inEvent);
	virtual void paintEvent(QPaintEvent *event);
	virtual void mousePressEvent(QMouseEvent *event);
	virtual void mouseMoveEvent(QMouseEvent *event);
	virtual void mouseReleaseEvent(QMouseEvent *event);
	virtual void closeEvent(QCloseEvent *event) override;
public:
	inline obs_scene_t * GetObsScene() { return m_obsScene; }
	inline obs_sceneitem_t * GetDShowSceneItem() { return m_dshowSceneItem; }
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
