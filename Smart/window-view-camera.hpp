
#pragma once

#include "qt-display.hpp"

class CStudentWindow;
class CViewCamera : public OBSQTDisplay
{
	Q_OBJECT
public:
	CViewCamera(QWidget *parent, Qt::WindowFlags flags = 0);
	~CViewCamera();
public:
	bool  doInitCamera();
	void  doRemoveDrawCallback();
private:
	void  initWindow();
	bool  doLoadNoCameraSource();
private:
	static void doDrawDShowPreview(void *data, uint32_t cx, uint32_t cy);
private:
	obs_source_t     *  m_lpDefaultSource = nullptr;   // 默认图片...
	obs_sceneitem_t  *  m_dshowSceneItem = nullptr;    // 本地摄像头...
	CStudentWindow   *  m_lpStudentWindow = nullptr;   // 主窗口对象...
};
