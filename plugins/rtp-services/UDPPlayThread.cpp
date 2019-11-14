
#include "UDPPlayThread.h"
#include "BitWritter.h"
#include "HYDefine.h"

static enum AVPixelFormat closest_format(enum AVPixelFormat fmt)
{
	switch (fmt) {
	case AV_PIX_FMT_YUYV422:
		return AV_PIX_FMT_YUYV422;

	case AV_PIX_FMT_YUV422P:
	case AV_PIX_FMT_YUVJ422P:
	case AV_PIX_FMT_UYVY422:
	case AV_PIX_FMT_YUV422P16LE:
	case AV_PIX_FMT_YUV422P16BE:
	case AV_PIX_FMT_YUV422P10BE:
	case AV_PIX_FMT_YUV422P10LE:
	case AV_PIX_FMT_YUV422P9BE:
	case AV_PIX_FMT_YUV422P9LE:
	case AV_PIX_FMT_YVYU422:
	case AV_PIX_FMT_YUV422P12BE:
	case AV_PIX_FMT_YUV422P12LE:
	case AV_PIX_FMT_YUV422P14BE:
	case AV_PIX_FMT_YUV422P14LE:
		return AV_PIX_FMT_UYVY422;

	case AV_PIX_FMT_NV12:
	case AV_PIX_FMT_NV21:
		return AV_PIX_FMT_NV12;

	case AV_PIX_FMT_YUV420P:
	case AV_PIX_FMT_YUVJ420P:
	case AV_PIX_FMT_YUV411P:
	case AV_PIX_FMT_UYYVYY411:
	case AV_PIX_FMT_YUV410P:
	case AV_PIX_FMT_YUV420P16LE:
	case AV_PIX_FMT_YUV420P16BE:
	case AV_PIX_FMT_YUV420P9BE:
	case AV_PIX_FMT_YUV420P9LE:
	case AV_PIX_FMT_YUV420P10BE:
	case AV_PIX_FMT_YUV420P10LE:
	case AV_PIX_FMT_YUV420P12BE:
	case AV_PIX_FMT_YUV420P12LE:
	case AV_PIX_FMT_YUV420P14BE:
	case AV_PIX_FMT_YUV420P14LE:
		return AV_PIX_FMT_YUV420P;

	case AV_PIX_FMT_RGBA:
	case AV_PIX_FMT_BGRA:
	case AV_PIX_FMT_BGR0:
		return fmt;

	default:
		break;
	}

	return AV_PIX_FMT_BGRA;
}

static inline enum video_format convert_pixel_format(int f)
{
	switch (f) {
	case AV_PIX_FMT_NONE:    return VIDEO_FORMAT_NONE;
	case AV_PIX_FMT_YUV420P: return VIDEO_FORMAT_I420;
	case AV_PIX_FMT_NV12:    return VIDEO_FORMAT_NV12;
	case AV_PIX_FMT_YUYV422: return VIDEO_FORMAT_YUY2;
	case AV_PIX_FMT_UYVY422: return VIDEO_FORMAT_UYVY;
	case AV_PIX_FMT_RGBA:    return VIDEO_FORMAT_RGBA;
	case AV_PIX_FMT_BGRA:    return VIDEO_FORMAT_BGRA;
	case AV_PIX_FMT_BGR0:    return VIDEO_FORMAT_BGRX;
	default:;
	}

	return VIDEO_FORMAT_NONE;
}

static inline enum video_colorspace convert_color_space(enum AVColorSpace s)
{
	return s == AVCOL_SPC_BT709 ? VIDEO_CS_709 : VIDEO_CS_DEFAULT;
}

static inline enum video_range_type convert_color_range(enum AVColorRange r)
{
	return r == AVCOL_RANGE_JPEG ? VIDEO_RANGE_FULL : VIDEO_RANGE_DEFAULT;
}

static inline enum audio_format convert_sample_format(int f)
{
	switch (f) {
	case AV_SAMPLE_FMT_U8:   return AUDIO_FORMAT_U8BIT;
	case AV_SAMPLE_FMT_S16:  return AUDIO_FORMAT_16BIT;
	case AV_SAMPLE_FMT_S32:  return AUDIO_FORMAT_32BIT;
	case AV_SAMPLE_FMT_FLT:  return AUDIO_FORMAT_FLOAT;
	case AV_SAMPLE_FMT_U8P:  return AUDIO_FORMAT_U8BIT_PLANAR;
	case AV_SAMPLE_FMT_S16P: return AUDIO_FORMAT_16BIT_PLANAR;
	case AV_SAMPLE_FMT_S32P: return AUDIO_FORMAT_32BIT_PLANAR;
	case AV_SAMPLE_FMT_FLTP: return AUDIO_FORMAT_FLOAT_PLANAR;
	default:;
	}

	return AUDIO_FORMAT_UNKNOWN;
}

static inline enum speaker_layout convert_speaker_layout(uint8_t channels)
{
	switch (channels) {
	case 0:     return SPEAKERS_UNKNOWN;
	case 1:     return SPEAKERS_MONO;
	case 2:     return SPEAKERS_STEREO;
	case 3:     return SPEAKERS_2POINT1;
	case 4:     return SPEAKERS_4POINT0;
	case 5:     return SPEAKERS_4POINT1;
	case 6:     return SPEAKERS_5POINT1;
	case 8:     return SPEAKERS_7POINT1;
	default:    return SPEAKERS_UNKNOWN;
	}
}

CDecoder::CDecoder()
  : m_lpCodec(NULL)
  , m_lpDFrame(NULL)
  , m_lpDecoder(NULL)
  , m_lpPlaySDL(NULL)
  , m_play_next_ns(-1)
  , m_bNeedSleep(false)
  , m_Mutex(NULL)
{
	pthread_mutex_init_value(&m_Mutex);
}

CDecoder::~CDecoder()
{
	// 释放解码结构体...
	if( m_lpDFrame != NULL ) {
		av_frame_free(&m_lpDFrame);
		m_lpDFrame = NULL;
	}
	// 释放解码器对象...
	if( m_lpDecoder != NULL ) {
		avcodec_close(m_lpDecoder);
		av_free(m_lpDecoder);
	}
	// 释放队列中的解码前的数据块...
	GM_MapPacket::iterator itorPack;
	for(itorPack = m_MapPacket.begin(); itorPack != m_MapPacket.end(); ++itorPack) {
		av_packet_unref(&itorPack->second);
	}
	m_MapPacket.clear();
	// 释放没有播放完毕的解码后的数据帧...
	GM_MapFrame::iterator itorFrame;
	for(itorFrame = m_MapFrame.begin(); itorFrame != m_MapFrame.end(); ++itorFrame) {
		av_frame_free(&itorFrame->second);
	}
	m_MapFrame.clear();
	// 释放互斥对象...
	pthread_mutex_destroy(&m_Mutex);
}

void CDecoder::doPushPacket(AVPacket & inPacket)
{
	// 注意：这里必须以DTS排序，决定了解码的先后顺序...
	// 注意：由于使用multimap，可以专门处理相同时间戳...
	m_MapPacket.insert(pair<int64_t, AVPacket>(inPacket.dts, inPacket));
}

void CDecoder::doSleepTo()
{
	// < 0 不能休息，有不能休息标志 => 都直接返回...
	if( !m_bNeedSleep || m_play_next_ns < 0 )
		return;
	// 计算要休息的时间 => 最大休息毫秒数...
	uint64_t cur_time_ns = os_gettime_ns();
	const uint64_t timeout_ns = MAX_SLEEP_MS * 1000000;
	// 如果比当前时间小(已过期)，直接返回...
	if( (uint64_t)m_play_next_ns <= cur_time_ns )
		return;
	// 计算超前时间的差值，最多休息10毫秒...
	uint64_t delta_ns = m_play_next_ns - cur_time_ns;
	delta_ns = ((delta_ns >= timeout_ns) ? timeout_ns : delta_ns);
	// 调用系统工具函数，进行sleep休息...
	os_sleepto_ns(cur_time_ns + delta_ns);
}

CVideoThread::CVideoThread(CPlaySDL * lpPlaySDL)
  : m_nDstHeight(0)
  , m_nDstWidth(0)
  , m_nDstFPS(0)
{
	m_lpPlaySDL = lpPlaySDL;
	memset(&m_obs_frame, 0, sizeof(m_obs_frame));
}

CVideoThread::~CVideoThread()
{
	//blog(LOG_INFO, "%s == [~CVideoThread] - Exit Start ==", m_lpPlaySDL->GetInnerName().c_str());
	this->StopAndWaitForThread();
	//blog(LOG_INFO, "%s == [~CVideoThread] - Exit End ==", m_lpPlaySDL->GetInnerName().c_str());
}

BOOL CVideoThread::InitVideo(string & inSPS, string & inPPS, int nWidth, int nHeight, int nFPS)
{
	// 如果已经初始化，直接返回...
	if( m_lpCodec != NULL || m_lpDecoder != NULL )
		return false;
	ASSERT( m_lpCodec == NULL && m_lpDecoder == NULL );
	// 保存传递过来的参数信息...
	m_nDstHeight = nHeight;
	m_nDstWidth = nWidth;
	m_nDstFPS = nFPS;
	m_strSPS = inSPS;
	m_strPPS = inPPS;
	// 初始化ffmpeg解码器...
	av_register_all();
	// 准备一些特定的参数...
	AVCodecID src_codec_id = AV_CODEC_ID_H264;
	// 查找需要的解码器 => 不用创建解析器...
	m_lpCodec = avcodec_find_decoder(src_codec_id);
	m_lpDecoder = avcodec_alloc_context3(m_lpCodec);
	// 设置支持低延时模式 => 没啥作用，必须配合具体过程...
	m_lpDecoder->flags |= CODEC_FLAG_LOW_DELAY;
	// 设置支持不完整片段解码 => 没啥作用...
	if( m_lpCodec->capabilities & CODEC_CAP_TRUNCATED ) {
		m_lpDecoder->flags |= CODEC_FLAG_TRUNCATED;
	}
	// 设置解码器关联参数配置 => 这里设置不起作用...
	//m_lpDecoder->refcounted_frames = 1;
	//m_lpDecoder->has_b_frames = 0;
	// 打开获取到的解码器...
	if( avcodec_open2(m_lpDecoder, m_lpCodec, NULL) < 0 ) {
		blog(LOG_INFO, "%s [Video] avcodec_open2 failed.", m_lpPlaySDL->GetInnerName().c_str());
		return false;
	}
	// 准备一个全局的解码结构体 => 解码数据帧是相互关联的...
	m_lpDFrame = av_frame_alloc();
	ASSERT( m_lpDFrame != NULL );
	// 启动线程开始运转...
	this->Start();
	return true;
}

void CVideoThread::Entry()
{
	while( !this->IsStopRequested() ) {
		// 设置休息标志 => 只要有解码或播放就不能休息...
		m_bNeedSleep = true;
		// 解码一帧视频...
		this->doDecodeFrame();
		// 显示一帧视频...
		this->doDisplayFrame();
		// 进行sleep等待...
		this->doSleepTo();
	}
}

void CVideoThread::doFillPacket(string & inData, int inPTS, bool bIsKeyFrame, int inOffset)
{
	// 线程正在退出中，直接返回...
	if( this->IsStopRequested() )
		return;
	// 每个关键帧都放入sps和pps，播放会加快...
	string strCurFrame;
	// 如果是关键帧，必须先写入sps，再写如pps...
	if( bIsKeyFrame ) {
		DWORD dwTag = 0x01000000;
		strCurFrame.append((char*)&dwTag, sizeof(DWORD));
		strCurFrame.append(m_strSPS);
		strCurFrame.append((char*)&dwTag, sizeof(DWORD));
		strCurFrame.append(m_strPPS);
	}
	// 修改视频帧的起始码 => 0x00000001
	char * lpszSrc = (char*)inData.c_str();
	memset((void*)lpszSrc, 0, sizeof(DWORD));
	lpszSrc[3] = 0x01;
	// 最后追加新数据...
	strCurFrame.append(inData);
	// 构造一个临时AVPacket，并存入帧数据内容...
	AVPacket  theNewPacket = {0};
	av_new_packet(&theNewPacket, strCurFrame.size());
	ASSERT(theNewPacket.size == strCurFrame.size());
	memcpy(theNewPacket.data, strCurFrame.c_str(), theNewPacket.size);
	// 计算当前帧的PTS，关键帧标志 => 视频流 => 1
	// 目前只有I帧和P帧，PTS和DTS是一致的...
	theNewPacket.pts = inPTS;
	theNewPacket.dts = inPTS - inOffset;
	theNewPacket.flags = bIsKeyFrame;
	theNewPacket.stream_index = 1;
	// 将数据压入解码前队列当中...
	pthread_mutex_lock(&m_Mutex);
	this->doPushPacket(theNewPacket);
	pthread_mutex_unlock(&m_Mutex);
}
//
// 取出一帧解码后的视频，比对系统时间，看看能否播放...
void CVideoThread::doDisplayFrame()
{
	// 如果没有已解码数据帧，直接返回休息最大毫秒数...
	if( m_MapFrame.size() <= 0 ) {
		m_play_next_ns = os_gettime_ns() + MAX_SLEEP_MS * 1000000;
		return;
	}
	// 这是为了测试原始PTS而获取的初始PTS值 => 只在打印调试信息时使用...
	int64_t inStartPtsMS = m_lpPlaySDL->GetStartPtsMS();
	// 计算当前时刻点与系统0点位置的时间差 => 转换成毫秒...
	int64_t sys_cur_ms = (os_gettime_ns() - m_lpPlaySDL->GetSysZeroNS())/1000000;
	// 累加人为设定的延时毫秒数 => 相减...
	sys_cur_ms -= m_lpPlaySDL->GetZeroDelayMS();
	// 取出第一个已解码数据帧 => 时间最小的数据帧...
	GM_MapFrame::iterator itorItem = m_MapFrame.begin();
	obs_source_frame * lpObsFrame = &m_obs_frame;
	AVFrame * lpSrcFrame = itorItem->second;
	int64_t   frame_pts_ms = itorItem->first;
	// 当前帧的显示时间还没有到 => 直接休息差值...
	if( frame_pts_ms > sys_cur_ms ) {
		m_play_next_ns = os_gettime_ns() + (frame_pts_ms - sys_cur_ms)*1000000;
		//blog(LOG_INFO, "%s [Video] Advance => PTS: %I64d, Delay: %I64d ms, VPackSize: %d, VFrameSize: %d",
		//     m_lpPlaySDL->GetInnerName().c_str(), frame_pts_ms + inStartPtsMS, frame_pts_ms - sys_cur_ms, m_MapPacket.size(), m_MapFrame.size());
		return;
	}
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：视频延时帧（落后帧），不能丢弃，必须继续显示，视频消耗速度相对较快，除非时间戳给错了，会造成播放问题。
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 将数据转换成jpg...
	//DoProcSaveJpeg(lpSrcFrame, m_lpDecoder->pix_fmt, frame_pts, "F:/MP4/Dst");
	// 打印正在播放的解码后视频数据...
	//blog(LOG_INFO, "%s [Video] Player => PTS: %I64d ms, Delay: %I64d ms, AVPackSize: %d, AVFrameSize: %d",
	//     m_lpPlaySDL->GetInnerName().c_str(), frame_pts_ms + inStartPtsMS, sys_cur_ms - frame_pts_ms, m_MapPacket.size(), m_MapFrame.size());
	// 判断视频数据是否需要翻转处理...
	bool bFlip = ((lpSrcFrame->linesize[0] < 0) && (lpSrcFrame->linesize[1] == 0));
	// 将数据指针复制到obs结构体当中...
	for (size_t i = 0; i < MAX_AV_PLANES; i++) {
		lpObsFrame->data[i] = lpSrcFrame->data[i];
		lpObsFrame->linesize[i] = abs(lpSrcFrame->linesize[i]);
	}
	// 对视频数据进行翻转处理...
	if ( bFlip ) {
		lpObsFrame->data[0] -= lpObsFrame->linesize[0] * (lpSrcFrame->height - 1);
	}
	// 对视频数据进行格式转换...
	bool bSuccess = false;
	enum video_format new_format = convert_pixel_format(closest_format((AVPixelFormat)lpSrcFrame->format));
	enum video_colorspace new_space = convert_color_space(lpSrcFrame->colorspace);
	enum video_range_type new_range = convert_color_range(lpSrcFrame->color_range);
	if (new_format != lpObsFrame->format) {
		lpObsFrame->format = new_format;
		lpObsFrame->full_range = (new_range == VIDEO_RANGE_FULL);
		bSuccess = video_format_get_parameters(new_space, new_range, lpObsFrame->color_matrix, lpObsFrame->color_range_min, lpObsFrame->color_range_max);
		lpObsFrame->format = new_format;
	}
	// 设置obs数据帧的长宽和翻转标志...
	lpObsFrame->flip = bFlip;
	lpObsFrame->width = lpSrcFrame->width;
	lpObsFrame->height = lpSrcFrame->height;
	// 设置obs数据帧的时间戳 => 将毫秒转换成纳秒...
	AVRational base_pack = { 1, 1000 };
	AVRational base_frame = { 1, 1000000000 };
	lpObsFrame->timestamp = av_rescale_q(frame_pts_ms, base_pack, base_frame);
	// 将填充后的obs数据帧，进行数据投递工作...
	obs_source_output_video(m_lpPlaySDL->GetObsSource(), lpObsFrame);
	// 释放并删除已经使用完毕原始数据块...
	av_frame_free(&lpSrcFrame);
	m_MapFrame.erase(itorItem);
	// 修改休息状态 => 已经有播放，不能休息...
	m_bNeedSleep = false;
}

#ifdef DEBUG_DECODE
static void DoSaveLocFile(AVFrame * lpAVFrame, bool bError, AVPacket & inPacket)
{
	static char szBuf[MAX_PATH] = {0};
	char * lpszPath = "F:/MP4/Src/loc.obj";
	FILE * pFile = fopen(lpszPath, "a+");
	if( bError ) {
		fwrite(&inPacket.pts, 1, sizeof(int64_t), pFile);
		fwrite(inPacket.data, 1, inPacket.size, pFile);
	} else {
		fwrite(&lpAVFrame->best_effort_timestamp, 1, sizeof(int64_t), pFile);
		for(int i = 0; i < AV_NUM_DATA_POINTERS; ++i) {
			if( lpAVFrame->data[i] != NULL && lpAVFrame->linesize[i] > 0 ) {
				fwrite(lpAVFrame->data[i], 1, lpAVFrame->linesize[i], pFile);
			}
		}
	}
	fclose(pFile);
}
static void DoSaveNetFile(AVFrame * lpAVFrame, bool bError, AVPacket & inPacket)
{
	static char szBuf[MAX_PATH] = {0};
	char * lpszPath = "F:/MP4/Src/net.obj";
	FILE * pFile = fopen(lpszPath, "a+");
	if( bError ) {
		fwrite(&inPacket.pts, 1, sizeof(int64_t), pFile);
		fwrite(inPacket.data, 1, inPacket.size, pFile);
	} else {
		fwrite(&lpAVFrame->best_effort_timestamp, 1, sizeof(int64_t), pFile);
		for(int i = 0; i < AV_NUM_DATA_POINTERS; ++i) {
			if( lpAVFrame->data[i] != NULL && lpAVFrame->linesize[i] > 0 ) {
				fwrite(lpAVFrame->data[i], 1, lpAVFrame->linesize[i], pFile);
			}
		}
	}
	fclose(pFile);
}
#endif // DEBUG_DECODE

void CVideoThread::doDecodeFrame()
{
	// 没有要解码的数据包，直接返回最大休息毫秒数...
	if( m_MapPacket.size() <= 0 ) {
		m_play_next_ns = os_gettime_ns() + MAX_SLEEP_MS * 1000000;
		return;
	}
	// AVPacket互斥对象保护...
	pthread_mutex_lock(&m_Mutex);
	// 这是为了测试原始PTS而获取的初始PTS值 => 只在打印调试信息时使用...
	int64_t inStartPtsMS = m_lpPlaySDL->GetStartPtsMS();
	// 抽取一个AVPacket进行解码操作，理论上一个AVPacket一定能解码出一个Picture...
	// 由于数据丢失或B帧，投递一个AVPacket不一定能返回Picture，这时解码器就会将数据缓存起来，造成解码延时...
	int got_picture = 0, nResult = 0;
	GM_MapPacket::iterator itorItem = m_MapPacket.begin();
	AVPacket & thePacket = itorItem->second;
	//////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：由于只有 I帧 和 P帧，没有B帧，要让解码器快速丢掉解码错误数据，通过设置has_b_frames来完成
	// 技术文档 => https://bbs.csdn.net/topics/390692774
	//////////////////////////////////////////////////////////////////////////////////////////////////
	m_lpDecoder->has_b_frames = 0;
	// 将完整压缩数据帧放入解码器进行解码操作...
	nResult = avcodec_decode_video2(m_lpDecoder, m_lpDFrame, &got_picture, &thePacket);
	/////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：目前只有 I帧 和 P帧  => 这里使用全局AVFrame，非常重要，能提供后续AVPacket的解码支持...
	// 解码失败或没有得到完整图像 => 是需要后续AVPacket的数据才能解码出图像...
	// 非常关键的操作 => m_lpDFrame 千万不能释放，继续灌AVPacket就能解码出图像...
	/////////////////////////////////////////////////////////////////////////////////////////////////
	if( nResult < 0 || !got_picture ) {
		// 打印解码失败信息，显示坏帧的个数...
		blog(LOG_INFO, "%s [Video] Error => decode_frame failed, BFrame: %d, PTS: %I64d, DecodeSize: %d, PacketSize: %d", 
			m_lpPlaySDL->GetInnerName().c_str(), m_lpDecoder->has_b_frames, thePacket.pts + inStartPtsMS, nResult, thePacket.size);
		// 这里非常关键，告诉解码器不要缓存坏帧(B帧)，一旦有解码错误，直接扔掉，这就是低延时解码模式...
		m_lpDecoder->has_b_frames = 0;
		// 丢掉解码失败的数据帧...
		av_packet_unref(&thePacket);
		m_MapPacket.erase(itorItem);
		pthread_mutex_unlock(&m_Mutex);
		return;
	}
	// 打印解码之后的数据帧信息...
	//blog( LOG_INFO, "%s [Video] Decode => BFrame: %d, PTS: %I64d, pkt_dts: %I64d, pkt_pts: %I64d, Type: %d, DecodeSize: %d, PacketSize: %d", m_lpDecoder->has_b_frames, m_lpPlaySDL->GetInnerName().c_str(),
	//		m_lpDFrame->best_effort_timestamp + inStartPtsMS, m_lpDFrame->pkt_dts + inStartPtsMS, m_lpDFrame->pkt_pts + inStartPtsMS, m_lpDFrame->pict_type, nResult, thePacket.size );
	///////////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：这里使用AVFrame计算的最佳有效时间戳，使用AVPacket的时间戳也是一样的...
	// 因为，采用了低延时的模式，坏帧都扔掉了，解码器内部没有缓存数据，不会出现时间戳错位问题...
	///////////////////////////////////////////////////////////////////////////////////////////////////////
	int64_t frame_pts_ms = m_lpDFrame->best_effort_timestamp;
	// 重新克隆AVFrame，自动分配空间，按时间排序...
	AVFrame * lpNewFrame = av_frame_clone(m_lpDFrame);
	m_MapFrame.insert(pair<int64_t, AVFrame*>(frame_pts_ms, lpNewFrame));
	//DoProcSaveJpeg(m_lpDFrame, m_lpDecoder->pix_fmt, frame_pts, "F:/MP4/Src");
	// 这里是引用，必须先free再erase...
	av_packet_unref(&thePacket);
	m_MapPacket.erase(itorItem);
	pthread_mutex_unlock(&m_Mutex);
	// 修改休息状态 => 已经有解码，不能休息...
	m_bNeedSleep = false;
}

CAudioThread::CAudioThread(CPlaySDL * lpPlaySDL)
{
	m_lpPlaySDL = lpPlaySDL;
	m_audio_sample_rate = 0;
	m_audio_rate_index = 0;
	m_audio_channel_num = 0;
}

CAudioThread::~CAudioThread()
{
	//blog(LOG_INFO, "%s == [~CAudioThread] - Exit Start ==", TM_RECV_NAME);
	this->StopAndWaitForThread();
	//blog(LOG_INFO, "%s == [~CAudioThread] - Exit End ==", TM_RECV_NAME);
}

void CAudioThread::doDecodeFrame()
{
	// 没有要解码的数据包，直接返回最大休息毫秒数...
	if( m_MapPacket.size() <= 0 ) {
		m_play_next_ns = os_gettime_ns() + MAX_SLEEP_MS * 1000000;
		return;
	}
	pthread_mutex_lock(&m_Mutex);
	// 这是为了测试原始PTS而获取的初始PTS值 => 只在打印调试信息时使用...
	int64_t inStartPtsMS = m_lpPlaySDL->GetStartPtsMS();
	// 抽取一个AVPacket进行解码操作，一个AVPacket不一定能解码出一个Picture...
	int got_picture = 0, nResult = 0;
	GM_MapPacket::iterator itorItem = m_MapPacket.begin();
	AVPacket & thePacket = itorItem->second;
	// 注意：这里解码后的格式是4bit，需要转换成16bit，调用swr_convert
	nResult = avcodec_decode_audio4(m_lpDecoder, m_lpDFrame, &got_picture, &thePacket);
	////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：这里使用全局AVFrame，非常重要，能提供后续AVPacket的解码支持...
	// 解码失败或没有得到完整图像 => 是需要后续AVPacket的数据才能解码出图像...
	// 非常关键的操作 => m_lpDFrame 千万不能释放，继续灌AVPacket就能解码出图像...
	////////////////////////////////////////////////////////////////////////////////////////////////
	if( nResult < 0 || !got_picture ) {
		blog(LOG_INFO, "%s [Audio] Error => decode_audio failed, PTS: %I64d, DecodeSize: %d, PacketSize: %d", 
			m_lpPlaySDL->GetInnerName().c_str(), thePacket.pts + inStartPtsMS, nResult, thePacket.size);
		av_packet_unref(&thePacket);
		m_MapPacket.erase(itorItem);
		pthread_mutex_unlock(&m_Mutex);
		return;
	}
	// 打印解码之后的数据帧信息...
	//blog(LOG_INFO, "%s [Audio] Decode => PTS: %I64d, pkt_dts: %I64d, pkt_pts: %I64d, Type: %d, DecodeSize: %d, PacketSize: %d", m_lpPlaySDL->GetInnerName().c_str(),
	//		m_lpDFrame->best_effort_timestamp + inStartPtsMS, m_lpDFrame->pkt_dts + inStartPtsMS, m_lpDFrame->pkt_pts + inStartPtsMS,
	//		m_lpDFrame->pict_type, nResult, thePacket.size );
	////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：这里使用AVFrame计算的最佳有效时间戳，使用AVPacket的时间戳也是一样的...
	// 因为，采用了低延时的模式，坏帧都扔掉了，解码器内部没有缓存数据，不会出现时间戳错位问题...
	////////////////////////////////////////////////////////////////////////////////////////////
	int64_t frame_pts_ms = m_lpDFrame->best_effort_timestamp;
	// 重新克隆AVFrame，自动分配空间，按时间排序...
	AVFrame * lpNewFrame = av_frame_clone(m_lpDFrame);
	m_MapFrame.insert(pair<int64_t, AVFrame*>(frame_pts_ms, lpNewFrame));
	// 这里是引用，必须先free再erase...
	av_packet_unref(&thePacket);
	m_MapPacket.erase(itorItem);
	pthread_mutex_unlock(&m_Mutex);
	// 修改休息状态 => 已经有解码，不能休息...
	m_bNeedSleep = false;
}

void CAudioThread::doDisplayFrame()
{
	// 如果没有已解码数据帧，直接返回休息最大毫秒数...
	if (m_MapFrame.size() <= 0) {
		m_play_next_ns = os_gettime_ns() + MAX_SLEEP_MS * 1000000;
		return;
	}
	// 这是为了测试原始PTS而获取的初始PTS值 => 只在打印调试信息时使用...
	int64_t inStartPtsMS = m_lpPlaySDL->GetStartPtsMS();
	// 计算当前时刻点与系统0点位置的时间差 => 转换成毫秒...
	int64_t sys_cur_ms = (os_gettime_ns() - m_lpPlaySDL->GetSysZeroNS()) / 1000000;
	// 累加人为设定的延时毫秒数 => 相减...
	sys_cur_ms -= m_lpPlaySDL->GetZeroDelayMS();
	// 取出第一个已解码数据帧 => 时间最小的数据帧...
	GM_MapFrame::iterator itorItem = m_MapFrame.begin();
	obs_source_audio theObsAudio = { 0 };
	AVFrame * lpSrcFrame = itorItem->second;
	int64_t   frame_pts_ms = itorItem->first;
	// 当前帧的显示时间还没有到 => 直接休息差值...
	if (frame_pts_ms > sys_cur_ms) {
		m_play_next_ns = os_gettime_ns() + (frame_pts_ms - sys_cur_ms) * 1000000;
		//blog(LOG_INFO, "%s [Audio] Advance => PTS: %I64d, Delay: %I64d ms, APackSize: %d, AFrameSize: %d", 
		//     m_lpPlaySDL->GetInnerName().c_str(), frame_pts_ms + inStartPtsMS, frame_pts_ms - sys_cur_ms, m_MapPacket.size(), m_MapFrame.size());
		return;
	}
	// 打印正在播放的解码后音频数据...
	//blog(LOG_INFO, "%s [Audio] Player => StartPTS: %I64d ms, PTS: %I64d ms, Delay: %I64d ms, APackSize: %d, AFrameSize: %d", 
	//     m_lpPlaySDL->GetInnerName().c_str(), inStartPtsMS, frame_pts_ms, sys_cur_ms - frame_pts_ms, m_MapPacket.size(), m_MapFrame.size());
	// 将音频数据指针复制到obs结构体当中...
	for (size_t i = 0; i < MAX_AV_PLANES; i++) {
		theObsAudio.data[i] = lpSrcFrame->data[i];
	}
	// 计算并设置音频相关信息 => 可以控制播放速度...
	theObsAudio.samples_per_sec = lpSrcFrame->sample_rate; // *m->speed / 100;
	theObsAudio.speakers = convert_speaker_layout(lpSrcFrame->channels);
	theObsAudio.format = convert_sample_format(lpSrcFrame->format);
	theObsAudio.frames = lpSrcFrame->nb_samples;
	// 设置obs数据帧的时间戳 => 将毫秒转换成纳秒...
	AVRational base_pack = { 1, 1000 };
	AVRational base_frame = { 1, 1000000000 };
	theObsAudio.timestamp = av_rescale_q(frame_pts_ms, base_pack, base_frame);
	// 将填充后的obs数据帧，进行数据投递工作...
	obs_source_output_audio(m_lpPlaySDL->GetObsSource(), &theObsAudio);
	// 释放并删除已经使用完毕原始数据块...
	av_frame_free(&lpSrcFrame);
	m_MapFrame.erase(itorItem);
	// 修改休息状态 => 已经有播放，不能休息...
	m_bNeedSleep = false;
}

BOOL CAudioThread::InitAudio(int nRateIndex, int nChannelNum)
{
	// 如果已经初始化，直接返回...
	if( m_lpCodec != NULL || m_lpDecoder != NULL )
		return false;
	ASSERT( m_lpCodec == NULL && m_lpDecoder == NULL );
	// 根据索引获取采样率...
	switch( nRateIndex )
	{
	case 0x03: m_audio_sample_rate = 48000; break;
	case 0x04: m_audio_sample_rate = 44100; break;
	case 0x05: m_audio_sample_rate = 32000; break;
	case 0x06: m_audio_sample_rate = 24000; break;
	case 0x07: m_audio_sample_rate = 22050; break;
	case 0x08: m_audio_sample_rate = 16000; break;
	case 0x09: m_audio_sample_rate = 12000; break;
	case 0x0a: m_audio_sample_rate = 11025; break;
	case 0x0b: m_audio_sample_rate =  8000; break;
	default:   m_audio_sample_rate = 48000; break;
	}
	// 保存采样率索引和声道...
	m_audio_rate_index = nRateIndex;
	m_audio_channel_num = nChannelNum;
	// 初始化ffmpeg解码器...
	av_register_all();
	// 准备一些特定的参数...
	AVCodecID src_codec_id = AV_CODEC_ID_AAC;
	// 查找需要的解码器和相关容器、解析器...
	m_lpCodec = avcodec_find_decoder(src_codec_id);
	m_lpDecoder = avcodec_alloc_context3(m_lpCodec);
	// 打开获取到的解码器...
	if( avcodec_open2(m_lpDecoder, m_lpCodec, NULL) != 0 )
		return false;
	// 准备一个全局的解码结构体 => 解码数据帧是相互关联的...
	m_lpDFrame = av_frame_alloc();
	ASSERT( m_lpDFrame != NULL );
	// 启动线程...
	this->Start();
	return true;
}

void CAudioThread::Entry()
{
	// 注意：提高线程优先级，并不能解决音频受系统干扰问题...
	// 设置线程优先级 => 最高级，防止外部干扰...
	//if( !::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST) ) {
	//	blog(LOG_INFO, "[Audio] SetThreadPriority to THREAD_PRIORITY_HIGHEST failed.");
	//}
	while( !this->IsStopRequested() ) {
		// 设置休息标志 => 只要有解码或播放就不能休息...
		m_bNeedSleep = true;
		// 解码一帧视频...
		this->doDecodeFrame();
		// 显示一帧视频...
		this->doDisplayFrame();
		// 进行sleep等待...
		this->doSleepTo();
	}
}
//
// 需要对音频帧数据添加头信息...
void CAudioThread::doFillPacket(string & inData, int inPTS, bool bIsKeyFrame, int inOffset)
{
	// 线程正在退出中，直接返回...
	if( this->IsStopRequested() )
		return;
	// 进入线程互斥状态中...
	// 先加入ADTS头，再加入数据帧内容...
	int nTotalSize = ADTS_HEADER_SIZE + inData.size();
	// 构造ADTS帧头...
	PutBitContext pb;
	char pbuf[ADTS_HEADER_SIZE * 2] = {0};
	init_put_bits(&pb, pbuf, ADTS_HEADER_SIZE);
    /* adts_fixed_header */    
    put_bits(&pb, 12, 0xfff);   /* syncword */    
    put_bits(&pb, 1, 0);        /* ID */    
    put_bits(&pb, 2, 0);        /* layer */    
    put_bits(&pb, 1, 1);        /* protection_absent */    
    put_bits(&pb, 2, 2);		/* profile_objecttype */    
    put_bits(&pb, 4, m_audio_rate_index);    
    put_bits(&pb, 1, 0);        /* private_bit */    
    put_bits(&pb, 3, m_audio_channel_num); /* channel_configuration */    
    put_bits(&pb, 1, 0);        /* original_copy */    
    put_bits(&pb, 1, 0);        /* home */    
    /* adts_variable_header */    
    put_bits(&pb, 1, 0);        /* copyright_identification_bit */    
    put_bits(&pb, 1, 0);        /* copyright_identification_start */    
	put_bits(&pb, 13, nTotalSize); /* aac_frame_length */    
    put_bits(&pb, 11, 0x7ff);   /* adts_buffer_fullness */    
    put_bits(&pb, 2, 0);        /* number_of_raw_data_blocks_in_frame */    
    
    flush_put_bits(&pb);    

	// 构造一个临时AVPacket，并存入帧数据内容...
	AVPacket  theNewPacket = {0};
	av_new_packet(&theNewPacket, nTotalSize);
	ASSERT(theNewPacket.size == nTotalSize);
	// 先添加帧头数据，再添加帧内容信息...
	memcpy(theNewPacket.data, pbuf, ADTS_HEADER_SIZE);
	memcpy(theNewPacket.data + ADTS_HEADER_SIZE, inData.c_str(), inData.size());
	// 计算当前帧的PTS，关键帧标志 => 音频流 => 0
	theNewPacket.pts = inPTS;
	theNewPacket.dts = inPTS - inOffset;
	theNewPacket.flags = bIsKeyFrame;
	theNewPacket.stream_index = 0;
	// 将数据压入解码前队列当中...
	pthread_mutex_lock(&m_Mutex);
	this->doPushPacket(theNewPacket);
	pthread_mutex_unlock(&m_Mutex);
}

CPlaySDL::CPlaySDL(obs_source_t * lpObsSource, int64_t inSysZeroNS, string & strInnerName)
  : m_lpObsSource(lpObsSource)
  , m_sys_zero_ns(inSysZeroNS)
  , m_bFindFirstVKey(false)
  , m_lpVideoThread(NULL)
  , m_lpAudioThread(NULL)
  , m_zero_delay_ms(-1)
  , m_start_pts_ms(-1)
{
	ASSERT(m_sys_zero_ns > 0);
	ASSERT(m_lpObsSource != NULL);
	m_strInnerName = strInnerName;
}

CPlaySDL::~CPlaySDL()
{
	blog(LOG_INFO, "%s == [~CPlaySDL] - Exit Start ==", m_strInnerName.c_str());
	if( m_lpAudioThread != NULL ) {
		delete m_lpAudioThread;
		m_lpAudioThread = NULL;
	}
	if( m_lpVideoThread != NULL ) {
		delete m_lpVideoThread;
		m_lpVideoThread = NULL;
	}
	blog(LOG_INFO, "%s == [~CPlaySDL] - Exit End ==", m_strInnerName.c_str());
}

BOOL CPlaySDL::InitVideo(string & inSPS, string & inPPS, int nWidth, int nHeight, int nFPS)
{
	// 创建新的视频对象...
	if( m_lpVideoThread != NULL ) {
		delete m_lpVideoThread;
		m_lpVideoThread = NULL;
	}
	m_lpVideoThread = new CVideoThread(this);
	return m_lpVideoThread->InitVideo(inSPS, inPPS, nWidth, nHeight, nFPS);
}

BOOL CPlaySDL::InitAudio(int nRateIndex, int nChannelNum)
{
	// 创建新的音频对象...
	if( m_lpAudioThread != NULL ) {
		delete m_lpAudioThread;
		m_lpAudioThread = NULL;
	}
	m_lpAudioThread = new CAudioThread(this);
	return m_lpAudioThread->InitAudio(nRateIndex, nChannelNum);
}

void CPlaySDL::PushPacket(int zero_delay_ms, string & inData, int inTypeTag, bool bIsKeyFrame, uint32_t inSendTime)
{
	// 为了解决突发延时抖动，要用一种遗忘衰减算法，进行播放延时控制...
	// 直接使用计算出的缓存时间设定延时时间 => 缓存就是延时...
	if( m_zero_delay_ms < 0 ) { m_zero_delay_ms = zero_delay_ms; }
	else { m_zero_delay_ms = (7 * m_zero_delay_ms + zero_delay_ms) / 8; }

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：不能在这里设置系统0点时刻，必须在之前设定0点时刻...
	// 系统0点时刻与帧时间戳没有任何关系，是指系统认为的第一帧应该准备好的系统时刻点...
	/////////////////////////////////////////////////////////////////////////////////////////////////

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：越快设置第一帧时间戳，播放延时越小，至于能否播放，不用管，这里只管设定启动时间戳...
	// 获取第一帧的PTS时间戳 => 做为启动时间戳，注意不是系统0点时刻...
	/////////////////////////////////////////////////////////////////////////////////////////////////
	if( m_start_pts_ms < 0 ) {
		m_start_pts_ms = inSendTime;
		blog(LOG_INFO, "%s StartPTS: %lu, Type: %d", m_strInnerName.c_str(), inSendTime, inTypeTag);
	}
	// 注意：寻找第一个视频关键帧的时候，音频帧不要丢弃...
	// 如果有视频，视频第一帧必须是视频关键帧，不丢弃的话解码会失败...
	if((inTypeTag == PT_TAG_VIDEO) && (m_lpVideoThread != NULL) && (!m_bFindFirstVKey)) {
		// 如果当前视频帧，不是关键帧，直接丢弃...
		if( !bIsKeyFrame ) {
			//blog(LOG_INFO, "%s Discard for First Video KeyFrame => PTS: %lu, Type: %d, Size: %d", m_strInnerName.c_str(), inSendTime, inTypeTag, inData.size());
			int nCurCalcPTS = ((inSendTime < (uint32_t)m_start_pts_ms) ? 0 : inSendTime - (uint32_t)m_start_pts_ms);
			if (m_lpObsSource != nullptr) {
				obs_data_t * settings = obs_source_get_settings(m_lpObsSource);
				obs_data_set_string(settings, "notice", "Render.Window.DropVideoFrame");
				obs_data_set_int(settings, "pts", nCurCalcPTS);
				obs_data_set_bool(settings, "render", false);
				obs_data_release(settings);
				// 通知外层界面进行信息更新...
				obs_source_updated(m_lpObsSource);
			}
			return;
		}
		// 设置已经找到第一个视频关键帧标志...
		m_bFindFirstVKey = true;
		if (m_lpObsSource != nullptr) {
			obs_data_t * settings = obs_source_get_settings(m_lpObsSource);
			obs_data_set_string(settings, "notice", "Render.Window.FindFirstVKey");
			obs_data_set_bool(settings, "render", true);
			obs_data_release(settings);
			// 通知外层界面进行信息更新...
			obs_source_updated(m_lpObsSource);
		}
		blog(LOG_INFO, "%s Find First Video KeyFrame OK => PTS: %lu, Type: %d, Size: %d", m_strInnerName.c_str(), inSendTime, inTypeTag, inData.size());
	}
	// 如果是音频，并且还没有找到视频关键帧，直接丢弃...
	// 目的是为了避免关键帧来的慢，造成音视频落差太大引起的不同步...
	if (inTypeTag == PT_TAG_AUDIO && !m_bFindFirstVKey)
		return;
	// 判断处理帧的对象是否存在，不存在，直接丢弃...
	if( inTypeTag == PT_TAG_AUDIO && m_lpAudioThread == NULL )
		return;
	if( inTypeTag == PT_TAG_VIDEO && m_lpVideoThread == NULL )
		return;
	// 如果当前帧的时间戳比第一帧的时间戳还要小，不要扔掉，设置成启动时间戳就可以了...
	if( inSendTime < (uint32_t)m_start_pts_ms ) {
		blog(LOG_INFO, "%s Error => SendTime: %lu, StartPTS: %I64d", m_strInnerName.c_str(), inSendTime, m_start_pts_ms);
		inSendTime = (uint32_t)m_start_pts_ms;
	}
	// 计算当前帧的时间戳 => 时间戳必须做修正，否则会混乱...
	int nCalcPTS = inSendTime - (uint32_t)m_start_pts_ms;

	///////////////////////////////////////////////////////////////////////////
	// 延时实验：在0~2000毫秒之间来回跳动，跳动间隔为1毫秒...
	///////////////////////////////////////////////////////////////////////////
	/*static bool bForwardFlag = true;
	if( bForwardFlag ) {
		m_zero_delay_ms -= 2;
		bForwardFlag = ((m_zero_delay_ms == 0) ? false : true);
	} else {
		m_zero_delay_ms += 2;
		bForwardFlag = ((m_zero_delay_ms == 2000) ? true : false);
	}*/
	//blog("%s Zero Delay => %I64d ms", TM_RECV_NAME, m_zero_delay_ms);

	///////////////////////////////////////////////////////////////////////////
	// 注意：延时模拟目前已经解决了，后续配合网络实测后再模拟...
	///////////////////////////////////////////////////////////////////////////

	///////////////////////////////////////////////////////////////////////////
	// 随机丢掉数据帧 => 每隔10秒，丢1秒的音视频数据帧...
	///////////////////////////////////////////////////////////////////////////
	//if( (inFrame.dwSendTime/1000>0) && ((inFrame.dwSendTime/1000)%5==0) ) {
	//	blog("%s [%s] Discard Packet, PTS: %d", m_strInnerName.c_str(), inFrame.typeFlvTag == PT_TAG_AUDIO ? "Audio" : "Video", nCalcPTS);
	//	return;
	//}

	// 根据音视频类型进行相关操作...
	if( inTypeTag == PT_TAG_AUDIO ) {
		m_lpAudioThread->doFillPacket(inData, nCalcPTS, bIsKeyFrame, 0);
	} else if( inTypeTag == PT_TAG_VIDEO ) {
		m_lpVideoThread->doFillPacket(inData, nCalcPTS, bIsKeyFrame, 0);
	}
	//blog("%s [%s] RenderOffset: %lu", m_strInnerName.c_str(), inFrame.typeFlvTag == PT_TAG_AUDIO ? "Audio" : "Video", inFrame.dwRenderOffset);
}

/*static bool DoProcSaveJpeg(AVFrame * pSrcFrame, AVPixelFormat inSrcFormat, int64_t inPTS, LPCTSTR lpPath)
{
	char szSavePath[MAX_PATH] = {0};
	sprintf(szSavePath, "%s/%I64d.jpg", lpPath, inPTS);
	/////////////////////////////////////////////////////////////////////////
	// 注意：input->conversion 是需要变换的格式，
	// 因此，应该从 video->info 当中获取原始数据信息...
	// 同时，sws_getContext 需要AVPixelFormat而不是video_format格式...
	/////////////////////////////////////////////////////////////////////////
	// 设置ffmpeg的日志回调函数...
	//av_log_set_level(AV_LOG_VERBOSE);
	//av_log_set_callback(my_av_logoutput);
	// 统一数据源输入格式，找到压缩器需要的像素格式...
	enum AVPixelFormat nDestFormat = AV_PIX_FMT_YUV420P;
	enum AVPixelFormat nSrcFormat = inSrcFormat;
	int nSrcWidth = pSrcFrame->width;
	int nSrcHeight = pSrcFrame->height;
	// 注意：长宽必须是4的整数倍，否则sws_scale崩溃...
	int nDstWidth = nSrcWidth / 4 * 4;
	int nDstHeight = nSrcHeight / 4 * 4;
	// 不管什么格式，都需要进行像素格式的转换...
	AVFrame * pDestFrame = av_frame_alloc();
	int nDestBufSize = avpicture_get_size(nDestFormat, nDstWidth, nDstHeight);
	uint8_t * pDestOutBuf = (uint8_t *)av_malloc(nDestBufSize);
	avpicture_fill((AVPicture *)pDestFrame, pDestOutBuf, nDestFormat, nDstWidth, nDstHeight);

	// 注意：这里不用libyuv的原因是，使用sws更简单，不用根据不同像素格式调用不同接口...
	// ffmpeg自带的sws_scale转换也是没有问题的，之前有问题是由于sws_getContext的输入源需要格式AVPixelFormat，写成了video_format，造成的格式错位问题...
	// 注意：目的像素格式不能为AV_PIX_FMT_YUVJ420P，会提示警告信息，但并不影响格式转换，因此，还是使用不会警告的AV_PIX_FMT_YUV420P格式...
	struct SwsContext * img_convert_ctx = sws_getContext(nSrcWidth, nSrcHeight, nSrcFormat, nDstWidth, nDstHeight, nDestFormat, SWS_BICUBIC, NULL, NULL, NULL);
	int nReturn = sws_scale(img_convert_ctx, (const uint8_t* const*)pSrcFrame->data, pSrcFrame->linesize, 0, nSrcHeight, pDestFrame->data, pDestFrame->linesize);
	sws_freeContext(img_convert_ctx);

	// 设置转换后的数据帧内容...
	pDestFrame->width = nDstWidth;
	pDestFrame->height = nDstHeight;
	pDestFrame->format = nDestFormat;

	// 将转换后的 YUV 数据存盘成 jpg 文件...
	AVCodecContext * pOutCodecCtx = NULL;
	bool bRetSave = false;
	do {
		// 预先查找jpeg压缩器需要的输入数据格式...
		AVOutputFormat * avOutputFormat = av_guess_format("mjpeg", NULL, NULL); //av_guess_format(0, lpszJpgName, 0);
		AVCodec * pOutAVCodec = avcodec_find_encoder(avOutputFormat->video_codec);
		if (pOutAVCodec == NULL)
			break;
		// 创建ffmpeg压缩器的场景对象...
		pOutCodecCtx = avcodec_alloc_context3(pOutAVCodec);
		if (pOutCodecCtx == NULL)
			break;
		// 准备数据结构需要的参数...
		pOutCodecCtx->bit_rate = 200000;
		pOutCodecCtx->width = nDstWidth;
		pOutCodecCtx->height = nDstHeight;
		// 注意：没有使用适配方式，适配出来格式有可能不是YUVJ420P，造成压缩器崩溃，因为传递的数据已经固定成YUV420P...
		// 注意：输入像素是YUV420P格式，压缩器像素是YUVJ420P格式...
		pOutCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P; //avcodec_find_best_pix_fmt_of_list(pOutAVCodec->pix_fmts, (AVPixelFormat)-1, 1, 0);
		pOutCodecCtx->codec_id = avOutputFormat->video_codec; //AV_CODEC_ID_MJPEG;  
		pOutCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
		pOutCodecCtx->time_base.num = 1;
		pOutCodecCtx->time_base.den = 25;
		// 打开 ffmpeg 压缩器...
		if (avcodec_open2(pOutCodecCtx, pOutAVCodec, 0) < 0)
			break;
		// 设置图像质量，默认是0.5，修改为0.8(图片太大,0.5刚刚好)...
		pOutCodecCtx->qcompress = 0.5f;
		// 准备接收缓存，开始压缩jpg数据...
		int got_pic = 0;
		int nResult = 0;
		AVPacket pkt = { 0 };
		// 采用新的压缩函数...
		nResult = avcodec_encode_video2(pOutCodecCtx, &pkt, pDestFrame, &got_pic);
		// 解码失败或没有得到完整图像，继续解析...
		if (nResult < 0 || !got_pic)
			break;
		// 打开jpg文件句柄...
		FILE * pFile = fopen(szSavePath, "wb");
		// 打开jpg失败，注意释放资源...
		if (pFile == NULL) {
			av_packet_unref(&pkt);
			break;
		}
		// 保存到磁盘，并释放资源...
		fwrite(pkt.data, 1, pkt.size, pFile);
		av_packet_unref(&pkt);
		// 释放文件句柄，返回成功...
		fclose(pFile); pFile = NULL;
		bRetSave = true;
	} while (false);
	// 清理中间产生的对象...
	if (pOutCodecCtx != NULL) {
		avcodec_close(pOutCodecCtx);
		av_free(pOutCodecCtx);
	}

	// 释放临时分配的数据空间...
	av_frame_free(&pDestFrame);
	av_free(pDestOutBuf);

	return bRetSave;
}*/

