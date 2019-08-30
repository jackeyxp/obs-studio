
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
	// 如果线程没有暂停，需要更新转换进度...
	if (!m_lpPPThread->m_bIsCanPause) {
		ui->timeBar->setValue(++m_nCurValue);
	}
	// 如果转换线程发生错误，需要弹框提醒...
	if (m_lpPPThread->m_strError.size() > 0) {
		OBSErrorBox(m_view, m_lpPPThread->m_strError.toStdString().c_str());
	}
	// 如果转换线程已经转换完毕，强制对话框退出...
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
	// 判断输入参数是否有效，如果无效，设定标志，退出线程...
	if (m_lpPPTWait == NULL || m_lpPPTWait->m_view == NULL || m_lpPPTWait->m_strFilePPT.size() <= 0) {
		m_bIsCanExit = true; m_strError = QTStr("PPT.ErrParam");
		return;
	}
	// 进行阻塞式格式转换...
	this->doPPTExportJPG();
	// 执行完毕，设定退出标志...
	m_bIsCanExit = true;
}

void CPPThread::doPPTExportJPG()
{
	OBSPropertiesView * lpView = m_lpPPTWait->m_view;
	QString & inStrFile = m_lpPPTWait->m_strFilePPT;
	// 计算文件全路径名称的MD5值..
	MD5	 md5; char path[512] = { 0 };
	md5.update(inStrFile.toStdString().c_str(), inStrFile.size());
	if (GetConfigPath(path, sizeof(path), "obs-smart/ppt") <= 0) {
		m_strError = QTStr("PPT.ErrPath").arg("obs-smart/ppt");
		return;
	}
	// 创建PPT转换后的JPG序列文件存放目录...
	sprintf(path, "%s/%s", path, md5.toString().c_str());
	int nError = os_mkdirs(path);
	if (nError == MKDIR_ERROR) {
		m_strError = QTStr("PPT.ErrMkDir").arg(path);
		return;
	}
	// 将转换后的目录路径保存到等待界面...
	m_lpPPTWait->m_strPathJPG = path;
	// 如果目录已经存在，询问是否覆盖...
	if (nError == MKDIR_EXISTS) {
		m_bIsCanPause = true;
		int button = QMessageBox::No;
		// 这里必须让界面线程去调用，否则，弹框时，会发生警告错误...
		QMetaObject::invokeMethod(this, "doConfirmExist",
			Qt::BlockingQueuedConnection,
			Q_RETURN_ARG(int, button));
		// 如果不覆盖，直接返回...
		if (button == QMessageBox::No)
			return;
		// 强制覆盖，界面继续更新...
		m_bIsCanPause = false;
	}
	// 线程必须注册到COM套件当中...
	CoInitialize(nullptr);
	// 打开PowerPoint软件...
	QAxObject *_powerPointAxObj = NULL;
	// 带空格和中文的目录，需要加上引号，否则打开PPT会失败...
	QVariant varFileName = QVariant(QString("\"%1\"").arg(inStrFile));
	do {
		_powerPointAxObj = new QAxObject("Powerpoint.Application", this);
		// 打开PowerPoint失败 => 没有安装PowerPoint软件...
		if (_powerPointAxObj == NULL || _powerPointAxObj->isNull()) {
			m_strError = QTStr("PPT.ErrSoft");
			break;
		}
		connect(_powerPointAxObj, SIGNAL(exception(int, QString, QString, QString)), this, SLOT(doPPTErrSlot(int, QString, QString, QString)));
		// 返回已打开演示文稿集合 => https://docs.microsoft.com/zh-cn/office/vba/api/powerpoint.application.presentations
		auto _presentations = _powerPointAxObj->querySubObject("Presentations");
		if (_presentations == NULL || _presentations->isNull()) {
			m_strError = QTStr("PPT.ErrPresent");
			break;
		}
		connect(_presentations, SIGNAL(exception(int, QString, QString, QString)), this, SLOT(doPPTErrSlot(int, QString, QString, QString)));
		// Presentations.Open方法参数 => https://docs.microsoft.com/zh-cn/office/vba/api/powerpoint.presentations.open
		auto _presentation = _presentations->querySubObject("Open(QString,QVariant,QVariant,QVariant)", varFileName, true, false, false);
		if (_presentation == NULL || _presentation->isNull()) {
			m_strError = QTStr("PPT.ErrOpen").arg(inStrFile);
			break;
		}
		connect(_presentation, SIGNAL(exception(int, QString, QString, QString)), this, SLOT(doPPTErrSlot(int, QString, QString, QString)));
		// MsoTriState枚举 => https://docs.microsoft.com/zh-cn/office/vba/api/office.msotristate
		// 整个窗口移出桌面 => https://docs.microsoft.com/zh-cn/office/vba/api/powerpoint.application.top
		// 将对象最小化 => https://docs.microsoft.com/zh-cn/office/vba/api/powerpoint.ppwindowstate
		auto _visible = _powerPointAxObj->setProperty("WindowState", 2);
		_visible = _powerPointAxObj->setProperty("Top", -9999);
		// Visible属性针对PowerPoint不起作用，Height和Width有效...
		_visible = _powerPointAxObj->setProperty("Visible", 0);
		_visible = _powerPointAxObj->setProperty("Height", 0);
		_visible = _powerPointAxObj->setProperty("Width", 0);
		// Presentation.Slides属性参数 => https://docs.microsoft.com/zh-cn/office/vba/api/powerpoint.presentation.slides
		//auto _slides = _presentation->querySubObject("Slides");
		//int numSlides = _slides->property("Count").toInt();
		// Presentation.Export方法参数 => https://docs.microsoft.com/zh-cn/office/vba/api/powerpoint.presentation.export
		_presentation->dynamicCall("Export(QString,QString,QVariant,QVariant)", path, "JPG");
		_presentation->dynamicCall("Close()");
	} while (false);
	// 关闭并删除PowerPoint对象的方法...
	if (_powerPointAxObj != NULL) {
		_powerPointAxObj->dynamicCall("Quit()");
		delete _powerPointAxObj; _powerPointAxObj = NULL;
	}
	// 线程注销COM套件...
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