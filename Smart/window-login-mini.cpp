
#include "window-login-mini.h"
#include "obs-app.hpp"
#include "FastSession.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMouseEvent>
#include <QPainter>
#include <QBitmap>
#include <QMovie>
#include <QMenu>
#include <time.h>

#include "win-update.hpp"
#include <windows.h>

#define UPDATE_CHECK_INTERVAL (60*60*24*4) /* 4 days */

void CLoginMini::TimedCheckForUpdates()
{
	// ����Ƿ����Զ����¿��أ�Ĭ���Ǵ��ڿ���״̬ => OBSApp::InitGlobalConfigDefaults()...
	if (!config_get_bool(App()->GlobalConfig(), "General", "EnableAutoUpdates"))
		return;
	// ��global.ini�ж�ȡ�ϴμ��ʱ����ϴ������汾��...
	long long lastUpdate = config_get_int(App()->GlobalConfig(), "General", "LastUpdateCheck");
	uint32_t lastVersion = config_get_int(App()->GlobalConfig(), "General", "LastVersion");
	// ����ϴ������汾�ȵ�ǰexe��Ű汾��ҪС����������...
	if (lastVersion < LIBOBS_API_VER) {
		lastUpdate = 0;
		config_set_int(App()->GlobalConfig(), "General", "LastUpdateCheck", 0);
	}
	// ���㵱ǰʱ�����ϴ�����֮���ʱ���...
	long long t    = (long long)time(nullptr);
	long long secs = t - lastUpdate;
	// ʱ����4�죬��ʼ��鲢ִ������...
	if (secs > UPDATE_CHECK_INTERVAL) {
		this->CheckForUpdates(false);
	}
}

void CLoginMini::CheckForUpdates(bool manualUpdate)
{
	// �������������˵��������������������ظ�����...
	if (updateCheckThread && updateCheckThread->isRunning())
		return;
	// ���������̣߳�������֮ => �����ֶ�ֱ������...
	updateCheckThread = new AutoUpdateThread(manualUpdate);
	updateCheckThread->start();

	UNUSED_PARAMETER(manualUpdate);
}

void CLoginMini::updateCheckFinished()
{
}

CLoginMini::CLoginMini(QWidget *parent)
  : QDialog(parent)
  , m_uTcpTimeID(0)
  , m_nDBUserID(-1)
  , m_nDBRoomID(-1)
  , m_nDBSmartID(-1)
  , m_lpMovieGif(NULL)
  , m_lpLoadBack(NULL)
  , m_nOnLineTimer(-1)
  , m_nTcpSocketFD(-1)
  , m_nCenterTimer(-1)
  , m_nCenterTcpPort(0)
  , m_CenterSession(NULL)
  , ui(new Ui::LoginMini)
  , m_eLoginCmd(kNoneCmd)
  , m_eMiniState(kCenterAddr)
{
	ui->setupUi(this);
	this->initWindow();
}

CLoginMini::~CLoginMini()
{
	if (m_nCenterTimer > 0) {
		this->killTimer(m_nCenterTimer);
		m_nCenterTimer = -1;
	}
	if (m_nOnLineTimer > 0) {
		this->killTimer(m_nOnLineTimer);
		m_nOnLineTimer = -1;
	}
	if (m_lpMovieGif != NULL) {
		delete m_lpMovieGif;
		m_lpMovieGif = NULL;
	}
	if (m_lpLoadBack != NULL) {
		delete m_lpLoadBack;
		m_lpLoadBack = NULL;
	}
	if (m_CenterSession != NULL) {
		delete m_CenterSession;
		m_CenterSession = NULL;
	}
	// �����˳�ʱ����Ҫ��֤�����߳��Ѿ���ȫ�˳���...
	if (updateCheckThread && updateCheckThread->isRunning()) {
		updateCheckThread->wait();
	}
	// ������Ҫ��ʾɾ�������̶߳���...
	if (updateCheckThread != NULL) {
		delete updateCheckThread;
		updateCheckThread = NULL;
	}
	// �������仯��������Ϣ���������ļ� => ��Ҫ���global.ini�ļ�...
	config_save_safe(GetGlobalConfig(), "tmp", nullptr);
}

void CLoginMini::loadStyleSheet(const QString &sheetName)
{
	QFile file(sheetName);
	file.open(QFile::ReadOnly);
	if (file.isOpen())
	{
		QString styleSheet = this->styleSheet();
		styleSheet += QLatin1String(file.readAll());
		this->setStyleSheet(styleSheet);
	}
}

// ����Բ�Ǵ��ڱ��� => �����Ƕ��㴰�ڣ���Ҫʹ���ɰ�...
void CLoginMini::paintEvent(QPaintEvent *event)
{
	QPainter painter(this);
	QPainterPath pathBack;

	// ��������������ɫ => Բ��ģʽ => WA_TranslucentBackground
	pathBack.setFillRule(Qt::WindingFill);
	painter.setRenderHint(QPainter::Antialiasing, true);
	pathBack.addRoundedRect(QRect(0, 0, this->width(), this->height()), 4, 4);
	painter.fillPath(pathBack, QBrush(QColor(0, 122, 204)));
	// ��ʾС�����ά��...
	if (!m_QPixQRCode.isNull() && m_QPixQRCode.width() > 0) {
		int nXPos = (this->size().width() - m_QPixQRCode.size().width()) / 2;
		int nYPos = ui->hori_title->totalSizeHint().height();
		painter.drawPixmap(nXPos, nYPos, m_QPixQRCode);
	}
	// ��ʾ�汾|ɨ��|����...
	ui->titleVer->setText(m_strVer);
	ui->titleScan->setText(m_strScan);
	ui->titleQR->setText(m_strQRNotice);

	QWidget::paintEvent(event);
}

void CLoginMini::initWindow()
{
	// ����ɨ��״̬��...
	m_strScan.clear();
	ui->iconScan->hide();
	// FramelessWindowHint�������ô���ȥ���߿�;
	// WindowMinimizeButtonHint ���������ڴ�����С��ʱ��������������ڿ�����ʾ��ԭ����;
	//Qt::WindowFlags flag = this->windowFlags();
	this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint);
	// ���ô��ڱ���͸�� => ����֮������ȫ������ => ���㴰����Ҫ�ñ����ɰ壬��ͨ����û����...
	this->setAttribute(Qt::WA_TranslucentBackground);
	// �رմ���ʱ�ͷ���Դ;
	this->setAttribute(Qt::WA_DeleteOnClose);
	// �����趨���ڴ�С...
	this->resize(360, 440);
	// ����PTZ���ڵĲ����ʾ��ʽ��...
	this->loadStyleSheet(":/mini/css/LoginMini.css");
	// ���ô���ͼ�� => ������pngͼƬ => �����Щ��������ico��������Ͻ�ͼ���޷���ʾ...
	this->setWindowIcon(QIcon(":/res/images/obs.png"));
	// ��ʼ���汾������Ϣ...
	m_strVer = QString("V%1").arg(OBS_VERSION);
	// ���ñ�������ͼƬ => gif
	m_lpMovieGif = new QMovie();
	m_lpLoadBack = new QLabel(this);
	m_lpMovieGif->setFileName(":/mini/images/mini/loading.gif");
	m_lpLoadBack->setMovie(m_lpMovieGif);
	m_lpMovieGif->start();
	m_lpLoadBack->raise();
	// �������ƶ������ھ�����ʾ...
	QRect rcRect = this->rect();
	QRect rcSize = m_lpMovieGif->frameRect();
	int nXPos = (rcRect.width() - rcSize.width()) / 2;
	int nYPos = (rcRect.height() - rcSize.height()) / 2 - ui->btnClose->height() * 2 - 10;
	m_lpLoadBack->move(nXPos, nYPos);
	// ���������С����ť|�رհ�ť���źŲ��¼�...
	connect(ui->btnMin, SIGNAL(clicked()), this, SLOT(onButtonMinClicked()));
	connect(ui->btnClose, SIGNAL(clicked()), this, SLOT(onButtonCloseClicked()));
	connect(ui->btnArrow, SIGNAL(clicked()), this, SLOT(onButtonTypeClicked()));
	// ���������źŲ۷�������¼�...
	connect(&m_objNetManager, SIGNAL(finished(QNetworkReply *)), this, SLOT(onReplyFinished(QNetworkReply *)));
	// ���±��������� => �����ն����ͽ��е���...
	this->doUpdateTitle();
	// �����ȡ���ķ�������TCP��ַ�Ͷ˿ڵ�����...
	this->doWebGetCenterAddr();
	// ֻ����һ�θ���״̬���...
	//this->TimedCheckForUpdates();
}

void CLoginMini::doUpdateTitle()
{
	// ������ϱ��������ƣ����µ����浱��...
	QString strTitle = QString("%1 - %2").arg(QTStr("MINI.Window.Title")).arg(App()->GetClientTypeName());
	ui->titleName->setText(strTitle);
	// ������ⲿ����ģʽ����Ҫ�޸ı���������...
	//ui->titleName->setText(QTStr("MINI.Window.Normal"));
}

void CLoginMini::onButtonTypeClicked()
{
	QMenu popup(this);
	QAction * actionStudent = NULL; QAction * actionTeacher = NULL;
	actionStudent = popup.addAction(QTStr("MINI.Menu.Student"), this, SLOT(onChangeToStudent()));
	actionTeacher = popup.addAction(QTStr("MINI.Menu.Teacher"), this, SLOT(onChangeToTeacher()));
	actionStudent->setCheckable(true); actionTeacher->setCheckable(true);
	switch (App()->GetClientType()) {
	case kClientStudent: actionStudent->setChecked(true); actionTeacher->setChecked(false); break;
	case kClientTeacher: actionStudent->setChecked(false); actionTeacher->setChecked(true); break;
	}
	popup.exec(QCursor::pos());
}

void CLoginMini::onChangeToStudent()
{
	this->doChangedNewType(kClientStudent);
}

void CLoginMini::onChangeToTeacher()
{
	this->doChangedNewType(kClientTeacher);
}

void CLoginMini::doChangedNewType(int nNewType)
{
	// ����ն�����û�з����仯��ֱ�ӷ���...
	if (App()->GetClientType() == nNewType)
		return;
	// �����ն����ͣ����±��⣬�������õ� global.ini����...
	config_set_int(GetGlobalConfig(), "General", "ClientType", (int64_t)nNewType);
	config_save_safe(GetGlobalConfig(), "tmp", nullptr);
	App()->SetClientType((CLIENT_TYPE)nNewType);
	this->doUpdateTitle();
	// �����������ķ�����...
	this->doWebGetCenterAddr();
}

void CLoginMini::doWebGetMiniToken()
{
	// �޸Ļ�ȡС����token״̬...
	m_eMiniState = kMiniToken;
	m_strQRNotice = QStringLiteral("���ڻ�ȡ����ƾ֤...");
	// ����ƾ֤���ʵ�ַ��������������...
	QNetworkReply * lpNetReply = NULL;
	QNetworkRequest theQTNetRequest;
	string & strWebCenter = App()->GetWebCenterAddr();
	QString strRequestURL = QString("%1%2").arg(strWebCenter.c_str()).arg("/wxapi.php/Mini/getToken");
	theQTNetRequest.setUrl(QUrl(strRequestURL));
	lpNetReply = m_objNetManager.get(theQTNetRequest);
	// ������ʾ��������...
	this->update();
}

void CLoginMini::onProcMiniToken(QNetworkReply *reply)
{
	Json::Value value;
	ASSERT(m_eMiniState == kMiniToken);
	QByteArray & theByteArray = reply->readAll();
	string & strData = theByteArray.toStdString();
	// ����ǻ�ȡtoken״̬��������ȡ��json����...
	blog(LOG_DEBUG, "QT Reply Data => %s", strData.c_str());
	if (!this->parseJson(strData, value, false)) {
		m_lpLoadBack->hide();
		this->update();
		return;
	}
	// ����json�ɹ�������token��·��...
	m_strMiniToken = OBSApp::getJsonString(value["access_token"]);
	m_strMiniPath = OBSApp::getJsonString(value["mini_path"]);
	// �����ȡС�����ά��Ĳ���...
	this->doWebGetMiniQRCode();
}

void CLoginMini::doWebGetMiniQRCode()
{
	// �ж�access_token��path�Ƿ���Ч...
	if (m_strMiniPath.size() <= 0 || m_strMiniToken.size() <= 0) {
		m_strQRNotice = QStringLiteral("��ȡ����ƾ֤ʧ�ܣ��޷���ȡС������");
		this->update();
		return;
	}
	// �޸Ļ�ȡС�����ά��״̬...
	m_strQRNotice = QStringLiteral("���ڻ�ȡС������...");
	m_eMiniState = kMiniQRCode;
	QNetworkReply * lpNetReply = NULL;
	QNetworkRequest theQTNetRequest;
	// ��ά�볡��ֵ�������������...
	//srand((unsigned int)time(NULL));
	// ׼����Ҫ�Ļ㱨���� => POST���ݰ�...
	Json::Value itemData;
	itemData["width"] = kQRCodeWidth;
	itemData["page"] = m_strMiniPath.c_str();
	// ��ά�볡��ֵ => �û����� + �׽��� + ʱ���ʶ => �ܳ��Ȳ�����32�ֽ�...
	itemData["scene"] = QString("%1_%2_%3").arg(App()->GetClientType()).arg(m_nTcpSocketFD).arg(m_uTcpTimeID).toStdString();
	QString strRequestURL = QString("https://api.weixin.qq.com/wxa/getwxacodeunlimit?access_token=%1").arg(m_strMiniToken.c_str());
	QString strContentVal = QString("%1").arg(itemData.toStyledString().c_str());
	theQTNetRequest.setUrl(QUrl(strRequestURL));
	theQTNetRequest.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/x-www-form-urlencoded"));
	lpNetReply = m_objNetManager.post(theQTNetRequest, strContentVal.toUtf8());
	// ������ʾ��������...
	this->update();
}

void CLoginMini::onProcMiniQRCode(QNetworkReply *reply)
{
	// ���۶Դ���Ҫ�رռ��ض���...
	m_lpLoadBack->hide();
	// ��ȡ��ȡ����������������...
	ASSERT(m_eMiniState == kMiniQRCode);
	QByteArray & theByteArray = reply->readAll();
	// ����·����ֱ�ӹ����ά���ͼƬ��Ϣ|�趨Ĭ��ɨ����Ϣ...
	if (m_QPixQRCode.loadFromData(theByteArray)) {
		m_strScan = QStringLiteral("��ʹ��΢��ɨ���ά���¼");
		m_strQRNotice.clear();
		this->update();
		return;
	}
	// ���λͼ��ʾ����...
	Json::Value value;
	m_QPixQRCode.detach();
	// �����ά��ͼƬ����ʧ�ܣ����д������...
	string & strData = theByteArray.toStdString();
	if (!this->parseJson(strData, value, true)) {
		this->update();
		return;
	}
	// ����json���ݰ���Ȼʧ�ܣ���ʾ�ض��Ĵ�����Ϣ...
	m_strQRNotice = QStringLiteral("������ȡ����JSON����ʧ��");
	this->update();
}

// ��ʽ��¼ָ������ => ��ȡ�ڵ��������Ϣ...
void CLoginMini::doWebGetMiniLoginRoom()
{
	// ��ʾ�������޸�״̬...
	ui->iconScan->hide();
	m_lpLoadBack->show();
	m_strQRNotice.clear();
	m_eMiniState = kMiniLoginRoom;
	m_strScan = QStringLiteral("���ڵ�¼��ѡ��ķ���...");
	ui->titleScan->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	// ����������ʵ�ַ��������������...
	QNetworkReply * lpNetReply = NULL;
	QNetworkRequest theQTNetRequest;
	string & strWebCenter = App()->GetWebCenterAddr();
	QString strContentVal = QString("room_id=%1&type_id=%2&debug_mode=%3").arg(m_nDBRoomID).arg(App()->GetClientType()).arg(App()->IsDebugMode());
	QString strRequestURL = QString("%1%2").arg(strWebCenter.c_str()).arg("/wxapi.php/Mini/loginRoom");
	theQTNetRequest.setUrl(QUrl(strRequestURL));
	theQTNetRequest.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/x-www-form-urlencoded"));
	lpNetReply = m_objNetManager.post(theQTNetRequest, strContentVal.toUtf8());
	// ������ʾ��������...
	this->update();
}

void CLoginMini::onProcMiniLoginRoom(QNetworkReply *reply)
{
	Json::Value value;
	bool bIsError = false;
	ASSERT(m_eMiniState == kMiniLoginRoom);
	QByteArray & theByteArray = reply->readAll();
	string & strData = theByteArray.toStdString();
	blog(LOG_DEBUG, "QT Reply Data => %s", strData.c_str());
	do {
		// ����json���ݰ�ʧ�ܣ����ñ�־����...
		if (!this->parseJson(strData, value, false)) {
			m_strScan = m_strQRNotice;
			m_strQRNotice.clear();
			bIsError = true;
			break;
		}
		// ����������ȡ���Ĵ洢��������ַ�Ͷ˿�...
		string strTrackerAddr = OBSApp::getJsonString(value["tracker_addr"]);
		string strTrackerPort = OBSApp::getJsonString(value["tracker_port"]);
		// ����������ȡ����Զ����ת��������ַ�Ͷ˿�...
		string strRemoteAddr = OBSApp::getJsonString(value["remote_addr"]);
		string strRemotePort = OBSApp::getJsonString(value["remote_port"]);
		// ����������ȡ����udp��������ַ�Ͷ˿�...
		string strUdpAddr = OBSApp::getJsonString(value["udp_addr"]);
		string strUdpPort = OBSApp::getJsonString(value["udp_port"]);
		// ����������ȡ���ķ�����Ľ�ʦ��ѧ������...
		string strTeacherCount = OBSApp::getJsonString(value["teacher"]);
		string strStudentCount = OBSApp::getJsonString(value["student"]);
		if (strTrackerAddr.size() <= 0 || strTrackerPort.size() <= 0 || strRemoteAddr.size() <= 0 || strRemotePort.size() <= 0 ||
			strUdpAddr.size() <= 0 || strUdpPort.size() <= 0 || strTeacherCount.size() <= 0 || strStudentCount.size() <= 0 ) {
			m_strScan = QTStr("Teacher.Room.Json");
			bIsError = true;
			break;
		}
		// ���㲢�жϷ�����Ľ�ʦ����������0�����ǽ�ʦ�նˣ����ܵ�¼...
		int  nTeacherCount = atoi(strTeacherCount.c_str());
		int  nStudentCount = atoi(strStudentCount.c_str());
		bool bIsTeacher = ((App()->GetClientType() == kClientTeacher) ? true : false);
		// ����������Ѿ��н�ʦ�������Լ�Ҳ�ǽ�ʦ����Ҫ���򾯸�...
		if (bIsTeacher && nTeacherCount > 0) {
			m_strScan = QTStr("Teacher.Room.Login");
			bIsError = true;
			break;
		}
		// ����ȡ������ص�ַ��Ϣ��ŵ�ȫ�ֶ�����...
		App()->SetTrackerAddr(strTrackerAddr);
		App()->SetTrackerPort(atoi(strTrackerPort.c_str()));
		App()->SetRemoteAddr(strRemoteAddr);
		App()->SetRemotePort(atoi(strRemotePort.c_str()));
		App()->SetUdpAddr(strUdpAddr);
		App()->SetUdpPort(atoi(strUdpPort.c_str()));
	} while (false);
	// ע�⣺�����΢��ɨ�룬������ʾ���󣬻���С������Զ�����...
	// ע�⣺Ϊ�˼��ݲ���ģʽ��������Ϣ��ʾ�ڶ�ά������...
	if (bIsError) {
		m_QPixQRCode = QPixmap();
		m_lpLoadBack->hide(); ui->iconScan->hide();
		m_strQRNotice = m_strScan; m_strScan.clear();
		//ui->titleScan->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		//QString strStyle = QString("background-image: url(:/mini/images/mini/scan.png);background-repeat: no-repeat;margin-left: 25px;margin-top: -87px;");
		//ui->iconScan->setStyleSheet(strStyle);
		this->update();
		return;
	}
	// ��ȡ��¼�û�����ϸ��Ϣ...
	this->doWebGetMiniUserInfo();
}

// �޸�״̬����ȡ�󶨵�¼�û�����ϸ��Ϣ...
void CLoginMini::doWebGetMiniUserInfo()
{
	// ��ʾ�������޸�״̬...
	ui->iconScan->hide();
	m_lpLoadBack->show();
	m_strQRNotice.clear();
	m_eMiniState = kMiniUserInfo;
	m_strScan = QStringLiteral("���ڻ�ȡ�ѵ�¼�û���Ϣ...");
	ui->titleScan->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	// ����������ʵ�ַ��������������...
	QNetworkReply * lpNetReply = NULL;
	QNetworkRequest theQTNetRequest;
	string & strWebCenter = App()->GetWebCenterAddr();
	QString strContentVal = QString("user_id=%1&room_id=%2&type_id=%3&smart_id=%4")
		.arg(m_nDBUserID).arg(m_nDBRoomID).arg(App()->GetClientType()).arg(m_nDBSmartID);
	QString strRequestURL = QString("%1%2").arg(strWebCenter.c_str()).arg("/wxapi.php/Mini/getLoginUser");
	theQTNetRequest.setUrl(QUrl(strRequestURL));
	theQTNetRequest.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/x-www-form-urlencoded"));
	lpNetReply = m_objNetManager.post(theQTNetRequest, strContentVal.toUtf8());
	// ������ʾ��������...
	this->update();
}

void CLoginMini::onProcMiniUserInfo(QNetworkReply *reply)
{
	Json::Value value;
	bool bIsError = false;
	ASSERT(m_eMiniState == kMiniUserInfo);
	QByteArray & theByteArray = reply->readAll();
	string & strData = theByteArray.toStdString();
	blog(LOG_DEBUG, "QT Reply Data => %s", strData.c_str());
	do {
		// ����json���ݰ�ʧ�ܣ����ñ�־����...
		if (!this->parseJson(strData, value, false)) {
			m_strScan = m_strQRNotice;
			m_strQRNotice.clear();
			bIsError = true;
			break;
		}
		// �ж��Ƿ��ȡ������ȷ���û��ͷ�����Ϣ...
		if (!value.isMember("user") || !value.isMember("room")) {
			m_strScan = QStringLiteral("������ʾ���޷��ӷ�������ȡ�û��򷿼����顣");
			bIsError = true;
			break;
		}
		// �жϻ�ȡ�����û���Ϣ�Ƿ���Ч...
		int nDBUserID = atoi(OBSApp::getJsonString(value["user"]["user_id"]).c_str());
		int nUserType = atoi(OBSApp::getJsonString(value["user"]["user_type"]).c_str());
		ASSERT(nDBUserID == m_nDBUserID);
		// ������ͱ�����ڵ��ڽ�ʦ���...
		if (nUserType < 2) {
			m_strScan = QStringLiteral("������ʾ����¼�û����ڽ�ʦ��ݣ��޷�ʹ�ý�ʦ�������");
			bIsError = true;
			break;
		}
		// �ж��Ƿ��ȡ������Ч������ͳ�Ƽ�¼�ֶ�...
		int nDBFlowID = (value.isMember("flow_id") ? atoi(OBSApp::getJsonString(value["flow_id"]).c_str()) : 0);
		if (!value.isMember("flow_id") || nDBFlowID <= 0) {
			m_strScan = QStringLiteral("������ʾ���޷��ӷ�������ȡ����ͳ�Ƽ�¼��š�");
			bIsError = true;
			break;
		}
		// ����������¼��ȫ�ֱ�������...
		App()->SetDBFlowID(nDBFlowID);
		App()->SetJsonUser(value["user"]);
	} while (false);
	// �������󣬹رն�������ʾͼ��|��Ϣ�����������...
	if (bIsError) {
		m_lpLoadBack->hide(); ui->iconScan->show();
		ui->titleScan->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		QString strStyle = QString("background-image: url(:/mini/images/mini/scan.png);background-repeat: no-repeat;margin-left: 25px;margin-top: -87px;");
		ui->iconScan->setStyleSheet(strStyle);
		this->update();
		return;
	}
	// ����ҳ����ת���ر�С�����¼����...
	emit this->doTriggerMiniSuccess();
}

void CLoginMini::onReplyFinished(QNetworkReply *reply)
{
	// �������������󣬴�ӡ������Ϣ������ѭ��...
	if (reply->error() != QNetworkReply::NoError) {
		blog(LOG_INFO, "QT error => %d, %s", reply->error(), reply->errorString().toStdString().c_str());
		m_strQRNotice = QString("%1, %2").arg(reply->error()).arg(reply->errorString());
		m_lpLoadBack->hide();
		this->update();
		return;
	}
	// ��ȡ�������������󷵻ص��������ݰ�...
	int nStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	blog(LOG_INFO, "QT Status Code => %d", nStatusCode);
	// ����״̬�ַ��������...
	switch (m_eMiniState) {
	case kCenterAddr: this->onProcCenterAddr(reply); break;
	case kMiniToken:  this->onProcMiniToken(reply); break;
	case kMiniQRCode: this->onProcMiniQRCode(reply); break;
	case kMiniUserInfo: this->onProcMiniUserInfo(reply); break;
	case kMiniLoginRoom: this->onProcMiniLoginRoom(reply); break;
	}
}

void CLoginMini::doWebGetCenterAddr()
{
	// ���ĻỰ������Ч����Ҫ�ؽ�֮...
	if (m_CenterSession != NULL) {
		delete m_CenterSession;
		m_CenterSession = NULL;
	}
	// ��ʾ���ض������ع���ά��...
	m_QPixQRCode = QPixmap();
	m_lpLoadBack->show();
	// ����ɨ��״̬��...
	m_strScan.clear();
	ui->iconScan->hide();
	// ɾ���������Ӽ��ʱ�Ӷ���...
	if (m_nCenterTimer != -1) {
		this->killTimer(m_nCenterTimer);
		m_nCenterTimer = -1;
	}
	// ɾ����������������������...
	if (m_nOnLineTimer != -1) {
		this->killTimer(m_nOnLineTimer);
		m_nOnLineTimer = -1;
	}
	// �޸Ļ�ȡ���ķ�����TCP��ַ״̬...
	m_eMiniState = kCenterAddr;
	m_strQRNotice = QStringLiteral("���ڻ�ȡ���ķ�������ַ...");
	ui->titleScan->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	// ׼����Ҫ�Ļ㱨���� => POST���ݰ�...
	// ע�⣺������ Json::Value ģʽ��QT���POST���ݱ��룬���PHP�޷�����...
	// ע�⣺���������ģʽ��û�в���json���ݰ���ʽ����ȡ΢�Ŷ�ά��ʱû�����⣬΢�Žӿڹ��ƴ��������ֱ��������������doWebGetMiniQRCode
	char szDNS[MAX_PATH] = { 0 };
	string strUTF8DNS = OBSApp::GetServerDNSName();
	// ע�⣺��Ϊ��HTTPЭ�鴫�䣬��Ҫ�Դ������ݽ���������룬��ֹ����ʱ�ж�...
	OBSApp::EncodeURI(strUTF8DNS.c_str(), strUTF8DNS.size(), szDNS, MAX_PATH);
	QString strContentVal = QString("mac_addr=%1&ip_addr=%2&os_name=%3&version=%4&name_pc=%5")
		.arg(App()->GetLocalMacAddr().c_str()).arg(App()->GetLocalIPAddr().c_str())
		.arg(OBSApp::GetServerOS()).arg(OBS_VERSION).arg(szDNS);
	// ����������վ��Ҫ��URL��ַ��POST�������ݰ�...
	QNetworkReply * lpNetReply = NULL;
	QNetworkRequest theQTNetRequest;
	string & strWebCenter = App()->GetWebCenterAddr();
	QString strRequestURL = QString("%1%2").arg(strWebCenter.c_str()).arg("/wxapi.php/Mini/regSmart");
	theQTNetRequest.setUrl(QUrl(strRequestURL));
	theQTNetRequest.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/x-www-form-urlencoded"));
	lpNetReply = m_objNetManager.post(theQTNetRequest, strContentVal.toUtf8());
	// ������ʾ��������...
	this->update();
}

void CLoginMini::onProcCenterAddr(QNetworkReply *reply)
{
	Json::Value value;
	ASSERT(m_eMiniState == kCenterAddr);
	QByteArray & theByteArray = reply->readAll();
	string & strData = theByteArray.toStdString();
	blog(LOG_DEBUG, "QT Reply Data => %s", strData.c_str());
	if (!this->parseJson(strData, value, false)) {
		m_lpLoadBack->hide();
		this->update();
		return;
	}
	// ����json�ɹ�������smart_id��UDPCenter��TCP��ַ�Ͷ˿�...
	m_nDBSmartID = atoi(OBSApp::getJsonString(value["smart_id"]).c_str());
	m_strCenterTcpAddr = OBSApp::getJsonString(value["udpcenter_addr"]);
	m_nCenterTcpPort = atoi(OBSApp::getJsonString(value["udpcenter_port"]).c_str());
	// �����ķ�������ַ�Ͷ˿ڱ��浽App���б���...
	App()->SetTcpCenterAddr(m_strCenterTcpAddr);
	App()->SetTcpCenterPort(m_nCenterTcpPort);
	App()->SetDBSmartID(m_nDBSmartID);
	// �����������ķ������ĻỰ����...
	this->doTcpConnCenterAddr();
}

void CLoginMini::doTcpConnCenterAddr()
{
	// �޸��������ķ�����״̬...
	m_eMiniState = kCenterConn;
	m_strQRNotice = QStringLiteral("�����������ķ�����...");
	// �������ĻỰ���� => ����С������...
	ASSERT(m_CenterSession == NULL);
	m_CenterSession = new CCenterSession();
	m_CenterSession->InitSession(m_strCenterTcpAddr.c_str(), m_nCenterTcpPort);
	// ע����ص��¼��������� => ͨ���źŲ���Ϣ��...
	this->connect(m_CenterSession, SIGNAL(doTriggerTcpConnect()), this, SLOT(onTriggerTcpConnect()));
	this->connect(m_CenterSession, SIGNAL(doTriggerBindMini(int, int, int)), this, SLOT(onTriggerBindMini(int, int, int)));
	// ����һ����ʱ�ؽ����ʱ�� => ÿ��5��ִ��һ��...
	m_nCenterTimer = this->startTimer(5 * 1000);
	// ������ʾ��������...
	this->update();
}

// ��Ӧ���ĻỰ������¼�֪ͨ�źŲ�...
void CLoginMini::onTriggerTcpConnect()
{
	// ����ʧ�ܣ���ӡ������Ϣ...
	ASSERT(m_CenterSession != NULL);
	if (!m_CenterSession->IsConnected()) {
		m_strQRNotice = QString("%1%2").arg(QStringLiteral("�������ķ�����ʧ�ܣ�����ţ�")).arg(m_CenterSession->GetErrCode());
		blog(LOG_INFO, "QT error => %d, %s:%d", m_CenterSession->GetErrCode(), m_strCenterTcpAddr.c_str(), m_nCenterTcpPort);
		m_eMiniState = kCenterAddr;
		m_lpLoadBack->hide();
		m_strScan.clear();
		this->update();
		return;
	}
	// ������������������ķ�����״̬��ֱ�ӷ���...
	if (m_eMiniState != kCenterConn)
		return;
	ASSERT(m_eMiniState == kCenterConn);
	// �����ĻỰ�ڷ������ϵ��׽��ֱ�Ž��б���...
	// ע�⣺CApp���б������CRemoteSession���׽���...
	m_nTcpSocketFD = m_CenterSession->GetTcpSocketFD();
	m_uTcpTimeID = m_CenterSession->GetTcpTimeID();
	// ÿ��30����һ�Σ��ն������ķ����������߻㱨֪ͨ...
	m_nOnLineTimer = this->startTimer(30 * 1000);
	// �����ȡС����Tokenֵ����������...
	//this->doWebGetMiniToken();
	
	/*== �������ٲ��� ==*/
	m_nDBUserID = 1;
	m_nDBRoomID = 10001;
	// һ����������ʼ��¼ָ���ķ���...
	this->doWebGetMiniLoginRoom();  
}

// ��Ӧ���ĻỰ������С����󶨵�¼�źŲ��¼�֪ͨ...
void CLoginMini::onTriggerBindMini(int nUserID, int nBindCmd, int nRoomID)
{
	// �����ǰ״̬���Ƕ�ά����ʾ״̬��ֱ�ӷ���...
	//if (m_eMiniState != kMiniQRCode)
	//	return;
	// ���ݰ���������ʾ��ͬ����Ϣ��ͼƬ״̬...
	if (nBindCmd == kScanCmd) {
		m_strScan = QStringLiteral("��ɨ��ɹ�������΢��������������룬�����Ȩ��¼��");
		QString strStyle = QString("background-image: url(:/mini/images/mini/scan.png);background-repeat: no-repeat;margin-left: 25px;margin-top: -46px;");
		ui->iconScan->setStyleSheet(strStyle);
		ui->iconScan->show();
		this->update();
	} else if (nBindCmd == kCancelCmd) {
		m_strScan = QStringLiteral("����ȡ���˴ε�¼�������ٴ�ɨ���¼����رմ��ڡ�");
		QString strStyle = QString("background-image: url(:/mini/images/mini/scan.png);background-repeat: no-repeat;margin-left: 25px;margin-top: -87px;");
		ui->iconScan->setStyleSheet(strStyle);
		ui->iconScan->show();
		this->update();
	} else if (nBindCmd == kSaveCmd) {
		// ����û����|��������Ч����ʾ������Ϣ...
		if (nUserID <= 0 || nRoomID <= 0) {
			m_strScan = QStringLiteral("�û���Ż򷿼�����Ч����ȷ�Ϻ�������΢��ɨ���¼��");
			QString strStyle = QString("background-image: url(:/mini/images/mini/scan.png);background-repeat: no-repeat;margin-left: 25px;margin-top: -87px;");
			ui->iconScan->setStyleSheet(strStyle);
			ui->iconScan->show();
			this->update();
			return;
		}
		// �����û����|������...
		m_nDBUserID = nUserID;
		m_nDBRoomID = nRoomID;
		// һ����������ʼ��¼ָ���ķ���...
		this->doWebGetMiniLoginRoom();
	}
}

void CLoginMini::timerEvent(QTimerEvent *inEvent)
{
	int nTimerID = inEvent->timerId();
	if (nTimerID == m_nCenterTimer) {
		this->doCheckSession();
	} else if (nTimerID == m_nOnLineTimer) {
		this->doCheckOnLine();
	}
}

void CLoginMini::doCheckSession()
{
	// ������ĻỰ������Ч��Ͽ��������ȡ���ķ�������TCP��ַ�Ͷ˿ڵ�����...
	if (m_CenterSession == NULL || !m_CenterSession->IsConnected()) {
		this->doWebGetCenterAddr();
	}
}

bool CLoginMini::doCheckOnLine()
{
	if (m_CenterSession == NULL)
		return false;
	return m_CenterSession->doSendOnLineCmd();
}

bool CLoginMini::parseJson(string & inData, Json::Value & outValue, bool bIsWeiXin)
{
	Json::Reader reader;
	if (!reader.parse(inData, outValue)) {
		m_strQRNotice = QStringLiteral("������ȡ����JSON����ʧ��");
		return false;
	}
	const char * lpszCode = bIsWeiXin ? "errcode" : "err_code";
	const char * lpszMsg = bIsWeiXin ? "errmsg" : "err_msg";
	if (outValue[lpszCode].asBool()) {
		string & strMsg = OBSApp::getJsonString(outValue[lpszMsg]);
		string & strCode = OBSApp::getJsonString(outValue[lpszCode]);
		m_strQRNotice = QString("%1").arg(strMsg.c_str());
		return false;
	}
	return true;
}

void CLoginMini::onButtonCloseClicked()
{
	this->close();
}

void CLoginMini::onButtonMinClicked()
{
	this->showMinimized();
}

// ����ͨ�� mousePressEvent | mouseMoveEvent | mouseReleaseEvent �����¼�ʵ��������϶��������ƶ����ڵ�Ч��;
void CLoginMini::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		m_isPressed = true;
		m_startMovePos = event->globalPos();
	}
	return QWidget::mousePressEvent(event);
}

void CLoginMini::mouseMoveEvent(QMouseEvent *event)
{
	if (m_isPressed) {
		QPoint movePoint = event->globalPos() - m_startMovePos;
		QPoint widgetPos = this->pos() + movePoint;
		m_startMovePos = event->globalPos();
		this->move(widgetPos.x(), widgetPos.y());
	}
	return QWidget::mouseMoveEvent(event);
}

void CLoginMini::mouseReleaseEvent(QMouseEvent *event)
{
	m_isPressed = false;
	return QWidget::mouseReleaseEvent(event);
}
