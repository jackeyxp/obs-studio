
#include <ActiveQt/QAxObject>
#include "window-wait-ppt.hpp"
#include "properties-view.hpp"
#include "qt-wrappers.hpp"
#include "obs-app.hpp"
#include <objbase.h>
#include "md5.h"

CPPTWait::CPPTWait(QWidget *parent, QString & inStrFile)
  : QDialog(parent)
  , m_nTimerID(-1)
  , m_nCurValue(6)
  , m_lpPPThread(NULL)
  , ui(new Ui::PPTWait)
  , m_strFilePPT(inStrFile)
{
	m_view = static_cast<OBSPropertiesView *>(parent);
	ASSERT(m_view != NULL);
	ui->setupUi(this);
	this->initWindow();
}

CPPTWait::~CPPTWait()
{
	this->killTimer(m_nTimerID); m_nTimerID = -1;
	if (m_lpPPThread && m_lpPPThread->isRunning()) {
		m_lpPPThread->wait();
	}
	if (m_lpPPThread != NULL) {
		delete m_lpPPThread;
		m_lpPPThread = NULL;
	}
}

void CPPTWait::initWindow()
{
	m_lpPPThread = new CPPThread(this);
	ui->titleName->setText(QTStr("PPT.Wait.Text"));
	ui->timeBar->setRange(0, 60);
	ui->timeBar->setValue(m_nCurValue);
	m_nTimerID = this->startTimer(500);
	m_lpPPThread->start();
}

void CPPTWait::timerEvent(QTimerEvent * inEvent)
{
	// ����߳�û����ͣ����Ҫ����ת������...
	if (!m_lpPPThread->m_bIsCanPause) {
		ui->timeBar->setValue(++m_nCurValue);
	}
	// ���ת���̷߳���������Ҫ��������...
	if (m_lpPPThread->m_strError.size() > 0) {
		OBSErrorBox(m_view, m_lpPPThread->m_strError.toStdString().c_str());
	}
	// ���ת���߳��Ѿ�ת����ϣ�ǿ�ƶԻ����˳�...
	if (m_lpPPThread->m_bIsCanExit) {
		this->doSavePathJPG();
		done(QDialog::Accepted);
	}
}

void CPPTWait::doSavePathJPG()
{
	if (m_strPathJPG.size() <= 0)
		return;
	obs_data_array *array = obs_data_array_create();
	obs_data_t *arrayItem = obs_data_create();
	obs_data_set_string(arrayItem, "value",	QT_TO_UTF8(m_strPathJPG));
	obs_data_set_bool(arrayItem, "selected", true);
	obs_data_set_bool(arrayItem, "hidden", false);
	obs_data_array_push_back(array, arrayItem);
	obs_data_release(arrayItem);
	obs_data_set_array(m_view->settings, "files", array);
	obs_data_array_release(array);
}

CPPThread::CPPThread(CPPTWait * lpPPTWait)
  : m_lpPPTWait(lpPPTWait)
  , m_bIsCanPause(false)
  , m_bIsCanExit(false)
{
	ASSERT(m_lpPPTWait != NULL);
}

void CPPThread::run()
{
	// �ж���������Ƿ���Ч�������Ч���趨��־���˳��߳�...
	if (m_lpPPTWait == NULL || m_lpPPTWait->m_view == NULL || m_lpPPTWait->m_strFilePPT.size() <= 0) {
		m_bIsCanExit = true; m_strError = QTStr("PPT.ErrParam");
		return;
	}
	// ��������ʽ��ʽת��...
	this->doPPTExportJPG();
	// ִ����ϣ��趨�˳���־...
	m_bIsCanExit = true;
}

void CPPThread::doPPTExportJPG()
{
	OBSPropertiesView * lpView = m_lpPPTWait->m_view;
	QString & inStrFile = m_lpPPTWait->m_strFilePPT;
	// �����ļ�ȫ·�����Ƶ�MD5ֵ..
	MD5	 md5; char path[512] = { 0 };
	md5.update(inStrFile.toStdString().c_str(), inStrFile.size());
	if (GetConfigPath(path, sizeof(path), "obs-smart/ppt") <= 0) {
		m_strError = QTStr("PPT.ErrPath").arg("obs-smart/ppt");
		return;
	}
	// ����PPTת�����JPG�����ļ����Ŀ¼...
	sprintf(path, "%s/%s", path, md5.toString().c_str());
	int nError = os_mkdirs(path);
	if (nError == MKDIR_ERROR) {
		m_strError = QTStr("PPT.ErrMkDir").arg(path);
		return;
	}
	// ��ת�����Ŀ¼·�����浽�ȴ�����...
	m_lpPPTWait->m_strPathJPG = path;
	// ���Ŀ¼�Ѿ����ڣ�ѯ���Ƿ񸲸�...
	if (nError == MKDIR_EXISTS) {
		m_bIsCanPause = true;
		int button = QMessageBox::No;
		// ��������ý����߳�ȥ���ã����򣬵���ʱ���ᷢ���������...
		QMetaObject::invokeMethod(this, "doConfirmExist",
			Qt::BlockingQueuedConnection,
			Q_RETURN_ARG(int, button));
		// ��������ǣ�ֱ�ӷ���...
		if (button == QMessageBox::No)
			return;
		// ǿ�Ƹ��ǣ������������...
		m_bIsCanPause = false;
	}
	// �̱߳���ע�ᵽCOM�׼�����...
	CoInitialize(nullptr);
	// ��PowerPoint���...
	QAxObject *_powerPointAxObj = NULL;
	// ���ո�����ĵ�Ŀ¼����Ҫ�������ţ������PPT��ʧ��...
	QVariant varFileName = QVariant(QString("\"%1\"").arg(inStrFile));
	do {
		_powerPointAxObj = new QAxObject("Powerpoint.Application", this);
		// ��PowerPointʧ�� => û�а�װPowerPoint���...
		if (_powerPointAxObj == NULL || _powerPointAxObj->isNull()) {
			m_strError = QTStr("PPT.ErrSoft");
			break;
		}
		connect(_powerPointAxObj, SIGNAL(exception(int, QString, QString, QString)), this, SLOT(doPPTErrSlot(int, QString, QString, QString)));
		// �����Ѵ���ʾ�ĸ弯�� => https://docs.microsoft.com/zh-cn/office/vba/api/powerpoint.application.presentations
		auto _presentations = _powerPointAxObj->querySubObject("Presentations");
		if (_presentations == NULL || _presentations->isNull()) {
			m_strError = QTStr("PPT.ErrPresent");
			break;
		}
		connect(_presentations, SIGNAL(exception(int, QString, QString, QString)), this, SLOT(doPPTErrSlot(int, QString, QString, QString)));
		// Presentations.Open�������� => https://docs.microsoft.com/zh-cn/office/vba/api/powerpoint.presentations.open
		auto _presentation = _presentations->querySubObject("Open(QString,QVariant,QVariant,QVariant)", varFileName, true, false, false);
		if (_presentation == NULL || _presentation->isNull()) {
			m_strError = QTStr("PPT.ErrOpen").arg(inStrFile);
			break;
		}
		connect(_presentation, SIGNAL(exception(int, QString, QString, QString)), this, SLOT(doPPTErrSlot(int, QString, QString, QString)));
		// MsoTriStateö�� => https://docs.microsoft.com/zh-cn/office/vba/api/office.msotristate
		// ���������Ƴ����� => https://docs.microsoft.com/zh-cn/office/vba/api/powerpoint.application.top
		// ��������С�� => https://docs.microsoft.com/zh-cn/office/vba/api/powerpoint.ppwindowstate
		auto _visible = _powerPointAxObj->setProperty("WindowState", 2);
		_visible = _powerPointAxObj->setProperty("Top", -9999);
		// Visible�������PowerPoint�������ã�Height��Width��Ч...
		_visible = _powerPointAxObj->setProperty("Visible", 0);
		_visible = _powerPointAxObj->setProperty("Height", 0);
		_visible = _powerPointAxObj->setProperty("Width", 0);
		// Presentation.Slides���Բ��� => https://docs.microsoft.com/zh-cn/office/vba/api/powerpoint.presentation.slides
		//auto _slides = _presentation->querySubObject("Slides");
		//int numSlides = _slides->property("Count").toInt();
		// Presentation.Export�������� => https://docs.microsoft.com/zh-cn/office/vba/api/powerpoint.presentation.export
		_presentation->dynamicCall("Export(QString,QString,QVariant,QVariant)", path, "JPG");
		_presentation->dynamicCall("Close()");
	} while (false);
	// �رղ�ɾ��PowerPoint����ķ���...
	if (_powerPointAxObj != NULL) {
		_powerPointAxObj->dynamicCall("Quit()");
		delete _powerPointAxObj; _powerPointAxObj = NULL;
	}
	// �߳�ע��COM�׼�...
	CoUninitialize();
}

int CPPThread::doConfirmExist()
{
	return OBSMessageBox::question( m_lpPPTWait->m_view,
		QTStr("PPT.Confirm.Title"), QTStr("PPT.Confirm.Text"));
}

void CPPThread::doPPTErrSlot(int nErrCode, QString inArg1, QString inArg2, QString inArg3)
{
	blog(LOG_INFO, "== ErrCode: 0x%x, %s, %s ==", nErrCode,
		inArg1.toStdString().c_str(), inArg2.toStdString().c_str());
}