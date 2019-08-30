#pragma once

#include <QDialog>
#include <QThread>

#include "ui_PPTWait.h"

class CPPTWait;
class OBSPropertiesView;
class CPPThread : public QThread {
	Q_OBJECT
public:
	CPPThread(CPPTWait * lpPPTWait);
protected:
	virtual void run() override;
private:
	void doPPTExportJPG();
private slots:
	int  doConfirmExist();
	void doPPTErrSlot(int nErrCode, QString inArg1, QString inArg2, QString inArg3);
private:
	bool         m_bIsCanExit;
	bool         m_bIsCanPause;
	QString      m_strError;
	CPPTWait  *  m_lpPPTWait;

	friend class CPPTWait;
};

class CPPTWait : public QDialog {
	Q_OBJECT
public:
	CPPTWait(QWidget *parent, QString & inStrFile);
	~CPPTWait();
private:
	void initWindow();
	void doSavePathJPG();
	void timerEvent(QTimerEvent * inEvent);
private:
	int m_nTimerID;
	int m_nCurValue;
	Ui::PPTWait  *  ui;
	QString     m_strFilePPT;
	QString     m_strPathJPG;
	CPPThread * m_lpPPThread;
	OBSPropertiesView * m_view;

	friend class CPPThread;
};
