
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
	// Ƶ��0 => ���ͷ�������������������Դ����...
	if (m_dshowSceneItem != nullptr) {
		obs_sceneitem_release(m_dshowSceneItem);
		m_dshowSceneItem = nullptr;
	}
	// �ͷ�Ĭ�ϵ�����ͷͼƬ����Դ...
	if (m_lpDefaultSource != nullptr) {
		obs_source_release(m_lpDefaultSource);
		m_lpDefaultSource = nullptr;
	}
}

void CViewCamera::initWindow()
{
	// ����������ڻ�ȡ��������ͷ����Դ => �����Ч����Ҫ�������ü���...
	m_dshowSceneItem = m_lpStudentWindow->GetDShowSceneItem();
	if (m_dshowSceneItem != nullptr) {
		obs_sceneitem_addref(m_dshowSceneItem);
	}
	// ���ȼ���Ĭ�ϵ�����ͷͼƬ����Դ...
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

// ����Ĭ�ϵ�����ͷͼƬ����Դ����...
bool CViewCamera::doLoadNoCameraSource()
{
	string strNoCameraPath;
	// ͨ���ⲿ��Դ�ķ�ʽ��ȡĬ������ͷ��ȫ·�� => ������qrc��Դ...
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

// ��ʼ������ͷ���� => ��������Դ...
bool CViewCamera::doInitCamera()
{
	// ������ʾ�ص��ӿں���...
	auto addDShowDrawCallback = [this](OBSQTDisplay *window) {
		// ע�⣺��������˱����޸ģ�������ɱ�����ɫ���ҵ�����...
		if (this->m_dshowSceneItem != nullptr) {
			this->SetDisplayBackgroundColor(QColor(40, 42, 49));
		}
		// ������ǰ��Ļ��ƺ����ӿ� => �� show() ��ʱ����ܵ�������...
		obs_display_add_draw_callback(window->GetDisplay(), CViewCamera::doDrawDShowPreview, this);
	};
	// ���� DisplayCreated ��Ӧ����ʾ�ص������ӿ�...
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
	
	// û������ͷʱ����Ҫ����������ʾ...
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
