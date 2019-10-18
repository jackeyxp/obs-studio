
#pragma once

#include <util/util.hpp>

#include "window-main.hpp"
#include "ui_StudentWindow.h"

#include <string>
#include <memory>

#include <QNetworkAccessManager>

using namespace std;

class CStudentWindow : public OBSMainWindow
{
    Q_OBJECT
private:
	ConfigFile               basicConfig;
	bool                     m_bIsLoaded = false;
	QPoint	                 m_startMovePos;
	bool	                 m_isPressed = false;
	int                      m_nClassTimer = -1;
	int                      m_nTimeSecond = 0;
	QPixmap                  m_QPixClock;
	QPixmap                  m_QPixUserHead;
	QString                  m_strUserHeadUrl;
	QString                  m_strUserNickName;
	QRect                    m_rcSrcGeometry;
	QNetworkAccessManager    m_objNetManager;	// QT 网络管理对象...
private:
	enum {
		kWebGetUserHead   = 0,
	} m_eNetState;
private slots:
	void onButtonMinClicked();
	void onButtonMaxClicked();
	void onButtonCloseClicked();
	void onButtonAboutClicked();
	void onButtonUpdateClicked();
	void onButtonCameraClicked();
	void onButtonSystemClicked();
	void onReplyFinished(QNetworkReply *reply);
private:
	int  doD3DSetup();
	int  ResetVideo();
	bool ResetAudio();
	bool InitBasicConfig();
	bool InitBasicConfigDefaults();
	void InitBasicConfigDefaults2();
	void GetFPSCommon(uint32_t &num, uint32_t &den) const;
	void GetFPSInteger(uint32_t &num, uint32_t &den) const;
	void GetFPSFraction(uint32_t &num, uint32_t &den) const;
	void GetFPSNanoseconds(uint32_t &num, uint32_t &den) const;
	void GetConfigFPS(uint32_t &num, uint32_t &den) const;

	void initWindow();
	void doWebGetUserHead();
	void doDrawTimeClock();
	void doDrawTitle(QPainter & inPainter);
	void loadStyleSheet(const QString &sheetName);
	void onProcGetUserHead(QNetworkReply *reply);
private:
	void timerEvent(QTimerEvent * inEvent);
	virtual void paintEvent(QPaintEvent *event);
	virtual void mousePressEvent(QMouseEvent *event);
	virtual void mouseMoveEvent(QMouseEvent *event);
	virtual void mouseReleaseEvent(QMouseEvent *event);
public:
	inline bool IsLoaded() { return m_bIsLoaded; }
public:
	explicit CStudentWindow(QWidget *parent = NULL);
	virtual ~CStudentWindow();
	virtual void OBSInit() override;
	virtual config_t *Config() const override;
	virtual int GetProfilePath(char *path, size_t size, const char *file) const override;
private:
	std::unique_ptr<Ui::StudentWindow> ui;
};
