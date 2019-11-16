
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
	// 频道0 => 先释放所有主动创建的数据源对象...
	if (m_dshowCameraItem != nullptr) {
		obs_sceneitem_remove(m_dshowCameraItem);
		m_dshowCameraItem = nullptr;
	}
	// 释放麦克风数据源对象...
	if (m_lpMicInputSource != nullptr) {
		obs_source_release(m_lpMicInputSource);
		m_lpMicInputSource = nullptr;
	}
	// 释放默认的摄像头图片数据源...
	if (m_lpImageSource != nullptr) {
		obs_source_release(m_lpImageSource);
		m_lpImageSource = nullptr;
	}
}

void CViewCamera::initWindow()
{
}

// 初始化摄像头对象 => 创建数据源...
bool CViewCamera::doInitCamera()
{
	// 首先加载默认的无摄像头图片数据源...
	this->doLoadNoDShowImage();
	// 创建本地数据源对象 => 摄像头和麦克风...
	this->doCreateDShowCamera();
	this->doCreateDShowMicphone();
	// 设置显示回调接口函数...
	auto addDShowDrawCallback = [this](OBSQTDisplay *window) {
		// 注意：只有这里才能改变背景，避免造成背景颜色混乱的问题...
		this->SetDisplayBackgroundColor(QColor(40, 42, 49));
		// 关联当前类的绘制函数接口 => 在 show() 的时候才能到达这里...
		obs_display_add_draw_callback(window->GetDisplay(), CViewCamera::doDrawDShowPreview, this);
	};
	// 设置 DisplayCreated 对应的显示回调函数接口...
	connect(this, &OBSQTDisplay::DisplayCreated, addDShowDrawCallback);
	return true;
}

void CViewCamera::doRemoveDrawCallback()
{
	obs_display_remove_draw_callback(this->GetDisplay(), CViewCamera::doDrawDShowPreview, this);
}

// 加载默认的摄像头图片数据源对象...
bool CViewCamera::doLoadNoDShowImage()
{
	string strNoCameraPath;
	// 通过外部资源的方式获取默认摄像头的全路径 => 不能用qrc资源...
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
	// 本地摄像头数据源已经创建直接返回...
	if (m_dshowCameraItem != nullptr)
		return;
	// 创建一个新的本地摄像头数据源，会激发AddSceneItem事件...
	obs_scene_t * lpObsScene = m_lpStudentWindow->GetObsScene();
	const char * lpDShowID = App()->DShowInputSource();
	const char * lpDShowName = obs_source_get_display_name(lpDShowID);
	obs_source_t * lpDShowSource = obs_source_create(lpDShowID, lpDShowName, NULL, nullptr);
	// 如果没有获取到了默认的DShow数据源 => 直接返回 => 注意处理存盘标志...
	if (lpObsScene == nullptr || lpDShowName == nullptr && lpDShowSource == nullptr)
		return;
	// 初始化默认数据源...
	obs_enter_graphics();
	vec2 vPos = { 0.0f, 0.0f };
	// 这里的位置是相对整个MainTexure的位置...
	obs_sceneitem_t * lpDShowItem = obs_scene_add(lpObsScene, lpDShowSource);
	// 注意：这里会激发信号事件AddSceneItem => 需要减少引用计数器...
	obs_source_release(lpDShowSource);
	// 设置摄像头可见，以及设定初始位置...
	// 注意：0点位置的数据源是需要对外输出的数据源...
	obs_sceneitem_set_visible(lpDShowItem, true);
	obs_sceneitem_set_pos(lpDShowItem, &vPos);
	obs_leave_graphics();
	// 对本地摄像头数据源进行常规配置...
	obs_properties_t * props = obs_source_properties(lpDShowSource);
	obs_property_t * property = obs_properties_get(props, "video_device_id");
	obs_property_type prop_type = obs_property_get_type(property);
	obs_combo_type comb_type = obs_property_list_type(property);
	obs_combo_format comb_format = obs_property_list_format(property);
	size_t comb_count = obs_property_list_item_count(property);
	const char * item_name = obs_property_list_item_name(property, 0);
	const char * item_data = obs_property_list_item_string(property, 0);

	// 设置并更新配置 => 注意释放引用计数器...
	if (item_name != nullptr && item_data != nullptr) {
		OBSData settings = obs_source_get_settings(lpDShowSource);
		obs_data_set_string(settings, "video_device_id", item_data);
		obs_source_update(lpDShowSource, settings);
		obs_data_release(settings);
	}
	// 数据源属性对象也要进行释放操作...
	obs_properties_destroy(props);
	// 关联数据源的预览窗口相关配置 => 只要创建就显示...
	//enum obs_source_type type = obs_source_get_type(lpDShowSource);
	//uint32_t caps = obs_source_get_output_flags(lpDShowSource);
	//bool drawable_type = type == OBS_SOURCE_TYPE_INPUT || type == OBS_SOURCE_TYPE_SCENE;
	//bool drawable_preview = (caps & OBS_SOURCE_VIDEO) != 0;
	// 保存本地摄像头场景数据源对象 => 没有采用激发AddSceneItem事件的方式...
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
		// 如果是音频输入设备，自动加入噪音抑制过滤器|自动屏蔽第三轨道混音...
		if (strcmp(sourceId, App()->InputAudioSource()) == 0) {
			//OBSBasicSourceSelect::AddFilterToSourceByID(source, App()->GetNSFilter());
			// 思路错误 => 轨道3(索引编号是2)专门用来本地统一播放使用的混音通道...
			//uint32_t new_mixers = obs_source_get_audio_mixers(source) & (~(1 << 2));
			//obs_source_set_audio_mixers(source, new_mixers);
		}
		// 如果是音频输出设备，自动设置为静音状态，避免发生多次叠加啸叫...
		if (strcmp(sourceId, App()->OutputAudioSource()) == 0) {
			obs_source_set_muted(source, true);
		}
	}
}

void CViewCamera::doCreateDShowMicphone()
{
	// 直接屏蔽电脑输出声音，彻底避免互动时的声音啸叫...
	//if (HasAudioDevices(App()->OutputAudioSource())) {
	//	ResetAudioDevice(App()->OutputAudioSource(), "default", Str("Basic.DesktopDevice1"), 1);
	//}
	// 有音频输入设备，直接使用默认的音频输入设备 => 放在频道3上面...
	if (CViewCamera::doHasAudioDevices(App()->InputAudioSource())) {
		this->doResetAudioDevice(App()->InputAudioSource(), "default", Str("Basic.AuxDevice1"), m_nMicInputChannel);
		// 注意：如果获取成功，会自动增加引用计数器，后续需要释放计数器...
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
	// 注意：这里进行了背景修改，避免造成背景颜色混乱的问题...
	//this->SetDisplayBackgroundColor(QColor(40, 42, 49));
	float scale = 1.0f; int x, y, newCX, newCY;
	uint32_t sourceCX = max(obs_source_get_width(lpDShowSource), 1u);
	uint32_t sourceCY = max(obs_source_get_height(lpDShowSource), 1u);
	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);
	// 这个接口调用非常重要，它决定了整个原始数据源画布的大小范围...
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));
	// 这个接口是设定视图在画布当中的投影...
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(lpDShowSource);
}

void CViewCamera::doRenderNoDShowImage(uint32_t cx, uint32_t cy)
{
	if (m_bIsDrawImage || m_lpImageSource == nullptr)
		return;
	// 注意：这里进行了背景修改，避免造成背景颜色混乱的问题...
	//backgroundColor = GREY_COLOR_BACKGROUND;
	//this->UpdateDisplayBackgroundColor();
	float scale = 1.0f; int x, y, newCX, newCY;
	uint32_t sourceCX = max(obs_source_get_width(m_lpImageSource), 1u);
	uint32_t sourceCY = max(obs_source_get_height(m_lpImageSource), 1u);
	GetCenterPosFromFixedScale(sourceCX, sourceCY, cx, cy, x, y, scale);
	// 这个接口调用非常重要，它决定了整个原始数据源画布的大小范围...
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));
	// 这个接口是设定视图在画布当中的投影...
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
		// 窗口退出全屏状态...
		this->setWindowFlags(Qt::SubWindow);
		this->showNormal();
		// 需要恢复到全屏前的矩形区域...
		this->setGeometry(m_rcNoramlRect);
	} else {
		// 需要先保存全屏前的矩形区域...
		m_rcNoramlRect = this->geometry();
		// 窗口进入全屏状态...
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
	// 全屏状态下不能关闭窗口...
	if (this->isFullScreen()) {
		event->ignore();
		return;
	}
	// 调用窗口的基础接口函数...
	QWidget::closeEvent(event);
}