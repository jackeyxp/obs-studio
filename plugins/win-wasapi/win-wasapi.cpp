#include "enum-wasapi.hpp"

#include <obs-module.h>
#include <obs.h>
#include <util/platform.h>
#include <util/windows/HRError.hpp>
#include <util/windows/ComPtr.hpp>
#include <util/windows/WinHandle.hpp>
#include <util/windows/CoTaskMemPtr.hpp>
#include <util/threading.h>
#include <util/circlebuf.h>
#include <util/dstr.h>

#include "media-io/audio-resampler.h"
#include "media-io/audio-io.h"
#include "echo_cancellation.h"
#include "noise_suppression_x.h"
#include "aec_core.h"
using namespace webrtc;
using namespace std;

#define DEF_WEBRTC_AEC_NN     160         // 默认每次处理样本个数
#define DEF_OUT_SAMPLE_RATE   16000       // 默认输出采样率
#define DEF_OUT_CHANNEL_NUM   1           // 默认输出声道数
#define OPT_DEVICE_ID "device_id"
#define OPT_USE_DEVICE_TIMING "use_device_timing"

#define KSAUDIO_SPEAKER_2POINT1 (KSAUDIO_SPEAKER_STEREO | SPEAKER_LOW_FREQUENCY)
#define OBS_KSAUDIO_SPEAKER_4POINT1 (KSAUDIO_SPEAKER_SURROUND | SPEAKER_LOW_FREQUENCY)

class WASAPISource {
	ComPtr<IMMDevice> device;
	ComPtr<IAudioClient> client;
	ComPtr<IAudioCaptureClient> capture;
	ComPtr<IAudioRenderClient> render;

	obs_source_t *source;
	string device_id;
	string device_name;
	bool isInputDevice;
	bool useDeviceTiming = false;
	bool isDefaultDevice = false;

	bool reconnecting = false;
	bool previouslyFailed = false;
	WinHandle reconnectThread;

	bool active = false;
	WinHandle captureThread;

	WinHandle stopSignal;
	WinHandle receiveSignal;

	int                         m_out_channel_num;    // 输出声道数
	int                         m_out_sample_rate;    // 输出采样率

	int                         m_nWebrtcMS;          // 每次处理毫秒数
	int                         m_nWebrtcNN;          // 每次处理样本数
	short                 *     m_lpMicBufNN;         // 麦克风原始数据 => short
	short                 *     m_lpHornBufNN;        // 扬声器原始数据 => short
	short                 *     m_lpEchoBufNN;        // 回音消除后数据 => short
	float                 *     m_pDMicBufNN;         // 麦克风原始数据 => float
	float                 *     m_pDHornBufNN;        // 扬声器原始数据 => float
	float                 *     m_pDEchoBufNN;        // 回音消除后数据 => float
												     
	circlebuf                   m_circle_mic;		  // PCM环形队列 => 只存放麦克风转换后的音频数据
	circlebuf                   m_circle_horn;        // PCM环形队列 => 只存放扬声器转换后的音频数据

	resample_info               m_mic_sample_info;    // 麦克风原始音频样本格式...
	resample_info               m_horn_sample_info;   // 扬声器原始音频样本格式...
	resample_info               m_echo_sample_info;   // 回音消除需要的音频样本格式...
	audio_resampler_t     *     m_mic_resampler;      // 麦克风原始样本数据转换成回音消除样本格式 => mic 转 echo
	audio_resampler_t     *     m_horn_resampler;     // 扬声器原始样本数据转换成回音消除样本格式 => horn 转 echo

	pthread_mutex_t             m_AECMutex;           // 回音消除互斥体
	void                  *     m_hAEC;               // 回音消除句柄...
	NsxHandle             *     m_lpNsxInst;          // 降噪模块句柄...

	//speaker_layout            speakers;
	//audio_format              format;
	//uint32_t                  sampleRate;

	static DWORD WINAPI ReconnectThread(LPVOID param);
	static DWORD WINAPI CaptureThread(LPVOID param);

	void doEchoMic(UINT64 ts, uint8_t * lpFrameData, uint32_t nFrameNum);
	void doEchoCancel(UINT64 ts);

	bool ProcessCaptureData();

	inline void Start();
	inline void Stop();
	void Reconnect();

	bool InitDevice(IMMDeviceEnumerator *enumerator);
	void InitClient();
	void InitRender();
	void InitFormat(WAVEFORMATEX *wfex);
	void InitCapture();
	void Initialize();

	void InitWebrtcAEC();
	void UnInitWebrtcAEC();
	void doReBuildWebrtcAEC();

	bool TryInitialize();

	void UpdateSettings(obs_data_t *settings);
public:
	WASAPISource(obs_data_t *settings, obs_source_t *source_, bool input);
	inline ~WASAPISource();

	void Update(obs_data_t *settings);

	obs_audio_data * AudioEchoCancelFilter(obs_audio_data *audio);
};

WASAPISource::WASAPISource(obs_data_t *settings, obs_source_t *source_,	bool input)
	: source          (source_)
	, isInputDevice   (input)
	, m_horn_resampler(nullptr)
	, m_mic_resampler (nullptr)
	, m_lpMicBufNN    (nullptr)
	, m_lpHornBufNN   (nullptr)
	, m_lpEchoBufNN   (nullptr)
	, m_pDMicBufNN    (nullptr)
	, m_pDHornBufNN   (nullptr)
	, m_pDEchoBufNN   (nullptr)
	, m_lpNsxInst     (nullptr)
	, m_hAEC          (nullptr)
{
	UpdateSettings(settings);

	stopSignal = CreateEvent(nullptr, true, false, nullptr);
	if (!stopSignal.Valid())
		throw "Could not create stop signal";

	receiveSignal = CreateEvent(nullptr, false, false, nullptr);
	if (!receiveSignal.Valid())
		throw "Could not create receive signal";

	Start();
}

inline void WASAPISource::Start()
{
	// 输入设备才初始化AEC...
	if (isInputDevice) {
		this->InitWebrtcAEC();
	}
	if (!TryInitialize()) {
		blog(LOG_INFO,
		     "[WASAPISource::WASAPISource] "
		     "Device '%s' not found.  Waiting for device",
		     device_id.c_str());
		Reconnect();
	}
}

// 重建回音消除对象，并清理扬声器缓存...
inline void WASAPISource::doReBuildWebrtcAEC()
{
	// 如果回音消除对象有效，并且扬声器缓存为空，不用重建，直接返回...
	if (m_hAEC != NULL && m_circle_horn.capacity <= 0)
		return;
	// 释放回音消除对象...
	if (m_hAEC != NULL) {
		WebRtcAec_Free(m_hAEC);
		m_hAEC = NULL;
	}
	// 初始化回音消除管理器 => Webrtc
	m_hAEC = WebRtcAec_Create();
	if (m_hAEC == NULL) {
		blog(LOG_INFO, "== ReBuild: WebRtcAec_Create failed ==");
		return;
	}
	// 设置不确定延时，自动对齐波形 => 必须在初始化之前设置...
	Aec * lpAEC = (Aec*)m_hAEC;
	WebRtcAec_enable_delay_agnostic(lpAEC->aec, true);
	WebRtcAec_enable_extended_filter(lpAEC->aec, true);
	// 初始化回音消除对象失败，直接返回...
	if (WebRtcAec_Init(m_hAEC, m_out_sample_rate, m_out_sample_rate) != 0) {
		blog(LOG_INFO, "== ReBuild: WebRtcAec_Init failed ==");
		return;
	}
	// 释放扬声器回音消除音频转换器...
	if (m_horn_resampler != nullptr) {
		audio_resampler_destroy(m_horn_resampler);
		m_horn_resampler = nullptr;
	}
	// 注意：缓存必须清理，否则会影响新的回音消除...
	// 重新初始化扬声器环形缓存队列对象...
	circlebuf_free(&m_circle_horn);
	// 打印webrtc的回音消除初始化完成...
	blog(LOG_INFO, "== ReBuild: InitWebrtcAEC OK ==");
}

inline void WASAPISource::InitWebrtcAEC()
{
	// 设置默认的输出声道数，输出采样率...
	m_out_channel_num = DEF_OUT_CHANNEL_NUM;
	m_out_sample_rate = DEF_OUT_SAMPLE_RATE;
	// 每次回音消除处理样本数，需要乘以声道数...
	m_nWebrtcNN = DEF_WEBRTC_AEC_NN * m_out_channel_num;
	// 计算每次处理设定样本数占用的毫秒数...
	m_nWebrtcMS = m_nWebrtcNN / ((m_out_sample_rate / 1000) * m_out_channel_num);
	// 根据每次处理样本数据分配回音消除需要用到的数据空间 => 数据是short格式...
	// 注意：这里使用obs的内存管理接口，可以进行泄漏跟踪 => bzalloc => bfree
	m_lpMicBufNN = (short*)bzalloc(m_nWebrtcNN * sizeof(short));
	m_lpHornBufNN = (short*)bzalloc(m_nWebrtcNN * sizeof(short));
	m_lpEchoBufNN = (short*)bzalloc(m_nWebrtcNN * sizeof(short));
	m_pDMicBufNN = (float*)bzalloc(m_nWebrtcNN * sizeof(float));
	m_pDHornBufNN = (float*)bzalloc(m_nWebrtcNN * sizeof(float));
	m_pDEchoBufNN = (float*)bzalloc(m_nWebrtcNN * sizeof(float));
	// 初始化各个环形队列对象...
	circlebuf_init(&m_circle_mic);
	circlebuf_init(&m_circle_horn);
	// 初始化回音消除互斥对象...
	pthread_mutex_init_value(&m_AECMutex);
	// 初始化回音消除管理器 => webrtc => DA-AEC
	m_hAEC = WebRtcAec_Create();
	if (m_hAEC == NULL) {
		blog(LOG_INFO, "== WebRtcAec_Create failed ==");
		return;
	}
	// 设置不确定延时，自动对齐波形 => 必须在初始化之前设置...
	Aec * lpAEC = (Aec*)m_hAEC;
	WebRtcAec_enable_delay_agnostic(lpAEC->aec, true);
	WebRtcAec_enable_extended_filter(lpAEC->aec, true);
	// 初始化回音消除对象失败，直接返回...
	if (WebRtcAec_Init(m_hAEC, m_out_sample_rate, m_out_sample_rate) != 0) {
		blog(LOG_INFO, "== WebRtcAec_Init failed ==");
		return;
	}
	// 打印webrtc的回音消除初始化完成...
	blog(LOG_INFO, "== InitWebrtcAEC OK ==");
	// 创建针对麦克风的降噪模块对象 => short格式...
	int err_code = 0;
	m_lpNsxInst = WebRtcNsx_Create();
	// 初始化降噪对象，使用16k频率...
	if (m_lpNsxInst != NULL) {
		err_code = WebRtcNsx_Init(m_lpNsxInst, m_out_sample_rate);
	}
	// 设定降噪程度 => 0: Mild, 1: Medium , 2: Aggressive
	if (m_lpNsxInst != NULL && err_code == 0) {
		err_code = WebRtcNsx_set_policy(m_lpNsxInst, 2);
	}
	// 打印创建麦克风降噪模块对象的结果...
	blog(LOG_INFO, "== Create NSX Code(%d) ==", err_code);
}

inline void WASAPISource::UnInitWebrtcAEC()
{
	// 释放麦克风回音消除音频转换器...
	if (m_mic_resampler != nullptr) {
		audio_resampler_destroy(m_mic_resampler);
		m_mic_resampler = nullptr;
	}
	// 释放扬声器回音消除音频转换器...
	if (m_horn_resampler != nullptr) {
		audio_resampler_destroy(m_horn_resampler);
		m_horn_resampler = nullptr;
	}
	// 释放回声消除相关对象...
	if (m_hAEC != NULL) {
		WebRtcAec_Free(m_hAEC);
		m_hAEC = NULL;
	}
	// 释放音频环形队列...
	circlebuf_free(&m_circle_mic);
	circlebuf_free(&m_circle_horn);
	// 释放回音消除使用到的缓存空间...
	if (m_lpMicBufNN != NULL) {
		bfree(m_lpMicBufNN);
		m_lpMicBufNN = NULL;
	}
	if (m_lpHornBufNN != NULL) {
		bfree(m_lpHornBufNN);
		m_lpHornBufNN = NULL;
	}
	if (m_lpEchoBufNN != NULL) {
		bfree(m_lpEchoBufNN);
		m_lpEchoBufNN = NULL;
	}
	if (m_pDMicBufNN != NULL) {
		bfree(m_pDMicBufNN);
		m_pDMicBufNN = NULL;
	}
	if (m_pDHornBufNN != NULL) {
		bfree(m_pDHornBufNN);
		m_pDHornBufNN = NULL;
	}
	if (m_pDEchoBufNN != NULL) {
		bfree(m_pDEchoBufNN);
		m_pDEchoBufNN = NULL;
	}
	// 释放降噪模块使用的对象和空间...
	if (m_lpNsxInst != NULL) {
		WebRtcNsx_Free(m_lpNsxInst);
		m_lpNsxInst = NULL;
	}
	// 释放回音消除互斥对象...
	pthread_mutex_destroy(&m_AECMutex);
}

inline void WASAPISource::Stop()
{
	SetEvent(stopSignal);

	if (active) {
		blog(LOG_INFO, "WASAPI: Device '%s' Terminated",
		     device_name.c_str());
		WaitForSingleObject(captureThread, INFINITE);
	}

	if (reconnecting)
		WaitForSingleObject(reconnectThread, INFINITE);

	ResetEvent(stopSignal);

	// 输入设备才处理AEC...
	if (isInputDevice) {
		UnInitWebrtcAEC();
	}
}

inline WASAPISource::~WASAPISource()
{
	Stop();
}

void WASAPISource::UpdateSettings(obs_data_t *settings)
{
	device_id = obs_data_get_string(settings, OPT_DEVICE_ID);
	useDeviceTiming = obs_data_get_bool(settings, OPT_USE_DEVICE_TIMING);
	isDefaultDevice = _strcmpi(device_id.c_str(), "default") == 0;
}

void WASAPISource::Update(obs_data_t *settings)
{
	string newDevice = obs_data_get_string(settings, OPT_DEVICE_ID);
	bool restart = newDevice.compare(device_id) != 0;

	if (restart)
		Stop();

	UpdateSettings(settings);

	if (restart)
		Start();
}

bool WASAPISource::InitDevice(IMMDeviceEnumerator *enumerator)
{
	HRESULT res;

	if (isDefaultDevice) {
		res = enumerator->GetDefaultAudioEndpoint(
			isInputDevice ? eCapture : eRender,
			isInputDevice ? eCommunications : eConsole,
			device.Assign());
	} else {
		wchar_t *w_id;
		os_utf8_to_wcs_ptr(device_id.c_str(), device_id.size(), &w_id);

		res = enumerator->GetDevice(w_id, device.Assign());

		bfree(w_id);
	}

	return SUCCEEDED(res);
}

#define BUFFER_TIME_100NS (5 * 10000000)

void WASAPISource::InitClient()
{
	CoTaskMemPtr<WAVEFORMATEX> wfex;
	HRESULT res;
	DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

	res = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
			       (void **)client.Assign());
	if (FAILED(res))
		throw HRError("Failed to activate client context", res);

	res = client->GetMixFormat(&wfex);
	if (FAILED(res))
		throw HRError("Failed to get mix format", res);

	InitFormat(wfex);

	if (!isInputDevice)
		flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;

	res = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags,
				 BUFFER_TIME_100NS, 0, wfex, nullptr);
	if (FAILED(res))
		throw HRError("Failed to get initialize audio client", res);
}

void WASAPISource::InitRender()
{
	CoTaskMemPtr<WAVEFORMATEX> wfex;
	HRESULT res;
	LPBYTE buffer;
	UINT32 frames;
	ComPtr<IAudioClient> client;

	res = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
			       (void **)client.Assign());
	if (FAILED(res))
		throw HRError("Failed to activate client context", res);

	res = client->GetMixFormat(&wfex);
	if (FAILED(res))
		throw HRError("Failed to get mix format", res);

	res = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, BUFFER_TIME_100NS,
				 0, wfex, nullptr);
	if (FAILED(res))
		throw HRError("Failed to get initialize audio client", res);

	/* Silent loopback fix. Prevents audio stream from stopping and */
	/* messing up timestamps and other weird glitches during silence */
	/* by playing a silent sample all over again. */

	res = client->GetBufferSize(&frames);
	if (FAILED(res))
		throw HRError("Failed to get buffer size", res);

	res = client->GetService(__uuidof(IAudioRenderClient),
				 (void **)render.Assign());
	if (FAILED(res))
		throw HRError("Failed to get render client", res);

	res = render->GetBuffer(frames, &buffer);
	if (FAILED(res))
		throw HRError("Failed to get buffer", res);

	memset(buffer, 0, frames * wfex->nBlockAlign);

	render->ReleaseBuffer(frames, 0);
}

static speaker_layout ConvertSpeakerLayout(DWORD layout, WORD channels)
{
	switch (layout) {
	case KSAUDIO_SPEAKER_2POINT1:
		return SPEAKERS_2POINT1;
	case KSAUDIO_SPEAKER_SURROUND:
		return SPEAKERS_4POINT0;
	case OBS_KSAUDIO_SPEAKER_4POINT1:
		return SPEAKERS_4POINT1;
	case KSAUDIO_SPEAKER_5POINT1_SURROUND:
		return SPEAKERS_5POINT1;
	case KSAUDIO_SPEAKER_7POINT1_SURROUND:
		return SPEAKERS_7POINT1;
	}

	return (speaker_layout)channels;
}

void WASAPISource::InitFormat(WAVEFORMATEX *wfex)
{
	DWORD layout = 0;

	if (wfex->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		WAVEFORMATEXTENSIBLE *ext = (WAVEFORMATEXTENSIBLE *)wfex;
		layout = ext->dwChannelMask;
	}

	/* WASAPI is always float */
	//sampleRate = wfex->nSamplesPerSec;
	//format     = AUDIO_FORMAT_FLOAT;
	//speakers   = ConvertSpeakerLayout(layout, wfex->nChannels);

	// 设定回音消除需要的音频样本格式，16位，单声道，16000采样率...
	m_echo_sample_info.format = AUDIO_FORMAT_16BIT;
	m_echo_sample_info.speakers = (speaker_layout)m_out_channel_num;
	m_echo_sample_info.samples_per_sec = m_out_sample_rate;

	/* WASAPI is always float for input */
	// 从系统配置当中获取麦克风原始样本格式...
	m_mic_sample_info.format = AUDIO_FORMAT_FLOAT;
	m_mic_sample_info.speakers = ConvertSpeakerLayout(layout, wfex->nChannels);
	m_mic_sample_info.samples_per_sec = wfex->nSamplesPerSec;
	// 创建麦克风原始数据的重采样对象，将麦克风采集到的数据样本转换成回音消除需要的样本格式...
	m_mic_resampler = audio_resampler_create(&m_echo_sample_info, &m_mic_sample_info);
}

void WASAPISource::InitCapture()
{
	HRESULT res = client->GetService(__uuidof(IAudioCaptureClient),
					 (void **)capture.Assign());
	if (FAILED(res))
		throw HRError("Failed to create capture context", res);

	res = client->SetEventHandle(receiveSignal);
	if (FAILED(res))
		throw HRError("Failed to set event handle", res);

	captureThread = CreateThread(nullptr, 0, WASAPISource::CaptureThread,
				     this, 0, nullptr);
	if (!captureThread.Valid())
		throw "Failed to create capture thread";

	client->Start();
	active = true;

	blog(LOG_INFO, "WASAPI: Device '%s' initialized", device_name.c_str());
}

void WASAPISource::Initialize()
{
	ComPtr<IMMDeviceEnumerator> enumerator;
	HRESULT res;

	res = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
			       CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
			       (void **)enumerator.Assign());
	if (FAILED(res))
		throw HRError("Failed to create enumerator", res);

	if (!InitDevice(enumerator))
		return;

	device_name = GetDeviceName(device);

	InitClient();
	if (!isInputDevice)
		InitRender();
	InitCapture();
}

bool WASAPISource::TryInitialize()
{
	try {
		Initialize();

	} catch (HRError &error) {
		if (previouslyFailed)
			return active;

		blog(LOG_WARNING, "[WASAPISource::TryInitialize]:[%s] %s: %lX",
		     device_name.empty() ? device_id.c_str()
					 : device_name.c_str(),
		     error.str, error.hr);

	} catch (const char *error) {
		if (previouslyFailed)
			return active;

		blog(LOG_WARNING, "[WASAPISource::TryInitialize]:[%s] %s",
		     device_name.empty() ? device_id.c_str()
					 : device_name.c_str(),
		     error);
	}

	previouslyFailed = !active;
	return active;
}

void WASAPISource::Reconnect()
{
	reconnecting = true;
	reconnectThread = CreateThread(
		nullptr, 0, WASAPISource::ReconnectThread, this, 0, nullptr);

	if (!reconnectThread.Valid())
		blog(LOG_WARNING,
		     "[WASAPISource::Reconnect] "
		     "Failed to initialize reconnect thread: %lu",
		     GetLastError());
}

static inline bool WaitForSignal(HANDLE handle, DWORD time)
{
	return WaitForSingleObject(handle, time) != WAIT_TIMEOUT;
}

#define RECONNECT_INTERVAL 3000

DWORD WINAPI WASAPISource::ReconnectThread(LPVOID param)
{
	WASAPISource *source = (WASAPISource *)param;

	os_set_thread_name("win-wasapi: reconnect thread");

	CoInitializeEx(0, COINIT_MULTITHREADED);

	obs_monitoring_type type =
		obs_source_get_monitoring_type(source->source);
	obs_source_set_monitoring_type(source->source,
				       OBS_MONITORING_TYPE_NONE);

	while (!WaitForSignal(source->stopSignal, RECONNECT_INTERVAL)) {
		if (source->TryInitialize())
			break;
	}

	obs_source_set_monitoring_type(source->source, type);

	source->reconnectThread = nullptr;
	source->reconnecting = false;
	return 0;
}

/*static void doSaveAudioPCM(uint8_t * lpBufData, int nBufSize, int nAudioRate, int nAudioChannel)
{
	// 注意：PCM数据必须用二进制方式打开文件...
	char szFullPath[MAX_PATH] = { 0 };
	sprintf(szFullPath, "F:/MP4/PCM/mic_%d_%d_short.pcm", nAudioRate, nAudioChannel);
	FILE * lpFile = fopen(szFullPath, "ab+");
	// 打开文件成功，开始写入音频PCM数据内容...
	if (lpFile != NULL) {
		fwrite(lpBufData, nBufSize, 1, lpFile);
		fclose(lpFile);
	}
}*/

bool WASAPISource::ProcessCaptureData()
{
	HRESULT res;
	LPBYTE buffer;
	UINT32 frames;
	DWORD flags;
	UINT64 pos, ts;
	UINT captureSize = 0;

	while (true) {
		res = capture->GetNextPacketSize(&captureSize);

		if (FAILED(res)) {
			if (res != AUDCLNT_E_DEVICE_INVALIDATED)
				blog(LOG_WARNING,
				     "[WASAPISource::GetCaptureData]"
				     " capture->GetNextPacketSize"
				     " failed: %lX",
				     res);
			return false;
		}

		if (!captureSize)
			break;

		res = capture->GetBuffer(&buffer, &frames, &flags, &pos, &ts);
		if (FAILED(res)) {
			if (res != AUDCLNT_E_DEVICE_INVALIDATED)
				blog(LOG_WARNING,
				     "[WASAPISource::GetCaptureData]"
				     " capture->GetBuffer"
				     " failed: %lX",
				     res);
			return false;
		}

		// 将麦克风数据统一转换...
		uint64_t  ts_offset = 0;
		uint32_t  output_frames = 0;
		uint8_t * output_data[MAX_AV_PLANES] = { 0 };
		// 对原始麦克风音频样本格式，转换成回音消除需要的样本格式...
		if (audio_resampler_resample(m_mic_resampler, output_data, &output_frames, &ts_offset, (const uint8_t *const *)&buffer, frames)) {
			if (m_horn_resampler != nullptr) {
				// 有扬声器数据，放入环形队列，进行回音消除处理，需要得到字节数才能放入环形队列...
				int cur_data_size = get_audio_size(m_echo_sample_info.format, m_echo_sample_info.speakers, output_frames);
				circlebuf_push_back(&m_circle_mic, output_data[0], cur_data_size);
				this->doEchoCancel(ts);
			} else {
				// 没有扬声器数据，需要重建回音消除对象，清理扬声器缓存...
				this->doReBuildWebrtcAEC();
				// 没有扬声器数据，直接投递到obs上层处理，这里使用样本数一次性处理...
				this->doEchoMic(ts, output_data[0], output_frames);
			}
		}

		// 释放硬件的捕捉过程...
		capture->ReleaseBuffer(frames);
	}

	return true;
}

// 只有麦克风数据，没有扬声器数据的处理 => 不是缓存长度，而是数据帧个数...
void WASAPISource::doEchoMic(UINT64 ts, uint8_t * lpFrameData, uint32_t nFrameNum)
{
	// 先删除之前创建的扬声器转换器对象...
	if (m_horn_resampler != nullptr) {
		audio_resampler_destroy(m_horn_resampler);
		m_horn_resampler = nullptr;
	}
	// 重置扬声器环形缓存队列对象，如果扬声器队列有效才进行重置操作...
	if (m_circle_horn.data != NULL || m_circle_horn.capacity > 0) {
		circlebuf_free(&m_circle_horn);
		circlebuf_init(&m_circle_horn);
	}
	// 构造需要向上层投递的音频数据...
	obs_source_audio data = {};
	data.data[0] = lpFrameData;
	data.frames = nFrameNum;
	data.speakers = m_echo_sample_info.speakers;
	data.samples_per_sec = m_echo_sample_info.samples_per_sec;
	data.format = m_echo_sample_info.format;
	data.timestamp = useDeviceTiming ? ts * 100 : os_gettime_ns();
	// 注意：数据帧个数不能包含声道数，同时，采样率应该以转换后的采样率为基准...
	if (!useDeviceTiming) {
		data.timestamp -= (uint64_t)nFrameNum * 1000000000ULL / (uint64_t)m_echo_sample_info.samples_per_sec;
	}
	// 向obs上层进行数据的传递工作...
	obs_source_output_audio(source, &data);
}

// 既有麦克风数据，又有扬声器数据的处理...
void WASAPISource::doEchoCancel(UINT64 ts)
{
	int err_code = 0, nCountNum = 0;
	// 注意：这里并不考虑扬声器数据是否足够，尽最大可能降低麦克风延时...
	// 计算处理单元 => 样本数 * 每个样本占用字节数...
	size_t nNeedBufBytes = m_nWebrtcNN * sizeof(short);
	// 将所有有效的麦克风和扬声器数据全部进行投递处理...
	while (true) {
		// 麦克风数据数据长度不够，终止回音消除...
		if (m_circle_mic.size < nNeedBufBytes)
			return;
		pthread_mutex_lock(&m_AECMutex);
		// 读取麦克风需要的处理样本内容 => 注意是读取字节数，不是样本数...
		circlebuf_pop_front(&m_circle_mic, m_lpMicBufNN, nNeedBufBytes);
		// 扬声器队列不一定有效，先将扬声器缓存置空...
		memset(m_lpHornBufNN, 0, nNeedBufBytes);
		// 如果扬声器数据足够，读取扬声器数据到待处理样本缓存当中..
		if (m_circle_horn.size >= nNeedBufBytes) {
			circlebuf_pop_front(&m_circle_horn, m_lpHornBufNN, nNeedBufBytes);
		}
		pthread_mutex_unlock(&m_AECMutex);
		// 将short数据格式转换成float数据格式...
		for (int i = 0; i < m_nWebrtcNN; ++i) {
			m_pDMicBufNN[i] = (float)m_lpMicBufNN[i];
			m_pDHornBufNN[i] = (float)m_lpHornBufNN[i];
		}
		// 注意：使用自动计算延时模式，可以将msInSndCardBuf参数设置为0...
		// 先将扬声器数据进行远端投递，再投递麦克风数据进行回音消除 => 投递样本个数...
		err_code = WebRtcAec_BufferFarend(m_hAEC, m_pDHornBufNN, m_nWebrtcNN);
		err_code = WebRtcAec_Process(m_hAEC, &m_pDMicBufNN, m_out_channel_num, &m_pDEchoBufNN, m_nWebrtcNN, 0, 0);
		// 将float数据格式转换成short数据格式...
		for (int i = 0; i < m_nWebrtcNN; ++i) {
			m_lpEchoBufNN[i] = (short)m_pDEchoBufNN[i];
		}
		// 对麦克风原始数据进行存盘处理 => 必须用二进制方式打开文件...
		//doSaveAudioPCM((uint8_t*)m_lpMicBufNN, nNeedBufBytes, m_out_sample_rate, 0);
		// 将扬声器的PCM数据进行存盘处理 => 必须用二进制方式打开文件...
		//doSaveAudioPCM((uint8_t*)m_lpHornBufNN, nNeedBufBytes, m_out_sample_rate, 1);
		// 对回音消除后的数据进行存盘处理 => 必须用二进制方式打开文件...
		//doSaveAudioPCM((uint8_t*)m_lpEchoBufNN, nNeedBufBytes, m_out_sample_rate, 2);
		// 对回音消除后的数据进行降噪处理 => short格式...
		if (m_lpNsxInst != NULL) {
			WebRtcNsx_Process(m_lpNsxInst, &m_lpEchoBufNN, m_out_channel_num, &m_lpMicBufNN);
			memcpy(m_lpEchoBufNN, m_lpMicBufNN, nNeedBufBytes);
		}
		// 对降噪后的回音消除数据进行存盘处理 => 必须用二进制方式打开文件...
		//doSaveAudioPCM((uint8_t*)m_lpEchoBufNN, nNeedBufBytes, m_out_sample_rate, 3);
		// 发生错误，打印错误信息...
		if (err_code != 0) {
			blog(LOG_INFO, "== err: %d, mic_buf: %d, horn_buf: %d ==", err_code, m_circle_mic.size, m_circle_horn.size);
		}
		// 构造回音消除之后的音频数据...
		obs_source_audio data = {};
		data.data[0] = (uint8_t*)m_lpEchoBufNN;
		data.frames = m_nWebrtcNN / m_out_channel_num;
		data.speakers = m_echo_sample_info.speakers;
		data.samples_per_sec = m_echo_sample_info.samples_per_sec;
		data.format = m_echo_sample_info.format;
		data.timestamp = useDeviceTiming ? ts * 100 : os_gettime_ns();
		// 注意：数据帧个数不能包含声道数，同时，采样率应该以转换后的采样率为基准...
		if (!useDeviceTiming) {
			uint64_t nTotalFrameNum = (++nCountNum) * data.frames;
			data.timestamp -= nTotalFrameNum * 1000000000ULL / (uint64_t)m_echo_sample_info.samples_per_sec;
		}
		// 向obs上层进行数据的传递工作...
		obs_source_output_audio(source, &data);
	}
}

obs_audio_data * WASAPISource::AudioEchoCancelFilter(obs_audio_data *audio)
{
	// 如果捕捉设备还没有初始化完毕，不能进行回音消除数据填充...
	if (m_mic_resampler == nullptr)
		return NULL;
	// 注意：实际传递的结构是 obs_source_audio，详见 wasapi-output.c::mix_monitor()
	obs_source_audio * lpSourceAudio = (obs_source_audio*)audio;
	// 如果扬声器的转换对象为空，需要进行转换器的生成...
	if (m_horn_resampler == nullptr) {
		m_horn_sample_info.samples_per_sec = lpSourceAudio->samples_per_sec;
		m_horn_sample_info.speakers = lpSourceAudio->speakers;
		m_horn_sample_info.format = lpSourceAudio->format;
		// 创建扬声器原始数据的重采样对象，将扬声器采集到的数据样本转换成回音消除需要的样本格式...
		m_horn_resampler = audio_resampler_create(&m_echo_sample_info, &m_horn_sample_info);
	}
	uint64_t  ts_offset = 0;
	uint32_t  output_frames = 0;
	uint8_t * output_data[MAX_AV_PLANES] = { 0 };
	//doSaveAudioPCM((uint8_t*)lpSourceAudio->data[0], lpSourceAudio->speakers*lpSourceAudio->frames*sizeof(float), lpSourceAudio->samples_per_sec, 1);
	// 对输入的原始音频样本格式进行统一的格式转换，转换成功，放入环形队列...
	if (audio_resampler_resample(m_horn_resampler, output_data, &output_frames, &ts_offset, lpSourceAudio->data, lpSourceAudio->frames)) {
		// 注意：这里需要进行互斥保护，是多线程数据访问...
		pthread_mutex_lock(&m_AECMutex);
		int cur_data_size = get_audio_size(m_echo_sample_info.format, m_echo_sample_info.speakers, output_frames);
		circlebuf_push_back(&m_circle_horn, output_data[0], cur_data_size);
		pthread_mutex_unlock(&m_AECMutex);
		//doSaveAudioPCM(output_data[0], cur_data_size, m_echo_sample_info.samples_per_sec, 2);
	}
	// 可以返回空指针...
	return audio;
}

static inline bool WaitForCaptureSignal(DWORD numSignals, const HANDLE *signals, DWORD duration)
{
	DWORD ret;
	ret = WaitForMultipleObjects(numSignals, signals, false, duration);

	return ret == WAIT_OBJECT_0 || ret == WAIT_TIMEOUT;
}

DWORD WINAPI WASAPISource::CaptureThread(LPVOID param)
{
	WASAPISource *source = (WASAPISource *)param;
	bool reconnect = false;

	/* Output devices don't signal, so just make it check every 10 ms */
	DWORD dur = source->isInputDevice ? RECONNECT_INTERVAL : 10;

	HANDLE sigs[2] = {source->receiveSignal, source->stopSignal};

	os_set_thread_name("win-wasapi: capture thread");

	while (WaitForCaptureSignal(2, sigs, dur)) {
		if (!source->ProcessCaptureData()) {
			reconnect = true;
			break;
		}
	}

	source->client->Stop();

	source->captureThread = nullptr;
	source->active = false;

	if (reconnect) {
		blog(LOG_INFO, "Device '%s' invalidated.  Retrying",
		     source->device_name.c_str());
		source->Reconnect();
	}

	return 0;
}

/* ------------------------------------------------------------------------- */

static const char *GetWASAPIInputName(void *)
{
	return obs_module_text("AudioInput");
}

static const char *GetWASAPIOutputName(void *)
{
	return obs_module_text("AudioOutput");
}

static void GetWASAPIDefaultsInput(obs_data_t *settings)
{
	obs_data_set_default_string(settings, OPT_DEVICE_ID, "default");
	obs_data_set_default_bool(settings, OPT_USE_DEVICE_TIMING, false);
}

static void GetWASAPIDefaultsOutput(obs_data_t *settings)
{
	obs_data_set_default_string(settings, OPT_DEVICE_ID, "default");
	obs_data_set_default_bool(settings, OPT_USE_DEVICE_TIMING, true);
}

static void *CreateWASAPISource(obs_data_t *settings, obs_source_t *source,
				bool input)
{
	try {
		return new WASAPISource(settings, source, input);
	} catch (const char *error) {
		blog(LOG_ERROR, "[CreateWASAPISource] %s", error);
	}

	return nullptr;
}

static void *CreateWASAPIInput(obs_data_t *settings, obs_source_t *source)
{
	return CreateWASAPISource(settings, source, true);
}

static void *CreateWASAPIOutput(obs_data_t *settings, obs_source_t *source)
{
	return CreateWASAPISource(settings, source, false);
}

static void DestroyWASAPISource(void *obj)
{
	delete static_cast<WASAPISource *>(obj);
}

static void UpdateWASAPISource(void *obj, obs_data_t *settings)
{
	static_cast<WASAPISource *>(obj)->Update(settings);
}

// 这是伪过滤器接口，专门供给 wasapi-output.c 使用的，填充投递到扬声器的音频数据...
static obs_audio_data * AudioEchoCancelFilter(void *obj, obs_audio_data *audio)
{
	return static_cast<WASAPISource*>(obj)->AudioEchoCancelFilter(audio);
}

static obs_properties_t *GetWASAPIProperties(bool input)
{
	obs_properties_t *props = obs_properties_create();
	vector<AudioDeviceInfo> devices;

	obs_property_t *device_prop = obs_properties_add_list(
		props, OPT_DEVICE_ID, obs_module_text("Device"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	GetWASAPIAudioDevices(devices, input);

	if (devices.size())
		obs_property_list_add_string(
			device_prop, obs_module_text("Default"), "default");

	for (size_t i = 0; i < devices.size(); i++) {
		AudioDeviceInfo &device = devices[i];
		obs_property_list_add_string(device_prop, device.name.c_str(),
					     device.id.c_str());
	}

	obs_properties_add_bool(props, OPT_USE_DEVICE_TIMING,
				obs_module_text("UseDeviceTiming"));

	return props;
}

static obs_properties_t *GetWASAPIPropertiesInput(void *)
{
	return GetWASAPIProperties(true);
}

static obs_properties_t *GetWASAPIPropertiesOutput(void *)
{
	return GetWASAPIProperties(false);
}

void RegisterWASAPIInput()
{
	obs_source_info info = {};
	info.id = "wasapi_input_capture";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
	info.get_name = GetWASAPIInputName;
	info.create = CreateWASAPIInput;
	info.destroy = DestroyWASAPISource;
	info.update = UpdateWASAPISource;
	info.get_defaults = GetWASAPIDefaultsInput;
	info.get_properties = GetWASAPIPropertiesInput;
	info.filter_audio = AudioEchoCancelFilter;
	obs_register_source(&info);
}

void RegisterWASAPIOutput()
{
	obs_source_info info = {};
	info.id = "wasapi_output_capture";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE |
			    OBS_SOURCE_DO_NOT_SELF_MONITOR;
	info.get_name = GetWASAPIOutputName;
	info.create = CreateWASAPIOutput;
	info.destroy = DestroyWASAPISource;
	info.update = UpdateWASAPISource;
	info.get_defaults = GetWASAPIDefaultsOutput;
	info.get_properties = GetWASAPIPropertiesOutput;
	obs_register_source(&info);
}
