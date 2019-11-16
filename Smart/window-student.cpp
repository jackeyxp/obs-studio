
#include "obs-app.hpp"
#include "platform.hpp"
#include "window-student.h"
#include "window-view-camera.hpp"
#include "window-view-teacher.hpp"
#include "window-basic-settings.hpp"
#include "window-student-output.hpp"

#include "display-helpers.hpp"
#include <util/profiler.hpp>
#include "qt-wrappers.hpp"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>

#ifdef _WIN32
#include "win-update/win-update.hpp"
#endif

#define DESKTOP_AUDIO_1 Str("DesktopAudioDevice1")
#define DESKTOP_AUDIO_2 Str("DesktopAudioDevice2")
#define AUX_AUDIO_1 Str("AuxAudioDevice1")
#define AUX_AUDIO_2 Str("AuxAudioDevice2")
#define AUX_AUDIO_3 Str("AuxAudioDevice3")
#define AUX_AUDIO_4 Str("AuxAudioDevice4")

#define SIMPLE_ENCODER_X264        "x264"
#define SIMPLE_ENCODER_X264_LOWCPU "x264_lowcpu"
#define SIMPLE_ENCODER_QSV         "qsv"
#define SIMPLE_ENCODER_NVENC       "nvenc"
#define SIMPLE_ENCODER_AMD         "amd"

namespace {
	template<typename OBSRef> struct SignalContainer {
		OBSRef ref;
		vector<shared_ptr<OBSSignal>> handlers;
	};
}

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
	if (m_nClassTimer > 0) {
		this->killTimer(m_nClassTimer);
		m_nClassTimer = -1;
	}
	if (m_nOutputTimer > 0) {
		this->killTimer(m_nOutputTimer);
		m_nOutputTimer = -1;
	}
	// 释放学生端输出对象集合体...
	if (m_lpStudentOutput != nullptr) {
		delete m_lpStudentOutput;
		m_lpStudentOutput = nullptr;
	}
}

#define STARTUP_SEPARATOR \
	"==== Startup complete ==============================================="
#define SHUTDOWN_SEPARATOR \
	"==== Shutting down =================================================="

#define UNSUPPORTED_ERROR                                                     \
	"Failed to initialize video:\n\nRequired graphics API functionality " \
	"not found.  Your GPU may not be supported."

#define UNKNOWN_ERROR                                                  \
	"Failed to initialize video.  Your GPU may not be supported, " \
	"or your graphics drivers may need to be updated."

#define OBS_D3D_INSTALL -1000

// 返回一个特殊错误号，避免发送异常...
int CStudentWindow::doD3DSetup()
{
	AutoUpdateThread::doLaunchDXWebSetup();
	return OBS_D3D_INSTALL;
}

void CStudentWindow::InitOBSCallbacks()
{
	ProfileScope("CStudentWindow::InitOBSCallbacks");

	signalHandlers.reserve(signalHandlers.size() + 6);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_create",
		CStudentWindow::SourceCreated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_remove",
		CStudentWindow::SourceRemoved, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_updated",
		CStudentWindow::SourceUpdated, this);
}

void CStudentWindow::SourceUpdated(void *data, calldata_t *params)
{
	// 注意：这里必须使用WaitConnection()，否则会出现参数错误...
	obs_source_t *source = (obs_source_t*)calldata_ptr(params, "source");
	QMetaObject::invokeMethod(static_cast<CStudentWindow*>(data),
		"UpdatedSmartSource", WaitConnection(),
		Q_ARG(OBSSource, OBSSource(source)));
}

void CStudentWindow::UpdatedSmartSource(OBSSource source)
{
	if (m_viewTeacher == nullptr) return;
	m_viewTeacher->doUpdatedSmartSource(source);
}

void CStudentWindow::SourceCreated(void *data, calldata_t *params)
{
	// 注意：这里必须使用WaitConnection()，否则会出现参数错误...
	obs_source_t *source = (obs_source_t *)calldata_ptr(params, "source");
	if (obs_scene_from_source(source) != NULL) {
		QMetaObject::invokeMethod(static_cast<CStudentWindow *>(data),
			"AddScene", WaitConnection(), Q_ARG(OBSSource, OBSSource(source)));
	}
}

void CStudentWindow::AddScene(OBSSource source)
{
	const char *name = obs_source_get_name(source);
	obs_scene_t *scene = obs_scene_from_source(source);
	signal_handler_t *handler = obs_source_get_signal_handler(source);
	signal_handler_connect(handler, "item_add", CStudentWindow::SceneItemAdded, this);
	// 注意：这里需要保存场景对象，并增加引用计数器...
	if (scene != nullptr && m_obsScene == nullptr) {
		obs_scene_addref(scene);
		m_obsScene = scene;
	}
}

void CStudentWindow::SceneItemAdded(void *data, calldata_t *params)
{
	CStudentWindow *window = static_cast<CStudentWindow *>(data);
	obs_sceneitem_t *item = (obs_sceneitem_t *)calldata_ptr(params, "item");
	QMetaObject::invokeMethod(window, "AddSceneItem", Q_ARG(OBSSceneItem, OBSSceneItem(item)));
}

void CStudentWindow::AddSceneItem(OBSSceneItem item)
{
	obs_scene_t *scene = obs_sceneitem_get_scene(item);
	obs_source_t *sceneSource = obs_scene_get_source(scene);
	obs_source_t *itemSource = obs_sceneitem_get_source(item);
	blog(LOG_INFO, "User added source '%s' (%s) to scene '%s'",
		obs_source_get_name(itemSource),
		obs_source_get_id(itemSource),
		obs_source_get_name(sceneSource));
	// 根据数据源id判断并保存摄像头数据源 => 保存场景条目，计数器在数据源上...
	/*bool bIsDShowInput = ((astrcmpi(obs_source_get_id(itemSource), App()->DShowInputSource()) == 0) ? true : false);
	if (itemSource != nullptr && bIsDShowInput && m_viewCamera.isNull()) {
		m_viewCamera->SaveDShowSceneItem(item);
	}
	// 如果是smart_source数据源 => 老师端数据源，直接存放到右侧窗口对象当中...
	bool bIsTeacherSource = ((astrcmpi(obs_source_get_id(itemSource), App()->InteractSmartSource()) == 0) ? true : false);
	if (itemSource != nullptr && bIsTeacherSource && m_viewTeacher.isNull()) {
		m_viewTeacher->SaveTeacherSceneItem(item);
	}*/
}

void CStudentWindow::SourceRemoved(void *data, calldata_t *params)
{
	obs_source_t *source = (obs_source_t *)calldata_ptr(params, "source");
}

void CStudentWindow::ResetOutputs()
{
	ProfileScope("CStudentWindow::ResetOutputs");
	const char *mode = config_get_string(basicConfig, "Output", "Mode");
	bool advOut = astrcmpi(mode, "Advanced") == 0;
	if (m_lpStudentOutput != nullptr) {
		delete m_lpStudentOutput;
		m_lpStudentOutput = nullptr;
	}
	// 创建新的学生端输出对象集合体 => 录像|推流...
	m_lpStudentOutput = new CStudentOutput(this);
}

void CStudentWindow::OBSInit()
{
	ProfileScope("CStudentWindow::OBSInit");

	const char *sceneCollection = Str("Student.Scene.Collection");
	char savePath[512] = { 0 };
	char fileName[512] = { 0 };
	int ret = -1;

	if (!sceneCollection) {
		throw "Failed to get scene collection name";
	}
	ret = snprintf(fileName, 512, "obs-smart/basic/scenes/%s.json", sceneCollection);
	if (ret <= 0) {
		throw "Failed to create scene collection file name";
	}

	ret = GetConfigPath(savePath, sizeof(savePath), fileName);
	if (ret <= 0) {
		throw "Failed to get scene collection json file path";
	}

	if (!this->InitBasicConfig()) {
		throw "Failed to load basic.ini";
	}
	if (!this->ResetAudio()) {
		throw "Failed to initialize audio";
	}
	// 后续还要调用这个接口...
	ret = this->ResetVideo();
	// 针对D3D做特殊拦截操作...
	if (ret == OBS_D3D_INSTALL)
		return;
	switch (ret) {
	case OBS_VIDEO_MODULE_NOT_FOUND:
		throw "Failed to initialize video:  Graphics module not found";
	case OBS_VIDEO_NOT_SUPPORTED:
		throw UNSUPPORTED_ERROR;
	case OBS_VIDEO_INVALID_PARAM:
		throw "Failed to initialize video:  Invalid parameters";
	default:
		if (ret != OBS_VIDEO_SUCCESS)
			throw UNKNOWN_ERROR;
	}

	/* load audio monitoring */
#if defined(_WIN32) || defined(__APPLE__) || HAVE_PULSEAUDIO
	const char *device_name = config_get_string(basicConfig, "Audio", "MonitoringDeviceName");
	const char *device_id = config_get_string(basicConfig, "Audio", "MonitoringDeviceId");
	obs_set_audio_monitoring_device(device_name, device_id);
	blog(LOG_INFO, "Audio monitoring device:\n\tname: %s\n\tid: %s", device_name, device_id);
#endif

	this->InitOBSCallbacks();

	//InitHotkeys();
	//AddExtraModulePaths();

	// 添加需要忽略的模块名称 => 忽略API模块...
	obs_add_ignore_module("frontend-tools.dll");

	blog(LOG_INFO, "---------------------------------");
	obs_load_all_modules();
	blog(LOG_INFO, "---------------------------------");
	obs_log_loaded_modules();
	blog(LOG_INFO, "---------------------------------");
	obs_post_load_modules();

	InitBasicConfigDefaults2();
	CheckForSimpleModeX264Fallback();

	blog(LOG_INFO, STARTUP_SEPARATOR);

	this->ResetOutputs();

	//InitPrimitives();
	
	// 注意：为了解决Load加载慢的问题，进行了2次异步操作...
	// 注意：实际Load执行代码位置 => DeferredLoad...

#ifdef _WIN32
	uint32_t winVer = GetWindowsVersion();
	if (winVer > 0 && winVer < 0x602) {
		bool disableAero = config_get_bool(basicConfig, "Video", "DisableAero");
		SetAeroEnabled(!disableAero);
	}
#endif

	// 可以进行保存配置了...
	disableSaving--;
	
	// 显示窗口...
	this->show();

	// 强制禁用一些用不到的数据源，避免干扰，混乱...
	obs_enable_source_type("game_capture", false);
	obs_enable_source_type("wasapi_output_capture", false);

	// 保存配置路径以便后续使用...
	m_strSavePath = QT_UTF8(savePath);
	// 为了避免阻塞主界面的显示，进行两次异步加载Load()配置...
	QTimer::singleShot(20, this, SLOT(OnFinishedLoad()));
}

// 异步时钟调用加载完毕事件通知...
void CStudentWindow::OnFinishedLoad()
{
	QMetaObject::invokeMethod(this, "DeferredLoad",
		Qt::QueuedConnection,
		Q_ARG(QString, m_strSavePath),
		Q_ARG(int, 1));
}

// 第二次异步信号槽调用实际的 Load() 接口...
void CStudentWindow::DeferredLoad(const QString &file, int requeueCount)
{
	ProfileScope("CStudentWindow::DeferredLoad");

	if (--requeueCount > 0) {
		QMetaObject::invokeMethod(this, "DeferredLoad",
			Qt::QueuedConnection,
			Q_ARG(QString, file),
			Q_ARG(int, requeueCount));
		return;
	}
	// 创建摄像头预览窗口，关联预览函数接口...
	m_viewCamera = new CViewCamera(this);
	// 创建右侧教师预览窗口，关联预览函数接口...
	m_viewTeacher = new CViewTeacher(this);
	// 这里的操作为了避免发生闪烁...
	m_viewTeacher->resize(0, 0);
	m_viewCamera->resize(0, 0);

	// 加载系统各种配置参数...
	this->Load(QT_TO_UTF8(file));

	// 加载场景配置之后再初始化并显示窗口...
	m_viewTeacher->doInitTeacher();
	m_viewTeacher->show();
	// 加载场景配置之后再初始化并显示窗口...
	m_viewCamera->doInitCamera();
	m_viewCamera->show();

	// 注意：这里必须在 Load() 之后调用，否则无法执行 CreateDefaultScene()...
	// 注意：RefreshSceneCollections() 放在 DeferredLoad() 里面的 Load() 之后执行...
	//this->RefreshSceneCollections();

	// 打印场景列表...
	this->LogScenes();

	// 设置已加载完毕的标志...
	m_bIsLoaded = true;
	// 立即启动远程连接...
	App()->doCheckRemote();
}

static void LoadAudioDevice(const char *name, int channel, obs_data_t *parent)
{
	obs_data_t *data = obs_data_get_obj(parent, name);
	if (!data)
		return;

	obs_source_t *source = obs_load_source(data);
	if (source) {
		obs_set_output_source(channel, source);
		obs_source_release(source);
	}

	obs_data_release(data);
}

void CStudentWindow::Load(const char *file)
{
	ProfileScope("CStudentWindow::Load");

	disableSaving++;

	// 查看指定的场景配置文件是否有效 => 无效，加载默认的场景并进行存盘...
	obs_data_t *data = obs_data_create_from_json_file_safe(file, "bak");
	if (data == nullptr) {
		disableSaving--;
		blog(LOG_INFO, "No scene file found, creating default scene");
		this->CreateDefaultScene(true);
		//this->SaveProject();
		return;
	}
	/////////////////////////////////////////////////////////////////////
	// 注意：这里需要后续改造，从json文件加载场景内容还没有具体改造...
	// 注意：这里需要结合 AddSceneItem 才能配合完成具体改造...
	/////////////////////////////////////////////////////////////////////

	// 已经读取到有效的场景数据...
	this->ClearSceneData();

	// 从json配置文件当中读取有效的字段内容...
	obs_data_array_t *sources = obs_data_get_array(data, "sources");
	const char *sceneName = obs_data_get_string(data, "current_scene");
	const char *curSceneCollection = Str("Student.Scene.Collection");
	obs_data_set_default_string(data, "name", curSceneCollection);
	const char *name = obs_data_get_string(data, "name");
	
	if (!name || !*name) {
		name = curSceneCollection;
	}
	// 加载其它频道下的音频数据源 => 频道3有效...
	LoadAudioDevice(DESKTOP_AUDIO_1, 1, data);
	LoadAudioDevice(DESKTOP_AUDIO_2, 2, data);
	LoadAudioDevice(AUX_AUDIO_1, 3, data);
	LoadAudioDevice(AUX_AUDIO_2, 4, data);
	LoadAudioDevice(AUX_AUDIO_3, 5, data);
	LoadAudioDevice(AUX_AUDIO_4, 6, data);

	// 注意：这里会激发多个信号事件 => SourceCreated | AddSceneItem
	// 注意：信号事件里会保存 => m_obsScene | m_dshowSceneItem
	// 注意：保存之后的引用计数器累加非常重要...
	obs_load_sources(sources, nullptr, nullptr);

	// 释放引用计数器对象...
	obs_data_array_release(sources);
	obs_data_release(data);

	disableSaving--;
}

static void LogFilter(obs_source_t *, obs_source_t *filter, void *v_val)
{
	const char *name = obs_source_get_name(filter);
	const char *id = obs_source_get_id(filter);
	int val = (int)(intptr_t)v_val;
	string indent;

	for (int i = 0; i < val; i++)
		indent += "    ";

	blog(LOG_INFO, "%s- filter: '%s' (%s)", indent.c_str(), name, id);
}

static bool LogSceneItem(obs_scene_t *, obs_sceneitem_t *item, void *v_val)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	const char *name = obs_source_get_name(source);
	const char *id = obs_source_get_id(source);
	int indent_count = (int)(intptr_t)v_val;
	string indent;

	for (int i = 0; i < indent_count; i++)
		indent += "    ";

	blog(LOG_INFO, "%s- source: '%s' (%s)", indent.c_str(), name, id);

	obs_monitoring_type monitoring_type =
		obs_source_get_monitoring_type(source);

	if (monitoring_type != OBS_MONITORING_TYPE_NONE) {
		const char *type =
			(monitoring_type == OBS_MONITORING_TYPE_MONITOR_ONLY)
			? "monitor only"
			: "monitor and output";

		blog(LOG_INFO, "    %s- monitoring: %s", indent.c_str(), type);
	}
	int child_indent = 1 + indent_count;
	obs_source_enum_filters(source, LogFilter,
		(void *)(intptr_t)child_indent);
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, LogSceneItem,
		(void *)(intptr_t)child_indent);
	return true;
}

void CStudentWindow::LogScenes()
{
	blog(LOG_INFO, "------------------------------------------------");
	blog(LOG_INFO, "Loaded scenes:");

	obs_source_t *source = obs_scene_get_source(m_obsScene);
	const char *name = obs_source_get_name(source);

	blog(LOG_INFO, "- scene '%s':", name);
	obs_scene_enum_items(m_obsScene, LogSceneItem, (void *)(intptr_t)1);
	obs_source_enum_filters(source, LogFilter, (void *)(intptr_t)1);

	blog(LOG_INFO, "------------------------------------------------");
}

static void SaveAudioDevice(const char *name, int channel, obs_data_t *parent, vector<OBSSource> &audioSources)
{
	obs_source_t *source = obs_get_output_source(channel);
	if (!source)
		return;

	audioSources.push_back(source);

	obs_data_t *data = obs_save_source(source);

	obs_data_set_obj(parent, name, data);

	obs_data_release(data);
	obs_source_release(source);
}

static obs_data_t * GenerateSaveData(OBSScene &scene)
{
	obs_data_t *saveData = obs_data_create();

	vector<OBSSource> audioSources;
	audioSources.reserve(5);

	SaveAudioDevice(DESKTOP_AUDIO_1, 1, saveData, audioSources);
	SaveAudioDevice(DESKTOP_AUDIO_2, 2, saveData, audioSources);
	SaveAudioDevice(AUX_AUDIO_1, 3, saveData, audioSources);
	SaveAudioDevice(AUX_AUDIO_2, 4, saveData, audioSources);
	SaveAudioDevice(AUX_AUDIO_3, 5, saveData, audioSources);
	SaveAudioDevice(AUX_AUDIO_4, 6, saveData, audioSources);

	/* -------------------------------- */
	/* save non-group sources           */

	auto FilterAudioSources = [&](obs_source_t *source) {
		if (obs_source_is_group(source))
			return false;
		return find(begin(audioSources), end(audioSources), source) == end(audioSources);
	};
	using FilterAudioSources_t = decltype(FilterAudioSources);
	obs_data_array_t *sourcesArray = obs_save_sources_filtered(
		[](void *data, obs_source_t *source) {
		return (*static_cast<FilterAudioSources_t *>(data))(source);
	}, static_cast<void *>(&FilterAudioSources));

	const char *sceneCollection = Str("Student.Scene.Collection");
	obs_source_t *currentScene = obs_scene_get_source(scene);
	const char *sceneName = obs_source_get_name(currentScene);

	obs_data_set_string(saveData, "current_scene", sceneName);
	obs_data_set_string(saveData, "name", sceneCollection);
	obs_data_set_array(saveData, "sources", sourcesArray);
	obs_data_array_release(sourcesArray);

	return saveData;
}

void CStudentWindow::SaveProject()
{
	// 静止存盘标志...
	if (disableSaving)
		return;
	// 判断存盘路径是否有效...
	if (m_strSavePath.length() <= 0)
		return;
	OBSScene scene = m_obsScene;
	obs_data_t * saveData = GenerateSaveData(scene);
	string strJsonFile = m_strSavePath.toStdString();
	if (!obs_data_save_json_safe(saveData, strJsonFile.c_str(), "tmp", "bak")) {
		blog(LOG_ERROR, "Could not save scene data to %s", strJsonFile.c_str());
	}
	obs_data_release(saveData);
}

void CStudentWindow::CreateDefaultScene(bool firstStart)
{
	disableSaving++;

	// 查看是否有桌面的输入输出音频设备...
	//bool hasDesktopAudio = HasAudioDevices(App()->OutputAudioSource());
	//bool hasInputAudio = HasAudioDevices(App()->InputAudioSource());
	// 直接屏蔽电脑输出声音，彻底避免互动时的声音啸叫...
	//if (hasDesktopAudio) {
	//	ResetAudioDevice(App()->OutputAudioSource(), "default", Str("Basic.DesktopDevice1"), 1);
	//}
	// 有音频输入设备，直接使用默认的音频输入设备 => 放在频道3上面...
	/*if (hasInputAudio) {
		ResetAudioDevice(App()->InputAudioSource(), "default", Str("Basic.AuxDevice1"), 3);
	}*/

	// 创建场景对象，并将这个主场景对象挂载到频道0上面...
	obs_scene_t * lpObsScene = obs_scene_create(Str("Basic.Scene"));
	/* set the scene as the primary draw source and go */
	obs_set_output_source(0, obs_scene_get_source(lpObsScene));
	// 注意：这里会激发信号事件SourceCreated => 需要减少引用计数器...
	obs_scene_release(lpObsScene);

	// 创建默认的本地摄像头对象...
	//this->doCreateDShowSource(lpObsScene);
	// 创建默认的老师端数据源对象...
	//this->doCreateTeacherSource(lpObsScene);

	disableSaving--;
}

void CStudentWindow::ClearSceneData()
{
	disableSaving++;

	// 主动移除绑定上去回调接口...
	if (!m_viewCamera.isNull()) {
		m_viewCamera->doRemoveDrawCallback();
	}
	// 主动移除绑定上去回调接口...
	if (!m_viewTeacher.isNull()) {
		m_viewTeacher->doRemoveDrawCallback();
	}

	obs_set_output_source(0, nullptr);
	obs_set_output_source(1, nullptr);
	obs_set_output_source(2, nullptr);
	obs_set_output_source(3, nullptr);
	obs_set_output_source(4, nullptr);
	obs_set_output_source(5, nullptr);

	auto cb = [](void *unused, obs_source_t *source) {
		obs_source_remove(source);
		UNUSED_PARAMETER(unused);
		return true;
	};
	// remove 只是设置标志，并没有删除...
	obs_enum_sources(cb, nullptr);
	
	// 注意：wasapi_input_source是在频道3上，上面obs_set_output_source会自动删除...

	// 主动删除左侧窗口对象...
	if (!m_viewCamera.isNull()) {
		delete m_viewCamera;
		m_viewCamera = nullptr;
	}
	if (!m_viewTeacher.isNull()) {
		delete m_viewTeacher;
		m_viewTeacher = nullptr;
	}
	// 频道0 => 再释放场景数据源对象...
	if (m_obsScene != nullptr) {
		obs_scene_release(m_obsScene);
		m_obsScene = nullptr;
	}

	disableSaving--;

	blog(LOG_INFO, "All scene data cleared");
	blog(LOG_INFO, "------------------------------------------------");
}

void CStudentWindow::closeEvent(QCloseEvent *event)
{
	/*if (outputHandler && outputHandler->Active()) {
		SetShowing(true);
		QMessageBox::StandardButton button = OBSMessageBox::question(
			this, QTStr("ConfirmExit.Title"),
			QTStr("ConfirmExit.Text"));

		if (button == QMessageBox::No) {
			event->ignore();
			return;
		}
	}*/

	QWidget::closeEvent(event);
	if (!event->isAccepted())
		return;

	blog(LOG_INFO, SHUTDOWN_SEPARATOR);

	disableSaving++;

	/* Clear all scene data (dialogs, widgets, widget sub-items, scenes,
	* sources, etc) so that all references are released before shutdown */
	this->ClearSceneData();

	// 调用退出事件通知...
	App()->doLogoutEvent();
	// 调用关闭退出接口...
	App()->quit();
}

void CStudentWindow::CheckForSimpleModeX264Fallback()
{
	const char *curStreamEncoder = config_get_string(basicConfig, "SimpleOutput", "StreamEncoder");
	const char *curRecEncoder = config_get_string(basicConfig, "SimpleOutput", "RecEncoder");
	bool qsv_supported = false;
	bool amd_supported = false;
	bool nve_supported = false;
	bool changed = false;
	size_t idx = 0;
	const char *id;

	while (obs_enum_encoder_types(idx++, &id)) {
		if (strcmp(id, "amd_amf_h264") == 0)
			amd_supported = true;
		else if (strcmp(id, "obs_qsv11") == 0)
			qsv_supported = true;
		else if (strcmp(id, "ffmpeg_nvenc") == 0)
			nve_supported = true;
	}

	auto CheckEncoder = [&](const char *&name) {
		if (strcmp(name, SIMPLE_ENCODER_QSV) == 0) {
			if (!qsv_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
		} else if (strcmp(name, SIMPLE_ENCODER_NVENC) == 0) {
			if (!nve_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
		} else if (strcmp(name, SIMPLE_ENCODER_AMD) == 0) {
			if (!amd_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
		}
		return true;
	};

	if (!CheckEncoder(curStreamEncoder)) {
		config_set_string(basicConfig, "SimpleOutput", "StreamEncoder", curStreamEncoder);
	}
	if (!CheckEncoder(curRecEncoder)) {
		config_set_string(basicConfig, "SimpleOutput", "RecEncoder", curRecEncoder);
	}
	if (changed) {
		config_save_safe(basicConfig, "tmp", nullptr);
	}
}

void CStudentWindow::GetFPSCommon(uint32_t &num, uint32_t &den) const
{
	const char *val = config_get_string(basicConfig, "Video", "FPSCommon");

	if (strcmp(val, "10") == 0) {
		num = 10;
		den = 1;
	} else if (strcmp(val, "20") == 0) {
		num = 20;
		den = 1;
	} else if (strcmp(val, "24 NTSC") == 0) {
		num = 24000;
		den = 1001;
	} else if (strcmp(val, "25 PAL") == 0) {
		num = 25;
		den = 1;
	} else if (strcmp(val, "29.97") == 0) {
		num = 30000;
		den = 1001;
	} else if (strcmp(val, "48") == 0) {
		num = 48;
		den = 1;
	} else if (strcmp(val, "50 PAL") == 0) {
		num = 50;
		den = 1;
	} else if (strcmp(val, "59.94") == 0) {
		num = 60000;
		den = 1001;
	} else if (strcmp(val, "60") == 0) {
		num = 60;
		den = 1;
	} else {
		num = 30;
		den = 1;
	}
}

void CStudentWindow::GetFPSInteger(uint32_t &num, uint32_t &den) const
{
	num = (uint32_t)config_get_uint(basicConfig, "Video", "FPSInt");
	den = 1;
}

void CStudentWindow::GetFPSFraction(uint32_t &num, uint32_t &den) const
{
	num = (uint32_t)config_get_uint(basicConfig, "Video", "FPSNum");
	den = (uint32_t)config_get_uint(basicConfig, "Video", "FPSDen");
}

void CStudentWindow::GetFPSNanoseconds(uint32_t &num, uint32_t &den) const
{
	num = 1000000000;
	den = (uint32_t)config_get_uint(basicConfig, "Video", "FPSNS");
}

void CStudentWindow::GetConfigFPS(uint32_t &num, uint32_t &den) const
{
	uint32_t type = config_get_uint(basicConfig, "Video", "FPSType");

	if (type == 1) //"Integer"
		this->GetFPSInteger(num, den);
	else if (type == 2) //"Fraction"
		this->GetFPSFraction(num, den);
	else if (false) //"Nanoseconds", currently not implemented
		this->GetFPSNanoseconds(num, den);
	else 
		this->GetFPSCommon(num, den);
}

#ifdef _WIN32
#define IS_WIN32 1
#else
#define IS_WIN32 0
#endif

static inline int AttemptToResetVideo(struct obs_video_info *ovi)
{
	return obs_reset_video(ovi);
}

static inline enum obs_scale_type GetScaleType(ConfigFile &basicConfig)
{
	const char *scaleTypeStr = config_get_string(basicConfig, "Video", "ScaleType");

	if (astrcmpi(scaleTypeStr, "bilinear") == 0)
		return OBS_SCALE_BILINEAR;
	//else if (astrcmpi(scaleTypeStr, "lanczos") == 0)
	//	return OBS_SCALE_LANCZOS;
	else if (astrcmpi(scaleTypeStr, "area") == 0)
		return OBS_SCALE_AREA;
	else
		return OBS_SCALE_BICUBIC;
}

static inline enum video_format GetVideoFormatFromName(const char *name)
{
	if (astrcmpi(name, "I420") == 0)
		return VIDEO_FORMAT_I420;
	else if (astrcmpi(name, "NV12") == 0)
		return VIDEO_FORMAT_NV12;
	else if (astrcmpi(name, "I444") == 0)
		return VIDEO_FORMAT_I444;
#if 0 //currently unsupported
	else if (astrcmpi(name, "YVYU") == 0)
		return VIDEO_FORMAT_YVYU;
	else if (astrcmpi(name, "YUY2") == 0)
		return VIDEO_FORMAT_YUY2;
	else if (astrcmpi(name, "UYVY") == 0)
		return VIDEO_FORMAT_UYVY;
#endif
	else
		return VIDEO_FORMAT_RGBA;
}

int CStudentWindow::ResetVideo()
{
	//if (outputHandler && outputHandler->Active())
	//	return OBS_VIDEO_CURRENTLY_ACTIVE;

	ProfileScope("CStudentWindow::ResetVideo");

	struct obs_video_info ovi;
	int ret;

	this->GetConfigFPS(ovi.fps_num, ovi.fps_den);

	const char *colorFormat = config_get_string(basicConfig, "Video", "ColorFormat");
	const char *colorSpace = config_get_string(basicConfig, "Video", "ColorSpace");
	const char *colorRange = config_get_string(basicConfig, "Video", "ColorRange");

	ovi.graphics_module = App()->GetRenderModule();
	// 注意：这里将Video分组换成了Student分组，为了与讲师模式进行区分...
	ovi.base_width = (uint32_t)config_get_uint(basicConfig, "Student", "BaseCX");
	ovi.base_height = (uint32_t)config_get_uint(basicConfig, "Student", "BaseCY");
	ovi.output_width = (uint32_t)config_get_uint(basicConfig, "Student", "OutputCX");
	ovi.output_height = (uint32_t)config_get_uint(basicConfig, "Student", "OutputCY");
	ovi.output_format = GetVideoFormatFromName(colorFormat);
	ovi.colorspace = astrcmpi(colorSpace, "601") == 0 ? VIDEO_CS_601 : VIDEO_CS_709;
	ovi.range = astrcmpi(colorRange, "Full") == 0 ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL;
	ovi.adapter = config_get_uint(App()->GlobalConfig(), "Video", "AdapterIdx");
	ovi.gpu_conversion = true;
	ovi.screen_mode = false;
	ovi.scale_type = GetScaleType(basicConfig);

	if (ovi.base_width == 0 || ovi.base_height == 0) {
		ovi.base_width = 1920;
		ovi.base_height = 1080;
		config_set_uint(basicConfig, "Student", "BaseCX", 1920);
		config_set_uint(basicConfig, "Student", "BaseCY", 1080);
	}

	if (ovi.output_width == 0 || ovi.output_height == 0) {
		ovi.output_width = ovi.base_width;
		ovi.output_height = ovi.base_height;
		config_set_uint(basicConfig, "Student", "OutputCX", ovi.base_width);
		config_set_uint(basicConfig, "Student", "OutputCY", ovi.base_height);
	}

	ret = AttemptToResetVideo(&ovi);
	if (IS_WIN32 && ret != OBS_VIDEO_SUCCESS) {
		if (ret == OBS_VIDEO_CURRENTLY_ACTIVE) {
			blog(LOG_WARNING, "Tried to reset when already active");
			return ret;
		}
		// 弹框询问是否安装D3D或者使用OpenGL => 父窗口设定为空，否则无法居中显示...
		QMessageBox::StandardButton button = OBSMessageBox::question(
			NULL, QTStr("ConfirmD3D.Title"), QTStr("ConfirmD3D.Text"),
			QMessageBox::Yes | QMessageBox::No);
		if (button == QMessageBox::Yes) {
			return this->doD3DSetup();
		}
		/* Try OpenGL if DirectX fails on windows */
		if (astrcmpi(ovi.graphics_module, DL_OPENGL) != 0) {
			blog(LOG_WARNING,
				"Failed to initialize obs video (%d) "
				"with graphics_module='%s', retrying "
				"with graphics_module='%s'",
				ret, ovi.graphics_module, DL_OPENGL);
			ovi.graphics_module = DL_OPENGL;
			ret = AttemptToResetVideo(&ovi);
		}
	} else if (ret == OBS_VIDEO_SUCCESS) {
		//this->ResizePreview(ovi.base_width, ovi.base_height);
	}

	if (ret == OBS_VIDEO_SUCCESS) {
		//OBSBasicStats::InitializeValues();
		//OBSProjector::UpdateMultiviewProjectors();
	}

	return ret;
}

bool CStudentWindow::ResetAudio()
{
	ProfileScope("CStudentWindow::ResetAudio");

	struct obs_audio_info ai;
	ai.samples_per_sec = config_get_uint(basicConfig, "Audio", "SampleRate");

	const char *channelSetupStr = config_get_string(basicConfig, "Audio", "ChannelSetup");

	if (strcmp(channelSetupStr, "Mono") == 0)
		ai.speakers = SPEAKERS_MONO;
	else if (strcmp(channelSetupStr, "2.1") == 0)
		ai.speakers = SPEAKERS_2POINT1;
	else if (strcmp(channelSetupStr, "4.0") == 0)
		ai.speakers = SPEAKERS_4POINT0;
	else if (strcmp(channelSetupStr, "4.1") == 0)
		ai.speakers = SPEAKERS_4POINT1;
	else if (strcmp(channelSetupStr, "5.1") == 0)
		ai.speakers = SPEAKERS_5POINT1;
	else if (strcmp(channelSetupStr, "7.1") == 0)
		ai.speakers = SPEAKERS_7POINT1;
	else
		ai.speakers = SPEAKERS_STEREO;

	return obs_reset_audio(&ai);
}

static const double scaled_vals[] = {
	1.0,
	1.25,
	(1.0 / 0.75),
	1.5,
	(1.0 / 0.6),
	1.75,
	2.0,
	2.25,
	2.5,
	2.75,
	3.0,
	0.0
};

bool CStudentWindow::InitBasicConfigDefaults()
{
	QList<QScreen *> screens = QGuiApplication::screens();

	if (!screens.size()) {
		OBSErrorBox(NULL, "There appears to be no monitors.  Er, this "
			"technically shouldn't be possible.");
		return false;
	}

	QScreen *primaryScreen = QGuiApplication::primaryScreen();

	uint32_t cx = primaryScreen->size().width();
	uint32_t cy = primaryScreen->size().height();

	bool oldResolutionDefaults = config_get_bool(
		App()->GlobalConfig(), "General", "Pre19Defaults");

	/* use 1920x1080 for new default base res if main monitor is above
	* 1920x1080, but don't apply for people from older builds -- only to
	* new users */
	if (!oldResolutionDefaults && (cx * cy) > (1920 * 1080)) {
		cx = 1920;
		cy = 1080;
	}

	bool changed = false;

	/* ----------------------------------------------------- */
	/* move over old FFmpeg track settings                   */
	if (config_has_user_value(basicConfig, "AdvOut", "FFAudioTrack") &&
		!config_has_user_value(basicConfig, "AdvOut", "Pre22.1Settings")) {

		int track = (int)config_get_int(basicConfig, "AdvOut", "FFAudioTrack");
		config_set_int(basicConfig, "AdvOut", "FFAudioMixes", 1LL << (track - 1));
		config_set_bool(basicConfig, "AdvOut", "Pre22.1Settings", true);
		changed = true;
	}

	/* ----------------------------------------------------- */
	/* move over mixer values in advanced if older config */
	if (config_has_user_value(basicConfig, "AdvOut", "RecTrackIndex") &&
		!config_has_user_value(basicConfig, "AdvOut", "RecTracks")) {

		uint64_t track = config_get_uint(basicConfig, "AdvOut", "RecTrackIndex");
		track = 1ULL << (track - 1);
		config_set_uint(basicConfig, "AdvOut", "RecTracks", track);
		config_remove_value(basicConfig, "AdvOut", "RecTrackIndex");
		changed = true;
	}

	/* ----------------------------------------------------- */

	if (changed) {
		config_save_safe(basicConfig, "tmp", nullptr);
	}

	/* ----------------------------------------------------- */

	// 对外输出模式选择 => 默认选择了 Advanced => 主要包含录像配置和压缩器配置...
	config_set_default_string(basicConfig, "Output", "Mode", "Advanced"); //"Simple");

	config_set_default_string(basicConfig, "SimpleOutput", "FilePath", GetDefaultVideoSavePath().c_str());
	config_set_default_string(basicConfig, "SimpleOutput", "RecFormat", "mp4");
	config_set_default_uint(basicConfig, "SimpleOutput", "VBitrate", 1024); //2500
	config_set_default_uint(basicConfig, "SimpleOutput", "ABitrate", 64); //160
	config_set_default_bool(basicConfig, "SimpleOutput", "UseAdvanced", false);
	config_set_default_bool(basicConfig, "SimpleOutput", "EnforceBitrate", true);
	config_set_default_string(basicConfig, "SimpleOutput", "Preset", "veryfast");
	config_set_default_string(basicConfig, "SimpleOutput", "NVENCPreset", "hq");
	config_set_default_string(basicConfig, "SimpleOutput", "RecQuality", "Stream");
	config_set_default_bool(basicConfig, "SimpleOutput", "RecRB", false);
	config_set_default_int(basicConfig, "SimpleOutput", "RecRBTime", 20);
	config_set_default_int(basicConfig, "SimpleOutput", "RecRBSize", 512);
	config_set_default_string(basicConfig, "SimpleOutput", "RecRBPrefix", "Replay");
	//config_set_default_string(basicConfig, "SimpleOutput", "StreamEncoder", SIMPLE_ENCODER_X264);
	//config_set_default_string(basicConfig, "SimpleOutput", "RecEncoder", SIMPLE_ENCODER_X264);

	// 这是针对 Advanced 输出模式的配置 => 包含 网络流输出 和 录像输出...
	config_set_default_bool(basicConfig, "AdvOut", "ApplyServiceSettings", true);
	config_set_default_bool(basicConfig, "AdvOut", "UseRescale", false);
	config_set_default_uint(basicConfig, "AdvOut", "TrackIndex", 1); // 默认网络流和录像都用轨道1(索引编号是0)
	config_set_default_string(basicConfig, "AdvOut", "Encoder", "obs_x264");

	// Advanced 输出模式当中针对录像的类型配置 => Standard|FFmpeg => 采用Standard模式...
	config_set_default_string(basicConfig, "AdvOut", "RecType", "Standard");

	// Standard标准录像模式的参数配置 => 音频可以支持多轨道录像 => 目前使用这些配置...
	config_set_default_string(basicConfig, "AdvOut", "RecFilePath", GetDefaultVideoSavePath().c_str());
	config_set_default_string(basicConfig, "AdvOut", "RecFormat", "mp4");
	config_set_default_bool(basicConfig, "AdvOut", "RecUseRescale", false);
	config_set_default_bool(basicConfig, "AdvOut", "RecFileNameWithoutSpace", true); //标准录像文件名不包含空格
	config_set_default_uint(basicConfig, "AdvOut", "RecTracks", (2 << 0)); //标准录像用轨道2(索引编号是1) => 支持多音轨录像
	config_set_default_string(basicConfig, "AdvOut", "RecEncoder", "none");

	// FFmpeg自定义录像模式的参数配置 => 音频只支持1个轨道录像 => 目前没有使用...
	config_set_default_bool(basicConfig, "AdvOut", "FFOutputToFile", true);
	config_set_default_string(basicConfig, "AdvOut", "FFFilePath", GetDefaultVideoSavePath().c_str());
	config_set_default_string(basicConfig, "AdvOut", "FFExtension", "mp4");
	config_set_default_uint(basicConfig, "AdvOut", "FFVBitrate", 1024); //2500);
	config_set_default_uint(basicConfig, "AdvOut", "FFVGOPSize", 150);  //250);
	config_set_default_bool(basicConfig, "AdvOut", "FFUseRescale", false);
	config_set_default_bool(basicConfig, "AdvOut", "FFIgnoreCompat", false);
	config_set_default_uint(basicConfig, "AdvOut", "FFABitrate", 64); //160);
	config_set_default_uint(basicConfig, "AdvOut", "FFAudioMixes", 1);   //FFmpeg录像用轨道1 => 单音轨录像
	//config_set_default_uint(basicConfig, "AdvOut", "FFAudioTrack", 1); //FFmpeg录像用轨道1 => 单音轨录像

	config_set_default_uint(basicConfig, "AdvOut", "Track1Bitrate", 64);//160);
	config_set_default_uint(basicConfig, "AdvOut", "Track2Bitrate", 64);//160);
	config_set_default_uint(basicConfig, "AdvOut", "Track3Bitrate", 64);//160);
	config_set_default_uint(basicConfig, "AdvOut", "Track4Bitrate", 64);//160);
	config_set_default_uint(basicConfig, "AdvOut", "Track5Bitrate", 64);//160);
	config_set_default_uint(basicConfig, "AdvOut", "Track6Bitrate", 64);//160);

	config_set_default_bool(basicConfig, "AdvOut", "RecRB", false);
	config_set_default_uint(basicConfig, "AdvOut", "RecRBTime", 20);
	config_set_default_int(basicConfig, "AdvOut", "RecRBSize", 512);

	// 为了与讲师模式区分，专门将Video修改成了Student，特殊处理...
	config_set_default_uint(basicConfig, "Student", "BaseCX", cx);
	config_set_default_uint(basicConfig, "Student", "BaseCY", cy);
	/* don't allow BaseCX/BaseCY to be susceptible to defaults changing */
	if (!config_has_user_value(basicConfig, "Student", "BaseCX") ||
		!config_has_user_value(basicConfig, "Student", "BaseCY")) {
		config_set_uint(basicConfig, "Student", "BaseCX", cx);
		config_set_uint(basicConfig, "Student", "BaseCY", cy);
		config_save_safe(basicConfig, "tmp", nullptr);
	}

	config_set_default_string(basicConfig, "Output", "FilenameFormatting", "%CCYY-%MM-%DD %hh-%mm-%ss");

	config_set_default_bool(basicConfig, "Output", "DelayEnable", false);
	config_set_default_uint(basicConfig, "Output", "DelaySec", 20);
	config_set_default_bool(basicConfig, "Output", "DelayPreserve", true);

	config_set_default_bool(basicConfig, "Output", "Reconnect", true);
	config_set_default_uint(basicConfig, "Output", "RetryDelay", 10);
	config_set_default_uint(basicConfig, "Output", "MaxRetries", 20);

	config_set_default_string(basicConfig, "Output", "BindIP", "default");
	config_set_default_bool(basicConfig, "Output", "NewSocketLoopEnable", false);
	config_set_default_bool(basicConfig, "Output", "LowLatencyEnable", false);

	int i = 0;
	uint32_t scale_cx = cx;
	uint32_t scale_cy = cy;

	/* use a default scaled resolution that has a pixel count no higher
	* than 1280x720 */
	while (((scale_cx * scale_cy) > (1280 * 720)) && scaled_vals[i] > 0.0) {
		double scale = scaled_vals[i++];
		scale_cx = uint32_t(double(cx) / scale);
		scale_cy = uint32_t(double(cy) / scale);
	}

	// 注意：这里将Video分组换成了Student分组，为了与讲师模式进行区分...
	config_set_default_uint(basicConfig, "Student", "OutputCX", scale_cx);
	config_set_default_uint(basicConfig, "Student", "OutputCY", scale_cy);
	/* don't allow OutputCX/OutputCY to be susceptible to defaults changing */
	if (!config_has_user_value(basicConfig, "Student", "OutputCX") ||
		!config_has_user_value(basicConfig, "Student", "OutputCY")) {
		config_set_uint(basicConfig, "Student", "OutputCX", scale_cx);
		config_set_uint(basicConfig, "Student", "OutputCY", scale_cy);
		config_save_safe(basicConfig, "tmp", nullptr);
	}

	config_set_default_uint(basicConfig, "Video", "FPSType", 0);
	config_set_default_string(basicConfig, "Video", "FPSCommon", "30");
	config_set_default_uint(basicConfig, "Video", "FPSInt", 30);
	config_set_default_uint(basicConfig, "Video", "FPSNum", 30);
	config_set_default_uint(basicConfig, "Video", "FPSDen", 1);
	config_set_default_string(basicConfig, "Video", "ScaleType", "bicubic");
	config_set_default_string(basicConfig, "Video", "ColorFormat", "NV12");
	config_set_default_string(basicConfig, "Video", "ColorSpace", "601");
	config_set_default_string(basicConfig, "Video", "ColorRange", "Partial");

	config_set_default_string(basicConfig, "Audio", "MonitoringDeviceId", "default");
	config_set_default_string(basicConfig, "Audio", "MonitoringDeviceName",
		Str("Basic.Settings.Advanced.Audio.MonitoringDevice.Default"));
	config_set_default_uint(basicConfig, "Audio", "SampleRate", 44100);
	config_set_default_string(basicConfig, "Audio", "ChannelSetup", "Stereo");
	config_set_default_double(basicConfig, "Audio", "MeterDecayRate", VOLUME_METER_DECAY_FAST);
	config_set_default_uint(basicConfig, "Audio", "PeakMeterType", 0);

	//CheckExistingCookieId();

	return true;
}

extern bool EncoderAvailable(const char *encoder);

void CStudentWindow::InitBasicConfigDefaults2()
{
	bool oldEncDefaults = config_get_bool(App()->GlobalConfig(), "General", "Pre23Defaults");
	bool useNV = EncoderAvailable("ffmpeg_nvenc") && !oldEncDefaults;

	config_set_default_string(basicConfig, "SimpleOutput", "StreamEncoder",
		useNV ? SIMPLE_ENCODER_NVENC : SIMPLE_ENCODER_X264);
	config_set_default_string(basicConfig, "SimpleOutput", "RecEncoder",
		useNV ? SIMPLE_ENCODER_NVENC : SIMPLE_ENCODER_X264);
}

bool CStudentWindow::InitBasicConfig()
{
	ProfileScope("CStudentWindow::InitBasicConfig");

	char configPath[512] = { 0 };

	int ret = this->GetProfilePath(configPath, sizeof(configPath), "");
	if (ret <= 0) {
		OBSErrorBox(nullptr, "Failed to get profile path");
		return false;
	}

	if (os_mkdir(configPath) == MKDIR_ERROR) {
		OBSErrorBox(nullptr, "Failed to create profile path");
		return false;
	}

	ret = this->GetProfilePath(configPath, sizeof(configPath), "basic.ini");
	if (ret <= 0) {
		OBSErrorBox(nullptr, "Failed to get base.ini path");
		return false;
	}

	int code = basicConfig.Open(configPath, CONFIG_OPEN_ALWAYS);
	if (code != CONFIG_SUCCESS) {
		OBSErrorBox(NULL, "Failed to open basic.ini: %d", code);
		return false;
	}

	if (config_get_string(basicConfig, "General", "Name") == nullptr) {
		const char *curName = config_get_string(App()->GlobalConfig(),
			"Basic", "Profile");

		config_set_string(basicConfig, "General", "Name", curName);
		basicConfig.SaveSafe("tmp");
	}

	return InitBasicConfigDefaults();
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
	// 注意：这里千万不能设置成透明背景，所有元素都要主动重绘，否则，就会出现镂空问题；
	// 注意：由于预览窗口是由底层系统重绘，所以，这里必须取消透明背景参数...
	//this->setAttribute(Qt::WA_TranslucentBackground);
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
	// 关联网络信号槽反馈结果事件...
	connect(&m_objNetManager, SIGNAL(finished(QNetworkReply *)), this, SLOT(onReplyFinished(QNetworkReply *)));
	// 加载窗口的层叠显示样式表...
	this->loadStyleSheet(":/student/css/Student.css");
	// 读取并保存默认的头像对象 => 需要进行必要的缩放操作...
	m_QPixClock = QPixmap(":/res/images/clock.png");
	m_QPixUserHead = QPixmap(":/res/images/avatar.png").scaled(50, 50);
	m_strUserNickName = QString("%1").arg(App()->GetUserNickName().c_str());
	m_strUserHeadUrl = QString("%1").arg(App()->GetUserHeadUrl().c_str());
	// 开启一个定时更新文字时钟 => 每隔1秒更新一次...
	m_nClassTimer = this->startTimer(1 * 1000);
	// 绘制初始的时钟内容...
	this->doDrawTimeClock();
	// 发起获取登录用户头像请求...
	this->doWebGetUserHead();
}

void CStudentWindow::doWebGetUserHead()
{
	m_eNetState = kWebGetUserHead;
	// 构造凭证访问地址，发起网络请求...
	QNetworkReply * lpNetReply = NULL;
	QNetworkRequest theQTNetRequest;
	QString strRequestURL = m_strUserHeadUrl;
	theQTNetRequest.setUrl(QUrl(strRequestURL));
	lpNetReply = m_objNetManager.get(theQTNetRequest);
}

void CStudentWindow::onReplyFinished(QNetworkReply *reply)
{
	// 如果发生网络错误，打印错误信息，跳出循环...
	if (reply->error() != QNetworkReply::NoError) {
		blog(LOG_INFO, "QT error => %d, %s", reply->error(), reply->errorString().toStdString().c_str());
		return;
	}
	// 读取完整的网络请求返回的内容数据包...
	int nStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	blog(LOG_INFO, "QT Status Code => %d", nStatusCode);
	// 根据状态分发处理过程...
	switch (m_eNetState) {
	case kWebGetUserHead: this->onProcGetUserHead(reply); break;
	}
}

void CStudentWindow::onProcGetUserHead(QNetworkReply *reply)
{
	// 读取获取到的网络数据内容...
	QPixmap theUserHead;
	ASSERT(m_eNetState == kWebGetUserHead);
	QByteArray & theByteArray = reply->readAll();
	// 从网路数据直接构造用户头像对象...
	if (!theUserHead.loadFromData(theByteArray))
		return;
	blog(LOG_INFO, "== QT WebGetUserHead OK ==");
	// 更新用户头像对象...
	m_QPixUserHead.detach();
	m_QPixUserHead = theUserHead.scaled(50, 50);
	this->update();
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
		// 注意：这里必须先还原，再进行坐标更新...
		this->showNormal();
		this->setGeometry(m_rcSrcGeometry);
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

void CStudentWindow::timerEvent(QTimerEvent *inEvent)
{
	int nTimerID = inEvent->timerId();
	if (nTimerID == m_nClassTimer) {
		++m_nTimeSecond;
		this->doDrawTimeClock();
	} else if (nTimerID == m_nOutputTimer) {
		this->doCheckOutput();
	}
}

void CStudentWindow::doDrawTimeClock()
{
	// 如果右侧窗口无效，或者，右侧窗口没有绘制播放，需要重置计数器...
	if (m_viewTeacher.isNull() || !m_viewTeacher->IsDrawImage()) {
		m_nTimeSecond = 0;
	}
	// 计算小时|分|秒...
	int nHours = m_nTimeSecond / 3600;
	int nMinute = (m_nTimeSecond % 3600) / 60;
	int nSecond = (m_nTimeSecond % 3600) % 60;
	QString strTime = (nHours <= 0) ? QString("%1:%2").arg(nMinute, 2, 10, QChar('0')).arg(nSecond, 2, 10, QChar('0')) :
		QString("%1:%2:%3").arg(nHours, 2, 10, QChar('0')).arg(nMinute, 2, 10, QChar('0')).arg(nSecond, 2, 10, QChar('0'));
	m_strClock = QTStr("Main.Window.TimeClock").arg(strTime);
	// 计算局部刷新位置的边界位置...
	QFontMetrics fontMetr(this->font());
	int nClockPixSize = fontMetr.width(m_strClock);
	QRect rcTitleRight = ui->hori_title->geometry();
	// 计算局部更新位置 => 只更新时钟显示区域 => 前后扩充2个像素边界...
	this->update(QRect(m_nXPosClock - 2, 0, nClockPixSize + 4, rcTitleRight.height()));
}

void CStudentWindow::doDrawTitle(QPainter & inPainter)
{
	// 先绘制标题栏矩形区域...
	QRect rcTitleRight = ui->hori_title->geometry();
	inPainter.fillRect(rcTitleRight, QColor(47, 47, 53));
	inPainter.setPen(QColor(61, 63, 70));
	rcTitleRight.adjust(-1, 0, 0, 0);
	inPainter.drawRect(rcTitleRight);
	// 对窗口标题进行修改 => 使用字典模式...
	QString strTitle = QTStr("Main.Window.TitleContent")
		.arg(App()->GetClientTypeName())
		.arg(App()->GetRoomIDStr().c_str());
	// 设置画笔颜色，计算标题显示位置，需要增加偏移...
	inPainter.setPen(QColor(255, 255, 255));
	QFontMetrics fontMetr(this->font());
	int nTitlePixSize = fontMetr.width(strTitle);
	int nPixelSize = QFontInfo(this->font()).pixelSize();
	int nPosX = (rcTitleRight.width() - nTitlePixSize) / 2 + rcTitleRight.left();
	int nPosY = (rcTitleRight.height() - nPixelSize) / 2 + nPixelSize;
	inPainter.drawText(nPosX, nPosY, strTitle);
	// 绘制时钟图标 => 直接绘制图标文件...
	nPosX += nTitlePixSize + 10;
	nPosY = (rcTitleRight.height() - m_QPixClock.height()) / 2;
	inPainter.drawPixmap(nPosX, nPosY, m_QPixClock);
	// 计算已开课时钟显示位置 => 绘制已生成的时间字符串内容...
	nPosY = (rcTitleRight.height() - nPixelSize) / 2 + nPixelSize;
	m_nXPosClock = nPosX + m_QPixClock.width() + 10;
	inPainter.drawText(m_nXPosClock, nPosY, m_strClock);
}

void CStudentWindow::doDrawLeftArea(QPainter & inPainter)
{
	// 再绘制左侧工具条矩形区域...
	QRect rcToolLeft = ui->vert_tool->geometry();
	inPainter.fillRect(rcToolLeft, QColor(67, 69, 85));
	// 绘制用户的昵称 => 需要计算是否显示省略号...
	inPainter.setPen(QColor(235, 235, 235));
	int nHeadSize = m_QPixUserHead.width();
	int nPosY = 30 + nHeadSize + 10;
	QFontMetrics fontMetr(this->font());
	int nNameSize = rcToolLeft.width() - 8 * 2;
	int nFontSize = fontMetr.width(m_strUserNickName);
	int nPosX = (rcToolLeft.width() - nNameSize) / 2;
	if (nFontSize > nNameSize) {
		QTextOption txtOption(Qt::AlignLeft | Qt::AlignTop);
		txtOption.setWrapMode(QTextOption::WrapAnywhere);
		QRect txtRect(nPosX, nPosY, nNameSize, (fontMetr.height() + 2) * 4);
		inPainter.drawText(txtRect, m_strUserNickName, txtOption);
		//m_strUserNickName = fontMetr.elidedText(m_strUserNickName, Qt::ElideRight, nNameSize);
		//nNameSize = fontMetr.width(m_strUserNickName);
	} else {
		// 注意：单行文字还要增加Y轴高度 => 从下往上...
		nPosY += QFontInfo(this->font()).pixelSize();
		nPosX = (rcToolLeft.width() - nFontSize) / 2;
		inPainter.drawText(nPosX, nPosY, m_strUserNickName);
	}

	// 抗锯齿 + 平滑边缘处理
	inPainter.setRenderHints(QPainter::Antialiasing, true);
	inPainter.setRenderHints(QPainter::SmoothPixmapTransform, true);

	// 需要计算显示位置 = > 设置裁剪圆形区域...
	QPainterPath pathCircle;
	nPosX = (rcToolLeft.width() - nHeadSize) / 2;
	pathCircle.addEllipse(nPosX, 30, nHeadSize, nHeadSize);
	inPainter.setClipPath(pathCircle);
	// 绘制用户的头像 => 固定位置，自动裁剪...
	inPainter.drawPixmap(nPosX, 30, m_QPixUserHead);
}

/*void CStudentWindow::doDShowResetVideo(int sourceCX, int sourceCY)
{
	int nResult = this->ResetVideo();
	blog(LOG_INFO, "== doDShowResetVideo result: %d ==", nResult);
	// 如果重置视频不成功，直接返回...
	if (nResult != OBS_VIDEO_SUCCESS)
		return;
	this->ResetOutputs();
	// 一旦重置视频成功，立即开启压缩输出功能...
	this->doStartStreaming();
}*/

/*if (!m_bIsStartOutput) {
	 // 通过异步信号消息通知，进行对外输出事件的操作...
	 QMetaObject::invokeMethod(this, "doStartOutput", Qt::QueuedConnection);
}
void CStudentWindow::doStartOutput()
{
	// 直接进行开始录像的启动测试操作...
	//this->doStartRecording();
	// 直接进行开始推流的启动测试操作...
	this->doStartStreaming();
	// 输出成功之后，一定要设定输出标志...
	m_bIsStartOutput = true;
}*/

// 老师端发出的直播推流的信号通知 => 直播编号和在线状态...
void CStudentWindow::onRemoteLiveOnLine(int nLiveID, bool bIsLiveOnLine)
{
	if (m_viewTeacher == nullptr) return;
	m_viewTeacher->onRemoteLiveOnLine(nLiveID, bIsLiveOnLine);
}

// 登录远程数据服务器成功之后的信号通知 => doTriggerSmartLogin()...
void CStudentWindow::onRemoteSmartLogin(int nLiveID)
{
	// 直接尝试拉取老师端正在推流直播...
	this->onRemoteLiveOnLine(nLiveID, (nLiveID > 0) ? true : false);
	// 如果时钟有效，需要先删除之...
	if (m_nOutputTimer > 0) {
		this->killTimer(m_nOutputTimer);
		m_nOutputTimer = -1;
	}
	// 开启一个推流上传时钟 => 每隔5秒检查一次...
	m_nOutputTimer = this->startTimer(5 * 1000);
	// 立即启动推流状态检测...
	this->doCheckOutput();
}

void CStudentWindow::onRemoteUdpLogout(int nLiveID, int tmTag, int idTag)
{
	// 如果不是学生端对象 => 直接返回...
	if (tmTag != TM_TAG_STUDENT)
		return;
	// 如果是学生观看端的处理过程...
	if (idTag == ID_TAG_LOOKER) {
		this->onRemoteLiveOnLine(nLiveID, false);
	}
	// 如果是学生推流端的处理过程...
	if (idTag == ID_TAG_PUSHER) {
	}
}

void CStudentWindow::doCheckOutput()
{
	// 如果发生格式变化...
	if (m_bIsResetVideo) {
		// 复原标志，重置视频...
		m_bIsResetVideo = false;
		// 必须先重置输出对象...
		this->ResetOutputs();
		// 再重置视频配置操作...
		int nResult = this->ResetVideo();
		blog(LOG_INFO, "== ResetVideo: %d ==", nResult);
	}
	// 如果不能对外输出，直接返回...
	if (!m_bIsStartOutput)
		return;
	ASSERT(m_bIsStartOutput);
	// 调用推流接口函数...
	this->doStartStreaming();
	// 调用录像接口函数...
	//this->doStartRecording();
}

void CStudentWindow::doStartStreaming()
{
	// 输出时钟无效，直接返回...
	if (m_nOutputTimer <= 0)
		return;
	// 学生输出对象无效，直接返回...
	if (m_lpStudentOutput == nullptr)
		return;
	// 如果正在推流，直接返回...
	if (m_lpStudentOutput->StreamingActive())
		return;
	// 开启学生推流操作...
	m_lpStudentOutput->StartStreaming();
}

void CStudentWindow::doStartRecording()
{
	// 输出时钟无效，直接返回...
	if (m_nOutputTimer <= 0)
		return;
	// 学生输出对象无效，直接返回...
	if (m_lpStudentOutput == nullptr)
		return;
	// 如果正在录像，直接返回...
	if (m_lpStudentOutput->RecordingActive())
		return;
	// 开启学生录像操作...
	m_lpStudentOutput->StartRecording();
}

float CStudentWindow::doDShowCheckRatio()
{
	float ratioScale = 3.0 / 4.0;
	uint32_t baseCX = (uint32_t)config_get_uint(basicConfig, "Student", "BaseCX");
	uint32_t baseCY = (uint32_t)config_get_uint(basicConfig, "Student", "BaseCY");
	uint32_t sourceCX = m_viewCamera.isNull() ? 0 : m_viewCamera->doGetCameraWidth();
	uint32_t sourceCY = m_viewCamera.isNull() ? 0 : m_viewCamera->doGetCameraHeight();
	// 摄像头的分辨率有效，需要进行相关的实际操作...
	if (sourceCX > 0 && sourceCY > 0) {
		// 计算当前摄像头有效的高宽比例...
		ratioScale = (sourceCY * 1.0f) / (sourceCX * 1.0f);
		// 如果分辨率与基础画布的分辨率不一致，需要重置...
		if (baseCX != sourceCX || baseCY != sourceCY) {
			// 直接将摄像头分辨率修改为输出分辨率，并保存到配置文件当中...
			config_set_uint(basicConfig, "Student", "BaseCX", sourceCX);
			config_set_uint(basicConfig, "Student", "BaseCY", sourceCY);
			config_set_uint(basicConfig, "Student", "OutputCX", sourceCX);
			config_set_uint(basicConfig, "Student", "OutputCY", sourceCY);
			config_save_safe(basicConfig, "tmp", nullptr);
			// 先存盘之后，再重置标志，最大降低对主界面的影响...
			// 注意：都放到doCheckOutput()处理，可以不用互斥...
			m_bIsResetVideo = true;
		}
		// 拿到有效摄像头数据，才能进行输出...
		m_bIsStartOutput = true;
	}
	// 返回摄像头高宽比例...
	return ratioScale;
}

void CStudentWindow::doDrawRightArea(QPainter & inPainter)
{
	// 在计算右侧整个空白区域...
	QRect rcTitleRight = ui->hori_title->geometry();
	QRect rcRightArea = ui->vert_right->geometry();
	rcRightArea.adjust(0, rcTitleRight.height()+1, 0, 0);
	// 绘制左侧自身摄像头区域...
	QRect rcRightSelf = rcRightArea;
	rcRightSelf.setWidth(rcRightArea.width() / 5);
	inPainter.fillRect(rcRightSelf, QColor(40, 42, 49));
	// 绘制两条不同的分割竖线...
	inPainter.setPen(QColor(27, 26, 28));
	inPainter.drawLine(rcRightSelf.right() + 1, rcRightSelf.top() + 1, rcRightSelf.right() + 1, rcRightSelf.bottom());
	inPainter.setPen(QColor(63, 64, 70));
	inPainter.drawLine(rcRightSelf.right() + 2, rcRightSelf.top() + 1, rcRightSelf.right() + 2, rcRightSelf.bottom());
	// 注意：当发现分辨率与系统设定的分辨率不一致时，需要重置ResetVideo()...
	// 预先计算原始数据源的长宽比例 => 默认4：3的比列...
	float ratioScale = this->doDShowCheckRatio();
	//blog(LOG_INFO, "== doDrawRightArea scale: %.2f ==", ratioScale);
	// 计算本地摄像头预览窗口的显示位置...
	QRect rcViewCamera;
	rcViewCamera.setLeft(rcRightSelf.left() + 1);
	rcViewCamera.setWidth(rcRightSelf.width() - 1);
	rcViewCamera.setTop(rcRightSelf.top() + (rcRightSelf.height() - rcViewCamera.width()) / 2);
	rcViewCamera.setHeight(rcViewCamera.width() * ratioScale);
	// 如果预览窗口有效，并且计算的新位置与当前获取的坐标位置不一致...
	if (!m_viewCamera.isNull() && !m_viewCamera->isFullScreen() &&
		rcViewCamera != m_viewCamera->geometry()) {
		m_viewCamera->setGeometry(rcViewCamera);
	}
	// 绘制右侧老师画面区域...
	QRect rcViewTeacher = rcRightArea;
	rcViewTeacher.setLeft(rcRightSelf.right() + 3);
	inPainter.fillRect(rcViewTeacher, QColor(46, 48, 55));
	// 进行右侧窗口的预览窗口位置调整...
	rcViewTeacher.setTop(rcRightArea.top() + 1);
	// 注意：如果已经处于全屏状态，不能进行右侧讲师端窗口的位置和大小调整...
	if (!m_viewTeacher.isNull() && !m_viewTeacher->isFullScreen() &&
		rcViewTeacher != m_viewTeacher->geometry()) {
		m_viewTeacher->setGeometry(rcViewTeacher);
	}
	// 最后绘制整个右侧区域的边框位置...
	inPainter.setPen(QColor(27, 26, 28));
	inPainter.drawRect(rcRightArea);
}

void CStudentWindow::paintEvent(QPaintEvent *event)
{
	QPainter painter(this);

	// 单独绘制标题栏文字信息...
	this->doDrawTitle(painter);
	// 必须先绘制右侧预览窗口信息...
	this->doDrawRightArea(painter);
	// 然后再绘制左侧工具栏信息...
	this->doDrawLeftArea(painter);

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
