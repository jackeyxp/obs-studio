
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
		obs_sceneitem_remove(m_teacherSceneItem);
		m_teacherSceneItem = nullptr;
	}
	// 释放默认的摄像头图片数据源...
	if (m_lpImageSource != nullptr) {
		obs_source_release(m_lpImageSource);
		m_lpImageSource = nullptr;
	}
	if (m_lpTextSource != nullptr) {
		obs_source_release(m_lpTextSource);
		m_lpTextSource = nullptr;
	}
}

void CViewTeacher::initWindow()
{
}

void CViewTeacher::doRemoveDrawCallback()
{
	obs_display_remove_draw_callback(this->GetDisplay(), CViewTeacher::doDrawTeacherPreview, this);
}

void CViewTeacher::doUpdatedSmartSource(OBSSource source)
{
	obs_source_t * lpTeacherSource = obs_sceneitem_get_source(m_teacherSceneItem);
	if (lpTeacherSource != source)
		return;
	obs_data_t * settings = obs_source_get_settings(lpTeacherSource);
	const char * lpLabel = obs_data_get_string(settings, "notice");
	uint32_t nCalcPTS = (uint32_t)obs_data_get_int(settings, "pts");
	bool bIsDrawImage = obs_data_get_bool(settings, "render");
	obs_data_release(settings);
	QString strNotice = QTStr(lpLabel);
	if (strNotice.size() <= 0)
		return;
	// 如果是丢帧的信息，需要带上时间戳内容...
	if (astrcmpi(lpLabel, "Render.Window.DropVideoFrame") == 0) {
		strNotice = QString(QTStr(lpLabel)).arg(nCalcPTS);
	}
	// 保存是否能够绘制图片的标志...
	m_bIsDrawImage = bIsDrawImage;
	// 将QString字符串转换成UTF8格式...
	m_strUTF8TextLabel = strNotice.toUtf8().toStdString();
	// 将更新后的UTF8字符串更新到提示信息数据源当中...
	const char * lpTxtNotice = m_strUTF8TextLabel.c_str();
	obs_data_t * lpTxtSettings = obs_source_get_settings(m_lpTextSource);
	// 更新文字信息，通过当前数据源的配置结构...
	obs_data_set_string(lpTxtSettings, "text", lpTxtNotice);
	// 将新的资源配置应用到当前text资源对象当中...
	obs_source_update(m_lpTextSource, lpTxtSettings);
	// 注意：这里必须手动进行引用计数减少，否则，会造成内存泄漏...
	obs_data_release(lpTxtSettings);
}

void CViewTeacher::onUpdateTeacherLabel(const char * lpLabelName)
{
	string strNewUTF8Label = Str(lpLabelName);
	if (strNewUTF8Label.size() <= 0 || m_lpTextSource == nullptr)
		return;
	m_strUTF8TextLabel = strNewUTF8Label;
	const char * lpStrNotice = m_strUTF8TextLabel.c_str();
	obs_data_t * settings = obs_source_get_settings(m_lpTextSource);
	// 更新文字信息，通过当前数据源的配置结构...
	obs_data_set_string(settings, "text", lpStrNotice);
	// 将新的资源配置应用到当前text资源对象当中...
	obs_source_update(m_lpTextSource, settings);
	// 注意：这里必须手动进行引用计数减少，否则，会造成内存泄漏...
	obs_data_release(settings);
}

bool CViewTeacher::doLoadNoTeacherLabel()
{
	m_strUTF8TextLabel = Str("Render.Window.TeacherNotice");
	const char *lpStrNotice = m_strUTF8TextLabel.c_str();
	const char *text_source_id = "text_gdiplus";
	obs_data_t *settings = obs_data_create();
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", "Arial");
	obs_data_set_int(font, "flags", 1); // Bold text
	obs_data_set_int(font, "size", 40);
	obs_data_set_obj(settings, "font", font);
	obs_data_set_string(settings, "text", lpStrNotice);
	obs_data_set_bool(settings, "outline", false);
	// 文本数据源的颜色顺序会在内部转换 => rgb_to_bgr()
	obs_data_set_int(settings, "color", QColor(0, 255, 255).rgb());

	const char * lpStrName = obs_source_get_display_name(text_source_id);
	obs_source_t * txtSource = obs_source_create_private(text_source_id, lpStrName, settings);

	obs_data_release(font);
	obs_data_release(settings);

	m_lpTextSource = txtSource;

	return true;
}

// 加载默认的老师端图片数据源对象...
bool CViewTeacher::doLoadNoTeacherImage()
{
	string strNoTeacherPath;
	// 通过外部资源的方式获取默认摄像头的全路径 => 不能用qrc资源...
	if (!GetDataFilePath("images/class.png", strNoTeacherPath))
		return false;
	assert(strNoTeacherPath.size() > 0);
	obs_data_t * settings = obs_data_create();
	obs_data_set_string(settings, "file", strNoTeacherPath.c_str());
	obs_source_t * source = obs_source_create_private("image_source", NULL, settings);
	obs_data_release(settings);
	m_lpImageSource = source;
	return true;
}

void CViewTeacher::doCreateTeacherSource()
{
	// 讲师端数据源已经创建，直接返回...
	if (m_teacherSceneItem != nullptr)
		return;
	// 创建一个新的讲师端数据源，会激发AddSceneItem事件...
	obs_scene_t * lpObsScene = m_lpStudentWindow->GetObsScene();
	const char * lpSmartID = App()->InteractSmartSource();
	const char * lpSmartName = obs_source_get_display_name(lpSmartID);
	obs_source_t * lpTeacherSource = obs_source_create(lpSmartID, lpSmartName, NULL, nullptr);
	// 如果没有获取到了默认的Smart数据源 => 直接返回 => 注意处理存盘标志...
	if (lpObsScene == nullptr || lpSmartID == nullptr && lpTeacherSource == nullptr)
		return;
	// 初始化默认数据源...
	obs_enter_graphics();
	vec2 vPos = { 120.0f, 120.0f };
	// 这里的位置是相对整个MainTexure的位置 => 不能设定为0点位置...
	obs_sceneitem_t * lpTeacherItem = obs_scene_add(lpObsScene, lpTeacherSource);
	// 注意：这里会激发信号事件AddSceneItem => 需要减少引用计数器...
	obs_source_release(lpTeacherSource);
	// 设置摄像头可见，以及设定初始位置...
	// 注意：0点位置的数据源是需要对外输出的数据源...
	obs_sceneitem_set_visible(lpTeacherItem, true);
	obs_sceneitem_set_pos(lpTeacherItem, &vPos);
	obs_leave_graphics();
	// 这里需要设置老师端数据源的音频为只播放不输出状态，默认是NONE，既不输出也不本地播放...
	obs_source_set_monitoring_type(lpTeacherSource, OBS_MONITORING_TYPE_MONITOR_ONLY);
	blog(LOG_INFO, "User changed audio monitoring for source '%s' to: %s", obs_source_get_name(lpTeacherSource), "monitor only");
	// 保存讲师端场景数据源对象 => 没有采用激发AddSceneItem事件的方式...
	m_teacherSceneItem = lpTeacherItem;
}

// 初始化摄像头对象 => 创建数据源...
bool CViewTeacher::doInitTeacher()
{
	// 首先加载默认的无讲师图片数据源...
	this->doLoadNoTeacherImage();
	this->doLoadNoTeacherLabel();
	// 创建讲师端数据源对象...
	this->doCreateTeacherSource();
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

void CViewTeacher::doRenderTeacherSource(uint32_t cx, uint32_t cy)
{
	obs_source_t * lpTeacherSource = obs_sceneitem_get_source(m_teacherSceneItem);
	if (!m_bIsDrawImage || lpTeacherSource == nullptr)
		return;
	int x, y;
	int newCX, newCY;
	float scale = 1.0f;
	uint32_t sourceCX = max(obs_source_get_width(lpTeacherSource), 1u);
	uint32_t sourceCY = max(obs_source_get_height(lpTeacherSource), 1u);
	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(lpTeacherSource);
}

void CViewTeacher::doRenderNoTeacherImage(uint32_t cx, uint32_t cy)
{
	obs_source_t * lpTeacherSource = m_lpImageSource;
	if (m_bIsDrawImage || lpTeacherSource == nullptr)
		return;
	int x, y;
	int newCX, newCY;
	float scale = 1.0f;
	uint32_t sourceCX = max(obs_source_get_width(lpTeacherSource), 1u);
	uint32_t sourceCY = max(obs_source_get_height(lpTeacherSource), 1u);
	GetCenterPosFromFixedScale(sourceCX, sourceCY, cx, cy, x, y, scale);
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));
	gs_set_viewport(x, y - 120, newCX, newCY);
	obs_source_video_render(lpTeacherSource);
}

void CViewTeacher::doRenderNoTeacherLabel(uint32_t cx, uint32_t cy)
{
	obs_source_t * lpTeacherSource = m_lpTextSource;
	if (m_bIsDrawImage || lpTeacherSource == nullptr)
		return;
	int x, y;
	int newCX, newCY;
	float scale = 1.0f;
	uint32_t sourceCX = max(obs_source_get_width(lpTeacherSource), 1u);
	uint32_t sourceCY = max(obs_source_get_height(lpTeacherSource), 1u);
	GetCenterPosFromFixedScale(sourceCX, sourceCY, cx, cy, x, y, scale);
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(lpTeacherSource);
}

void CViewTeacher::doRenderAllSource(uint32_t cx, uint32_t cy)
{
	gs_viewport_push();
	gs_projection_push();

	this->doRenderTeacherSource(cx, cy);
	this->doRenderNoTeacherImage(cx, cy);
	this->doRenderNoTeacherLabel(cx, cy);

	gs_projection_pop();
	gs_viewport_pop();
}

void CViewTeacher::doDrawTeacherPreview(void *data, uint32_t cx, uint32_t cy)
{
	CViewTeacher * window = static_cast<CViewTeacher *>(data);
	window->doRenderAllSource(cx, cy);
}

void CViewTeacher::onRemoteLiveOnLine(int nLiveID, bool bIsLiveOnLine)
{
	// 如果右侧讲师端数据源无效，直接返回...
	obs_source_t * lpTeacherSource = obs_sceneitem_get_source(m_teacherSceneItem);
	if (lpTeacherSource == nullptr || m_teacherSceneItem == nullptr)
		return;
	// 更新正在尝试连接的文字标签信息...
	this->onUpdateTeacherLabel(bIsLiveOnLine ? "Render.Window.ConnectServer" : "Render.Window.TeacherNotice");
	// 无论在线还是离线，都要重新显示文字提示信息...
	m_bIsDrawImage = false;
	// 保存老师端数据源是否在线，以便显示使用...
	m_bTeacherOnLine = bIsLiveOnLine;
	// 尝试创建右侧老师端播放画面对象...
	obs_data_t * lpSettings = obs_source_get_settings(lpTeacherSource);
	int nRoomID = atoi(App()->GetRoomIDStr().c_str());
	obs_data_set_int(lpSettings, "room_id", nRoomID);
	obs_data_set_int(lpSettings, "live_id", nLiveID);
	obs_data_set_bool(lpSettings, "live_on", bIsLiveOnLine);
	obs_data_set_int(lpSettings, "udp_port", App()->GetUdpPort());
	obs_data_set_string(lpSettings, "udp_addr", App()->GetUdpAddr().c_str());
	obs_data_set_int(lpSettings, "tcp_socket", App()->GetRemoteTcpSockFD());
	obs_data_set_int(lpSettings, "client_type", App()->GetClientType());
	// 将新的资源配置应用到当前smart_source资源对象当中...
	obs_source_update(lpTeacherSource, lpSettings);
	// 注意：这里必须手动进行引用计数减少，否则，会造成内存泄漏...
	obs_data_release(lpSettings);
}