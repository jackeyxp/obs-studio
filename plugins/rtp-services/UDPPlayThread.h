
#pragma once

#include "OSThread.h"
#include <util/threading.h>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
};

typedef	multimap<int64_t, AVPacket>		GM_MapPacket;	// DTS => AVPacket  => ����ǰ������֡ => ���� => 1/1000
typedef multimap<int64_t, AVFrame*>		GM_MapFrame;	// PTS => AVFrame   => ��������Ƶ֡ => ���� => 1/1000

class CPlaySDL;
class CDecoder
{
public:
	CDecoder();
	~CDecoder();
public:
	void		doSleepTo();
	void		doPushPacket(AVPacket & inPacket);
	int			GetMapPacketSize() { return m_MapPacket.size(); }
	int         GetMapFrameSize() { return m_MapFrame.size(); }
protected:
	AVCodec         *   m_lpCodec;			// ������...
	AVFrame         *   m_lpDFrame;			// ����ṹ��...
	AVCodecContext  *   m_lpDecoder;		// ����������...
	GM_MapPacket		m_MapPacket;		// ����ǰ������֡...
	GM_MapFrame			m_MapFrame;			// ����������֡....
	CPlaySDL        *   m_lpPlaySDL;		// ���ſ���
	bool				m_bNeedSleep;		// ��Ϣ��־ => ֻҪ�н���򲥷žͲ�����Ϣ...
	int64_t				m_play_next_ns;		// ��һ��Ҫ����֡��ϵͳ����ֵ...
	pthread_mutex_t     m_Mutex;            // �������
};

class CVideoThread : public CDecoder, public OSThread
{
public:
	CVideoThread(CPlaySDL * lpPlaySDL);
	virtual ~CVideoThread();
	virtual void Entry();
public:
	BOOL	InitVideo(string & inSPS, string & inPPS, int nWidth, int nHeight, int nFPS);
	void	doFillPacket(string & inData, int inPTS, bool bIsKeyFrame, int inOffset);
private:
	void	doDecodeFrame();
	void	doDisplayFrame();
private:
	int					m_nDstFPS;			// ��Ƶ֡��
	int					m_nDstWidth;		// ��Ƶ���
	int					m_nDstHeight;		// ��Ƶ�߶�
	string				m_strSPS;			// ��ƵSPS
	string				m_strPPS;			// ��ƵPPS
	obs_source_frame	m_obs_frame;		// obs����֡
};

class CAudioThread : public CDecoder, public OSThread
{
public:
	CAudioThread(CPlaySDL * lpPlaySDL);
	virtual ~CAudioThread();
	virtual void Entry();
public:
	BOOL	InitAudio(int nRateIndex, int nChannelNum);
	void	doFillPacket(string & inData, int inPTS, bool bIsKeyFrame, int inOffset);
private:
	void	doDecodeFrame();
	void	doDisplayFrame();
private:
	int		m_audio_rate_index;
	int		m_audio_channel_num;
	int		m_audio_sample_rate;
};

class CPlaySDL
{
public:
	CPlaySDL(obs_source_t * lpObsSource, int64_t inSysZeroNS, string & strInnerName);
	~CPlaySDL();
public:
	void		PushPacket(int zero_delay_ms, string & inData, int inTypeTag, bool bIsKeyFrame, uint32_t inSendTime);
	BOOL		InitVideo(string & inSPS, string & inPPS, int nWidth, int nHeight, int nFPS);
	BOOL		InitAudio(int nRateIndex, int nChannelNum);
	int			GetAPacketSize() { return ((m_lpAudioThread != NULL) ? m_lpAudioThread->GetMapPacketSize() : 0); }
	int			GetVPacketSize() { return ((m_lpVideoThread != NULL) ? m_lpVideoThread->GetMapPacketSize() : 0); }
	int			GetAFrameSize() { return ((m_lpAudioThread != NULL) ? m_lpAudioThread->GetMapFrameSize() : 0); }
	int			GetVFrameSize() { return ((m_lpVideoThread != NULL) ? m_lpVideoThread->GetMapFrameSize() : 0); }
	int64_t		GetZeroDelayMS() { return m_zero_delay_ms; }
	int64_t		GetSysZeroNS() { return m_sys_zero_ns; }
	int64_t		GetStartPtsMS() { return m_start_pts_ms; }
	string   &  GetInnerName() { return m_strInnerName; }
	obs_source_t * GetObsSource() { return m_lpObsSource; }
private:
	bool				m_bFindFirstVKey;	// �Ƿ��ҵ���һ����Ƶ�ؼ�֡��־...
	int64_t				m_sys_zero_ns;		// ϵͳ��ʱ��� => ����ʱ��� => ����...
	int64_t				m_start_pts_ms;		// ��һ֡��PTSʱ�����ʱ��� => ����...
	int64_t				m_zero_delay_ms;	// ��ʱ�趨������ => ���Ը�������Զ�����...

	CVideoThread    *   m_lpVideoThread;	// ��Ƶ�߳�...
	CAudioThread    *   m_lpAudioThread;	// ��Ƶ�߳�...
	obs_source_t    *   m_lpObsSource;		// obs��Դ����
	string              m_strInnerName;     // �ն��ڲ�����...
};
