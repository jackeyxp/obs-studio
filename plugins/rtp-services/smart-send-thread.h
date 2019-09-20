
#pragma once

#include <util/threading.h>
#include "OSThread.h"
#include "HYDefine.h"

class UDPSocket;
class CSmartSendThread : public OSThread
{
public:
	CSmartSendThread(CLIENT_TYPE inType, int nTCPSockFD, int nDBRoomID);
	virtual ~CSmartSendThread();
	virtual void Entry();
public:
	BOOL			InitThread(obs_output_t * lpObsOutput, const char * lpUdpAddr, int nUdpPort);
	BOOL			PushFrame(encoder_packet * lpEncPacket);
protected:
	uint8_t         GetTmTag() { return m_tmTag; }
	uint8_t         GetIdTag() { return m_idTag; }
	BOOL			InitVideo(string & inSPS, string & inPPS, int nWidth, int nHeight, int nFPS);
	BOOL			InitAudio(int inAudioRate, int inAudioChannel);
	BOOL			ParseAVHeader();
private:
	void			CloseSocket();
	void			doCalcAVBitRate();
	void			doSendCreateCmd();
	void			doSendHeaderCmd();
	void			doSendDeleteCmd();
	void			doSendDetectCmd();
	void			doSendLosePacket(bool bIsAudio);
	void			doSendPacket(bool bIsAudio);
	void			doRecvPacket();
	void			doSleepTo();

	void			doTagCreateProcess(char * lpBuffer, int inRecvLen);
	void			doTagHeaderProcess(char * lpBuffer, int inRecvLen);
	void			doTagDetectProcess(char * lpBuffer, int inRecvLen);
	void			doTagSupplyProcess(char * lpBuffer, int inRecvLen);

	void			doProcMaxConSeq(bool bIsAudio, uint32_t inMaxConSeq);
private:
	enum {
		kCmdSendCreate	= 0,				// 开始发送 => 创建命令状态
		kCmdSendHeader	= 1,				// 开始发送 => 序列头命令状态
		kCmdSendAVPack	= 2,				// 开始发送 => 音视频数据包状态
		kCmdUnkownState = 3,				// 未知状态 => 线程已经退出		
	} m_nCmdState;							// 命令状态变量...

	string			m_strSPS;				// 视频sps
	string			m_strPPS;				// 视频pps
	
	CLIENT_TYPE     m_nClientType;          // 终端内部类型...
	string          m_strInnerName;         // 终端内部名称...
	uint8_t         m_tmTag;                // 终端类型
	uint8_t         m_idTag;                // 终端标识

	UDPSocket	 *  m_lpUDPSocket;			// UDP对象
	obs_output_t *  m_lpObsOutput;			// obs输出对象
	pthread_mutex_t m_Mutex;                // 互斥对象 => 保护环形队列...

	uint16_t		m_HostServerPort;		// 服务器端口 => host
	uint32_t	    m_HostServerAddr;		// 服务器地址 => host

	bool			m_bNeedSleep;			// 休息标志 => 只要有发包或收包就不能休息...
	int32_t			m_start_dts_ms;			// 第一个数据帧的dts时间，0点计时...

	int				m_dt_to_dir;			// 发包路线方向 => TO_SERVER | TO_P2P
	int				m_server_rtt_ms;		// Server => 网络往返延迟值 => 毫秒
	int				m_server_rtt_var_ms;	// Server => 网络抖动时间差 => 毫秒

	rtp_detect_t	m_rtp_detect;			// RTP探测命令结构体
	rtp_create_t	m_rtp_create;			// RTP创建房间和直播结构体
	rtp_delete_t	m_rtp_delete;			// RTP删除房间和直播结构体
	rtp_header_t	m_rtp_header;			// RTP序列头结构体

	int64_t			m_next_create_ns;		// 下次发送创建命令时间戳 => 纳秒 => 每隔100毫秒发送一次...
	int64_t			m_next_header_ns;		// 下次发送序列头命令时间戳 => 纳秒 => 每隔100毫秒发送一次...
	int64_t			m_next_detect_ns;		// 下次发送探测包的时间戳 => 纳秒 => 每隔1秒发送一次...

	circlebuf		m_audio_circle;			// 音频环形队列
	circlebuf		m_video_circle;			// 视频环形队列

	uint32_t		m_nAudioCurPackSeq;		// 音频RTP当前打包序列号
	uint32_t		m_nAudioCurSendSeq;		// 音频RTP当前发送序列号

	uint32_t		m_nVideoCurPackSeq;		// 视频RTP当前打包序列号
	uint32_t		m_nVideoCurSendSeq;		// 视频RTP当前发送序列号

	GM_MapLose		m_AudioMapLose;			// 音频检测到的丢包集合队列...
	GM_MapLose		m_VideoMapLose;			// 视频检测到的丢包集合队列...

	int64_t			m_start_time_ns;		// 码流计算启动时间...
	int64_t			m_total_time_ms;		// 总的持续毫秒数...

	int64_t			m_audio_input_bytes;	// 音频输入总字节数...
	int64_t			m_video_input_bytes;	// 视频输入总字节数...
	int				m_audio_input_kbps;		// 音频输入平均码率...
	int				m_video_input_kbps;		// 视频输入平均码流...

	int64_t			m_total_output_bytes;	// 总的输出字节数...
	int64_t			m_audio_output_bytes;	// 音频输出总字节数...
	int64_t			m_video_output_bytes;	// 视频输出总字节数...
	int				m_total_output_kbps;	// 总的输出平均码流...
	int				m_audio_output_kbps;	// 音频输出平均码率...
	int				m_video_output_kbps;	// 视频输出平均码流...
};