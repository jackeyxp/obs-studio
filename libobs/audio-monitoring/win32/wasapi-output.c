#include "../../media-io/audio-resampler.h"
#include "../../util/circlebuf.h"
#include "../../util/platform.h"
#include "../../util/darray.h"
#include "../../obs-internal.h"

#include "wasapi-output.h"

#define ACTUALLY_DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
	EXTERN_C const GUID DECLSPEC_SELECTANY name = {                       \
		l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}

ACTUALLY_DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E,
		     0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
ACTUALLY_DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7,
		     0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
ACTUALLY_DEFINE_GUID(IID_IAudioClient, 0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78,
		     0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
ACTUALLY_DEFINE_GUID(IID_IAudioRenderClient, 0xF294ACFC, 0x3146, 0x4483, 0xA7,
		     0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);

struct audio_monitor {
	obs_source_t *source;
	IMMDevice *device;
	IAudioClient *client;
	IAudioRenderClient *render;

	uint64_t last_recv_time;
	uint64_t prev_video_ts;
	uint64_t time_since_prev;
	audio_resampler_t *resampler;
	uint32_t sample_rate;
	uint32_t channels;
	enum speaker_layout speakers;
	bool source_has_video;
	bool ignore;
	
	bool               is_on;
	bool               is_muted;
	uint32_t           max_frame_size;
	struct circlebuf   play_buffer;
	DARRAY(float)      buf_play;

	int64_t lowest_audio_offset;
	struct circlebuf delay_buffer;

	DARRAY(float)      buf_delay;
	pthread_mutex_t playback_mutex;
};

/* #define DEBUG_AUDIO */

static bool process_audio_delay(struct audio_monitor *monitor, float **data,
				uint32_t *frames, uint64_t ts, uint32_t pad)
{
	obs_source_t *s = monitor->source;
	uint64_t last_frame_ts = s->last_frame_ts;
	uint64_t cur_time = os_gettime_ns();
	uint64_t front_ts;
	uint64_t cur_ts;
	int64_t diff;
	uint32_t blocksize = monitor->channels * sizeof(float);

	/* cut off audio if long-since leftover audio in delay buffer */
	if (cur_time - monitor->last_recv_time > 1000000000)
		circlebuf_free(&monitor->delay_buffer);
	monitor->last_recv_time = cur_time;

	ts += monitor->source->sync_offset;

	circlebuf_push_back(&monitor->delay_buffer, &ts, sizeof(ts));
	circlebuf_push_back(&monitor->delay_buffer, frames, sizeof(*frames));
	circlebuf_push_back(&monitor->delay_buffer, *data, *frames * blocksize);

	if (!monitor->prev_video_ts) {
		monitor->prev_video_ts = last_frame_ts;

	} else if (monitor->prev_video_ts == last_frame_ts) {
		monitor->time_since_prev += (uint64_t)*frames * 1000000000ULL /
					    (uint64_t)monitor->sample_rate;
	} else {
		monitor->time_since_prev = 0;
	}

	while (monitor->delay_buffer.size != 0) {
		size_t size;
		bool bad_diff;

		circlebuf_peek_front(&monitor->delay_buffer, &cur_ts, sizeof(ts));
		front_ts = cur_ts - ((uint64_t)pad * 1000000000ULL / (uint64_t)monitor->sample_rate);
		diff = (int64_t)front_ts - (int64_t)last_frame_ts;
		bad_diff = !last_frame_ts || llabs(diff) > 5000000000 || monitor->time_since_prev > 100000000ULL;

		/* delay audio if rushing */
		if (!bad_diff && diff > 75000000) {
#ifdef DEBUG_AUDIO
			blog(LOG_INFO,
			     "audio rushing, cutting audio, "
			     "diff: %lld, delay buffer size: %lu, "
			     "v: %llu: a: %llu",
			     diff, (int)monitor->delay_buffer.size,
			     last_frame_ts, front_ts);
#endif
			return false;
		}

		circlebuf_pop_front(&monitor->delay_buffer, NULL, sizeof(ts));
		circlebuf_pop_front(&monitor->delay_buffer, frames, sizeof(*frames));

		size = *frames * blocksize;
		da_resize(monitor->buf_delay, size);
		circlebuf_pop_front(&monitor->delay_buffer,	monitor->buf_delay.array, size);

		/* cut audio if dragging */
		if (!bad_diff && diff < -75000000 && monitor->delay_buffer.size > 0) {
#ifdef DEBUG_AUDIO
			blog(LOG_INFO,
			     "audio dragging, cutting audio, "
			     "diff: %lld, delay buffer size: %lu, "
			     "v: %llu: a: %llu",
			     diff, (int)monitor->delay_buffer.size,
			     last_frame_ts, front_ts);
#endif
			continue;
		}

		// 注意：这种直接丢帧方式需要优化，应该采用加速播放或减速播放的方式，而不是直接丢帧...
		// 播放内部遗留缓存超过 75 毫秒，就不能继续投递数据，直接丢弃，继续使用已投递播放缓存里面的数据...
		int64_t pad_ns_buff = (uint64_t)pad * 1000000000ULL / (uint64_t)monitor->sample_rate;
		if (pad_ns_buff >= 75 * 1000000) {
#ifdef DEBUG_AUDIO
			blog(LOG_DEBUG, "audio delay, diff: %lld, delay buffer size: %lu,"
				"v: %llu : a : %llu : pad_ns_buff: %llu, pad: %lu",
				diff, (int)monitor->delay_buffer.size, last_frame_ts,
				front_ts, pad_ns_buff, pad);
#endif
			return false;
		}

		*data = monitor->buf_delay.array;
		return true;
	}

	return false;
}

// 枚举所有的音频数据源对象，找到麦克风对象，投递数据，进行回音消除...
// 注意：不要用obs_enum_sources，互斥data.sources_mutex会跟rtp-source发生互锁...
void doPushEchoDataToMic(struct obs_source_audio * lpObsAudio)
{
	struct obs_source *source;
	struct obs_core_data *data = &obs->data;
	pthread_mutex_lock(&data->audio_sources_mutex);
	source = data->first_audio_source;
	while (source) {
		// 如果找到数据源是麦克风输入对象，投递数据，退出...
		if (astrcmpi(obs_source_get_id(source), "wasapi_input_capture") == 0) {
			// 如果找到的数据源是麦克风输入对象，查看投递接口是否有效，有效进行数据投递...
			if (source->context.data && source->info.filter_audio) {
				source->info.filter_audio(source->context.data, (struct obs_audio_data*)lpObsAudio);
				break;
			}
		}
		// 继续寻找麦克风数据源对象...
		source = (struct obs_source*)source->next_audio_source;
	}
	pthread_mutex_unlock(&data->audio_sources_mutex);
}

static void on_audio_playback(void *param, obs_source_t *source,
			      const struct audio_data *audio_data, bool muted)
{
	struct audio_monitor *monitor = param;
	IAudioRenderClient *render = monitor->render;
	uint8_t *resample_data[MAX_AV_PLANES];
	float vol = source->user_volume;
	uint32_t resample_frames;
	uint64_t ts_offset;
	bool success;
	//BYTE *output;

	if (pthread_mutex_trylock(&monitor->playback_mutex) != 0) {
		return;
	}
	if (os_atomic_load_long(&source->activate_refs) == 0) {
		goto unlock;
	}

	success = audio_resampler_resample(
		monitor->resampler, resample_data, &resample_frames, &ts_offset,
		(const uint8_t *const *)audio_data->data,
		(uint32_t)audio_data->frames);
	if (!success) {
		goto unlock;
	}

	UINT32 pad = 0;
	//monitor->client->lpVtbl->GetCurrentPadding(monitor->client, &pad);

	bool decouple_audio = source->async_unbuffered && source->async_decoupled;

	if (monitor->source_has_video && !decouple_audio) {
		uint64_t ts = audio_data->timestamp - ts_offset;
		if (!process_audio_delay(monitor, (float**)(&resample_data[0]), &resample_frames, ts, pad)) {
			goto unlock;
		}
	}

	monitor->max_frame_size = max(monitor->max_frame_size, resample_frames);
	uint32_t audio_size = resample_frames * monitor->channels * sizeof(float);
	//blog(LOG_DEBUG, "== source_name: %s, audio_frames: %d, audio_ts: %llu, cur_time: %llu ==",
	//	source->context.name, resample_frames, ts, os_gettime_ns());

	/*HRESULT hr = render->lpVtbl->GetBuffer(render, resample_frames, &output);
	if (FAILED(hr)) {
		goto unlock;
	}*/

	if (!muted) {
		/* apply volume */
		if (!close_float(vol, 1.0f, EPSILON)) {
			register float *cur = (float *)resample_data[0];
			register float *end = cur + resample_frames * monitor->channels;

			while (cur < end)
				*(cur++) *= vol;
		}
		// 将音频缓存到对应的监视器缓存对象当中 => 直接利用playback_mutex互斥体...
		circlebuf_push_back(&monitor->play_buffer, resample_data[0], audio_size);
		// 将转换后的音频数据投递到监视器(扬声器)的缓冲区当中...
		// memcpy(output, resample_data[0], audio_size);
		monitor->is_on = true;
	} else {
		// 如果静音，并且播放缓存有效，释放之...
		if (monitor->play_buffer.size > 0) {
			circlebuf_free(&monitor->play_buffer);
		}
		monitor->is_on = false;
	}
	// 保存静音标志到当前监视器对象当中...
	monitor->is_muted = muted;

	//render->lpVtbl->ReleaseBuffer(render, resample_frames,
	//		muted ? AUDCLNT_BUFFERFLAGS_SILENT : 0);
unlock:
	pthread_mutex_unlock(&monitor->playback_mutex);
	// 调用场景监听器去进行各个监听通道的混音并投递工作...
	if (!monitor->is_muted) {
		scene_monitor_mix_play();
	}
}

static inline void audio_monitor_free(struct audio_monitor *monitor)
{
	if (monitor->ignore)
		return;

	if (monitor->source) {
		obs_source_remove_audio_capture_callback(
			monitor->source, on_audio_playback, monitor);
	}

	if (monitor->client)
		monitor->client->lpVtbl->Stop(monitor->client);

	safe_release(monitor->device);
	safe_release(monitor->client);
	safe_release(monitor->render);
	audio_resampler_destroy(monitor->resampler);
	circlebuf_free(&monitor->delay_buffer);
	da_free(monitor->buf_delay);
	
	pthread_mutex_destroy(&monitor->playback_mutex);
	circlebuf_free(&monitor->play_buffer);
	da_free(monitor->buf_play);
}

static enum speaker_layout convert_speaker_layout(DWORD layout, WORD channels)
{
	switch (layout) {
	case KSAUDIO_SPEAKER_2POINT1:
		return SPEAKERS_2POINT1;
	case KSAUDIO_SPEAKER_SURROUND:
		return SPEAKERS_4POINT0;
	case KSAUDIO_SPEAKER_4POINT1:
		return SPEAKERS_4POINT1;
	case KSAUDIO_SPEAKER_5POINT1:
		return SPEAKERS_5POINT1;
	case KSAUDIO_SPEAKER_7POINT1:
		return SPEAKERS_7POINT1;
	}

	return (enum speaker_layout)channels;
}

extern bool devices_match(const char *id1, const char *id2);

static bool audio_monitor_init(struct audio_monitor *monitor,
			       obs_source_t *source)
{
	IMMDeviceEnumerator *immde = NULL;
	WAVEFORMATEX *wfex = NULL;
	bool success = false;
	UINT32 frames;
	HRESULT hr;

	pthread_mutex_init_value(&monitor->playback_mutex);

	monitor->source = source;

	const char *id = obs->audio.monitoring_device_id;
	if (!id) {
		return false;
	}

	if (source->info.output_flags & OBS_SOURCE_DO_NOT_SELF_MONITOR) {
		obs_data_t *s = obs_source_get_settings(source);
		const char *s_dev_id = obs_data_get_string(s, "device_id");
		bool match = devices_match(s_dev_id, id);
		obs_data_release(s);

		if (match) {
			monitor->ignore = true;
			return true;
		}
	}

	/* ------------------------------------------ *
	 * Init device                                */

	hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
			      &IID_IMMDeviceEnumerator, (void **)&immde);
	if (FAILED(hr)) {
		return false;
	}

	if (strcmp(id, "default") == 0) {
		hr = immde->lpVtbl->GetDefaultAudioEndpoint(
			immde, eRender, eConsole, &monitor->device);
	} else {
		wchar_t w_id[512];
		os_utf8_to_wcs(id, 0, w_id, 512);

		hr = immde->lpVtbl->GetDevice(immde, w_id, &monitor->device);
	}

	if (FAILED(hr)) {
		goto fail;
	}

	/* ------------------------------------------ *
	 * Init client                                */

	hr = monitor->device->lpVtbl->Activate(monitor->device,
					       &IID_IAudioClient, CLSCTX_ALL,
					       NULL, (void **)&monitor->client);
	if (FAILED(hr)) {
		goto fail;
	}

	hr = monitor->client->lpVtbl->GetMixFormat(monitor->client, &wfex);
	if (FAILED(hr)) {
		goto fail;
	}

	hr = monitor->client->lpVtbl->Initialize(monitor->client,
						 AUDCLNT_SHAREMODE_SHARED, 0,
						 10000000, 0, wfex, NULL);
	if (FAILED(hr)) {
		goto fail;
	}

	/* ------------------------------------------ *
	 * Init resampler                             */

	const struct audio_output_info *info = audio_output_get_info(obs->audio.audio);
	WAVEFORMATEXTENSIBLE *ext = (WAVEFORMATEXTENSIBLE *)wfex;
	struct resample_info from;
	struct resample_info to;

	from.samples_per_sec = info->samples_per_sec;
	from.speakers = info->speakers;
	from.format = AUDIO_FORMAT_FLOAT_PLANAR;

	to.samples_per_sec = (uint32_t)wfex->nSamplesPerSec;
	to.speakers = convert_speaker_layout(ext->dwChannelMask, wfex->nChannels);
	to.format = AUDIO_FORMAT_FLOAT;

	monitor->sample_rate = (uint32_t)wfex->nSamplesPerSec;
	monitor->speakers = to.speakers;
	monitor->channels = wfex->nChannels;
	monitor->resampler = audio_resampler_create(&to, &from);
	if (!monitor->resampler) {
		goto fail;
	}

	/* ------------------------------------------ *
	 * Init client                                */

	hr = monitor->client->lpVtbl->GetBufferSize(monitor->client, &frames);
	if (FAILED(hr)) {
		goto fail;
	}

	hr = monitor->client->lpVtbl->GetService(monitor->client,
			&IID_IAudioRenderClient, (void**)&monitor->render);
	if (FAILED(hr)) {
		goto fail;
	}

	if (pthread_mutex_init(&monitor->playback_mutex, NULL) != 0) {
		goto fail;
	}

	hr = monitor->client->lpVtbl->Start(monitor->client);
	if (FAILED(hr)) {
		goto fail;
	}

	success = true;

fail:
	safe_release(immde);
	if (wfex)
		CoTaskMemFree(wfex);
	return success;
}

static void audio_monitor_init_final(struct audio_monitor *monitor)
{
	if (monitor->ignore)
		return;
	// 判断是否是 obs_scene_t 的接口 => obs_scene_from_source(monitor->source);
	// 注意：这里保持原样，如果是scene下面的source，必然包含视频标志，仍然需要以视频为基础对音频进行同步处理...
	monitor->source_has_video = (monitor->source->info.output_flags & OBS_SOURCE_VIDEO) != 0;

	//bool source_has_video = (monitor->source->info.output_flags & OBS_SOURCE_VIDEO) != 0;
	//monitor->source_has_video = (obs_scene_from_source(monitor->source) ? false : source_has_video);

	obs_source_add_audio_capture_callback(monitor->source, on_audio_playback, monitor);
}

struct audio_monitor *audio_monitor_create(obs_source_t *source)
{
	struct audio_monitor monitor = {0};
	struct audio_monitor *out;

	if (!audio_monitor_init(&monitor, source)) {
		goto fail;
	}

	out = bmemdup(&monitor, sizeof(monitor));

	pthread_mutex_lock(&obs->audio.monitoring_mutex);
	da_push_back(obs->audio.monitors, &out);
	pthread_mutex_unlock(&obs->audio.monitoring_mutex);

	audio_monitor_init_final(out);
	return out;

fail:
	audio_monitor_free(&monitor);
	return NULL;
}

void audio_monitor_reset(struct audio_monitor *monitor)
{
	struct audio_monitor new_monitor = {0};
	bool success;

	pthread_mutex_lock(&monitor->playback_mutex);
	success = audio_monitor_init(&new_monitor, monitor->source);
	pthread_mutex_unlock(&monitor->playback_mutex);

	if (success) {
		obs_source_t *source = monitor->source;
		audio_monitor_free(monitor);
		*monitor = new_monitor;
		audio_monitor_init_final(monitor);
	} else {
		audio_monitor_free(&new_monitor);
	}
}

// 改进 => 必须整体互斥，否则，混音会发生问题...
void audio_monitor_destroy(struct audio_monitor *monitor)
{
	pthread_mutex_lock(&obs->audio.monitoring_mutex);
	if (monitor) {
		audio_monitor_free(monitor);
		da_erase_item(obs->audio.monitors, &monitor);
		bfree(monitor);
	}
	pthread_mutex_unlock(&obs->audio.monitoring_mutex);
}

/*static void doSaveAudioPCM(uint8_t * lpBufData, int nBufSize, int nAudioRate, int nAudioChannel)
{
	// 注意：PCM数据必须用二进制方式打开文件...
	char szFullPath[MAX_PATH] = { 0 };
	sprintf(szFullPath, "F:/MP4/PCM/horn_%d_%d_float.pcm", nAudioRate, nAudioChannel);
	FILE * lpFile = fopen(szFullPath, "ab+");
	// 打开文件成功，开始写入音频PCM数据内容...
	if (lpFile != NULL) {
		fwrite(lpBufData, nBufSize, 1, lpFile);
		fclose(lpFile);
	}
}*/

void scene_monitor_mix_play()
{
	struct audio_monitor * monitor_source = NULL;
	struct obs_core_audio * audio = &obs->audio;
	struct audio_monitor * monitor_scene = audio->monitor_scene;
	// 锁定所有的监视器对象...
	pthread_mutex_lock(&audio->monitoring_mutex);
	uint32_t nMaxFrameSize = 0;
	int  nCanMixCount = 0;
	// 如果没有监视器，直接返回...
	if (monitor_scene == NULL || audio->monitors.num <= 1)
		goto unlock;
	// 计算当前所有监视器中最大数据帧的长度...
	for (size_t i = 0; i < audio->monitors.num; ++i) {
		monitor_source = audio->monitors.array[i];
		// 如果是场景监视器自己，下一个...
		if (monitor_source == monitor_scene)
			continue;
		// 如果当前监视器处于静音状态，下一个...
		if (audio_monitor_is_muted(monitor_source))
			continue;
		// 计算当前所有监视器中最大数据帧的长度 => 所有监视器都要凑够这个数据量...
		nMaxFrameSize = max(monitor_source->max_frame_size, nMaxFrameSize);
	}
	// 如果最大数据帧长度无效，直接返回...
	if (nMaxFrameSize <= 0)
		goto unlock;
	// 对最大混音缓存进行重置操作...
	size_t total_size = AUDIO_OUTPUT_FRAMES * MAX_AUDIO_CHANNELS * sizeof(float) * 2;
	memset(audio->monitoring_mix, 0, total_size);
	// 必须等待所有通道的监视器都有足够混音的数据...
	for (size_t i = 0; i < audio->monitors.num; ++i) {
		monitor_source = audio->monitors.array[i];
		// 如果是场景监视器自己，下一个...
		if (monitor_source == monitor_scene)
			continue;
		// 如果当前监视器处于静音状态，下一个...
		if (audio_monitor_is_muted(monitor_source))
			continue;
		// 如果当前监视器不能混音，直接返回...
		if (!audio_monitor_can_mix(monitor_source, nMaxFrameSize))
			goto unlock;
		// 对当前监视器进行正常的混音处理...
		audio_monitor_mixer(monitor_source, audio->monitoring_mix, nMaxFrameSize);
		// 没有静音并且可以混音 => 累加计数器...
		++nCanMixCount;
	}
	// 如果混音计数器无效，直接返回...
	if (nCanMixCount <= 0)
		goto unlock;
	// 混合完毕，使用最后的监视器进行本地播放...
	// 注意：所有的监视器都是相同的声道和采样率...
	audio_monitor_play(monitor_scene, audio->monitoring_mix, nMaxFrameSize);
unlock:
	pthread_mutex_unlock(&audio->monitoring_mutex);
}

bool audio_monitor_is_muted(struct audio_monitor *monitor)
{
	return (monitor ? (monitor->is_muted || !monitor->is_on) : true);
}

// 这一步非常重要，需要预先判断该通道能否混音...
bool audio_monitor_can_mix(struct audio_monitor *monitor, uint32_t inMaxFrameSize)
{
	if (monitor == NULL) return false;
	pthread_mutex_lock(&monitor->playback_mutex);
	size_t channels = monitor->channels;
	size_t play_size = monitor->play_buffer.size;
	size_t audio_size = inMaxFrameSize * channels * sizeof(float);
	pthread_mutex_unlock(&monitor->playback_mutex);
	return ((play_size < audio_size) ? false : true);
}

// 混音前必须等待所有监视通道的声音准备完毕才能操作，否则，会出现断断续续的问题...
void audio_monitor_mixer(struct audio_monitor *monitor, float *p_out, uint32_t inMaxFrameSize)
{
	if (monitor == NULL || p_out == NULL)
		return;
	// 如果当前通道处于静音状态，直接返回...
	if (monitor->is_muted)
		return;
	size_t channels = monitor->channels;
	size_t audio_size = inMaxFrameSize * channels * sizeof(float);
	// 如果播放缓存不够需要的长度，返回失败...
	pthread_mutex_lock(&monitor->playback_mutex);
	if (monitor->play_buffer.size < audio_size) {
		pthread_mutex_unlock(&monitor->playback_mutex);
		return;
	}
	// 保证临时缓存有效，并从环形队列提取...
	da_resize(monitor->buf_play, audio_size);
	circlebuf_pop_front(&monitor->play_buffer, monitor->buf_play.array, audio_size);
	// 释放监视器的播放互斥对象 => 缓存已经发生了转移...
	//blog(LOG_DEBUG, "== monitor max-calc: %d, max-self: %d, play-size: %d ==",
	//	 inMaxFrameSize, monitor->max_frame_size, monitor->play_buffer.size/(channels*sizeof(float)));
	pthread_mutex_unlock(&monitor->playback_mutex);
	float * p_in = monitor->buf_play.array;
	register float *out = p_out;
	register float *in = p_in + 0;
	register float *end = in + audio_size;
	// 对音频进行混音处理...
	while (in < end) {
		*(out++) += *(in++);
	}
}

void audio_monitor_play(struct audio_monitor *monitor, float *p_mix, uint32_t inMaxFrameSize)
{
	if (monitor == NULL || p_mix == NULL)
		return;
	UINT32 pad = 0;
	BYTE * output = NULL;
	size_t channels = monitor->channels;
	size_t resample_frames = inMaxFrameSize;
	size_t audio_size = resample_frames * channels * sizeof(float);
	IAudioRenderClient *render = monitor->render;
	monitor->client->lpVtbl->GetCurrentPadding(monitor->client, &pad);
	// 播放内部遗留缓存超过 75 毫秒，就不能继续投递数据，直接丢弃，继续使用已投递播放缓存里面的数据...
	int64_t pad_ns_buff = (uint64_t)pad * 1000000000ULL / (uint64_t)monitor->sample_rate;
	if (pad_ns_buff >= 75 * 1000000) {
		blog(LOG_DEBUG, "== monitor play, pad_ns_buff: %llu, pad: %lu ==", pad_ns_buff, pad);
		return;
	}
	// 获取投递缓存的头指针 => 播放器内部会分配内存空间...
	HRESULT hr = render->lpVtbl->GetBuffer(render, resample_frames, &output);
	if (FAILED(hr))
		return;
	// 将混音后的数据投递到扬声器当中...
	memcpy(output, p_mix, audio_size);
	// 构造需要投递给麦克风数据源的结构体 => 样本总是float格式...
	struct obs_source_audio theEchoData = { 0 };
	theEchoData.data[0] = (uint8_t*)p_mix;
	theEchoData.frames = resample_frames;
	theEchoData.format = AUDIO_FORMAT_FLOAT;
	theEchoData.speakers = monitor->speakers;
	theEchoData.samples_per_sec = monitor->sample_rate;
	theEchoData.timestamp = os_gettime_ns();
	// 注意：采用的是归一法 => 将所有监视音频混音，统一播放...
	// 枚举所有的音频数据源对象，找到麦克风，进行回音消除...
	doPushEchoDataToMic(&theEchoData);
	// 释放扬声器缓存 => 这时，硬件设备才真正开始播放...
	render->lpVtbl->ReleaseBuffer(render, resample_frames, 0);
}
