
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

// ����һ���������ţ����ⷢ���쳣...
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

	// ���D3D���������ز���...
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

	// �����Ҫ���Ե�ģ������ => ����APIģ��...
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
		// ����ѯ���Ƿ�װD3D����ʹ��OpenGL => �������趨Ϊ�գ������޷�������ʾ...
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

	// �������ģʽѡ�� => Ĭ��ѡ���� Advanced => ��Ҫ����¼�����ú�ѹ��������...
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

	// ������� Advanced ���ģʽ������ => ���� ��������� �� ¼�����...
	config_set_default_bool(basicConfig, "AdvOut", "ApplyServiceSettings", true);
	config_set_default_bool(basicConfig, "AdvOut", "UseRescale", false);
	config_set_default_uint(basicConfig, "AdvOut", "TrackIndex", 1); // Ĭ����������¼���ù��1(���������0)
	config_set_default_string(basicConfig, "AdvOut", "Encoder", "obs_x264");

	// Advanced ���ģʽ�������¼����������� => Standard|FFmpeg => ����Standardģʽ...
	config_set_default_string(basicConfig, "AdvOut", "RecType", "Standard");

	// Standard��׼¼��ģʽ�Ĳ������� => ��Ƶ����֧�ֶ���¼�� => Ŀǰʹ����Щ����...
	config_set_default_string(basicConfig, "AdvOut", "RecFilePath", GetDefaultVideoSavePath().c_str());
	config_set_default_string(basicConfig, "AdvOut", "RecFormat", "mp4");
	config_set_default_bool(basicConfig, "AdvOut", "RecUseRescale", false);
	config_set_default_bool(basicConfig, "AdvOut", "RecFileNameWithoutSpace", true); //��׼¼���ļ����������ո�
	config_set_default_uint(basicConfig, "AdvOut", "RecTracks", (2 << 0)); //��׼¼���ù��2(���������1) => ֧�ֶ�����¼��
	config_set_default_string(basicConfig, "AdvOut", "RecEncoder", "none");

	// FFmpeg�Զ���¼��ģʽ�Ĳ������� => ��Ƶֻ֧��1�����¼�� => Ŀǰû��ʹ��...
	config_set_default_bool(basicConfig, "AdvOut", "FFOutputToFile", true);
	config_set_default_string(basicConfig, "AdvOut", "FFFilePath", GetDefaultVideoSavePath().c_str());
	config_set_default_string(basicConfig, "AdvOut", "FFExtension", "mp4");
	config_set_default_uint(basicConfig, "AdvOut", "FFVBitrate", 1024); //2500);
	config_set_default_uint(basicConfig, "AdvOut", "FFVGOPSize", 150);  //250);
	config_set_default_bool(basicConfig, "AdvOut", "FFUseRescale", false);
	config_set_default_bool(basicConfig, "AdvOut", "FFIgnoreCompat", false);
	config_set_default_uint(basicConfig, "AdvOut", "FFABitrate", 64); //160);
	config_set_default_uint(basicConfig, "AdvOut", "FFAudioMixes", 1);   //FFmpeg¼���ù��1 => ������¼��
	//config_set_default_uint(basicConfig, "AdvOut", "FFAudioTrack", 1); //FFmpeg¼���ù��1 => ������¼��

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
	// FramelessWindowHint�������ô���ȥ���߿�;
	// WindowMinimizeButtonHint ���������ڴ�����С��ʱ��������������ڿ�����ʾ��ԭ����;
	//Qt::WindowFlags flag = this->windowFlags();
	this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
	// ���ô��ڱ���͸�� => ����֮������ȫ������ => ���㴰����Ҫ�ñ����ɰ壬��ͨ����û����...
	this->setAttribute(Qt::WA_TranslucentBackground);
	// �رմ���ʱ�ͷ���Դ;
	this->setAttribute(Qt::WA_DeleteOnClose);
	// ���ô���ͼ�� => ������pngͼƬ => �����Щ��������ico��������Ͻ�ͼ���޷���ʾ...
	this->setWindowIcon(QIcon(":/res/images/obs.png"));
	// �жϵ�ǰ������ķֱ����Ƿ񳬹�1280x720�����û�г�������Ĭ�ϵ�...
	QScreen * lpCurScreen = QGuiApplication::primaryScreen();
	QRect rcScreen = lpCurScreen->geometry();
	// �����趨���ڴ�С => ������ֱ��ʳ���1280x720��ʱ��...
	if (rcScreen.width() >= 1280 && rcScreen.height() >= 720) {
		this->resize(1280, 720);
	}
	// ���¼��㴰����ʾλ��...
	QRect rcRect = this->geometry();
	int nLeftPos = (rcScreen.width() - rcRect.width()) / 2;
	int nTopPos = (rcScreen.height() - rcRect.height()) / 2;
	this->setGeometry(nLeftPos, nTopPos, rcRect.width(), rcRect.height());
	// ������º�Ĵ��ڵ������꣬�Ա����֮��Ļָ�ʹ��...
	m_rcSrcGeometry = this->geometry();
	// ���������С����ť|���|�رհ�ť���źŲ��¼�...
	connect(ui->btnMin, SIGNAL(clicked()), this, SLOT(onButtonMinClicked()));
	connect(ui->btnMax, SIGNAL(clicked()), this, SLOT(onButtonMaxClicked()));
	connect(ui->btnClose, SIGNAL(clicked()), this, SLOT(onButtonCloseClicked()));
	// �������������������ť���źŲ��¼�...
	connect(ui->btnAbout, SIGNAL(clicked()), this, SLOT(onButtonAboutClicked()));
	connect(ui->btnUpdate, SIGNAL(clicked()), this, SLOT(onButtonUpdateClicked()));
	connect(ui->btnCamera, SIGNAL(clicked()), this, SLOT(onButtonCameraClicked()));
	connect(ui->btnSystem, SIGNAL(clicked()), this, SLOT(onButtonSystemClicked()));
	// ���������źŲ۷�������¼�...
	connect(&m_objNetManager, SIGNAL(finished(QNetworkReply *)), this, SLOT(onReplyFinished(QNetworkReply *)));
	// ���ش��ڵĲ����ʾ��ʽ��...
	this->loadStyleSheet(":/student/css/Student.css");
	// ��ȡ������Ĭ�ϵ�ͷ����� => ��Ҫ���б�Ҫ�����Ų���...
	m_QPixClock = QPixmap(":/res/images/clock.png");
	m_QPixUserHead = QPixmap(":/res/images/avatar.png").scaled(50, 50);
	m_strUserNickName = QString("%1").arg(App()->GetUserNickName().c_str());
	m_strUserHeadUrl = QString("%1").arg(App()->GetUserHeadUrl().c_str());
	// ����һ����ʱ��������ʱ�� => ÿ��1�����һ��...
	m_nClassTimer = this->startTimer(1 * 1000);
	// �޸�ʱ�Ӷ��������������ɫ...
	ui->labelClock->setFont(this->font());
	ui->labelClock->setStyleSheet("color:#FFFFFF;");
	// ���Ƴ�ʼ��ʱ������...
	this->doDrawTimeClock();
	// �����ȡ��¼�û�ͷ������...
	this->doWebGetUserHead();
}

void CStudentWindow::doWebGetUserHead()
{
	m_eNetState = kWebGetUserHead;
	// ����ƾ֤���ʵ�ַ��������������...
	QNetworkReply * lpNetReply = NULL;
	QNetworkRequest theQTNetRequest;
	QString strRequestURL = m_strUserHeadUrl;
	theQTNetRequest.setUrl(QUrl(strRequestURL));
	lpNetReply = m_objNetManager.get(theQTNetRequest);
}

void CStudentWindow::onReplyFinished(QNetworkReply *reply)
{
	// �������������󣬴�ӡ������Ϣ������ѭ��...
	if (reply->error() != QNetworkReply::NoError) {
		blog(LOG_INFO, "QT error => %d, %s", reply->error(), reply->errorString().toStdString().c_str());
		return;
	}
	// ��ȡ�������������󷵻ص��������ݰ�...
	int nStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	blog(LOG_INFO, "QT Status Code => %d", nStatusCode);
	// ����״̬�ַ��������...
	switch (m_eNetState) {
	case kWebGetUserHead: this->onProcGetUserHead(reply); break;
	}
}

void CStudentWindow::onProcGetUserHead(QNetworkReply *reply)
{
	// ��ȡ��ȡ����������������...
	QPixmap theUserHead;
	ASSERT(m_eNetState == kWebGetUserHead);
	QByteArray & theByteArray = reply->readAll();
	// ����·����ֱ�ӹ����û�ͷ�����...
	if (!theUserHead.loadFromData(theByteArray))
		return;
	// �����û�ͷ�����...
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
	// ����Ѿ������ => �ָ���ʼ����...
	if (this->isMaximized()) {
		ui->btnMax->setStyleSheet(QString("%1%2%3").arg("QPushButton{border-image:url(:/student/images/student/maxing.png) 0 40 0 0;}")
			.arg("QPushButton:hover{border-image:url(:/student/images/student/maxing.png) 0 20 0 20;}")
			.arg("QPushButton:pressed{border-image:url(:/student/images/student/maxing.png) 0 0 0 40;}"));
		ui->btnMax->setToolTip(QTStr("Student.Tips.Maxing"));
		this->setGeometry(m_rcSrcGeometry);
		this->showNormal();
		return;
	}
	// ���������� => �����ʾ...
	this->showMaximized();
	// ������󻯰�ť��ͼ����ʽ����ʾ��Ϣ...
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
	// �Դ��ڱ�������޸� => ʹ���ֵ�ģʽ...
	QRect rcTitleRight = ui->hori_title->geometry();
	QString strTitle = QTStr("Main.Window.TitleContent")
		.arg(App()->GetClientTypeName())
		.arg(App()->GetRoomIDStr().c_str());
	// ���û�����ɫ�����������ʾλ�ã���Ҫ����ƫ��...
	inPainter.setPen(QColor(255, 255, 255));
	QFontMetrics fontMetr(this->font());
	int nTitlePixSize = fontMetr.width(strTitle);
	int nPixelSize = QFontInfo(this->font()).pixelSize();
	int nPosX = (rcTitleRight.width() - nTitlePixSize) / 2 + rcTitleRight.left();
	int nPosY = (rcTitleRight.height() - nPixelSize) / 2 + nPixelSize;
	inPainter.drawText(nPosX, nPosY, strTitle);
	// ����ʱ��ͼ�� => ֱ�ӻ���ͼ���ļ�...
	nPosX += nTitlePixSize + 10;
	nPosY = (rcTitleRight.height() - m_QPixClock.height()) / 2;
	inPainter.drawPixmap(nPosX, nPosY, m_QPixClock);
	// �����޸�ʱ�ӱ�ǩ���������λ�� => ��С����...
	QRect rcSrcClock = ui->labelClock->geometry();
	nPosX += m_QPixClock.width() + 10;
	nPosY = (rcTitleRight.height() - rcSrcClock.height()) / 2 + 1;
	QRect rcDstClock(nPosX, nPosY, rcSrcClock.width(), rcSrcClock.height());
	ui->labelClock->setGeometry(rcDstClock);
}

void CStudentWindow::paintEvent(QPaintEvent *event)
{
	QPainter painter(this);
	// �Ȼ��Ʊ�������������...
	QRect rcTitleRight = ui->hori_title->geometry();
	painter.fillRect(rcTitleRight, QColor(47, 47, 53));
	painter.setPen(QColor(61, 63, 70));
	rcTitleRight.adjust(-1, 0, 0, 0);
	painter.drawRect(rcTitleRight);
	// �������Ʊ�����������Ϣ...
	this->doDrawTitle(painter);
	// �ٻ�����๤������������...
	QRect rcToolLeft = ui->vert_tool->geometry();
	painter.fillRect(rcToolLeft, QColor(67, 69, 85));
	// �ڼ����Ҳ������հ�����...
	QRect rcRightArea = ui->vert_right->geometry();
	rcRightArea.adjust(0, rcTitleRight.height()+1, 0, 0);
	// �����Ҳ���������ͷ����...
	QRect rcRightSelf = rcRightArea;
	rcRightSelf.setWidth(rcRightArea.width() / 5);
	painter.fillRect(rcRightSelf, QColor(40, 42, 49));
	// ����������ͬ�ķָ�����...
	painter.setPen(QColor(27, 26, 28));
	painter.drawLine(rcRightSelf.right() + 1, rcRightSelf.top() + 1, rcRightSelf.right() + 1, rcRightSelf.bottom());
	painter.setPen(QColor(63, 64, 70));
	painter.drawLine(rcRightSelf.right() + 2, rcRightSelf.top() + 1, rcRightSelf.right() + 2, rcRightSelf.bottom());
	// �����Ҳ���ʦ��������...
	QRect rcTeacher = rcRightArea;
	rcTeacher.setLeft(rcRightSelf.right() + 3);
	painter.fillRect(rcTeacher, QColor(46, 48, 55));

	// �����������Ҳ�����ı߿�λ��...
	painter.setPen(QColor(27, 26, 28));
	painter.drawRect(rcRightArea);

	// �����û����ǳ� => ��Ҫ�����Ƿ���ʾʡ�Ժ�...
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
		// ע�⣺�������ֻ�Ҫ����Y��߶� => ��������...
		nPosY += QFontInfo(this->font()).pixelSize();
		nPosX = (rcToolLeft.width() - nFontSize) / 2;
		painter.drawText(nPosX, nPosY, m_strUserNickName);
	}

	// ����� + ƽ����Ե����
	painter.setRenderHints(QPainter::Antialiasing, true);
	painter.setRenderHints(QPainter::SmoothPixmapTransform, true);

	// ��Ҫ������ʾλ�� = > ���òü�Բ������...
	QPainterPath pathCircle;
	nPosX = (rcToolLeft.width() - nHeadSize) / 2;
	pathCircle.addEllipse(nPosX, 30, nHeadSize, nHeadSize);
	painter.setClipPath(pathCircle);
	// �����û���ͷ�� => �̶�λ�ã��Զ��ü�...
	painter.drawPixmap(nPosX, 30, m_QPixUserHead);
	QWidget::paintEvent(event);
}

// ����ͨ�� mousePressEvent | mouseMoveEvent | mouseReleaseEvent �����¼�ʵ��������϶��������ƶ����ڵ�Ч��;
void CStudentWindow::mousePressEvent(QMouseEvent *event)
{
	// ������������Ҳ������״̬�� => ���ܽ����Ϸ��ƶ�����...
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
	// �ж������������λ��Խ�磬����λ��...
	QRect rcRect = this->geometry();
	int nWidth = rcRect.width();
	int nHeight = rcRect.height();
	// ������Խ�� => ��Ȳ���...
	if (rcRect.left() < 0) {
		rcRect.setLeft(0);
		rcRect.setWidth(nWidth);
		this->setGeometry(rcRect);
	}
	// �������Խ�� => �߶Ȳ���...
	if (rcRect.top() < 0) {
		rcRect.setTop(0);
		rcRect.setHeight(nHeight);
		this->setGeometry(rcRect);
	}
}
