
#include "window-view-camera.hpp"

#include <QPainter>

CViewCamera::CViewCamera(QWidget *parent, Qt::WindowFlags flags)
	: OBSQTDisplay(parent, flags)
{
	//setMouseTracking(true);
}

CViewCamera::~CViewCamera()
{
}

void CViewCamera::paintEvent(QPaintEvent *event)
{
	QPainter painter(this);

	painter.fillRect(this->geometry(), QColor(255, 255, 255));

	QWidget::paintEvent(event);
}