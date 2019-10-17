
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
	// FramelessWindowHint属性设置窗口去除边框;
	// WindowMinimizeButtonHint 属性设置在窗口最小化时，点击任务栏窗口可以显示出原窗口;
	//Qt::WindowFlags flag = this->windowFlags();
	this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
	// 设置窗口背景透明 => 设置之后会造成全黑问题 => 顶层窗口需要用背景蒙板，普通窗口没问题...
	this->setAttribute(Qt::WA_TranslucentBackground);
	// 关闭窗口时释放资源;
	this->setAttribute(Qt::WA_DeleteOnClose);
	// 设置窗口图标 => 必须用png图片 => 解决有些机器不认ico，造成左上角图标无法显示...
	this->setWindowIcon(QIcon(":/res/images/obs.png"));
	// 判断当前主桌面的分辨率是否超过1280x720，如果没有超过就用默认的...
	QScreen * lpCurScreen = QGuiApplication::primaryScreen();
	QRect rcScreen = lpCurScreen->geometry();
	// 重新设定窗口大小 => 当桌面分辨率超过1280x720的时候...
	if (rcScreen.width() >= 1280 && rcScreen.height() >= 720) {
		this->resize(1280, 720);
	}
	// 重新计算窗口显示位置...
	QRect rcRect = this->geometry();
	int nLeftPos = (rcScreen.width() - rcRect.width()) / 2;
	int nTopPos = (rcScreen.height() - rcRect.height()) / 2;
	this->setGeometry(nLeftPos, nTopPos, rcRect.width(), rcRect.height());
	// 保存更新后的窗口地理坐标，以便最大化之后的恢复使用...
	m_rcSrcGeometry = this->geometry();
	// 关联点击最小化按钮|最大化|关闭按钮的信号槽事件...
	connect(ui->btnMin, SIGNAL(clicked()), this, SLOT(onButtonMinClicked()));
	connect(ui->btnMax, SIGNAL(clicked()), this, SLOT(onButtonMaxClicked()));
	connect(ui->btnClose, SIGNAL(clicked()), this, SLOT(onButtonCloseClicked()));
	// 关联点击工具栏其它按钮的信号槽事件...
	connect(ui->btnAbout, SIGNAL(clicked()), this, SLOT(onButtonAboutClicked()));
	connect(ui->btnUpdate, SIGNAL(clicked()), this, SLOT(onButtonUpdateClicked()));
	connect(ui->btnCamera, SIGNAL(clicked()), this, SLOT(onButtonCameraClicked()));
	connect(ui->btnSystem, SIGNAL(clicked()), this, SLOT(onButtonSystemClicked()));
	// 加载窗口的层叠显示样式表...
	this->loadStyleSheet(":/student/css/Student.css");
	// 读取并保存默认的头像对象 => 需要进行必要的缩放操作...
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
	// 如果已经是最大化 => 恢复初始坐标...
	if (this->isMaximized()) {
		ui->btnMax->setStyleSheet(QString("%1%2%3").arg("QPushButton{border-image:url(:/student/images/student/maxing.png) 0 40 0 0;}")
			.arg("QPushButton:hover{border-image:url(:/student/images/student/maxing.png) 0 20 0 20;}")
			.arg("QPushButton:pressed{border-image:url(:/student/images/student/maxing.png) 0 0 0 40;}"));
		ui->btnMax->setToolTip(QTStr("Student.Tips.Maxing"));
		this->setGeometry(m_rcSrcGeometry);
		this->showNormal();
		return;
	}
	// 如果不是最大化 => 最大化显示...
	this->showMaximized();
	// 调整最大化按钮的图标样式，提示信息...
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
	// 先绘制标题栏矩形区域...
	QRect rcTitleRight = ui->hori_title->geometry();
	painter.fillRect(rcTitleRight, QColor(47, 47, 53));
	painter.setPen(QColor(61, 63, 70));
	rcTitleRight.adjust(-1, 0, 0, 0);
	painter.drawRect(rcTitleRight);
	// 再绘制左侧工具条矩形区域...
	QRect rcToolLeft = ui->vert_tool->geometry();
	painter.fillRect(rcToolLeft, QColor(67, 69, 85));
	// 在计算右侧整个空白区域...
	QRect rcRightArea = ui->vert_right->geometry();
	rcRightArea.adjust(0, rcTitleRight.height()+1, 0, 0);
	// 绘制右侧自身摄像头区域...
	QRect rcRightSelf = rcRightArea;
	rcRightSelf.setWidth(rcRightArea.width() / 5);
	painter.fillRect(rcRightSelf, QColor(40, 42, 49));
	// 绘制两条不同的分割竖线...
	painter.setPen(QColor(27, 26, 28));
	painter.drawLine(rcRightSelf.right() + 1, rcRightSelf.top() + 1, rcRightSelf.right() + 1, rcRightSelf.bottom());
	painter.setPen(QColor(63, 64, 70));
	painter.drawLine(rcRightSelf.right() + 2, rcRightSelf.top() + 1, rcRightSelf.right() + 2, rcRightSelf.bottom());
	// 绘制右侧老师画面区域...
	QRect rcTeacher = rcRightArea;
	rcTeacher.setLeft(rcRightSelf.right() + 3);
	painter.fillRect(rcTeacher, QColor(46, 48, 55));

	// 最后绘制整个右侧区域的边框位置...
	painter.setPen(QColor(27, 26, 28));
	painter.drawRect(rcRightArea);

	// 绘制用户的昵称 => 需要计算是否显示省略号...
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
		// 注意：单行文字还要增加Y轴高度10个像素...
		nPosX = (rcToolLeft.width() - nFontSize) / 2;
		painter.drawText(nPosX, nPosY + 10, m_strUserNickName);
	}

	// 抗锯齿 + 平滑边缘处理
	painter.setRenderHints(QPainter::Antialiasing, true);
	painter.setRenderHints(QPainter::SmoothPixmapTransform, true);

	// 需要计算显示位置 = > 设置裁剪圆形区域...
	QPainterPath pathCircle;
	nPosX = (rcToolLeft.width() - nHeadSize) / 2;
	pathCircle.addEllipse(nPosX, 30, nHeadSize, nHeadSize);
	painter.setClipPath(pathCircle);
	// 绘制用户的头像 => 固定位置，自动裁剪...
	painter.drawPixmap(nPosX, 30, m_QPixUserHead);
	QWidget::paintEvent(event);
}

// 以下通过 mousePressEvent | mouseMoveEvent | mouseReleaseEvent 三个事件实现了鼠标拖动标题栏移动窗口的效果;
void CStudentWindow::mousePressEvent(QMouseEvent *event)
{
	// 单击左键，并且不是最大化状态下 => 才能进行拖放移动操作...
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
	// 判断如果顶点坐标位置越界，重置位置...
	QRect rcRect = this->geometry();
	int nWidth = rcRect.width();
	int nHeight = rcRect.height();
	// 如果左侧越界 => 宽度不变...
	if (rcRect.left() < 0) {
		rcRect.setLeft(0);
		rcRect.setWidth(nWidth);
		this->setGeometry(rcRect);
	}
	// 如果上面越界 => 高度不变...
	if (rcRect.top() < 0) {
		rcRect.setTop(0);
		rcRect.setHeight(nHeight);
		this->setGeometry(rcRect);
	}
}
