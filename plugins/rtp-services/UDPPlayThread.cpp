
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
	// �ͷŽ���ṹ��...
	if( m_lpDFrame != NULL ) {
		av_frame_free(&m_lpDFrame);
		m_lpDFrame = NULL;
	}
	// �ͷŽ���������...
	if( m_lpDecoder != NULL ) {
		avcodec_close(m_lpDecoder);
		av_free(m_lpDecoder);
	}
	// �ͷŶ����еĽ���ǰ�����ݿ�...
	GM_MapPacket::iterator itorPack;
	for(itorPack = m_MapPacket.begin(); itorPack != m_MapPacket.end(); ++itorPack) {
		av_packet_unref(&itorPack->second);
	}
	m_MapPacket.clear();
	// �ͷ�û�в�����ϵĽ���������֡...
	GM_MapFrame::iterator itorFrame;
	for(itorFrame = m_MapFrame.begin(); itorFrame != m_MapFrame.end(); ++itorFrame) {
		av_frame_free(&itorFrame->second);
	}
	m_MapFrame.clear();
	// �ͷŻ������...
	pthread_mutex_destroy(&m_Mutex);
}

void CDecoder::doPushPacket(AVPacket & inPacket)
{
	// ע�⣺���������DTS���򣬾����˽�����Ⱥ�˳��...
	// ע�⣺����ʹ��multimap������ר�Ŵ�����ͬʱ���...
	m_MapPacket.insert(pair<int64_t, AVPacket>(inPacket.dts, inPacket));
}

void CDecoder::doSleepTo()
{
	// < 0 ������Ϣ���в�����Ϣ��־ => ��ֱ�ӷ���...
	if( !m_bNeedSleep || m_play_next_ns < 0 )
		return;
	// ����Ҫ��Ϣ��ʱ�� => �����Ϣ������...
	uint64_t cur_time_ns = os_gettime_ns();
	const uint64_t timeout_ns = MAX_SLEEP_MS * 1000000;
	// ����ȵ�ǰʱ��С(�ѹ���)��ֱ�ӷ���...
	if( (uint64_t)m_play_next_ns <= cur_time_ns )
		return;
	// ���㳬ǰʱ��Ĳ�ֵ�������Ϣ10����...
	uint64_t delta_ns = m_play_next_ns - cur_time_ns;
	delta_ns = ((delta_ns >= timeout_ns) ? timeout_ns : delta_ns);
	// ����ϵͳ���ߺ���������sleep��Ϣ...
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
	// ����Ѿ���ʼ����ֱ�ӷ���...
	if( m_lpCodec != NULL || m_lpDecoder != NULL )
		return false;
	ASSERT( m_lpCodec == NULL && m_lpDecoder == NULL );
	// ���洫�ݹ����Ĳ�����Ϣ...
	m_nDstHeight = nHeight;
	m_nDstWidth = nWidth;
	m_nDstFPS = nFPS;
	m_strSPS = inSPS;
	m_strPPS = inPPS;
	// ��ʼ��ffmpeg������...
	av_register_all();
	// ׼��һЩ�ض��Ĳ���...
	AVCodecID src_codec_id = AV_CODEC_ID_H264;
	// ������Ҫ�Ľ����� => ���ô���������...
	m_lpCodec = avcodec_find_decoder(src_codec_id);
	m_lpDecoder = avcodec_alloc_context3(m_lpCodec);
	// ����֧�ֵ���ʱģʽ => ûɶ���ã�������Ͼ������...
	m_lpDecoder->flags |= CODEC_FLAG_LOW_DELAY;
	// ����֧�ֲ�����Ƭ�ν��� => ûɶ����...
	if( m_lpCodec->capabilities & CODEC_CAP_TRUNCATED ) {
		m_lpDecoder->flags |= CODEC_FLAG_TRUNCATED;
	}
	// ���ý����������������� => �������ò�������...
	//m_lpDecoder->refcounted_frames = 1;
	//m_lpDecoder->has_b_frames = 0;
	// �򿪻�ȡ���Ľ�����...
	if( avcodec_open2(m_lpDecoder, m_lpCodec, NULL) < 0 ) {
		blog(LOG_INFO, "%s [Video] avcodec_open2 failed.", m_lpPlaySDL->GetInnerName().c_str());
		return false;
	}
	// ׼��һ��ȫ�ֵĽ���ṹ�� => ��������֡���໥������...
	m_lpDFrame = av_frame_alloc();
	ASSERT( m_lpDFrame != NULL );
	// �����߳̿�ʼ��ת...
	this->Start();
	return true;
}

void CVideoThread::Entry()
{
	while( !this->IsStopRequested() ) {
		// ������Ϣ��־ => ֻҪ�н���򲥷žͲ�����Ϣ...
		m_bNeedSleep = true;
		// ����һ֡��Ƶ...
		this->doDecodeFrame();
		// ��ʾһ֡��Ƶ...
		this->doDisplayFrame();
		// ����sleep�ȴ�...
		this->doSleepTo();
	}
}

void CVideoThread::doFillPacket(string & inData, int inPTS, bool bIsKeyFrame, int inOffset)
{
	// �߳������˳��У�ֱ�ӷ���...
	if( this->IsStopRequested() )
		return;
	// ÿ���ؼ�֡������sps��pps�����Ż�ӿ�...
	string strCurFrame;
	// ����ǹؼ�֡��������д��sps����д��pps...
	if( bIsKeyFrame ) {
		DWORD dwTag = 0x01000000;
		strCurFrame.append((char*)&dwTag, sizeof(DWORD));
		strCurFrame.append(m_strSPS);
		strCurFrame.append((char*)&dwTag, sizeof(DWORD));
		strCurFrame.append(m_strPPS);
	}
	// �޸���Ƶ֡����ʼ�� => 0x00000001
	char * lpszSrc = (char*)inData.c_str();
	memset((void*)lpszSrc, 0, sizeof(DWORD));
	lpszSrc[3] = 0x01;
	// ���׷��������...
	strCurFrame.append(inData);
	// ����һ����ʱAVPacket��������֡��������...
	AVPacket  theNewPacket = {0};
	av_new_packet(&theNewPacket, strCurFrame.size());
	ASSERT(theNewPacket.size == strCurFrame.size());
	memcpy(theNewPacket.data, strCurFrame.c_str(), theNewPacket.size);
	// ���㵱ǰ֡��PTS���ؼ�֡��־ => ��Ƶ�� => 1
	// Ŀǰֻ��I֡��P֡��PTS��DTS��һ�µ�...
	theNewPacket.pts = inPTS;
	theNewPacket.dts = inPTS - inOffset;
	theNewPacket.flags = bIsKeyFrame;
	theNewPacket.stream_index = 1;
	// ������ѹ�����ǰ���е���...
	pthread_mutex_lock(&m_Mutex);
	this->doPushPacket(theNewPacket);
	pthread_mutex_unlock(&m_Mutex);
}
//
// ȡ��һ֡��������Ƶ���ȶ�ϵͳʱ�䣬�����ܷ񲥷�...
void CVideoThread::doDisplayFrame()
{
	// ���û���ѽ�������֡��ֱ�ӷ�����Ϣ��������...
	if( m_MapFrame.size() <= 0 ) {
		m_play_next_ns = os_gettime_ns() + MAX_SLEEP_MS * 1000000;
		return;
	}
	// ����Ϊ�˲���ԭʼPTS����ȡ�ĳ�ʼPTSֵ => ֻ�ڴ�ӡ������Ϣʱʹ��...
	int64_t inStartPtsMS = m_lpPlaySDL->GetStartPtsMS();
	// ���㵱ǰʱ�̵���ϵͳ0��λ�õ�ʱ��� => ת���ɺ���...
	int64_t sys_cur_ms = (os_gettime_ns() - m_lpPlaySDL->GetSysZeroNS())/1000000;
	// �ۼ���Ϊ�趨����ʱ������ => ���...
	sys_cur_ms -= m_lpPlaySDL->GetZeroDelayMS();
	// ȡ����һ���ѽ�������֡ => ʱ����С������֡...
	GM_MapFrame::iterator itorItem = m_MapFrame.begin();
	obs_source_frame * lpObsFrame = &m_obs_frame;
	AVFrame * lpSrcFrame = itorItem->second;
	int64_t   frame_pts_ms = itorItem->first;
	// ��ǰ֡����ʾʱ�仹û�е� => ֱ����Ϣ��ֵ...
	if( frame_pts_ms > sys_cur_ms ) {
		m_play_next_ns = os_gettime_ns() + (frame_pts_ms - sys_cur_ms)*1000000;
		//blog(LOG_INFO, "%s [Video] Advance => PTS: %I64d, Delay: %I64d ms, VPackSize: %d, VFrameSize: %d",
		//     m_lpPlaySDL->GetInnerName().c_str(), frame_pts_ms + inStartPtsMS, frame_pts_ms - sys_cur_ms, m_MapPacket.size(), m_MapFrame.size());
		return;
	}
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺��Ƶ��ʱ֡�����֡�������ܶ��������������ʾ����Ƶ�����ٶ���ԽϿ죬����ʱ��������ˣ�����ɲ������⡣
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ������ת����jpg...
	//DoProcSaveJpeg(lpSrcFrame, m_lpDecoder->pix_fmt, frame_pts, "F:/MP4/Dst");
	// ��ӡ���ڲ��ŵĽ������Ƶ����...
	//blog(LOG_INFO, "%s [Video] Player => PTS: %I64d ms, Delay: %I64d ms, AVPackSize: %d, AVFrameSize: %d",
	//     m_lpPlaySDL->GetInnerName().c_str(), frame_pts_ms + inStartPtsMS, sys_cur_ms - frame_pts_ms, m_MapPacket.size(), m_MapFrame.size());
	// �ж���Ƶ�����Ƿ���Ҫ��ת����...
	bool bFlip = ((lpSrcFrame->linesize[0] < 0) && (lpSrcFrame->linesize[1] == 0));
	// ������ָ�븴�Ƶ�obs�ṹ�嵱��...
	for (size_t i = 0; i < MAX_AV_PLANES; i++) {
		lpObsFrame->data[i] = lpSrcFrame->data[i];
		lpObsFrame->linesize[i] = abs(lpSrcFrame->linesize[i]);
	}
	// ����Ƶ���ݽ��з�ת����...
	if ( bFlip ) {
		lpObsFrame->data[0] -= lpObsFrame->linesize[0] * (lpSrcFrame->height - 1);
	}
	// ����Ƶ���ݽ��и�ʽת��...
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
	// ����obs����֡�ĳ���ͷ�ת��־...
	lpObsFrame->flip = bFlip;
	lpObsFrame->width = lpSrcFrame->width;
	lpObsFrame->height = lpSrcFrame->height;
	// ����obs����֡��ʱ��� => ������ת��������...
	AVRational base_pack = { 1, 1000 };
	AVRational base_frame = { 1, 1000000000 };
	lpObsFrame->timestamp = av_rescale_q(frame_pts_ms, base_pack, base_frame);
	// �������obs����֡����������Ͷ�ݹ���...
	obs_source_output_video(m_lpPlaySDL->GetObsSource(), lpObsFrame);
	// �ͷŲ�ɾ���Ѿ�ʹ�����ԭʼ���ݿ�...
	av_frame_free(&lpSrcFrame);
	m_MapFrame.erase(itorItem);
	// �޸���Ϣ״̬ => �Ѿ��в��ţ�������Ϣ...
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
	// û��Ҫ��������ݰ���ֱ�ӷ��������Ϣ������...
	if( m_MapPacket.size() <= 0 ) {
		m_play_next_ns = os_gettime_ns() + MAX_SLEEP_MS * 1000000;
		return;
	}
	// AVPacket������󱣻�...
	pthread_mutex_lock(&m_Mutex);
	// ����Ϊ�˲���ԭʼPTS����ȡ�ĳ�ʼPTSֵ => ֻ�ڴ�ӡ������Ϣʱʹ��...
	int64_t inStartPtsMS = m_lpPlaySDL->GetStartPtsMS();
	// ��ȡһ��AVPacket���н��������������һ��AVPacketһ���ܽ����һ��Picture...
	// �������ݶ�ʧ��B֡��Ͷ��һ��AVPacket��һ���ܷ���Picture����ʱ�������ͻὫ���ݻ�����������ɽ�����ʱ...
	int got_picture = 0, nResult = 0;
	GM_MapPacket::iterator itorItem = m_MapPacket.begin();
	AVPacket & thePacket = itorItem->second;
	//////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺����ֻ�� I֡ �� P֡��û��B֡��Ҫ�ý��������ٶ�������������ݣ�ͨ������has_b_frames�����
	// �����ĵ� => https://bbs.csdn.net/topics/390692774
	//////////////////////////////////////////////////////////////////////////////////////////////////
	m_lpDecoder->has_b_frames = 0;
	// ������ѹ������֡������������н������...
	nResult = avcodec_decode_video2(m_lpDecoder, m_lpDFrame, &got_picture, &thePacket);
	/////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺Ŀǰֻ�� I֡ �� P֡  => ����ʹ��ȫ��AVFrame���ǳ���Ҫ�����ṩ����AVPacket�Ľ���֧��...
	// ����ʧ�ܻ�û�еõ�����ͼ�� => ����Ҫ����AVPacket�����ݲ��ܽ����ͼ��...
	// �ǳ��ؼ��Ĳ��� => m_lpDFrame ǧ�����ͷţ�������AVPacket���ܽ����ͼ��...
	/////////////////////////////////////////////////////////////////////////////////////////////////
	if( nResult < 0 || !got_picture ) {
		// ��ӡ����ʧ����Ϣ����ʾ��֡�ĸ���...
		blog(LOG_INFO, "%s [Video] Error => decode_frame failed, BFrame: %d, PTS: %I64d, DecodeSize: %d, PacketSize: %d", 
			m_lpPlaySDL->GetInnerName().c_str(), m_lpDecoder->has_b_frames, thePacket.pts + inStartPtsMS, nResult, thePacket.size);
		// ����ǳ��ؼ������߽�������Ҫ���滵֡(B֡)��һ���н������ֱ���ӵ�������ǵ���ʱ����ģʽ...
		m_lpDecoder->has_b_frames = 0;
		// ��������ʧ�ܵ�����֡...
		av_packet_unref(&thePacket);
		m_MapPacket.erase(itorItem);
		pthread_mutex_unlock(&m_Mutex);
		return;
	}
	// ��ӡ����֮�������֡��Ϣ...
	//blog( LOG_INFO, "%s [Video] Decode => BFrame: %d, PTS: %I64d, pkt_dts: %I64d, pkt_pts: %I64d, Type: %d, DecodeSize: %d, PacketSize: %d", m_lpDecoder->has_b_frames, m_lpPlaySDL->GetInnerName().c_str(),
	//		m_lpDFrame->best_effort_timestamp + inStartPtsMS, m_lpDFrame->pkt_dts + inStartPtsMS, m_lpDFrame->pkt_pts + inStartPtsMS, m_lpDFrame->pict_type, nResult, thePacket.size );
	///////////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺����ʹ��AVFrame����������Чʱ�����ʹ��AVPacket��ʱ���Ҳ��һ����...
	// ��Ϊ�������˵���ʱ��ģʽ����֡���ӵ��ˣ��������ڲ�û�л������ݣ��������ʱ�����λ����...
	///////////////////////////////////////////////////////////////////////////////////////////////////////
	int64_t frame_pts_ms = m_lpDFrame->best_effort_timestamp;
	// ���¿�¡AVFrame���Զ�����ռ䣬��ʱ������...
	AVFrame * lpNewFrame = av_frame_clone(m_lpDFrame);
	m_MapFrame.insert(pair<int64_t, AVFrame*>(frame_pts_ms, lpNewFrame));
	//DoProcSaveJpeg(m_lpDFrame, m_lpDecoder->pix_fmt, frame_pts, "F:/MP4/Src");
	// ���������ã�������free��erase...
	av_packet_unref(&thePacket);
	m_MapPacket.erase(itorItem);
	pthread_mutex_unlock(&m_Mutex);
	// �޸���Ϣ״̬ => �Ѿ��н��룬������Ϣ...
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
	// û��Ҫ��������ݰ���ֱ�ӷ��������Ϣ������...
	if( m_MapPacket.size() <= 0 ) {
		m_play_next_ns = os_gettime_ns() + MAX_SLEEP_MS * 1000000;
		return;
	}
	pthread_mutex_lock(&m_Mutex);
	// ����Ϊ�˲���ԭʼPTS����ȡ�ĳ�ʼPTSֵ => ֻ�ڴ�ӡ������Ϣʱʹ��...
	int64_t inStartPtsMS = m_lpPlaySDL->GetStartPtsMS();
	// ��ȡһ��AVPacket���н��������һ��AVPacket��һ���ܽ����һ��Picture...
	int got_picture = 0, nResult = 0;
	GM_MapPacket::iterator itorItem = m_MapPacket.begin();
	AVPacket & thePacket = itorItem->second;
	// ע�⣺��������ĸ�ʽ��4bit����Ҫת����16bit������swr_convert
	nResult = avcodec_decode_audio4(m_lpDecoder, m_lpDFrame, &got_picture, &thePacket);
	////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺����ʹ��ȫ��AVFrame���ǳ���Ҫ�����ṩ����AVPacket�Ľ���֧��...
	// ����ʧ�ܻ�û�еõ�����ͼ�� => ����Ҫ����AVPacket�����ݲ��ܽ����ͼ��...
	// �ǳ��ؼ��Ĳ��� => m_lpDFrame ǧ�����ͷţ�������AVPacket���ܽ����ͼ��...
	////////////////////////////////////////////////////////////////////////////////////////////////
	if( nResult < 0 || !got_picture ) {
		blog(LOG_INFO, "%s [Audio] Error => decode_audio failed, PTS: %I64d, DecodeSize: %d, PacketSize: %d", 
			m_lpPlaySDL->GetInnerName().c_str(), thePacket.pts + inStartPtsMS, nResult, thePacket.size);
		av_packet_unref(&thePacket);
		m_MapPacket.erase(itorItem);
		pthread_mutex_unlock(&m_Mutex);
		return;
	}
	// ��ӡ����֮�������֡��Ϣ...
	//blog(LOG_INFO, "%s [Audio] Decode => PTS: %I64d, pkt_dts: %I64d, pkt_pts: %I64d, Type: %d, DecodeSize: %d, PacketSize: %d", m_lpPlaySDL->GetInnerName().c_str(),
	//		m_lpDFrame->best_effort_timestamp + inStartPtsMS, m_lpDFrame->pkt_dts + inStartPtsMS, m_lpDFrame->pkt_pts + inStartPtsMS,
	//		m_lpDFrame->pict_type, nResult, thePacket.size );
	////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺����ʹ��AVFrame����������Чʱ�����ʹ��AVPacket��ʱ���Ҳ��һ����...
	// ��Ϊ�������˵���ʱ��ģʽ����֡���ӵ��ˣ��������ڲ�û�л������ݣ��������ʱ�����λ����...
	////////////////////////////////////////////////////////////////////////////////////////////
	int64_t frame_pts_ms = m_lpDFrame->best_effort_timestamp;
	// ���¿�¡AVFrame���Զ�����ռ䣬��ʱ������...
	AVFrame * lpNewFrame = av_frame_clone(m_lpDFrame);
	m_MapFrame.insert(pair<int64_t, AVFrame*>(frame_pts_ms, lpNewFrame));
	// ���������ã�������free��erase...
	av_packet_unref(&thePacket);
	m_MapPacket.erase(itorItem);
	pthread_mutex_unlock(&m_Mutex);
	// �޸���Ϣ״̬ => �Ѿ��н��룬������Ϣ...
	m_bNeedSleep = false;
}

void CAudioThread::doDisplayFrame()
{
	// ���û���ѽ�������֡��ֱ�ӷ�����Ϣ��������...
	if (m_MapFrame.size() <= 0) {
		m_play_next_ns = os_gettime_ns() + MAX_SLEEP_MS * 1000000;
		return;
	}
	// ����Ϊ�˲���ԭʼPTS����ȡ�ĳ�ʼPTSֵ => ֻ�ڴ�ӡ������Ϣʱʹ��...
	int64_t inStartPtsMS = m_lpPlaySDL->GetStartPtsMS();
	// ���㵱ǰʱ�̵���ϵͳ0��λ�õ�ʱ��� => ת���ɺ���...
	int64_t sys_cur_ms = (os_gettime_ns() - m_lpPlaySDL->GetSysZeroNS()) / 1000000;
	// �ۼ���Ϊ�趨����ʱ������ => ���...
	sys_cur_ms -= m_lpPlaySDL->GetZeroDelayMS();
	// ȡ����һ���ѽ�������֡ => ʱ����С������֡...
	GM_MapFrame::iterator itorItem = m_MapFrame.begin();
	obs_source_audio theObsAudio = { 0 };
	AVFrame * lpSrcFrame = itorItem->second;
	int64_t   frame_pts_ms = itorItem->first;
	// ��ǰ֡����ʾʱ�仹û�е� => ֱ����Ϣ��ֵ...
	if (frame_pts_ms > sys_cur_ms) {
		m_play_next_ns = os_gettime_ns() + (frame_pts_ms - sys_cur_ms) * 1000000;
		//blog(LOG_INFO, "%s [Audio] Advance => PTS: %I64d, Delay: %I64d ms, APackSize: %d, AFrameSize: %d", 
		//     m_lpPlaySDL->GetInnerName().c_str(), frame_pts_ms + inStartPtsMS, frame_pts_ms - sys_cur_ms, m_MapPacket.size(), m_MapFrame.size());
		return;
	}
	// ��ӡ���ڲ��ŵĽ������Ƶ����...
	//blog(LOG_INFO, "%s [Audio] Player => StartPTS: %I64d ms, PTS: %I64d ms, Delay: %I64d ms, APackSize: %d, AFrameSize: %d", 
	//     m_lpPlaySDL->GetInnerName().c_str(), inStartPtsMS, frame_pts_ms, sys_cur_ms - frame_pts_ms, m_MapPacket.size(), m_MapFrame.size());
	// ����Ƶ����ָ�븴�Ƶ�obs�ṹ�嵱��...
	for (size_t i = 0; i < MAX_AV_PLANES; i++) {
		theObsAudio.data[i] = lpSrcFrame->data[i];
	}
	// ���㲢������Ƶ�����Ϣ => ���Կ��Ʋ����ٶ�...
	theObsAudio.samples_per_sec = lpSrcFrame->sample_rate; // *m->speed / 100;
	theObsAudio.speakers = convert_speaker_layout(lpSrcFrame->channels);
	theObsAudio.format = convert_sample_format(lpSrcFrame->format);
	theObsAudio.frames = lpSrcFrame->nb_samples;
	// ����obs����֡��ʱ��� => ������ת��������...
	AVRational base_pack = { 1, 1000 };
	AVRational base_frame = { 1, 1000000000 };
	theObsAudio.timestamp = av_rescale_q(frame_pts_ms, base_pack, base_frame);
	// �������obs����֡����������Ͷ�ݹ���...
	obs_source_output_audio(m_lpPlaySDL->GetObsSource(), &theObsAudio);
	// �ͷŲ�ɾ���Ѿ�ʹ�����ԭʼ���ݿ�...
	av_frame_free(&lpSrcFrame);
	m_MapFrame.erase(itorItem);
	// �޸���Ϣ״̬ => �Ѿ��в��ţ�������Ϣ...
	m_bNeedSleep = false;
}

BOOL CAudioThread::InitAudio(int nRateIndex, int nChannelNum)
{
	// ����Ѿ���ʼ����ֱ�ӷ���...
	if( m_lpCodec != NULL || m_lpDecoder != NULL )
		return false;
	ASSERT( m_lpCodec == NULL && m_lpDecoder == NULL );
	// ����������ȡ������...
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
	// �������������������...
	m_audio_rate_index = nRateIndex;
	m_audio_channel_num = nChannelNum;
	// ��ʼ��ffmpeg������...
	av_register_all();
	// ׼��һЩ�ض��Ĳ���...
	AVCodecID src_codec_id = AV_CODEC_ID_AAC;
	// ������Ҫ�Ľ����������������������...
	m_lpCodec = avcodec_find_decoder(src_codec_id);
	m_lpDecoder = avcodec_alloc_context3(m_lpCodec);
	// �򿪻�ȡ���Ľ�����...
	if( avcodec_open2(m_lpDecoder, m_lpCodec, NULL) != 0 )
		return false;
	// ׼��һ��ȫ�ֵĽ���ṹ�� => ��������֡���໥������...
	m_lpDFrame = av_frame_alloc();
	ASSERT( m_lpDFrame != NULL );
	// �����߳�...
	this->Start();
	return true;
}

void CAudioThread::Entry()
{
	// ע�⣺����߳����ȼ��������ܽ����Ƶ��ϵͳ��������...
	// �����߳����ȼ� => ��߼�����ֹ�ⲿ����...
	//if( !::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST) ) {
	//	blog(LOG_INFO, "[Audio] SetThreadPriority to THREAD_PRIORITY_HIGHEST failed.");
	//}
	while( !this->IsStopRequested() ) {
		// ������Ϣ��־ => ֻҪ�н���򲥷žͲ�����Ϣ...
		m_bNeedSleep = true;
		// ����һ֡��Ƶ...
		this->doDecodeFrame();
		// ��ʾһ֡��Ƶ...
		this->doDisplayFrame();
		// ����sleep�ȴ�...
		this->doSleepTo();
	}
}
//
// ��Ҫ����Ƶ֡�������ͷ��Ϣ...
void CAudioThread::doFillPacket(string & inData, int inPTS, bool bIsKeyFrame, int inOffset)
{
	// �߳������˳��У�ֱ�ӷ���...
	if( this->IsStopRequested() )
		return;
	// �����̻߳���״̬��...
	// �ȼ���ADTSͷ���ټ�������֡����...
	int nTotalSize = ADTS_HEADER_SIZE + inData.size();
	// ����ADTS֡ͷ...
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

	// ����һ����ʱAVPacket��������֡��������...
	AVPacket  theNewPacket = {0};
	av_new_packet(&theNewPacket, nTotalSize);
	ASSERT(theNewPacket.size == nTotalSize);
	// �����֡ͷ���ݣ������֡������Ϣ...
	memcpy(theNewPacket.data, pbuf, ADTS_HEADER_SIZE);
	memcpy(theNewPacket.data + ADTS_HEADER_SIZE, inData.c_str(), inData.size());
	// ���㵱ǰ֡��PTS���ؼ�֡��־ => ��Ƶ�� => 0
	theNewPacket.pts = inPTS;
	theNewPacket.dts = inPTS - inOffset;
	theNewPacket.flags = bIsKeyFrame;
	theNewPacket.stream_index = 0;
	// ������ѹ�����ǰ���е���...
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
	// �����µ���Ƶ����...
	if( m_lpVideoThread != NULL ) {
		delete m_lpVideoThread;
		m_lpVideoThread = NULL;
	}
	m_lpVideoThread = new CVideoThread(this);
	return m_lpVideoThread->InitVideo(inSPS, inPPS, nWidth, nHeight, nFPS);
}

BOOL CPlaySDL::InitAudio(int nRateIndex, int nChannelNum)
{
	// �����µ���Ƶ����...
	if( m_lpAudioThread != NULL ) {
		delete m_lpAudioThread;
		m_lpAudioThread = NULL;
	}
	m_lpAudioThread = new CAudioThread(this);
	return m_lpAudioThread->InitAudio(nRateIndex, nChannelNum);
}

void CPlaySDL::PushPacket(int zero_delay_ms, string & inData, int inTypeTag, bool bIsKeyFrame, uint32_t inSendTime)
{
	// Ϊ�˽��ͻ����ʱ������Ҫ��һ������˥���㷨�����в�����ʱ����...
	// ֱ��ʹ�ü�����Ļ���ʱ���趨��ʱʱ�� => ���������ʱ...
	if( m_zero_delay_ms < 0 ) { m_zero_delay_ms = zero_delay_ms; }
	else { m_zero_delay_ms = (7 * m_zero_delay_ms + zero_delay_ms) / 8; }

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺��������������ϵͳ0��ʱ�̣�������֮ǰ�趨0��ʱ��...
	// ϵͳ0��ʱ����֡ʱ���û���κι�ϵ����ָϵͳ��Ϊ�ĵ�һ֡Ӧ��׼���õ�ϵͳʱ�̵�...
	/////////////////////////////////////////////////////////////////////////////////////////////////

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺Խ�����õ�һ֡ʱ�����������ʱԽС�������ܷ񲥷ţ����ùܣ�����ֻ���趨����ʱ���...
	// ��ȡ��һ֡��PTSʱ��� => ��Ϊ����ʱ�����ע�ⲻ��ϵͳ0��ʱ��...
	/////////////////////////////////////////////////////////////////////////////////////////////////
	if( m_start_pts_ms < 0 ) {
		m_start_pts_ms = inSendTime;
		blog(LOG_INFO, "%s StartPTS: %lu, Type: %d", m_strInnerName.c_str(), inSendTime, inTypeTag);
	}
	// ע�⣺Ѱ�ҵ�һ����Ƶ�ؼ�֡��ʱ����Ƶ֡��Ҫ����...
	// �������Ƶ����Ƶ��һ֡��������Ƶ�ؼ�֡���������Ļ������ʧ��...
	if((inTypeTag == PT_TAG_VIDEO) && (m_lpVideoThread != NULL) && (!m_bFindFirstVKey)) {
		// �����ǰ��Ƶ֡�����ǹؼ�֡��ֱ�Ӷ���...
		if( !bIsKeyFrame ) {
			//blog(LOG_INFO, "%s Discard for First Video KeyFrame => PTS: %lu, Type: %d, Size: %d", m_strInnerName.c_str(), inSendTime, inTypeTag, inData.size());
			int nCurCalcPTS = ((inSendTime < (uint32_t)m_start_pts_ms) ? 0 : inSendTime - (uint32_t)m_start_pts_ms);
			if (m_lpObsSource != nullptr) {
				obs_data_t * settings = obs_source_get_settings(m_lpObsSource);
				obs_data_set_string(settings, "notice", "Render.Window.DropVideoFrame");
				obs_data_set_int(settings, "pts", nCurCalcPTS);
				obs_data_set_bool(settings, "render", false);
				obs_data_release(settings);
				// ֪ͨ�����������Ϣ����...
				obs_source_updated(m_lpObsSource);
			}
			return;
		}
		// �����Ѿ��ҵ���һ����Ƶ�ؼ�֡��־...
		m_bFindFirstVKey = true;
		if (m_lpObsSource != nullptr) {
			obs_data_t * settings = obs_source_get_settings(m_lpObsSource);
			obs_data_set_string(settings, "notice", "Render.Window.FindFirstVKey");
			obs_data_set_bool(settings, "render", true);
			obs_data_release(settings);
			// ֪ͨ�����������Ϣ����...
			obs_source_updated(m_lpObsSource);
		}
		blog(LOG_INFO, "%s Find First Video KeyFrame OK => PTS: %lu, Type: %d, Size: %d", m_strInnerName.c_str(), inSendTime, inTypeTag, inData.size());
	}
	// �������Ƶ�����һ�û���ҵ���Ƶ�ؼ�֡��ֱ�Ӷ���...
	// Ŀ����Ϊ�˱���ؼ�֡���������������Ƶ���̫������Ĳ�ͬ��...
	if (inTypeTag == PT_TAG_AUDIO && !m_bFindFirstVKey)
		return;
	// �жϴ���֡�Ķ����Ƿ���ڣ������ڣ�ֱ�Ӷ���...
	if( inTypeTag == PT_TAG_AUDIO && m_lpAudioThread == NULL )
		return;
	if( inTypeTag == PT_TAG_VIDEO && m_lpVideoThread == NULL )
		return;
	// �����ǰ֡��ʱ����ȵ�һ֡��ʱ�����ҪС����Ҫ�ӵ������ó�����ʱ����Ϳ�����...
	if( inSendTime < (uint32_t)m_start_pts_ms ) {
		blog(LOG_INFO, "%s Error => SendTime: %lu, StartPTS: %I64d", m_strInnerName.c_str(), inSendTime, m_start_pts_ms);
		inSendTime = (uint32_t)m_start_pts_ms;
	}
	// ���㵱ǰ֡��ʱ��� => ʱ�����������������������...
	int nCalcPTS = inSendTime - (uint32_t)m_start_pts_ms;

	///////////////////////////////////////////////////////////////////////////
	// ��ʱʵ�飺��0~2000����֮�������������������Ϊ1����...
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
	// ע�⣺��ʱģ��Ŀǰ�Ѿ�����ˣ������������ʵ�����ģ��...
	///////////////////////////////////////////////////////////////////////////

	///////////////////////////////////////////////////////////////////////////
	// �����������֡ => ÿ��10�룬��1�������Ƶ����֡...
	///////////////////////////////////////////////////////////////////////////
	//if( (inFrame.dwSendTime/1000>0) && ((inFrame.dwSendTime/1000)%5==0) ) {
	//	blog("%s [%s] Discard Packet, PTS: %d", m_strInnerName.c_str(), inFrame.typeFlvTag == PT_TAG_AUDIO ? "Audio" : "Video", nCalcPTS);
	//	return;
	//}

	// ��������Ƶ���ͽ�����ز���...
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
	// ע�⣺input->conversion ����Ҫ�任�ĸ�ʽ��
	// ��ˣ�Ӧ�ô� video->info ���л�ȡԭʼ������Ϣ...
	// ͬʱ��sws_getContext ��ҪAVPixelFormat������video_format��ʽ...
	/////////////////////////////////////////////////////////////////////////
	// ����ffmpeg����־�ص�����...
	//av_log_set_level(AV_LOG_VERBOSE);
	//av_log_set_callback(my_av_logoutput);
	// ͳһ����Դ�����ʽ���ҵ�ѹ������Ҫ�����ظ�ʽ...
	enum AVPixelFormat nDestFormat = AV_PIX_FMT_YUV420P;
	enum AVPixelFormat nSrcFormat = inSrcFormat;
	int nSrcWidth = pSrcFrame->width;
	int nSrcHeight = pSrcFrame->height;
	// ע�⣺���������4��������������sws_scale����...
	int nDstWidth = nSrcWidth / 4 * 4;
	int nDstHeight = nSrcHeight / 4 * 4;
	// ����ʲô��ʽ������Ҫ�������ظ�ʽ��ת��...
	AVFrame * pDestFrame = av_frame_alloc();
	int nDestBufSize = avpicture_get_size(nDestFormat, nDstWidth, nDstHeight);
	uint8_t * pDestOutBuf = (uint8_t *)av_malloc(nDestBufSize);
	avpicture_fill((AVPicture *)pDestFrame, pDestOutBuf, nDestFormat, nDstWidth, nDstHeight);

	// ע�⣺���ﲻ��libyuv��ԭ���ǣ�ʹ��sws���򵥣����ø��ݲ�ͬ���ظ�ʽ���ò�ͬ�ӿ�...
	// ffmpeg�Դ���sws_scaleת��Ҳ��û������ģ�֮ǰ������������sws_getContext������Դ��Ҫ��ʽAVPixelFormat��д����video_format����ɵĸ�ʽ��λ����...
	// ע�⣺Ŀ�����ظ�ʽ����ΪAV_PIX_FMT_YUVJ420P������ʾ������Ϣ��������Ӱ���ʽת������ˣ�����ʹ�ò��ᾯ���AV_PIX_FMT_YUV420P��ʽ...
	struct SwsContext * img_convert_ctx = sws_getContext(nSrcWidth, nSrcHeight, nSrcFormat, nDstWidth, nDstHeight, nDestFormat, SWS_BICUBIC, NULL, NULL, NULL);
	int nReturn = sws_scale(img_convert_ctx, (const uint8_t* const*)pSrcFrame->data, pSrcFrame->linesize, 0, nSrcHeight, pDestFrame->data, pDestFrame->linesize);
	sws_freeContext(img_convert_ctx);

	// ����ת���������֡����...
	pDestFrame->width = nDstWidth;
	pDestFrame->height = nDstHeight;
	pDestFrame->format = nDestFormat;

	// ��ת����� YUV ���ݴ��̳� jpg �ļ�...
	AVCodecContext * pOutCodecCtx = NULL;
	bool bRetSave = false;
	do {
		// Ԥ�Ȳ���jpegѹ������Ҫ���������ݸ�ʽ...
		AVOutputFormat * avOutputFormat = av_guess_format("mjpeg", NULL, NULL); //av_guess_format(0, lpszJpgName, 0);
		AVCodec * pOutAVCodec = avcodec_find_encoder(avOutputFormat->video_codec);
		if (pOutAVCodec == NULL)
			break;
		// ����ffmpegѹ�����ĳ�������...
		pOutCodecCtx = avcodec_alloc_context3(pOutAVCodec);
		if (pOutCodecCtx == NULL)
			break;
		// ׼�����ݽṹ��Ҫ�Ĳ���...
		pOutCodecCtx->bit_rate = 200000;
		pOutCodecCtx->width = nDstWidth;
		pOutCodecCtx->height = nDstHeight;
		// ע�⣺û��ʹ�����䷽ʽ�����������ʽ�п��ܲ���YUVJ420P�����ѹ������������Ϊ���ݵ������Ѿ��̶���YUV420P...
		// ע�⣺����������YUV420P��ʽ��ѹ����������YUVJ420P��ʽ...
		pOutCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P; //avcodec_find_best_pix_fmt_of_list(pOutAVCodec->pix_fmts, (AVPixelFormat)-1, 1, 0);
		pOutCodecCtx->codec_id = avOutputFormat->video_codec; //AV_CODEC_ID_MJPEG;  
		pOutCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
		pOutCodecCtx->time_base.num = 1;
		pOutCodecCtx->time_base.den = 25;
		// �� ffmpeg ѹ����...
		if (avcodec_open2(pOutCodecCtx, pOutAVCodec, 0) < 0)
			break;
		// ����ͼ��������Ĭ����0.5���޸�Ϊ0.8(ͼƬ̫��,0.5�ոպ�)...
		pOutCodecCtx->qcompress = 0.5f;
		// ׼�����ջ��棬��ʼѹ��jpg����...
		int got_pic = 0;
		int nResult = 0;
		AVPacket pkt = { 0 };
		// �����µ�ѹ������...
		nResult = avcodec_encode_video2(pOutCodecCtx, &pkt, pDestFrame, &got_pic);
		// ����ʧ�ܻ�û�еõ�����ͼ�񣬼�������...
		if (nResult < 0 || !got_pic)
			break;
		// ��jpg�ļ����...
		FILE * pFile = fopen(szSavePath, "wb");
		// ��jpgʧ�ܣ�ע���ͷ���Դ...
		if (pFile == NULL) {
			av_packet_unref(&pkt);
			break;
		}
		// ���浽���̣����ͷ���Դ...
		fwrite(pkt.data, 1, pkt.size, pFile);
		av_packet_unref(&pkt);
		// �ͷ��ļ���������سɹ�...
		fclose(pFile); pFile = NULL;
		bRetSave = true;
	} while (false);
	// �����м�����Ķ���...
	if (pOutCodecCtx != NULL) {
		avcodec_close(pOutCodecCtx);
		av_free(pOutCodecCtx);
	}

	// �ͷ���ʱ��������ݿռ�...
	av_frame_free(&pDestFrame);
	av_free(pDestOutBuf);

	return bRetSave;
}*/

