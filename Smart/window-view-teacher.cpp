
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
	// Ƶ��0 => ���ͷ�������������������Դ����...
	if (m_teacherSceneItem != nullptr) {
		obs_sceneitem_release(m_teacherSceneItem);
		m_teacherSceneItem = nullptr;
	}
	// �ͷ�Ĭ�ϵ�����ͷͼƬ����Դ...
	if (m_lpDefaultSource != nullptr) {
		obs_source_release(m_lpDefaultSource);
		m_lpDefaultSource = nullptr;
	}
}

void CViewTeacher::initWindow()
{
	// ����������ڻ�ȡ��������ͷ����Դ => �����Ч����Ҫ�������ü���...
	m_teacherSceneItem = m_lpStudentWindow->GetTeacherSceneItem();
	if (m_teacherSceneItem != nullptr) {
		obs_sceneitem_addref(m_teacherSceneItem);
	}
	// ���ȼ���Ĭ�ϵ�����ͷͼƬ����Դ...
	this->doLoadNoTeacherSource();
}

void CViewTeacher::doRemoveDrawCallback()
{
	obs_display_remove_draw_callback(this->GetDisplay(), CViewTeacher::doDrawTeacherPreview, this);
}

// ����Ĭ�ϵ���ʦ��ͼƬ����Դ����...
bool CViewTeacher::doLoadNoTeacherSource()
{
	string strNoTeacherPath;
	// ͨ���ⲿ��Դ�ķ�ʽ��ȡĬ������ͷ��ȫ·�� => ������qrc��Դ...
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

// ��ʼ������ͷ���� => ��������Դ...
bool CViewTeacher::doInitTeacher()
{
	// ������ʾ�ص��ӿں���...
	auto addTeacherDrawCallback = [this](OBSQTDisplay *window) {
		// ע�⣺��������˱����޸ģ�������ɱ�����ɫ���ҵ�����...
		this->SetDisplayBackgroundColor(QColor(46, 48, 55));
		// ������ǰ��Ļ��ƺ����ӿ� => �� show() ��ʱ����ܵ�������...
		obs_display_add_draw_callback(window->GetDisplay(), CViewTeacher::doDrawTeacherPreview, this);
	};
	// ���� DisplayCreated ��Ӧ����ʾ�ص������ӿ�...
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
	
	// û����ʦ����Դʱ����Ҫ����������ʾ...
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
