
#include "obs-app.hpp"
#include "platform.hpp"
#include "window-student.h"
#include "window-basic-settings.hpp"

#include <util/profiler.hpp>
#include "qt-wrappers.hpp"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

#ifdef _WIN32
#include "win-update/win-update.hpp"
#endif

#define SIMPLE_ENCODER_X264        "x264"
#define SIMPLE_ENCODER_X264_LOWCPU "x264_lowcpu"
#define SIMPLE_ENCODER_QSV         "qsv"
#define SIMPLE_ENCODER_NVENC       "nvenc"
#define SIMPLE_ENCODER_AMD         "amd"

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

	//InitOBSCallbacks();
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

	this->show();
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
	ovi.base_width = (uint32_t)config_get_uint(basicConfig, "Video", "BaseCX");
	ovi.base_height = (uint32_t)config_get_uint(basicConfig, "Video", "BaseCY");
	ovi.output_width = (uint32_t)config_get_uint(basicConfig, "Video", "OutputCX");
	ovi.output_height = (uint32_t)config_get_uint(basicConfig, "Video", "OutputCY");
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
		config_set_uint(basicConfig, "Video", "BaseCX", 1920);
		config_set_uint(basicConfig, "Video", "BaseCY", 1080);
	}

	if (ovi.output_width == 0 || ovi.output_height == 0) {
		ovi.output_width = ovi.base_width;
		ovi.output_height = ovi.base_height;
		config_set_uint(basicConfig, "Video", "OutputCX", ovi.base_width);
		config_set_uint(basicConfig, "Video", "OutputCY", ovi.base_height);
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

	config_set_default_uint(basicConfig, "Video", "BaseCX", cx);
	config_set_default_uint(basicConfig, "Video", "BaseCY", cy);

	/* don't allow BaseCX/BaseCY to be susceptible to defaults changing */
	if (!config_has_user_value(basicConfig, "Video", "BaseCX") ||
		!config_has_user_value(basicConfig, "Video", "BaseCY")) {
		config_set_uint(basicConfig, "Video", "BaseCX", cx);
		config_set_uint(basicConfig, "Video", "BaseCY", cy);
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

	config_set_default_uint(basicConfig, "Video", "OutputCX", scale_cx);
	config_set_default_uint(basicConfig, "Video", "OutputCY", scale_cy);

	/* don't allow OutputCX/OutputCY to be susceptible to defaults
	* changing */
	if (!config_has_user_value(basicConfig, "Video", "OutputCX") ||
		!config_has_user_value(basicConfig, "Video", "OutputCY")) {
		config_set_uint(basicConfig, "Video", "OutputCX", scale_cx);
		config_set_uint(basicConfig, "Video", "OutputCY", scale_cy);
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
	// 设置窗口背景透明 => 设置之后会造成全黑问题 => 顶层窗口需要用背景蒙板，普通窗口没问题...
	this->setAttribute(Qt::WA_TranslucentBackground);
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
	// 修改时钟对象字体和文字颜色...
	ui->labelClock->setFont(this->font());
	ui->labelClock->setStyleSheet("color:#FFFFFF;");
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
		this->setGeometry(m_rcSrcGeometry);
		this->showNormal();
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
	}
}

void CStudentWindow::doDrawTimeClock()
{
	int nHours = m_nTimeSecond / 3600;
	int nMinute = (m_nTimeSecond % 3600) / 60;
	int nSecond = (m_nTimeSecond % 3600) % 60;
	QString strTime = (nHours <= 0) ? QString("%1:%2").arg(nMinute, 2, 10, QChar('0')).arg(nSecond, 2, 10, QChar('0')) :
					  QString("%1:%2:%3").arg(nHours, 2, 10, QChar('0')).arg(nMinute, 2, 10, QChar('0')).arg(nSecond, 2, 10, QChar('0'));
	ui->labelClock->setText(QTStr("Main.Window.TimeClock").arg(strTime));
}

void CStudentWindow::doDrawTitle(QPainter & inPainter)
{
	// 对窗口标题进行修改 => 使用字典模式...
	QRect rcTitleRight = ui->hori_title->geometry();
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
	// 重新修改时钟标签对象的坐标位置 => 大小不变...
	QRect rcSrcClock = ui->labelClock->geometry();
	nPosX += m_QPixClock.width() + 10;
	nPosY = (rcTitleRight.height() - rcSrcClock.height()) / 2 + 1;
	QRect rcDstClock(nPosX, nPosY, rcSrcClock.width(), rcSrcClock.height());
	ui->labelClock->setGeometry(rcDstClock);
}

void CStudentWindow::paintEvent(QPaintEvent *event)
{
	QPainter painter(this);
	// 先绘制标题栏矩形区域...
	QRect rcTitleRight = ui->hori_title->geometry();
	painter.fillRect(rcTitleRight, QColor(47, 47, 53));
	painter.setPen(QColor(61, 63, 70));
	rcTitleRight.adjust(-1, 0, 0, 0);
	painter.drawRect(rcTitleRight);
	// 单独绘制标题栏文字信息...
	this->doDrawTitle(painter);
	// 再绘制左侧工具条矩形区域...
	QRect rcToolLeft = ui->vert_tool->geometry();
	painter.fillRect(rcToolLeft, QColor(67, 69, 85));
	// 在计算右侧整个空白区域...
	QRect rcRightArea = ui->vert_right->geometry();
	rcRightArea.adjust(0, rcTitleRight.height()+1, 0, 0);
	// 绘制右侧自身摄像头区域...
	QRect rcRightSelf = rcRightArea;
	rcRightSelf.setWidth(rcRightArea.width() / 5);
	painter.fillRect(rcRightSelf, QColor(40, 42, 49));
	// 绘制两条不同的分割竖线...
	painter.setPen(QColor(27, 26, 28));
	painter.drawLine(rcRightSelf.right() + 1, rcRightSelf.top() + 1, rcRightSelf.right() + 1, rcRightSelf.bottom());
	painter.setPen(QColor(63, 64, 70));
	painter.drawLine(rcRightSelf.right() + 2, rcRightSelf.top() + 1, rcRightSelf.right() + 2, rcRightSelf.bottom());
	// 绘制右侧老师画面区域...
	QRect rcTeacher = rcRightArea;
	rcTeacher.setLeft(rcRightSelf.right() + 3);
	painter.fillRect(rcTeacher, QColor(46, 48, 55));

	// 最后绘制整个右侧区域的边框位置...
	painter.setPen(QColor(27, 26, 28));
	painter.drawRect(rcRightArea);

	// 绘制用户的昵称 => 需要计算是否显示省略号...
	painter.setPen(QColor(235, 235, 235));
	int nHeadSize = m_QPixUserHead.width();
	int nPosY = 30 + nHeadSize + 10;
	QFontMetrics fontMetr(this->font());
	int nNameSize = rcToolLeft.width() - 8 * 2;
	int nFontSize = fontMetr.width(m_strUserNickName);
	int nPosX = (rcToolLeft.width() - nNameSize) / 2;
	if (nFontSize > nNameSize) {
		QTextOption txtOption(Qt::AlignLeft|Qt::AlignTop);
		txtOption.setWrapMode(QTextOption::WrapAnywhere);
		QRect txtRect(nPosX, nPosY, nNameSize, (fontMetr.height() + 2) * 4);
		painter.drawText(txtRect, m_strUserNickName, txtOption);
		//m_strUserNickName = fontMetr.elidedText(m_strUserNickName, Qt::ElideRight, nNameSize);
		//nNameSize = fontMetr.width(m_strUserNickName);
	} else {
		// 注意：单行文字还要增加Y轴高度 => 从下往上...
		nPosY += QFontInfo(this->font()).pixelSize();
		nPosX = (rcToolLeft.width() - nFontSize) / 2;
		painter.drawText(nPosX, nPosY, m_strUserNickName);
	}

	// 抗锯齿 + 平滑边缘处理
	painter.setRenderHints(QPainter::Antialiasing, true);
	painter.setRenderHints(QPainter::SmoothPixmapTransform, true);

	// 需要计算显示位置 = > 设置裁剪圆形区域...
	QPainterPath pathCircle;
	nPosX = (rcToolLeft.width() - nHeadSize) / 2;
	pathCircle.addEllipse(nPosX, 30, nHeadSize, nHeadSize);
	painter.setClipPath(pathCircle);
	// 绘制用户的头像 => 固定位置，自动裁剪...
	painter.drawPixmap(nPosX, 30, m_QPixUserHead);
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
