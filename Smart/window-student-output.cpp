
#include "window-student-output.hpp"
#include "audio-encoders.hpp"
#include "window-student.h"
#include "qt-wrappers.hpp"
#include "obs-app.hpp"

#define STUDENT_JSON    "studentEncoder.json"

static OBSData GetDataFromJsonFile(const char *jsonFile)
{
	char fullPath[512] = { 0 };
	obs_data_t *data = nullptr;

	int ret = GetProfilePath(fullPath, sizeof(fullPath), jsonFile);
	if (ret > 0) {
		BPtr<char> jsonData = os_quick_read_utf8_file(fullPath);
		if (!!jsonData) {
			data = obs_data_create_from_json(jsonData);
		}
	}

	if (!data) {
		data = obs_data_create();
	}
	OBSData dataRet(data);
	obs_data_release(data);
	return dataRet;
}

static void ApplyEncoderDefaults(OBSData &settings, const obs_encoder_t *encoder)
{
	OBSData dataRet = obs_encoder_get_defaults(encoder);
	obs_data_release(dataRet);

	if (!!settings) {
		obs_data_apply(dataRet, settings);
	}
	settings = std::move(dataRet);
}

static void WriteJsonFromData(obs_data_t * settings)
{
	char full_path[512] = { 0 };
	int ret = GetProfilePath(full_path, sizeof(full_path), STUDENT_JSON);
	if (ret > 0 && settings != NULL) {
		obs_data_save_json_safe(settings, full_path, "tmp", "bak");
	}
}

static bool CreateAACEncoder(OBSEncoder &res, string &id, int bitrate, const char *name, size_t idx)
{
	const char *id_ = GetAACEncoderForBitrate(bitrate);
	if (!id_) {
		id.clear();
		res = nullptr;
		return false;
	}

	if (id == id_)
		return true;

	id = id_;
	res = obs_audio_encoder_create(id_, name, nullptr, idx, nullptr);

	if (res) {
		obs_encoder_release(res);
		return true;
	}

	return false;
}

static void ensure_directory_exists(string &path)
{
	replace(path.begin(), path.end(), '\\', '/');

	size_t last = path.rfind('/');
	if (last == string::npos)
		return;

	string directory = path.substr(0, last);
	os_mkdirs(directory.c_str());
}

static void FindBestFilename(string &strPath, bool noSpace)
{
	int num = 2;

	if (!os_file_exists(strPath.c_str()))
		return;

	const char *ext = strrchr(strPath.c_str(), '.');
	if (!ext)
		return;

	int extStart = int(ext - strPath.c_str());
	for (;;) {
		string testPath = strPath;
		string numStr;

		numStr = noSpace ? "_" : " (";
		numStr += to_string(num++);
		if (!noSpace)
			numStr += ")";

		testPath.insert(extStart, numStr);

		if (!os_file_exists(testPath.c_str())) {
			strPath = testPath;
			break;
		}
	}
}

static void OBSStartRecording(void *data, calldata_t *params)
{
	CStudentOutput *output = static_cast<CStudentOutput *>(data);
	output->recordingActive = true;

	blog(LOG_INFO, "OBSStartRecording => recordingActive");
	//QMetaObject::invokeMethod(output->m_lpStudentMain, "RecordingStart");

	UNUSED_PARAMETER(params);
}

static void OBSStopRecording(void *data, calldata_t *params)
{
	CStudentOutput *output = static_cast<CStudentOutput *>(data);
	int code = (int)calldata_int(params, "code");
	const char *last_error = calldata_string(params, "last_error");
	QString arg_last_error = QString::fromUtf8(last_error);
	output->recordingActive = false;

	blog(LOG_INFO, "OBSStopRecording => code: %d, error: %s", code, ((last_error == nullptr) ? "" : last_error));
	
	//QMetaObject::invokeMethod(output->m_lpStudentMain, "RecordingStop",
	//		Q_ARG(int, code), Q_ARG(QString, arg_last_error));

	UNUSED_PARAMETER(params);
}

/*static void OBSRecordStopping(void *data, calldata_t *params)
{
	//CStudentOutput *output = static_cast<CStudentOutput *>(data);
	//QMetaObject::invokeMethod(output->m_lpStudentMain, "RecordStopping");

	UNUSED_PARAMETER(params);
}*/

/*static void OBSStreamStarting(void *data, calldata_t *params)
{
	CStudentOutput *output = static_cast<CStudentOutput *>(data);
	obs_output_t *obj = (obs_output_t *)calldata_ptr(params, "output");
	int sec = (int)obs_output_get_active_delay(obj);
	if (sec == 0)
		return;
	//output->delayActive = true;
	//QMetaObject::invokeMethod(output->m_lpStudentMain, "StreamDelayStarting", Q_ARG(int, sec));
}

static void OBSStreamStopping(void *data, calldata_t *params)
{
	CStudentOutput *output = static_cast<CStudentOutput *>(data);
	obs_output_t *obj = (obs_output_t *)calldata_ptr(params, "output");

	int sec = (int)obs_output_get_active_delay(obj);
	//if (sec == 0) QMetaObject::invokeMethod(output->m_lpStudentMain, "StreamStopping");
	//else QMetaObject::invokeMethod(output->m_lpStudentMain, "StreamDelayStopping",	Q_ARG(int, sec));
}*/

static void OBSStartStreaming(void *data, calldata_t *params)
{
	CStudentOutput *output = static_cast<CStudentOutput *>(data);
	output->streamingActive = true;

	blog(LOG_INFO, "OBSStartStreaming => streamingActive");
	//QMetaObject::invokeMethod(output->m_lpStudentMain, "StreamingStart");

	UNUSED_PARAMETER(params);
}

static void OBSStopStreaming(void *data, calldata_t *params)
{
	CStudentOutput *output = static_cast<CStudentOutput *>(data);
	int code = (int)calldata_int(params, "code");
	const char *last_error = calldata_string(params, "last_error");
	QString arg_last_error = QString::fromUtf8(last_error);
	output->streamingActive = false;

	blog(LOG_INFO, "OBSStopStreaming => code: %d, error: %s", code, ((last_error == nullptr) ? "" : last_error));

	//QMetaObject::invokeMethod(output->main, "StreamingStop",
	//	Q_ARG(int, code), Q_ARG(QString, arg_last_error));
}

int CStudentOutput::GetAudioBitrate(size_t i) const
{
	static const char *names[] = {
		"Track1Bitrate", "Track2Bitrate", "Track3Bitrate",
		"Track4Bitrate", "Track5Bitrate", "Track6Bitrate",
	};
	int bitrate = (int)config_get_uint(m_lpStudentMain->Config(), "AdvOut", names[i]);
	return FindClosestAvailableAACBitrate(bitrate);
}

CStudentOutput::CStudentOutput(CStudentWindow * main_)
  : m_lpStudentMain(main_)
{
	const char *recType = config_get_string(m_lpStudentMain->Config(), "AdvOut", "RecType");
	const char *streamEncoder = config_get_string(m_lpStudentMain->Config(), "AdvOut", "Encoder");
	const char *recordEncoder = config_get_string(m_lpStudentMain->Config(), "AdvOut", "RecEncoder");
	// 录像压缩器是否使用跟网络流一样的压缩器 => 默认使用相同压缩器...
	useStreamEncoder = astrcmpi(recordEncoder, "none") == 0;
	// 这里使用学生端自己特定的压缩器配置 => 录像和推流使用相同压缩配置...
	OBSData streamEncSettings = GetDataFromJsonFile(STUDENT_JSON);
	// 如果.json文件不存在，创建新的文件，注意释放引用计数...
	if (streamEncSettings == NULL) {
		obs_data_t * lpNewData = obs_data_create();
		streamEncSettings = lpNewData;
		obs_data_release(lpNewData);
	}
	// 从配置当中获取压缩码率、压缩方式、缓存模式...
	bool bEncSetChanged = false;
	int nVBitrate = obs_data_get_int(streamEncSettings, "bitrate");
	const char * lpTune = obs_data_get_string(streamEncSettings, "tune");
	const char * lpProfile = obs_data_get_string(streamEncSettings, "profile");
	// 压缩方式为空，设置默认压缩模式...
	if (lpProfile == NULL || strlen(lpProfile) <= 0) {
		obs_data_set_string(streamEncSettings, "profile", "baseline");
		bEncSetChanged = true;
	}
	// 缓存模式为空，设置默认缓存模式...
	if (lpTune == NULL || strlen(lpTune) <= 0) {
		obs_data_set_string(streamEncSettings, "tune", "zerolatency");
		bEncSetChanged = true;
	}
	// 压缩码率为空，设置模式压缩码率...
	if (nVBitrate <= 0) {
		obs_data_set_int(streamEncSettings, "bitrate", 512);
		bEncSetChanged = true;
	}
	// 关键帧间隔为空，设置默认关键帧间隔 => 通过配置页面，手动配置...
	/*int nKeyIntSec = obs_data_get_int(streamEncSettings, "keyint_sec");
	if (nKeyIntSec <= 0) {
		obs_data_set_int(streamEncSettings, "keyint_sec", 2);
		bEncSetChanged = true;
	}*/
	// 配置放生变化，进行存盘操作...
	if (bEncSetChanged) {
		WriteJsonFromData(streamEncSettings);
	}
	// 读取rate_control配置信息...
	const char *rate_control = obs_data_get_string(streamEncSettings, "rate_control");
	if (!rate_control) rate_control = "";
	usesBitrate = astrcmpi(rate_control, "CBR") == 0 ||
		astrcmpi(rate_control, "VBR") == 0 ||
		astrcmpi(rate_control, "ABR") == 0;
	// 创建文件输出模块接口对象...
	fileOutput = obs_output_create("ffmpeg_muxer", "adv_file_output", nullptr, nullptr);
	if (!fileOutput) {
		throw "Failed to create recording output (advanced output)";
	}
	obs_output_release(fileOutput);
	// 创建h264压缩输出接口对象...
	h264Streaming = obs_video_encoder_create(streamEncoder, "streaming_h264", streamEncSettings, nullptr);
	if (!h264Streaming) {
		throw "Failed to create streaming h264 encoder (advanced output)";
	}
	obs_encoder_release(h264Streaming);
	// 注意：之前版本没有主动创建音频流压缩对象，是从 aacTrack 当中赋值...
	int streamTrack = config_get_int(m_lpStudentMain->Config(), "AdvOut", "TrackIndex") - 1;
	if (!CreateAACEncoder(streamAudioEnc, strAudioEncID, GetAudioBitrate(streamTrack), "avc_aac_stream", streamTrack)) {
		throw "Failed to create streaming audio encoder (advanced output)";
	}
}

CStudentOutput::~CStudentOutput()
{
	this->StopStreaming();
	this->StopRecording();
}

bool CStudentOutput::StreamingActive() const
{
	return obs_output_active(streamOutput);
}

bool CStudentOutput::RecordingActive() const
{
	return obs_output_active(fileOutput);
}

bool CStudentOutput::Active() const
{
	return (streamingActive || recordingActive);
}

bool CStudentOutput::StartStreaming()
{
	if (!obs_output_active(fileOutput)) {
		this->UpdateStreamSettings();
	}
	this->UpdateAudioSettings();
	if (!this->Active()) {
		this->SetupOutputs();
	}
	// 获得当前配置的声音轨道编号 => 始终默认选择流输出轨道1...
	int streamTrack = config_get_int(m_lpStudentMain->Config(), "AdvOut", "TrackIndex") - 1;
	// 注意：这里直接指定使用smart输出流...
	const char * type = "smart_output";
	startStreaming.Disconnect();
	stopStreaming.Disconnect();
	streamOutput = obs_output_create(type, "adv_stream", nullptr, nullptr);
	if (!streamOutput) {
		blog(LOG_WARNING, "Creation of stream output type '%s' failed!", type);
		return false;
	}
	obs_output_release(streamOutput);
	startStreaming.Connect(obs_output_get_signal_handler(streamOutput), "starting", OBSStartStreaming, this);
	stopStreaming.Connect(obs_output_get_signal_handler(streamOutput), "stopping", OBSStopStreaming, this);

	bool isEncoded = obs_output_get_flags(streamOutput) & OBS_OUTPUT_ENCODED;
	const char *codec = obs_output_get_supported_audio_codecs(streamOutput);
	if (isEncoded && codec == nullptr) {
		blog(LOG_WARNING, "Failed to load audio codec");
		return false;
	}
	obs_output_set_video_encoder(streamOutput, h264Streaming);
	obs_output_set_audio_encoder(streamOutput, streamAudioEnc, 0);

	bool reconnect = config_get_bool(m_lpStudentMain->Config(), "Output", "Reconnect");
	int retryDelay = config_get_int(m_lpStudentMain->Config(), "Output", "RetryDelay");
	int maxRetries = config_get_int(m_lpStudentMain->Config(), "Output", "MaxRetries");
	bool useDelay = config_get_bool(m_lpStudentMain->Config(), "Output", "DelayEnable");
	int delaySec = config_get_int(m_lpStudentMain->Config(), "Output", "DelaySec");
	bool preserveDelay = config_get_bool(m_lpStudentMain->Config(), "Output", "DelayPreserve");
	const char *bindIP = config_get_string(m_lpStudentMain->Config(), "Output", "BindIP");
	bool enableNewSocketLoop = config_get_bool(m_lpStudentMain->Config(), "Output", "NewSocketLoopEnable");
	bool enableLowLatencyMode = config_get_bool(m_lpStudentMain->Config(), "Output", "LowLatencyEnable");
	bool enableDynBitrate = config_get_bool(m_lpStudentMain->Config(), "Output", "DynamicBitrate");

	// 创建输出配置结构体对象...
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "bind_ip", bindIP);
	obs_data_set_bool(settings, "new_socket_loop_enabled", enableNewSocketLoop);
	obs_data_set_bool(settings, "low_latency_mode_enabled", enableLowLatencyMode);
	obs_data_set_bool(settings, "dyn_bitrate", enableDynBitrate);
	// 设置smart推流输出需要的特殊变量 => room_id | udp_addr | udp_port | tcp_socket | client_type
	int nRoomID = atoi(App()->GetRoomIDStr().c_str());
	obs_data_set_int(settings, "room_id", nRoomID);
	obs_data_set_int(settings, "udp_port", App()->GetUdpPort());
	obs_data_set_string(settings, "udp_addr", App()->GetUdpAddr().c_str());
	obs_data_set_int(settings, "tcp_socket", App()->GetRemoteTcpSockFD());
	obs_data_set_int(settings, "client_type", App()->GetClientType());
	// rtp模式下阻止重连，重连次数修改为0...
	reconnect = false; maxRetries = 0;
	// 将新的输出配置应用到当前输出对象当中...
	obs_output_update(streamOutput, settings);
	// 释放输出配置结构体对象...
	obs_data_release(settings);

	obs_output_set_delay(streamOutput, useDelay ? delaySec : 0,
		preserveDelay ? OBS_OUTPUT_DELAY_PRESERVE : 0);

	obs_output_set_reconnect_settings(streamOutput, maxRetries, retryDelay);

	if (obs_output_start(streamOutput))
		return true;

	const char *error = obs_output_get_last_error(streamOutput);
	bool hasLastError = error && *error;

	blog(LOG_WARNING, "Stream output type '%s' failed to start!%s%s", type,
		hasLastError ? "  Last Error: " : "", hasLastError ? error : "");
	return false;
}

void CStudentOutput::StopStreaming()
{
	//obs_output_force_stop(streamOutput);
	obs_output_stop(streamOutput);
}

void CStudentOutput::StopRecording()
{
	//obs_output_force_stop(fileOutput);
	obs_output_stop(fileOutput);
}

bool CStudentOutput::StartRecording()
{
	const char *path;
	const char *recFormat;
	const char *filenameFormat;
	bool noSpace = false;
	bool overwriteIfExists = false;
	if (!obs_output_active(streamOutput)) {
		this->UpdateStreamSettings();
	}
	this->UpdateAudioSettings();
	if (!this->Active()) {
		this->SetupOutputs();
	}

	startRecording.Connect(obs_output_get_signal_handler(fileOutput), "start", OBSStartRecording, this);
	stopRecording.Connect(obs_output_get_signal_handler(fileOutput), "stop", OBSStopRecording, this);

	path = config_get_string(m_lpStudentMain->Config(), "AdvOut", "RecFilePath");
	recFormat = config_get_string(m_lpStudentMain->Config(), "AdvOut", "RecFormat");
	filenameFormat = config_get_string(m_lpStudentMain->Config(), "Output",	"FilenameFormatting");
	overwriteIfExists = config_get_bool(m_lpStudentMain->Config(), "Output", "OverwriteIfExists");
	noSpace = config_get_bool(m_lpStudentMain->Config(), "AdvOut", "RecFileNameWithoutSpace");

	os_dir_t *dir = path && path[0] ? os_opendir(path) : nullptr;

	if (!dir) {	return false; }

	os_closedir(dir);

	string strPath;
	strPath += path;

	char lastChar = strPath.back();
	if (lastChar != '/' && lastChar != '\\')
		strPath += "/";

	bool autoRemux = config_get_bool(m_lpStudentMain->Config(), "Video", "AutoRemux");
	strPath += GenerateSpecifiedFilename(autoRemux, recFormat, noSpace, filenameFormat);
	ensure_directory_exists(strPath);
	if (!overwriteIfExists) {
		FindBestFilename(strPath, noSpace);
	}
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "path", strPath.c_str());
	obs_output_update(fileOutput, settings);
	obs_data_release(settings);
	if (!obs_output_start(fileOutput)) {
		QString error_reason;
		const char *error = obs_output_get_last_error(fileOutput);
		error_reason = error ? QT_UTF8(error) : QTStr("Output.StartFailedGeneric");
		blog(LOG_INFO, "Error: %s", error_reason.toStdString().c_str());
		return false;
	}
	return true;
}

void CStudentOutput::UpdateStreamSettings()
{
	OBSData settings = GetDataFromJsonFile(STUDENT_JSON);
	ApplyEncoderDefaults(settings, h264Streaming);

	video_t *video = obs_get_video();
	enum video_format format = video_output_get_format(video);

	if (format != VIDEO_FORMAT_NV12 && format != VIDEO_FORMAT_I420) {
		obs_encoder_set_preferred_video_format(h264Streaming, VIDEO_FORMAT_NV12);
	}
	obs_encoder_update(h264Streaming, settings);
}

void CStudentOutput::UpdateAudioSettings()
{
	int streamTrackIndex = config_get_int(m_lpStudentMain->Config(), "AdvOut", "TrackIndex");
	obs_data_t *settings = obs_data_create();
	int nABitRate = this->GetAudioBitrate(streamTrackIndex - 1);
	obs_data_set_int(settings, "bitrate", nABitRate);
	obs_encoder_update(streamAudioEnc, settings);
	obs_data_release(settings);
}

void CStudentOutput::SetupOutputs()
{
	obs_encoder_set_video(h264Streaming, obs_get_video());
	obs_encoder_set_audio(streamAudioEnc, obs_get_audio());
	this->SetupStreaming();
	this->SetupRecording();
}

void CStudentOutput::SetupStreaming()
{
	bool rescale = config_get_bool(m_lpStudentMain->Config(), "AdvOut", "Rescale");
	const char *rescaleRes = config_get_string(m_lpStudentMain->Config(), "AdvOut", "RescaleRes");
	int streamTrack = config_get_int(m_lpStudentMain->Config(), "AdvOut", "TrackIndex") - 1;
	uint32_t caps = obs_encoder_get_caps(h264Streaming);
	unsigned int cx = 0;
	unsigned int cy = 0;

	if ((caps & OBS_ENCODER_CAP_PASS_TEXTURE) != 0) {
		rescale = false;
	}

	if (rescale && rescaleRes && *rescaleRes) {
		if (sscanf(rescaleRes, "%ux%u", &cx, &cy) != 2) {
			cx = 0; cy = 0;
		}
	}

	// 这一行可以屏蔽掉，因为streamOutput还没有被创建，后面创建后又追加了调用...
	obs_output_set_audio_encoder(streamOutput, streamAudioEnc, streamTrack);
	obs_encoder_set_scaled_size(h264Streaming, cx, cy);
	obs_encoder_set_video(h264Streaming, obs_get_video());
}

void CStudentOutput::SetupRecording()
{
	const char *path = config_get_string(m_lpStudentMain->Config(), "AdvOut", "RecFilePath");
	const char *mux = config_get_string(m_lpStudentMain->Config(), "AdvOut", "RecMuxerCustom");
	bool rescale = config_get_bool(m_lpStudentMain->Config(), "AdvOut", "RecRescale");
	const char *rescaleRes = config_get_string(m_lpStudentMain->Config(), "AdvOut", "RecRescaleRes");
	int tracks = config_get_int(m_lpStudentMain->Config(), "AdvOut", "RecTracks");
	obs_data_t *settings = obs_data_create();
	unsigned int cx = 0;
	unsigned int cy = 0;
	int idx = 0;

	tracks = config_get_int(m_lpStudentMain->Config(), "AdvOut", "TrackIndex");

	obs_output_set_video_encoder(fileOutput, h264Streaming);
	obs_output_set_audio_encoder(fileOutput, streamAudioEnc, idx);

	obs_data_set_string(settings, "path", path);
	obs_data_set_string(settings, "muxer_settings", mux);
	obs_output_update(fileOutput, settings);
	obs_data_release(settings);
}
