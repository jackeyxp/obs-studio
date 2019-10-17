
#pragma once

#include <util/util.hpp>

#include "window-main.hpp"
#include "ui_StudentWindow.h"

#include <string>
#include <memory>

using namespace std;

class CStudentWindow : public OBSMainWindow
{
    Q_OBJECT
private:
	ConfigFile        basicConfig;
	bool              m_bIsLoaded = false;
	QPoint	          m_startMovePos;
	bool	          m_isPressed = false;
	QPixmap           m_QPixUserHead;
	QString           m_strUserHeadUrl;
	QString           m_strUserNickName;
	QRect             m_rcSrcGeometry;
private slots:
	void onButtonMinClicked();
	void onButtonMaxClicked();
	void onButtonCloseClicked();
	void onButtonAboutClicked();
	void onButtonUpdateClicked();
	void onButtonCameraClicked();
	void onButtonSystemClicked();
private:
	void initWindow();
	void loadStyleSheet(const QString &sheetName);
private:
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
