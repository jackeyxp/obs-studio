
#include "window-view-teacher.hpp"
#include "display-helpers.hpp"
#include "window-student.h"
#include "qt-wrappers.hpp"
#include "platform.hpp"
#include "obs-app.hpp"

#include <QPainter>
#include <QResource>

CViewTeacher::CViewTeacher(QWidget *parent, Qt::WindowFlags flags)
	: OBSQTDisplay(parent, flags)
{
	m_lpStudentWindow = static_cast<CStudentWindow *>(parent);
	assert(m_lpStudentWindow != nullptr);
	//this->setMouseTracking(true);
	this->initWindow();
}

CViewTeacher::~CViewTeacher()
{
	// 频道0 => 先释放所有主动创建的数据源对象...
	if (m_teacherSceneItem != nullptr) {
		obs_sceneitem_release(m_teacherSceneItem);
		m_teacherSceneItem = nullptr;
	}
	// 释放默认的摄像头图片数据源...
	if (m_lpDefaultSource != nullptr) {
		obs_source_release(m_lpDefaultSource);
		m_lpDefaultSource = nullptr;
	}
}

void CViewTeacher::initWindow()
{
	// 保存从主窗口获取到的摄像头数据源 => 如果有效，需要增加引用计数...
	m_teacherSceneItem = m_lpStudentWindow->GetTeacherSceneItem();
	if (m_teacherSceneItem != nullptr) {
		obs_sceneitem_addref(m_teacherSceneItem);
	}
	// 首先加载默认的摄像头图片数据源...
	this->doLoadNoTeacherSource();
}

void CViewTeacher::doRemoveDrawCallback()
{
	obs_display_remove_draw_callback(this->GetDisplay(), CViewTeacher::doDrawTeacherPreview, this);
}

// 加载默认的老师端图片数据源对象...
bool CViewTeacher::doLoadNoTeacherSource()
{
	string strNoTeacherPath;
	// 通过外部资源的方式获取默认摄像头的全路径 => 不能用qrc资源...
	if (!GetDataFilePath("images/camera.png", strNoTeacherPath))
		return false;
	assert(strNoTeacherPath.size() > 0);
	obs_source_t *source = nullptr;
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", strNoTeacherPath.c_str());
	source = obs_source_create_private("image_source", NULL, settings);
	obs_data_release(settings);
	m_lpDefaultSource = source;
	return true;
}

// 初始化摄像头对象 => 创建数据源...
bool CViewTeacher::doInitTeacher()
{
	// 设置显示回调接口函数...
	auto addTeacherDrawCallback = [this](OBSQTDisplay *window) {
		// 注意：这里进行了背景修改，避免造成背景颜色混乱的问题...
		this->SetDisplayBackgroundColor(QColor(46, 48, 55));
		// 关联当前类的绘制函数接口 => 在 show() 的时候才能到达这里...
		obs_display_add_draw_callback(window->GetDisplay(), CViewTeacher::doDrawTeacherPreview, this);
	};
	// 设置 DisplayCreated 对应的显示回调函数接口...
	connect(this, &OBSQTDisplay::DisplayCreated, addTeacherDrawCallback);
	return true;
}

void CViewTeacher::doDrawTeacherPreview(void *data, uint32_t cx, uint32_t cy)
{
	CViewTeacher * window = static_cast<CViewTeacher *>(data);
	obs_source_t * lpTeacherSource = obs_sceneitem_get_source(window->m_teacherSceneItem);
	lpTeacherSource = (lpTeacherSource == nullptr) ? window->m_lpDefaultSource : lpTeacherSource;
	if (lpTeacherSource == NULL)
		return;
	uint32_t sourceCX = max(obs_source_get_width(lpTeacherSource), 1u);
	uint32_t sourceCY = max(obs_source_get_height(lpTeacherSource), 1u);

	int x, y;
	int newCX, newCY;
	float scale = 1.0f;
	
	// 没有老师数据源时，需要进行锁定显示...
	if (window->m_teacherSceneItem == nullptr) {
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

	obs_source_video_render(lpTeacherSource);

	gs_projection_pop();
	gs_viewport_pop();
}
