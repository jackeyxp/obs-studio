
#include "window-view-camera.hpp"
#include "display-helpers.hpp"
#include "window-student.h"
#include "qt-wrappers.hpp"
#include "platform.hpp"
#include "obs-app.hpp"

#include <QPainter>
#include <QResource>
#include <QKeyEvent>
#include <QTimer>

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
	if (m_dshowCameraItem != nullptr) {
		obs_sceneitem_remove(m_dshowCameraItem);
		m_dshowCameraItem = nullptr;
	}
	// �ͷ���˷�����Դ����...
	if (m_lpMicInputSource != nullptr) {
		obs_source_release(m_lpMicInputSource);
		m_lpMicInputSource = nullptr;
	}
	// �ͷ�Ĭ�ϵ�����ͷͼƬ����Դ...
	if (m_lpImageSource != nullptr) {
		obs_source_release(m_lpImageSource);
		m_lpImageSource = nullptr;
	}
}

void CViewCamera::initWindow()
{
}

// ��ʼ������ͷ���� => ��������Դ...
bool CViewCamera::doInitCamera()
{
	// ���ȼ���Ĭ�ϵ�������ͷͼƬ����Դ...
	this->doLoadNoDShowImage();
	// ������������Դ���� => ����ͷ����˷�...
	this->doCreateDShowCamera();
	this->doCreateDShowMicphone();
	// ������ʾ�ص��ӿں���...
	auto addDShowDrawCallback = [this](OBSQTDisplay *window) {
		// ע�⣺ֻ��������ܸı䱳����������ɱ�����ɫ���ҵ�����...
		this->SetDisplayBackgroundColor(QColor(40, 42, 49));
		// ������ǰ��Ļ��ƺ����ӿ� => �� show() ��ʱ����ܵ�������...
		obs_display_add_draw_callback(window->GetDisplay(), CViewCamera::doDrawDShowPreview, this);
	};
	// ���� DisplayCreated ��Ӧ����ʾ�ص������ӿ�...
	connect(this, &OBSQTDisplay::DisplayCreated, addDShowDrawCallback);
	return true;
}

void CViewCamera::doRemoveDrawCallback()
{
	obs_display_remove_draw_callback(this->GetDisplay(), CViewCamera::doDrawDShowPreview, this);
}

// ����Ĭ�ϵ�����ͷͼƬ����Դ����...
bool CViewCamera::doLoadNoDShowImage()
{
	string strNoCameraPath;
	// ͨ���ⲿ��Դ�ķ�ʽ��ȡĬ������ͷ��ȫ·�� => ������qrc��Դ...
	if (!GetDataFilePath("images/camera.png", strNoCameraPath))
		return false;
	assert(strNoCameraPath.size() > 0);
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", strNoCameraPath.c_str());
	obs_source_t *source = obs_source_create_private("image_source", NULL, settings);
	obs_data_release(settings);
	m_lpImageSource = source;
	return true;
}

void CViewCamera::doCreateDShowCamera()
{
	// ��������ͷ����Դ�Ѿ�����ֱ�ӷ���...
	if (m_dshowCameraItem != nullptr)
		return;
	// ����һ���µı�������ͷ����Դ���ἤ��AddSceneItem�¼�...
	obs_scene_t * lpObsScene = m_lpStudentWindow->GetObsScene();
	const char * lpDShowID = App()->DShowInputSource();
	const char * lpDShowName = obs_source_get_display_name(lpDShowID);
	obs_source_t * lpDShowSource = obs_source_create(lpDShowID, lpDShowName, NULL, nullptr);
	// ���û�л�ȡ����Ĭ�ϵ�DShow����Դ => ֱ�ӷ��� => ע�⴦����̱�־...
	if (lpObsScene == nullptr || lpDShowName == nullptr && lpDShowSource == nullptr)
		return;
	// ��ʼ��Ĭ������Դ...
	obs_enter_graphics();
	vec2 vPos = { 0.0f, 0.0f };
	// �����λ�����������MainTexure��λ��...
	obs_sceneitem_t * lpDShowItem = obs_scene_add(lpObsScene, lpDShowSource);
	// ע�⣺����ἤ���ź��¼�AddSceneItem => ��Ҫ�������ü�����...
	obs_source_release(lpDShowSource);
	// ��������ͷ�ɼ����Լ��趨��ʼλ��...
	// ע�⣺0��λ�õ�����Դ����Ҫ�������������Դ...
	obs_sceneitem_set_visible(lpDShowItem, true);
	obs_sceneitem_set_pos(lpDShowItem, &vPos);
	obs_leave_graphics();
	// �Ա�������ͷ����Դ���г�������...
	obs_properties_t * props = obs_source_properties(lpDShowSource);
	obs_property_t * property = obs_properties_get(props, "video_device_id");
	obs_property_type prop_type = obs_property_get_type(property);
	obs_combo_type comb_type = obs_property_list_type(property);
	obs_combo_format comb_format = obs_property_list_format(property);
	size_t comb_count = obs_property_list_item_count(property);
	const char * item_name = obs_property_list_item_name(property, 0);
	const char * item_data = obs_property_list_item_string(property, 0);

	// ���ò��������� => ע���ͷ����ü�����...
	if (item_name != nullptr && item_data != nullptr) {
		OBSData settings = obs_source_get_settings(lpDShowSource);
		obs_data_set_string(settings, "video_device_id", item_data);
		obs_source_update(lpDShowSource, settings);
		obs_data_release(settings);
	}
	// ����Դ���Զ���ҲҪ�����ͷŲ���...
	obs_properties_destroy(props);
	// ��������Դ��Ԥ������������� => ֻҪ��������ʾ...
	//enum obs_source_type type = obs_source_get_type(lpDShowSource);
	//uint32_t caps = obs_source_get_output_flags(lpDShowSource);
	//bool drawable_type = type == OBS_SOURCE_TYPE_INPUT || type == OBS_SOURCE_TYPE_SCENE;
	//bool drawable_preview = (caps & OBS_SOURCE_VIDEO) != 0;
	// ���汾������ͷ��������Դ���� => û�в��ü���AddSceneItem�¼��ķ�ʽ...
	m_dshowCameraItem = lpDShowItem;
}

bool CViewCamera::doHasAudioDevices(const char *source_id)
{
	const char *output_id = source_id;
	obs_properties_t *props = obs_get_source_properties(output_id);
	size_t count = 0;

	if (!props) return false;

	obs_property_t *devices = obs_properties_get(props, "device_id");
	if (devices) {
		count = obs_property_list_item_count(devices);
		/*for (int i = 0; i < count; ++i) {
			const char * lpValue = obs_property_list_item_string(devices, i);
			const char * lpName = obs_property_list_item_name(devices, i);
		}*/
	}
	obs_properties_destroy(props);

	return count != 0;
}

void CViewCamera::doResetAudioDevice(const char *sourceId, const char *deviceId, const char *deviceDesc, int channel)
{
	bool disable = deviceId && strcmp(deviceId, "disabled") == 0;
	obs_source_t * source;
	obs_data_t * settings;

	source = obs_get_output_source(channel);
	if (source) {
		if (disable) {
			obs_set_output_source(channel, nullptr);
		} else {
			settings = obs_source_get_settings(source);
			const char *oldId = obs_data_get_string(settings, "device_id");
			if (strcmp(oldId, deviceId) != 0) {
				obs_data_set_string(settings, "device_id", deviceId);
				obs_source_update(source, settings);
			}
			obs_data_release(settings);
		}
		obs_source_release(source);
	} else if (!disable) {
		settings = obs_data_create();
		obs_data_set_string(settings, "device_id", deviceId);
		source = obs_source_create(sourceId, deviceDesc, settings, nullptr);
		obs_data_release(settings);

		obs_set_output_source(channel, source);
		obs_source_release(source);
		// �������Ƶ�����豸���Զ������������ƹ�����|�Զ����ε����������...
		if (strcmp(sourceId, App()->InputAudioSource()) == 0) {
			//OBSBasicSourceSelect::AddFilterToSourceByID(source, App()->GetNSFilter());
			// ˼·���� => ���3(���������2)ר����������ͳһ����ʹ�õĻ���ͨ��...
			//uint32_t new_mixers = obs_source_get_audio_mixers(source) & (~(1 << 2));
			//obs_source_set_audio_mixers(source, new_mixers);
		}
		// �������Ƶ����豸���Զ�����Ϊ����״̬�����ⷢ����ε���Х��...
		if (strcmp(sourceId, App()->OutputAudioSource()) == 0) {
			obs_source_set_muted(source, true);
		}
	}
}

void CViewCamera::doCreateDShowMicphone()
{
	// ֱ�����ε���������������ױ��⻥��ʱ������Х��...
	//if (HasAudioDevices(App()->OutputAudioSource())) {
	//	ResetAudioDevice(App()->OutputAudioSource(), "default", Str("Basic.DesktopDevice1"), 1);
	//}
	// ����Ƶ�����豸��ֱ��ʹ��Ĭ�ϵ���Ƶ�����豸 => ����Ƶ��3����...
	if (CViewCamera::doHasAudioDevices(App()->InputAudioSource())) {
		this->doResetAudioDevice(App()->InputAudioSource(), "default", Str("Basic.AuxDevice1"), m_nMicInputChannel);
		// ע�⣺�����ȡ�ɹ������Զ��������ü�������������Ҫ�ͷż�����...
		m_lpMicInputSource = obs_get_output_source(m_nMicInputChannel);
	}
}

void CViewCamera::doRenderAllSource(uint32_t cx, uint32_t cy)
{
	gs_viewport_push();
	gs_projection_push();
	
	obs_source_t * lpDShowSource = obs_sceneitem_get_source(m_dshowCameraItem);
	obs_data_t * settings = obs_source_get_settings(lpDShowSource);
	m_bIsDrawImage = obs_data_get_bool(settings, "draw_image");
	obs_data_release(settings);

	this->doRenderDShowCamera(cx, cy);
	this->doRenderNoDShowImage(cx, cy);

	gs_projection_pop();
	gs_viewport_pop();
}

void CViewCamera::doRenderDShowCamera(uint32_t cx, uint32_t cy)
{
	obs_source_t * lpDShowSource = obs_sceneitem_get_source(m_dshowCameraItem);
	if (!m_bIsDrawImage || lpDShowSource == nullptr)
		return;
	// ע�⣺��������˱����޸ģ�������ɱ�����ɫ���ҵ�����...
	//this->SetDisplayBackgroundColor(QColor(40, 42, 49));
	float scale = 1.0f; int x, y, newCX, newCY;
	uint32_t sourceCX = max(obs_source_get_width(lpDShowSource), 1u);
	uint32_t sourceCY = max(obs_source_get_height(lpDShowSource), 1u);
	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);
	// ����ӿڵ��÷ǳ���Ҫ��������������ԭʼ����Դ�����Ĵ�С��Χ...
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));
	// ����ӿ����趨��ͼ�ڻ������е�ͶӰ...
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(lpDShowSource);
}

void CViewCamera::doRenderNoDShowImage(uint32_t cx, uint32_t cy)
{
	if (m_bIsDrawImage || m_lpImageSource == nullptr)
		return;
	// ע�⣺��������˱����޸ģ�������ɱ�����ɫ���ҵ�����...
	//backgroundColor = GREY_COLOR_BACKGROUND;
	//this->UpdateDisplayBackgroundColor();
	float scale = 1.0f; int x, y, newCX, newCY;
	uint32_t sourceCX = max(obs_source_get_width(m_lpImageSource), 1u);
	uint32_t sourceCY = max(obs_source_get_height(m_lpImageSource), 1u);
	GetCenterPosFromFixedScale(sourceCX, sourceCY, cx, cy, x, y, scale);
	// ����ӿڵ��÷ǳ���Ҫ��������������ԭʼ����Դ�����Ĵ�С��Χ...
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));
	// ����ӿ����趨��ͼ�ڻ������е�ͶӰ...
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(m_lpImageSource);
}

void CViewCamera::doDrawDShowPreview(void *data, uint32_t cx, uint32_t cy)
{
	CViewCamera * window = static_cast<CViewCamera *>(data);
	window->doRenderAllSource(cx, cy);
}

uint32_t CViewCamera::doGetCameraWidth()
{
	obs_source_t * lpDShowSource = obs_sceneitem_get_source(m_dshowCameraItem);
	return ((lpDShowSource != NULL) ? obs_source_get_width(lpDShowSource) : 0);
}

uint32_t CViewCamera::doGetCameraHeight()
{
	obs_source_t * lpDShowSource = obs_sceneitem_get_source(m_dshowCameraItem);
	return ((lpDShowSource != NULL) ? obs_source_get_height(lpDShowSource) : 0);
}

void CViewCamera::mouseDoubleClickEvent(QMouseEvent *event)
{
	this->onFullScreenAction();
}

void CViewCamera::onFullScreenAction()
{
	if (this->isFullScreen()) {
		// �����˳�ȫ��״̬...
		this->setWindowFlags(Qt::SubWindow);
		this->showNormal();
		// ��Ҫ�ָ���ȫ��ǰ�ľ�������...
		this->setGeometry(m_rcNoramlRect);
	} else {
		// ��Ҫ�ȱ���ȫ��ǰ�ľ�������...
		m_rcNoramlRect = this->geometry();
		// ���ڽ���ȫ��״̬...
		this->setWindowFlags(Qt::Window);
		this->showFullScreen();
	}
}

void CViewCamera::keyPressEvent(QKeyEvent *event)
{
	int nKeyItem = event->key();
	if (nKeyItem != Qt::Key_Escape)
		return;
	if (!this->isFullScreen())
		return;
	this->onFullScreenAction();
}

void CViewCamera::closeEvent(QCloseEvent *event)
{
	// ȫ��״̬�²��ܹرմ���...
	if (this->isFullScreen()) {
		event->ignore();
		return;
	}
	// ���ô��ڵĻ����ӿں���...
	QWidget::closeEvent(event);
}