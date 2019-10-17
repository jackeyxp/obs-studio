
#include "obs-app.hpp"
#include "window-student.h"

#include <util/profiler.hpp>

#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

CStudentWindow::CStudentWindow(QWidget *parent)
  : OBSMainWindow(parent)
  , ui(new Ui::StudentWindow)
{
	setAttribute(Qt::WA_NativeWindow);
	ui->setupUi(this);
	this->initWindow();
}

CStudentWindow::~CStudentWindow()
{

}

void CStudentWindow::OBSInit()
{
	ProfileScope("CStudentWindow::OBSInit");
	this->show();
}

config_t * CStudentWindow::Config() const
{
	return basicConfig;
}

int CStudentWindow::GetProfilePath(char *path, size_t size, const char *file) const
{
	char profiles_path[512];
	const char *profile = config_get_string(App()->GlobalConfig(), "Basic", "ProfileDir");
	int ret;

	if (!profile)
		return -1;
	if (!path)
		return -1;
	if (!file)
		file = "";

	ret = GetConfigPath(profiles_path, 512, "obs-smart/basic/profiles");
	if (ret <= 0)
		return ret;

	if (!*file) {
		return snprintf(path, size, "%s/%s", profiles_path, profile);
	}
	return snprintf(path, size, "%s/%s/%s", profiles_path, profile, file);
}

void CStudentWindow::initWindow()
{
	// FramelessWindowHint�������ô���ȥ���߿�;
	// WindowMinimizeButtonHint ���������ڴ�����С��ʱ��������������ڿ�����ʾ��ԭ����;
	//Qt::WindowFlags flag = this->windowFlags();
	this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
	// ���ô��ڱ���͸�� => ����֮������ȫ������ => ���㴰����Ҫ�ñ����ɰ壬��ͨ����û����...
	this->setAttribute(Qt::WA_TranslucentBackground);
	// �رմ���ʱ�ͷ���Դ;
	this->setAttribute(Qt::WA_DeleteOnClose);
	// ���ô���ͼ�� => ������pngͼƬ => �����Щ��������ico��������Ͻ�ͼ���޷���ʾ...
	this->setWindowIcon(QIcon(":/res/images/obs.png"));
	// �жϵ�ǰ������ķֱ����Ƿ񳬹�1280x720�����û�г�������Ĭ�ϵ�...
	QScreen * lpCurScreen = QGuiApplication::primaryScreen();
	QRect rcScreen = lpCurScreen->geometry();
	// �����趨���ڴ�С => ������ֱ��ʳ���1280x720��ʱ��...
	if (rcScreen.width() >= 1280 && rcScreen.height() >= 720) {
		this->resize(1280, 720);
	}
	// ���¼��㴰����ʾλ��...
	QRect rcRect = this->geometry();
	int nLeftPos = (rcScreen.width() - rcRect.width()) / 2;
	int nTopPos = (rcScreen.height() - rcRect.height()) / 2;
	this->setGeometry(nLeftPos, nTopPos, rcRect.width(), rcRect.height());
	// ������º�Ĵ��ڵ������꣬�Ա����֮��Ļָ�ʹ��...
	m_rcSrcGeometry = this->geometry();
	// ���������С����ť|���|�رհ�ť���źŲ��¼�...
	connect(ui->btnMin, SIGNAL(clicked()), this, SLOT(onButtonMinClicked()));
	connect(ui->btnMax, SIGNAL(clicked()), this, SLOT(onButtonMaxClicked()));
	connect(ui->btnClose, SIGNAL(clicked()), this, SLOT(onButtonCloseClicked()));
	// �������������������ť���źŲ��¼�...
	connect(ui->btnAbout, SIGNAL(clicked()), this, SLOT(onButtonAboutClicked()));
	connect(ui->btnUpdate, SIGNAL(clicked()), this, SLOT(onButtonUpdateClicked()));
	connect(ui->btnCamera, SIGNAL(clicked()), this, SLOT(onButtonCameraClicked()));
	connect(ui->btnSystem, SIGNAL(clicked()), this, SLOT(onButtonSystemClicked()));
	// ���ش��ڵĲ����ʾ��ʽ��...
	this->loadStyleSheet(":/student/css/Student.css");
	// ��ȡ������Ĭ�ϵ�ͷ����� => ��Ҫ���б�Ҫ�����Ų���...
	m_QPixUserHead = QPixmap(":/res/images/avatar.png").scaled(50, 50);
	m_strUserNickName = QString("%1").arg(App()->GetUserNickName().c_str());
	m_strUserHeadUrl = QString("%1").arg(App()->GetUserHeadUrl().c_str());
}

void CStudentWindow::loadStyleSheet(const QString &sheetName)
{
	QFile file(sheetName);
	file.open(QFile::ReadOnly);
	if (file.isOpen()) {
		QString styleSheet = this->styleSheet();
		styleSheet += QLatin1String(file.readAll());
		this->setStyleSheet(styleSheet);
	}
}

void CStudentWindow::onButtonMinClicked()
{
	this->showMinimized();
}

void CStudentWindow::onButtonMaxClicked()
{
	// ����Ѿ������ => �ָ���ʼ����...
	if (this->isMaximized()) {
		ui->btnMax->setStyleSheet(QString("%1%2%3").arg("QPushButton{border-image:url(:/student/images/student/maxing.png) 0 40 0 0;}")
			.arg("QPushButton:hover{border-image:url(:/student/images/student/maxing.png) 0 20 0 20;}")
			.arg("QPushButton:pressed{border-image:url(:/student/images/student/maxing.png) 0 0 0 40;}"));
		ui->btnMax->setToolTip(QTStr("Student.Tips.Maxing"));
		this->setGeometry(m_rcSrcGeometry);
		this->showNormal();
		return;
	}
	// ���������� => �����ʾ...
	this->showMaximized();
	// ������󻯰�ť��ͼ����ʽ����ʾ��Ϣ...
	ui->btnMax->setToolTip(QTStr("Student.Tips.Maxed"));
	ui->btnMax->setStyleSheet(QString("%1%2%3").arg("QPushButton{border-image:url(:/student/images/student/maxed.png) 0 40 0 0;}")
		.arg("QPushButton:hover{border-image:url(:/student/images/student/maxed.png) 0 20 0 20;}")
		.arg("QPushButton:pressed{border-image:url(:/student/images/student/maxed.png) 0 0 0 40;}"));
}

void CStudentWindow::onButtonCloseClicked()
{
	this->close();
}

void CStudentWindow::onButtonAboutClicked()
{

}

void CStudentWindow::onButtonUpdateClicked()
{

}

void CStudentWindow::onButtonCameraClicked()
{

}

void CStudentWindow::onButtonSystemClicked()
{

}

void CStudentWindow::paintEvent(QPaintEvent *event)
{
	QPainter painter(this);
	// �Ȼ��Ʊ�������������...
	QRect rcTitleRight = ui->hori_title->geometry();
	painter.fillRect(rcTitleRight, QColor(47, 47, 53));
	painter.setPen(QColor(61, 63, 70));
	rcTitleRight.adjust(-1, 0, 0, 0);
	painter.drawRect(rcTitleRight);
	// �ٻ�����๤������������...
	QRect rcToolLeft = ui->vert_tool->geometry();
	painter.fillRect(rcToolLeft, QColor(67, 69, 85));
	// �ڼ����Ҳ������հ�����...
	QRect rcRightArea = ui->vert_right->geometry();
	rcRightArea.adjust(0, rcTitleRight.height()+1, 0, 0);
	// �����Ҳ���������ͷ����...
	QRect rcRightSelf = rcRightArea;
	rcRightSelf.setWidth(rcRightArea.width() / 5);
	painter.fillRect(rcRightSelf, QColor(40, 42, 49));
	// ����������ͬ�ķָ�����...
	painter.setPen(QColor(27, 26, 28));
	painter.drawLine(rcRightSelf.right() + 1, rcRightSelf.top() + 1, rcRightSelf.right() + 1, rcRightSelf.bottom());
	painter.setPen(QColor(63, 64, 70));
	painter.drawLine(rcRightSelf.right() + 2, rcRightSelf.top() + 1, rcRightSelf.right() + 2, rcRightSelf.bottom());
	// �����Ҳ���ʦ��������...
	QRect rcTeacher = rcRightArea;
	rcTeacher.setLeft(rcRightSelf.right() + 3);
	painter.fillRect(rcTeacher, QColor(46, 48, 55));

	// �����������Ҳ�����ı߿�λ��...
	painter.setPen(QColor(27, 26, 28));
	painter.drawRect(rcRightArea);

	// �����û����ǳ� => ��Ҫ�����Ƿ���ʾʡ�Ժ�...
	painter.setPen(QColor(235, 235, 235));
	int nHeadSize = m_QPixUserHead.width();
	int nPosY = 30 + nHeadSize + 10;
	QFontMetrics fontMetr(this->font());
	int nNameSize = rcToolLeft.width() - 8 * 2;
	int nFontSize = fontMetr.width(m_strUserNickName);
	int nPosX = (rcToolLeft.width() - nNameSize) / 2;
	if (nFontSize > nNameSize) {
		QTextOption txtOption(Qt::AlignLeft|Qt::AlignTop);
		txtOption.setWrapMode(QTextOption::WrapAnywhere);
		QRect txtRect(nPosX, nPosY, nNameSize, (fontMetr.height() + 2) * 4);
		painter.drawText(txtRect, m_strUserNickName, txtOption);
		//m_strUserNickName = fontMetr.elidedText(m_strUserNickName, Qt::ElideRight, nNameSize);
		//nNameSize = fontMetr.width(m_strUserNickName);
	} else {
		// ע�⣺�������ֻ�Ҫ����Y��߶�10������...
		nPosX = (rcToolLeft.width() - nFontSize) / 2;
		painter.drawText(nPosX, nPosY + 10, m_strUserNickName);
	}

	// ����� + ƽ����Ե����
	painter.setRenderHints(QPainter::Antialiasing, true);
	painter.setRenderHints(QPainter::SmoothPixmapTransform, true);

	// ��Ҫ������ʾλ�� = > ���òü�Բ������...
	QPainterPath pathCircle;
	nPosX = (rcToolLeft.width() - nHeadSize) / 2;
	pathCircle.addEllipse(nPosX, 30, nHeadSize, nHeadSize);
	painter.setClipPath(pathCircle);
	// �����û���ͷ�� => �̶�λ�ã��Զ��ü�...
	painter.drawPixmap(nPosX, 30, m_QPixUserHead);
	QWidget::paintEvent(event);
}

// ����ͨ�� mousePressEvent | mouseMoveEvent | mouseReleaseEvent �����¼�ʵ��������϶��������ƶ����ڵ�Ч��;
void CStudentWindow::mousePressEvent(QMouseEvent *event)
{
	// ������������Ҳ������״̬�� => ���ܽ����Ϸ��ƶ�����...
	if ((event->button() == Qt::LeftButton) && !this->isMaximized()) {
		m_isPressed = true; m_startMovePos = event->globalPos();
	}
	QWidget::mousePressEvent(event);
}

void CStudentWindow::mouseMoveEvent(QMouseEvent *event)
{
	if (m_isPressed) {
		QPoint movePoint = event->globalPos() - m_startMovePos;
		QPoint widgetPos = this->pos() + movePoint;
		m_startMovePos = event->globalPos();
		this->move(widgetPos.x(), widgetPos.y());
	}
	QWidget::mouseMoveEvent(event);
}

void CStudentWindow::mouseReleaseEvent(QMouseEvent *event)
{
	m_isPressed = false;
	QWidget::mouseReleaseEvent(event);
	// �ж������������λ��Խ�磬����λ��...
	QRect rcRect = this->geometry();
	int nWidth = rcRect.width();
	int nHeight = rcRect.height();
	// ������Խ�� => ��Ȳ���...
	if (rcRect.left() < 0) {
		rcRect.setLeft(0);
		rcRect.setWidth(nWidth);
		this->setGeometry(rcRect);
	}
	// �������Խ�� => �߶Ȳ���...
	if (rcRect.top() < 0) {
		rcRect.setTop(0);
		rcRect.setHeight(nHeight);
		this->setGeometry(rcRect);
	}
}
