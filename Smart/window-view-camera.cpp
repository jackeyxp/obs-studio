
#include "window-view-camera.hpp"
#include "display-helpers.hpp"
#include "window-student.h"
#include "qt-wrappers.hpp"
#include "platform.hpp"
#include "obs-app.hpp"

#include <QPainter>
#include <QResource>

CViewCamera::CViewCamera(QWidget *parent, Qt::WindowFlags flags)
	: OBSQTDisplay(parent, flags)
{
	m_lpStudentWindow = static_cast<CStudentWindow *>(parent);
	assert(m_lpStudentWindow != nullptr);
	//this->setMouseTracking(true);
	this->initWindow();
}

CViewCamera::~CViewCamera()
{
	// 频道0 => 先释放所有主动创建的数据源对象...
	if (m_dshowSceneItem != nullptr) {
		obs_sceneitem_release(m_dshowSceneItem);
		m_dshowSceneItem = nullptr;
	}
	// 释放默认的摄像头图片数据源...
	if (m_lpDefaultSource != nullptr) {
		obs_source_release(m_lpDefaultSource);
		m_lpDefaultSource = nullptr;
	}
}

void CViewCamera::initWindow()
{
	// 保存从主窗口获取到的摄像头数据源 => 如果有效，需要增加引用计数...
	m_dshowSceneItem = m_lpStudentWindow->GetDShowSceneItem();
	if (m_dshowSceneItem != nullptr) {
		obs_sceneitem_addref(m_dshowSceneItem);
	}
	// 首先加载默认的摄像头图片数据源...
	this->doLoadNoCameraSource();
}

void CViewCamera::doRemoveDrawCallback()
{
	obs_display_remove_draw_callback(this->GetDisplay(), CViewCamera::doDrawDShowPreview, this);
}

float CViewCamera::doGetSourceRatioScale()
{
	float ratioScale = 3.0 / 4.0;
	obs_source_t * lpDShowSource = obs_sceneitem_get_source(m_dshowSceneItem);
	if (lpDShowSource != NULL) {
		uint32_t sourceCX = max(obs_source_get_width(lpDShowSource), 1u);
		uint32_t sourceCY = max(obs_source_get_height(lpDShowSource), 1u);
		ratioScale = (sourceCY * 1.0f) / (sourceCX * 1.0f);
	}
	return ratioScale;
}

// 加载默认的摄像头图片数据源对象...
bool CViewCamera::doLoadNoCameraSource()
{
	string strNoCameraPath;
	// 通过外部资源的方式获取默认摄像头的全路径 => 不能用qrc资源...
	if (!GetDataFilePath("images/camera.png", strNoCameraPath))
		return false;
	assert(strNoCameraPath.size() > 0);
	obs_source_t *source = nullptr;
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", strNoCameraPath.c_str());
	source = obs_source_create_private("image_source", NULL, settings);
	obs_data_release(settings);
	m_lpDefaultSource = source;
	return true;
}

// 初始化摄像头对象 => 创建数据源...
bool CViewCamera::doInitCamera()
{
	// 设置显示回调接口函数...
	auto addDShowDrawCallback = [this](OBSQTDisplay *window) {
		// 注意：这里进行了背景修改，避免造成背景颜色混乱的问题...
		if (this->m_dshowSceneItem != nullptr) {
			this->SetDisplayBackgroundColor(QColor(40, 42, 49));
		}
		// 关联当前类的绘制函数接口 => 在 show() 的时候才能到达这里...
		obs_display_add_draw_callback(window->GetDisplay(), CViewCamera::doDrawDShowPreview, this);
	};
	// 设置 DisplayCreated 对应的显示回调函数接口...
	connect(this, &OBSQTDisplay::DisplayCreated, addDShowDrawCallback);
	return true;
}

void CViewCamera::doDrawDShowPreview(void *data, uint32_t cx, uint32_t cy)
{
	CViewCamera * window = static_cast<CViewCamera *>(data);
	obs_source_t * lpDShowSource = obs_sceneitem_get_source(window->m_dshowSceneItem);
	lpDShowSource = (lpDShowSource == nullptr) ? window->m_lpDefaultSource : lpDShowSource;
	if (lpDShowSource == NULL)
		return;
	uint32_t sourceCX = max(obs_source_get_width(lpDShowSource), 1u);
	uint32_t sourceCY = max(obs_source_get_height(lpDShowSource), 1u);

	int x, y;
	int newCX, newCY;
	float scale = 1.0f;
	
	// 没有摄像头时，需要进行锁定显示...
	if (window->m_dshowSceneItem == nullptr) {
		GetCenterPosFromFixedScale(sourceCX, sourceCY, cx, cy, x, y, scale);
	} else {
		GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);
	}

	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	gs_set_viewport(x, y, newCX, newCY);

	obs_source_video_render(lpDShowSource);

	gs_projection_pop();
	gs_viewport_pop();
}
