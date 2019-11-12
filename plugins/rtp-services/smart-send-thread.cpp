
#include <obs-avc.h>
#include "UDPSocket.h"
#include "SocketUtils.h"
#include "smart-send-thread.h"

#define MILLISECOND_DEN   1000
static int32_t get_ms_time(struct encoder_packet *packet, int64_t val)
{
	return (int32_t)(val * MILLISECOND_DEN / packet->timebase_den);
}

CSmartSendThread::CSmartSendThread(CLIENT_TYPE inType, int nTCPSockFD, int nDBRoomID)
  : m_total_output_bytes(0)
  , m_audio_output_bytes(0)
  , m_video_output_bytes(0)
  , m_total_output_kbps(0)
  , m_audio_output_kbps(0)
  , m_video_output_kbps(0)
  , m_video_input_bytes(0)
  , m_audio_input_bytes(0)
  , m_audio_input_kbps(0)
  , m_video_input_kbps(0)
  , m_next_create_ns(-1)
  , m_next_header_ns(-1)
  , m_next_detect_ns(-1)
  , m_start_time_ns(0)
  , m_total_time_ms(0)
  , m_bNeedSleep(false)
  , m_lpUDPSocket(NULL)
  , m_lpObsOutput(NULL)
  , m_HostServerPort(0)
  , m_HostServerAddr(0)
  , m_nAudioCurPackSeq(0)
  , m_nAudioCurSendSeq(0)
  , m_nVideoCurPackSeq(0)
  , m_nVideoCurSendSeq(0)
  , m_server_rtt_var_ms(-1)
  , m_server_rtt_ms(-1)
  , m_start_dts_ms(-1)
{
	m_nClientType = inType;
	m_idTag = ID_TAG_PUSHER;
	// 保存数据源终端的内部名称...
	switch (m_nClientType) {
	case kClientStudent:
		m_strInnerName = ST_SEND_NAME;
		m_tmTag = TM_TAG_STUDENT;
		break;
	case kClientTeacher:
		m_strInnerName = TH_SEND_NAME;
		m_tmTag = TM_TAG_TEACHER;
		break;
	}
	// 初始化发包路线 => 服务器方向...
	m_dt_to_dir = DT_TO_SERVER;
	// 初始化命令状态...
	m_nCmdState = kCmdSendCreate;
	// 初始化rtp序列头结构体...
	memset(&m_rtp_detect, 0, sizeof(m_rtp_detect));
	memset(&m_rtp_create, 0, sizeof(m_rtp_create));
	memset(&m_rtp_delete, 0, sizeof(m_rtp_delete));
	memset(&m_rtp_header, 0, sizeof(m_rtp_header));
	// 初始化音视频环形队列，预分配空间...
	circlebuf_init(&m_audio_circle);
	circlebuf_init(&m_video_circle);
	circlebuf_reserve(&m_audio_circle, DEF_CIRCLE_SIZE);
	circlebuf_reserve(&m_video_circle, DEF_CIRCLE_SIZE);
	// 设置终端类型和结构体类型 => 推流者身份...
	m_rtp_detect.tm = m_rtp_create.tm = m_rtp_delete.tm = m_rtp_header.tm = m_tmTag;
	m_rtp_detect.id = m_rtp_create.id = m_rtp_delete.id = m_rtp_header.id = m_idTag;
	m_rtp_detect.pt = PT_TAG_DETECT;
	m_rtp_create.pt = PT_TAG_CREATE;
	m_rtp_delete.pt = PT_TAG_DELETE;
	m_rtp_header.pt = PT_TAG_HEADER;
	// 填充房间号和直播通道号...
	m_rtp_create.roomID = nDBRoomID;
	m_rtp_create.liveID = 0;
	m_rtp_delete.roomID = nDBRoomID;
	m_rtp_delete.liveID = 0;
	// 填充与远程关联的TCP套接字...
	m_rtp_create.tcpSock = nTCPSockFD;
	// 初始化互斥对象...
	pthread_mutex_init_value(&m_Mutex);
}

CSmartSendThread::~CSmartSendThread()
{
	blog(LOG_INFO, "%s == [~CSmartSendThread Thread] - Exit Start ==", m_strInnerName.c_str());
	// 未知状态，阻止继续塞包...
	m_nCmdState = kCmdUnkownState;
	// 停止线程，等待退出...
	this->StopAndWaitForThread();
	// 关闭UDPSocket对象...
	this->CloseSocket();
	// 释放音视频环形队列空间...
	circlebuf_free(&m_audio_circle);
	circlebuf_free(&m_video_circle);
	// 释放互斥对象...
	pthread_mutex_destroy(&m_Mutex);
	blog(LOG_INFO, "%s == [~CSmartSendThread Thread] - Exit End ==", m_strInnerName.c_str());
}

void CSmartSendThread::CloseSocket()
{
	if( m_lpUDPSocket != NULL ) {
		m_lpUDPSocket->Close();
		delete m_lpUDPSocket;
		m_lpUDPSocket = NULL;
	}
}

BOOL CSmartSendThread::InitVideo(string & inSPS, string & inPPS, int nWidth, int nHeight, int nFPS)
{
	// 设置视频标志...
	m_rtp_header.hasVideo = true;
	// 保存传递过来的参数信息...
	//m_rtp_header.vk_seq = 1;
	m_rtp_header.fpsNum = nFPS;
	m_rtp_header.picWidth = nWidth;
	m_rtp_header.picHeight = nHeight;
	m_rtp_header.spsSize = (uint16_t)inSPS.size();
	m_rtp_header.ppsSize = (uint16_t)inPPS.size();
	m_strSPS = inSPS; m_strPPS = inPPS;
	// 打印已初始化视频信息...
	blog(LOG_INFO, "%s InitVideo OK", m_strInnerName.c_str());
	// 线程一定要确认音视频都准备好之后才能启动...
	ASSERT( this->GetThreadHandle() == NULL );
	return true;
}

BOOL CSmartSendThread::InitAudio(int inAudioRate, int inAudioChannel)
{
	int	audio_rate_index = 0;
	// 首先解析并存储传递过来的参数...
	if (inAudioRate == 48000)
		audio_rate_index = 0x03;
	else if (inAudioRate == 44100)
		audio_rate_index = 0x04;
	else if (inAudioRate == 32000)
		audio_rate_index = 0x05;
	else if (inAudioRate == 24000)
		audio_rate_index = 0x06;
	else if (inAudioRate == 22050)
		audio_rate_index = 0x07;
	else if (inAudioRate == 16000)
		audio_rate_index = 0x08;
	else if (inAudioRate == 12000)
		audio_rate_index = 0x09;
	else if (inAudioRate == 11025)
		audio_rate_index = 0x0a;
	else if (inAudioRate == 8000)
		audio_rate_index = 0x0b;
	// 设置音频标志...
	m_rtp_header.hasAudio = true;
	// 保存采样率索引和声道...
	m_rtp_header.rateIndex = audio_rate_index;
	m_rtp_header.channelNum = inAudioChannel;
	// 打印已初始化音频信息...
	blog(LOG_INFO, "%s InitAudio OK", m_strInnerName.c_str());
	// 线程一定要确认音视频都准备好之后才能启动...
	ASSERT( this->GetThreadHandle() == NULL );
	return true;
}

BOOL CSmartSendThread::ParseAVHeader()
{
	// 判断obs输出对象是否有效...
	if (m_lpObsOutput == NULL)
		return false;
	// 先获取视频相关的格式头信息...
	obs_encoder_t * lpVEncoder = obs_output_get_video_encoder(m_lpObsOutput);
	video_output_info * lpVideo = (video_output_info*)obs_encoder_video(lpVEncoder);
	uint8_t * lpVHeader = NULL; uint8_t * lpSPS = NULL; uint8_t * lpPPS = NULL;
	size_t v_size = 0; size_t sps_size = 0; size_t pps_size = 0;
	string strSPS, strPPS;
	// 如果没有获取到有效的视频格式头对象，直接返回...
	if (lpVideo == NULL || lpVEncoder == NULL) {
		blog(LOG_INFO, "%s ParseAVHeader Failed.", m_strInnerName.c_str());
		return false;
	}
	// 如果获取视频扩展格式头信息失败，返回错误...
	if (!obs_encoder_get_extra_data(lpVEncoder, &lpVHeader, &v_size)) {
		blog(LOG_INFO, "%s ParseAVHeader Failed.", m_strInnerName.c_str());
		return false;
	}
	// 判断获取的视频扩展格式头信息是否有效...
	if (lpVHeader == NULL || v_size <= 0) {
		blog(LOG_INFO, "%s ParseAVHeader Failed.", m_strInnerName.c_str());
		return false;
	}
	// 解析视频的sps和pps格式信息，解析失败，直接返回...
	if (!obs_get_sps_pps(lpVHeader, v_size, &lpSPS, &sps_size, &lpPPS, &pps_size)) {
		blog(LOG_INFO, "%s ParseAVHeader Failed.", m_strInnerName.c_str());
		return false;
	}
	// 将SPS和PPS保存为string对象...
	strSPS.assign((char*)lpSPS, sps_size);
	strPPS.assign((char*)lpPPS, pps_size);
	// 获取音频的相关的格式头信息...
	obs_encoder_t * lpAEncoder = obs_output_get_audio_encoder(m_lpObsOutput, 0);
	audio_output_info * lpAudio = (audio_output_info*)obs_encoder_audio(lpAEncoder);
	if( lpAEncoder == NULL || lpAudio == NULL ) {
		blog(LOG_INFO, "%s ParseAVHeader Failed.", m_strInnerName.c_str());
		return false;
	}
	// 根据获取的音视频格式头，进行音视频对象的初始化工作...
	this->InitVideo(strSPS, strPPS, lpVideo->width, lpVideo->height, lpVideo->fps_num);
	this->InitAudio(lpAudio->samples_per_sec, (int)lpAudio->speakers);
	return true;
}

BOOL CSmartSendThread::InitThread(obs_output_t * lpObsOutput, const char * lpUdpAddr, int nUdpPort)
{
	// 保存并解析obs音视频格式头信息...
	m_lpObsOutput = lpObsOutput;
	// 从obs中解析音视频格式头，解析失败，直接返回...
	if (!this->ParseAVHeader())
		return false;
	// 首先，关闭socket...
	this->CloseSocket();
	// 再新建socket...
	ASSERT( m_lpUDPSocket == NULL );
	m_lpUDPSocket = new UDPSocket();
	// 建立UDP,发送音视频数据,接收指令...
	GM_Error theErr = GM_NoErr;
	theErr = m_lpUDPSocket->Open();
	if( theErr != GM_NoErr ) {
		MsgLogGM(theErr);
		return false;
	}
	// 设置异步模式...
	m_lpUDPSocket->NonBlocking();
	// 设置重复使用端口...
	m_lpUDPSocket->ReuseAddr();
	// 设置发送和接收缓冲区...
	m_lpUDPSocket->SetSocketSendBufSize(128 * 1024);
	m_lpUDPSocket->SetSocketRecvBufSize(128 * 1024);
	// 设置TTL网络穿越数值...
	m_lpUDPSocket->SetTTL(64);
	// 获取服务器地址信息 => 假设输入信息就是一个IPV4域名...
	const char * lpszAddr = lpUdpAddr;
	hostent * lpHost = gethostbyname(lpszAddr);
	if( lpHost != NULL && lpHost->h_addr_list != NULL ) {
		lpszAddr = inet_ntoa(*(in_addr*)lpHost->h_addr_list[0]);
	}
	// 保存服务器地址，简化SendTo参数......
	m_lpUDPSocket->SetRemoteAddr(lpszAddr, nUdpPort);
	// 服务器地址和端口转换成host格式，保存起来...
	m_HostServerPort = nUdpPort;
	m_HostServerAddr = ntohl(inet_addr(lpszAddr));
	// 启动组播接收线程...
	this->Start();
	// 返回执行结果...
	return true;
}

#ifdef DEBUG_FRAME
static void DoSaveSendFile(uint32_t inPTS, int inType, bool bIsKeyFrame, string & strFrame)
{
	static char szBuf[MAX_PATH] = {0};
	char * lpszPath = "F:/MP4/Dst/send.txt";
	FILE * pFile = fopen(lpszPath, "a+");
	sprintf(szBuf, "PTS: %lu, Type: %d, Key: %d, Size: %d\n", inPTS, inType, bIsKeyFrame, strFrame.size());
	fwrite(szBuf, 1, strlen(szBuf), pFile);
	fwrite(strFrame.c_str(), 1, strFrame.size(), pFile);
	fclose(pFile);
}

static void DoSaveSendSeq(uint32_t inPSeq, int inPSize, bool inPST, bool inPED, uint32_t inPTS)
{
	static char szBuf[MAX_PATH] = {0};
	char * lpszPath = "F:/MP4/Dst/send_seq.txt";
	FILE * pFile = fopen(lpszPath, "a+");
	sprintf(szBuf, "PSeq: %lu, PSize: %d, PST: %d, PED: %d, PTS: %lu\n", inPSeq, inPSize, inPST, inPED, inPTS);
	fwrite(szBuf, 1, strlen(szBuf), pFile);
	fclose(pFile);
}
#endif // DEBUG_FRAME

BOOL CSmartSendThread::PushFrame(encoder_packet * lpEncPacket)
{
	// 判断线程是否已经退出...
	if( this->IsStopRequested() ) {
		blog(LOG_INFO, "%s Error => Send Thread has been stoped", m_strInnerName.c_str());
		return false;
	}
	// 如果数据帧的长度为0，打印错误，直接返回...
	if( lpEncPacket->size <= 0 ) {
		blog(LOG_INFO, "%s Error => Input Frame Size is Zero", m_strInnerName.c_str());
		return false;
	}

	// 计算当前帧的dts时间，以及与pts的时间差 => 视频是序号，音频是刻度...
	int32_t cur_offset_ms = get_ms_time(lpEncPacket, lpEncPacket->pts - lpEncPacket->dts);
	int32_t cur_time_ms = get_ms_time(lpEncPacket, lpEncPacket->dts);
	// 记录第一帧的时间偏移值 => 默认值是负数...
	if (m_start_dts_ms < 0) { m_start_dts_ms = cur_time_ms; }
	// 当前帧时间减去第一帧时间偏移 => 注意修正为负数的情况...
	cur_time_ms -= m_start_dts_ms;
	if (cur_time_ms < 0) { cur_time_ms = 0; }

	// 将获取的数据包进行变量转移，方便后续使用...
	int pt_type = ((lpEncPacket->type == OBS_ENCODER_VIDEO) ? PT_TAG_VIDEO : PT_TAG_AUDIO);
	uint32_t dwSendTime = (uint32_t)cur_time_ms;
	bool is_keyframe = lpEncPacket->keyframe;
	uint8_t * pDataPtr = lpEncPacket->data;
	size_t nDataSize = lpEncPacket->size;

	// 打印所有的音视频数据帧...
	//blog(LOG_INFO, "%s Frame => PTS: %lu, Offset: %d, Type: %d, Key: %d, Size: %d", TH_SEND_NAME, dwSendTime, cur_offset_ms, pt_type, is_keyframe, nDataSize);

	///////////////////////////////////////////////////////////////////////////////////////////////////
	// 2018.10.10 - by jackey => 对数据帧的接收时刻点进行了修改，不进行状态限制...
	// 注意：为了尽量少丢数据，不能发包时的状态，也可以接收数据帧，进行数据打包...
	// 同时，由于发包是按最快速度发包，最开始的数据包很快就能发走，不会造成延时...
	///////////////////////////////////////////////////////////////////////////////////////////////////
	// 处于不能发包的状态时，打印所有的音视频数据帧...
	if( m_nCmdState == kCmdUnkownState || m_nCmdState <= kCmdSendHeader ) {
		blog(LOG_INFO, "%s Frame => PTS: %lu, Type: %d, Key: %d, Size: %d", m_strInnerName.c_str(), dwSendTime, pt_type, is_keyframe, nDataSize);
	}

	// 保存输入音视频字节总数，用于计算音视频输入码率...
	m_audio_input_bytes += ((pt_type == PT_TAG_AUDIO) ? nDataSize : 0);
	m_video_input_bytes += ((pt_type == PT_TAG_VIDEO) ? nDataSize : 0);

	// 对环形队列相关资源进行互斥保护...
	pthread_mutex_lock(&m_Mutex);

	// 音视频使用不同的打包对象和变量...
	uint32_t & nCurPackSeq = (pt_type == PT_TAG_AUDIO) ? m_nAudioCurPackSeq : m_nVideoCurPackSeq;
	circlebuf & cur_circle = (pt_type == PT_TAG_AUDIO) ? m_audio_circle : m_video_circle;

	// 构造RTP包头结构体 => 推流者...
	rtp_hdr_t rtpHeader = {0};
	rtpHeader.tm  = this->GetTmTag();
	rtpHeader.id  = this->GetIdTag();
	rtpHeader.pt  = pt_type;
	rtpHeader.pk  = is_keyframe;
	rtpHeader.ts  = dwSendTime;
	// 计算需要分片的个数 => 需要注意累加最后一个分片...
	int nSliceSize = DEF_MTU_SIZE;
	int nFrameSize = nDataSize;
	int nSliceNum = nFrameSize / DEF_MTU_SIZE;
	char * lpszFramePtr = (char*)pDataPtr;
	nSliceNum += ((nFrameSize % DEF_MTU_SIZE) ? 1 : 0);
	int nEndSize = nFrameSize - (nSliceNum - 1) * DEF_MTU_SIZE;
	// 进行循环压入环形队列当中...
	for(int i = 0; i < nSliceNum; ++i) {
		rtpHeader.seq = ++nCurPackSeq; // 累加打包序列号...
		rtpHeader.pst = ((i == 0) ? true : false); // 是否是第一个分片...
		rtpHeader.ped = ((i+1 == nSliceNum) ? true: false); // 是否是最后一个分片...
		rtpHeader.psize = rtpHeader.ped ? nEndSize : DEF_MTU_SIZE; // 如果是最后一个分片，取计算值(不能取余数，如果是MTU整数倍会出错)，否则，取MTU值...
		ASSERT( rtpHeader.psize > 0 && rtpHeader.psize <= DEF_MTU_SIZE );
		// 计算填充数据长度...
		int nZeroSize = DEF_MTU_SIZE - rtpHeader.psize;
		// 计算分片包的数据头指针...
		const char * lpszSlicePtr = lpszFramePtr + i * DEF_MTU_SIZE;
		// 加入环形队列当中 => rtp_hdr_t + slice + Zero
		circlebuf_push_back(&cur_circle, &rtpHeader, sizeof(rtpHeader));
		circlebuf_push_back(&cur_circle, lpszSlicePtr, rtpHeader.psize);
		// 加入填充数据内容，保证数据总是保持一个MTU单元大小...
		if( nZeroSize > 0 ) {
			circlebuf_push_back_zero(&cur_circle, nZeroSize);
		}
		// 打印调试信息...
		//blog(LOG_INFO, "%s Seq: %lu, Type: %d, Key: %d, Size: %d, TS: %lu", m_strInnerName.c_str(),
		//		rtpHeader.seq, rtpHeader.pt, rtpHeader.pk, rtpHeader.psize, rtpHeader.ts);
	}
	// 对环形队列相关资源互斥保护结束...
	pthread_mutex_unlock(&m_Mutex);
	return true;
}

void CSmartSendThread::doCalcAVBitRate()
{
	// 设定码率的检测刻度值 => 越小越精确，可以判断瞬时码率...
	int rate_tick_ms = 1000;
	// 计算持续线程启动到现在总的毫秒数，如果不到1秒钟，直接返回...
	int64_t cur_total_ms = (os_gettime_ns() - m_start_time_ns)/1000000;
	if((cur_total_ms - m_total_time_ms) < rate_tick_ms )
		return;
	// 保存总的持续时间 => 毫秒数...
	m_total_time_ms = cur_total_ms;
	// 根据音视频当前输入输出的总字节数，计算输入输出平均码率...
	m_audio_input_kbps = (int)(m_audio_input_bytes * 8 / (m_total_time_ms / rate_tick_ms)) / 1024;
	m_video_input_kbps = (int)(m_video_input_bytes * 8 / (m_total_time_ms / rate_tick_ms)) / 1024;
	//m_audio_output_kbps = (m_audio_output_bytes * 8 / (m_total_time_ms / rate_tick_ms)) / 1024;
	//m_video_output_kbps = (m_video_output_bytes * 8 / (m_total_time_ms / rate_tick_ms)) / 1024;
	//m_total_output_kbps = (m_total_output_bytes * 8 / (m_total_time_ms / rate_tick_ms)) / 1024;
	m_audio_output_kbps = (int)(m_audio_output_bytes * 8) / 1024; m_audio_output_bytes = 0;
	m_video_output_kbps = (int)(m_video_output_bytes * 8) / 1024; m_video_output_bytes = 0;
	m_total_output_kbps = (int)(m_total_output_bytes * 8) / 1024; m_total_output_bytes = 0;
	// 打印计算获得的音视频输入输出平均码流值...
	//blog(LOG_INFO, "%s AVBitRate =>  audio_input: %d kbps,  video_input: %d kbps", m_strInnerName.c_str(), m_audio_input_kbps, m_video_input_kbps);
	//blog(LOG_INFO, "%s AVBitRate => audio_output: %d kbps, video_output: %d kbps, total_output: %d kbps", m_strInnerName.c_str(), m_audio_output_kbps, m_video_output_kbps, m_total_output_kbps);
}

void CSmartSendThread::Entry()
{
	// 码率计算计时起点...
	m_start_time_ns = os_gettime_ns();
	// 开始进行线程循环操作...
	while( !this->IsStopRequested() ) {
		// 计算音视频输入输出平均码流...
		this->doCalcAVBitRate();
		// 设置休息标志 => 只要有发包或收包就不能休息...
		m_bNeedSleep = true;
		// 发送观看端需要的音频丢失数据包...
		this->doSendLosePacket(true);
		// 发送观看端需要的视频丢失数据包...
		this->doSendLosePacket(false);
		// 发送创建房间和直播通道命令包...
		this->doSendCreateCmd();
		// 发送序列头命令包...
		this->doSendHeaderCmd();
		// 发送探测命令包...
		this->doSendDetectCmd();
		// 发送一个封装好的音频RTP数据包...
		this->doSendPacket(true);
		// 发送一个封装好的视频RTP数据包...
		this->doSendPacket(false);
		// 接收一个到达的服务器反馈包...
		this->doRecvPacket();
		// 等待发送或接收下一个数据包...
		this->doSleepTo();
	}
	// 只发送一次删除命令包...
	this->doSendDeleteCmd();
}

void CSmartSendThread::doSendDeleteCmd()
{
	GM_Error theErr = GM_NoErr;
	if( m_lpUDPSocket == NULL )
		return;
	// 套接字有效，直接发送删除命令...
	ASSERT( m_lpUDPSocket != NULL );
	theErr = m_lpUDPSocket->SendTo((void*)&m_rtp_delete, sizeof(m_rtp_delete));
	// 累加总的输出字节数，便于计算输出平均码流...
	m_total_output_bytes += sizeof(m_rtp_delete);
	// 打印已发送删除命令包...
	blog(LOG_INFO, "%s Send Delete RoomID: %lu", m_strInnerName.c_str(), m_rtp_delete.roomID);
}

void CSmartSendThread::doSendCreateCmd()
{
	// 如果命令状态不是创建命令，不发送命令，直接返回...
	if( m_nCmdState != kCmdSendCreate )
		return;
	// 每隔100毫秒发送创建命令包 => 必须转换成有符号...
	int64_t cur_time_ns = os_gettime_ns();
	int64_t period_ns = 100 * 1000000;
	// 如果发包时间还没到，直接返回...
	if( m_next_create_ns > cur_time_ns )
		return;
	ASSERT( cur_time_ns >= m_next_create_ns );
	// 首先，发送一个创建房间命令...
	GM_Error theErr = m_lpUDPSocket->SendTo((void*)&m_rtp_create, sizeof(m_rtp_create));
	(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
	// 累加总的输出字节数，便于计算输出平均码流...
	m_total_output_bytes += sizeof(m_rtp_create);
	// 打印已发送创建命令包 => 第一个包有可能没有发送出去，也返回正常...
	blog(LOG_INFO, "%s Send Create RoomID: %lu", m_strInnerName.c_str(), m_rtp_create.roomID);
	// 计算下次发送创建命令的时间戳...
	m_next_create_ns = os_gettime_ns() + period_ns;
	// 修改休息状态 => 已经有发包，不能休息...
	m_bNeedSleep = false;
}

void CSmartSendThread::doSendHeaderCmd()
{
	// 如果命令状态不是序列头命令，不发送命令，直接返回...
	if( m_nCmdState != kCmdSendHeader )
		return;
	// 每隔100毫秒发送序列头命令包 => 必须转换成有符号...
	int64_t cur_time_ns = os_gettime_ns();
	int64_t period_ns = 100 * 1000000;
	// 如果发包时间还没到，直接返回...
	if( m_next_header_ns > cur_time_ns )
		return;
	ASSERT( cur_time_ns >= m_next_header_ns );
	// 然后，发送序列头命令包...
	string strSeqHeader;
	strSeqHeader.append((char*)&m_rtp_header, sizeof(m_rtp_header));
	// 加入SPS数据区内容...
	if( m_strSPS.size() > 0 ) {
		strSeqHeader.append(m_strSPS);
	}
	// 加入PPS数据区内容...
	if( m_strPPS.size() > 0 ) {
		strSeqHeader.append(m_strPPS);
	}
	// 调用套接字接口，直接发送RTP数据包...
	GM_Error theErr = m_lpUDPSocket->SendTo((void*)strSeqHeader.c_str(), strSeqHeader.size());
	(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
	// 累加总的输出字节数，便于计算输出平均码流...
	m_total_output_bytes += strSeqHeader.size();
	// 打印已发送序列头命令包...
	blog(LOG_INFO, "%s Send Header SPS: %lu, PPS: %d", m_strInnerName.c_str(), m_strSPS.size(), m_strPPS.size());
	// 计算下次发送创建命令的时间戳...
	m_next_header_ns = os_gettime_ns() + period_ns;
	// 修改休息状态 => 已经有发包，不能休息...
	m_bNeedSleep = false;
}

void CSmartSendThread::doSendDetectCmd()
{
	// 每隔1秒发送一个探测命令包 => 必须转换成有符号...
	int64_t cur_time_ns = os_gettime_ns();
	int64_t period_ns = 1000 * 1000000;
	// 第一个探测包延时1/3秒发送 => 避免第一个探测包先到达，引发服务器发送重建命令...
	if( m_next_detect_ns < 0 ) { 
		m_next_detect_ns = cur_time_ns + period_ns / 3;
	}
	// 如果发包时间还没到，直接返回...
	if( m_next_detect_ns > cur_time_ns )
		return;
	ASSERT( cur_time_ns >= m_next_detect_ns );
	// 通过服务器中转的探测 => 将探测起点时间转换成毫秒，累加探测计数器...
	m_rtp_detect.tsSrc  = (uint32_t)(cur_time_ns / 1000000);
	m_rtp_detect.dtDir  = DT_TO_SERVER;
	m_rtp_detect.dtNum += 1;

	// 注意：推流端本身不会用来针对观看者进行补包，补包都放到服务器端完成...
	// 因此，采用了根据探测结果进行丢包的处理方法，而不是固定缓存的方法...
	// 采用了新的拥塞处理 => 删除指定缓存时间点之前的音视频数据包...
	//this->doCalcAVJamStatus();

	// 调用接口发送探测命令包 => 推流端只有一个服务器探测方向...
	GM_Error theErr = m_lpUDPSocket->SendTo((void*)&m_rtp_detect, sizeof(m_rtp_detect));
	(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
	// 累加总的输出字节数，便于计算输出平均码流...
	m_total_output_bytes += sizeof(m_rtp_detect);
	// 打印已发送探测命令包...
	//blog(LOG_INFO, "%s Send Detect dtNum: %d", m_strInnerName.c_str(), m_rtp_detect.dtNum);
	// 计算下次发送探测命令的时间戳...
	m_next_detect_ns = os_gettime_ns() + period_ns;
	// 修改休息状态 => 已经有发包，不能休息...
	m_bNeedSleep = false;
}

void CSmartSendThread::doSendLosePacket(bool bIsAudio)
{
	if( m_lpUDPSocket == NULL )
		return;
	// 对环形队列相关资源进行互斥保护...
	pthread_mutex_lock(&m_Mutex);
	do {
		// 根据数据包类型，找到丢包集合、环形队列...
		GM_MapLose & theMapLose = bIsAudio ? m_AudioMapLose : m_VideoMapLose;
		circlebuf  & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;
		// 丢包集合队列为空，直接返回...
		if( theMapLose.size() <= 0 )
			break;
		// 拿出一个丢包记录，无论是否发送成功，都要删除这个丢包记录...
		// 如果观看端，没有收到这个数据包，会再次发起补包命令...
		GM_MapLose::iterator itorItem = theMapLose.begin();
		rtp_lose_t rtpLose = itorItem->second;
		theMapLose.erase(itorItem);
		// 如果环形队列为空，直接返回...
		if( cur_circle.size <= 0 )
			break;
		// 先找到环形队列中最前面数据包的头指针 => 最小序号...
		GM_Error    theErr = GM_NoErr;
		rtp_hdr_t * lpFrontHeader = NULL;
		rtp_hdr_t * lpSendHeader = NULL;
		size_t nSendPos = 0, nSendSize = 0;
		/////////////////////////////////////////////////////////////////////////////////////////////////
		// 注意：千万不能在环形队列当中进行指针操作，当start_pos > end_pos时，可能会有越界情况...
		// 所以，一定要用接口读取完整的数据包之后，再进行操作；如果用指针，一旦发生回还，就会错误...
		/////////////////////////////////////////////////////////////////////////////////////////////////
		const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
		char szPacketBuffer[nPerPackSize] = {0};
		circlebuf_peek_front(&cur_circle, szPacketBuffer, nPerPackSize);
		lpFrontHeader = (rtp_hdr_t*)szPacketBuffer;
		// 如果要补充的数据包序号比最小序号还要小 => 没有找到，直接返回...
		if( rtpLose.lose_seq < lpFrontHeader->seq ) {
			blog(LOG_INFO, "%s Supply Error => lose: %lu, min: %lu, Type: %d", m_strInnerName.c_str(), rtpLose.lose_seq, lpFrontHeader->seq, rtpLose.lose_type);
			break;
		}
		ASSERT( rtpLose.lose_seq >= lpFrontHeader->seq );
		// 注意：环形队列当中的序列号一定是连续的...
		// 两者之差就是要发送数据包的头指针位置...
		nSendPos = (rtpLose.lose_seq - lpFrontHeader->seq) * nPerPackSize;
		// 如果补包位置大于或等于环形队列长度 => 补包越界...
		if( nSendPos >= cur_circle.size ) {
			blog(LOG_INFO, "%s Supply Error => Position Excessed", m_strInnerName.c_str());
			break;
		}
		////////////////////////////////////////////////////////////////////////////////////////////////////
		// 注意：不能用简单的指针操作，环形队列可能会回还，必须用接口 => 从指定相对位置拷贝指定长度数据...
		// 获取将要发送数据包的包头位置和有效数据长度...
		////////////////////////////////////////////////////////////////////////////////////////////////////
		memset(szPacketBuffer, 0, nPerPackSize);
		circlebuf_read(&cur_circle, nSendPos, szPacketBuffer, nPerPackSize);
		lpSendHeader = (rtp_hdr_t*)szPacketBuffer;
		// 如果找到的序号位置不对 => 缓存里面的所有包都是有效包，因为是数据源头...
		if( lpSendHeader->seq != rtpLose.lose_seq ) {
			blog(LOG_INFO, "%s Supply Error => Seq: %lu, Find: %lu, Type: %d", m_strInnerName.c_str(), rtpLose.lose_seq, lpSendHeader->seq, rtpLose.lose_type);
			break;
		}
		// 获取有效的数据区长度 => 包头 + 数据...
		nSendSize = sizeof(rtp_hdr_t) + lpSendHeader->psize;
		// 注意：推流端只能有一个服务器方向的补包路线...
		ASSERT(m_dt_to_dir == DT_TO_SERVER);
		theErr = m_lpUDPSocket->SendTo((void*)lpSendHeader, nSendSize);
		// 如果有错误发生，打印出来...
		(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
		// 累加总的输出字节数，便于计算输出平均码流...
		m_total_output_bytes += nSendSize;
		// 打印已经发送补包信息...
		blog(LOG_INFO, "%s Supply Send => Dir: %d, Seq: %lu, TS: %lu, Slice: %d, Type: %d", m_strInnerName.c_str(), m_dt_to_dir, lpSendHeader->seq, lpSendHeader->ts, lpSendHeader->psize, lpSendHeader->pt);
		// 修改休息状态 => 已经有发包，不能休息...
		m_bNeedSleep = false;
	} while (false);
	// 对环形队列相关资源互斥保护结束...
	pthread_mutex_unlock(&m_Mutex);
}

void CSmartSendThread::doSendPacket(bool bIsAudio)
{
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 2018.10.10 - by jackey => 之前是对收包限制状态，限制是对发包限制状态，可以避免数据帧由于状态原因而丢失...
	// 注意：改进后的方式，最好加入拥塞检测，在 doSendDetectCmd() 中检测，目前没有做，学生端已加入（超过4秒缓存就认为拥塞，需要中断）
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 如果命令状态不对，打印已缓存的音视频数据帧信息，并返回，不发包...
	if (m_nCmdState == kCmdUnkownState || m_nCmdState <= kCmdSendHeader) {
		//blog(LOG_INFO, "%s State Error => VideoSize: %d, AudioSize: %d", m_strInnerName.c_str(), m_video_circle.size, m_audio_circle.size);
		return;
	}
	// 对环形队列相关资源进行互斥保护...
	pthread_mutex_lock(&m_Mutex);
	do {
		// 根据数据包类型，找到打包序号、发包序号、环形队列...
		uint32_t  & nCurPackSeq = bIsAudio ? m_nAudioCurPackSeq : m_nVideoCurPackSeq;
		uint32_t  & nCurSendSeq = bIsAudio ? m_nAudioCurSendSeq : m_nVideoCurSendSeq;
		circlebuf & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;
		// 如果环形队列没有可发送数据，直接返回...
		if( cur_circle.size <= 0 || m_lpUDPSocket == NULL )
			break;
		// 如果要发送序列号比打包序列号还要大 => 没有数据包可以发送...
		if( nCurSendSeq > nCurPackSeq )
			break;
		// 取出最前面的RTP数据包，但不从环形队列中移除 => 目的是给接收端补包用...
		GM_Error    theErr = GM_NoErr;
		rtp_hdr_t * lpFrontHeader = NULL;
		rtp_hdr_t * lpSendHeader = NULL;
		size_t nSendPos = 0, nSendSize = 0;
		/////////////////////////////////////////////////////////////////////////////////////////////////
		// 注意：千万不能在环形队列当中进行指针操作，当start_pos > end_pos时，可能会有越界情况...
		// 所以，一定要用接口读取完整的数据包之后，再进行操作；如果用指针，一旦发生回还，就会错误...
		/////////////////////////////////////////////////////////////////////////////////////////////////
		const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
		char szPacketBuffer[nPerPackSize] = {0};
		circlebuf_peek_front(&cur_circle, szPacketBuffer, nPerPackSize);
		// 计算环形队列中最前面数据包的头指针 => 最小序号...
		lpFrontHeader = (rtp_hdr_t*)szPacketBuffer;
		// 第一次发包 或 发包序号太小 => 使用最前面包的序列号...
		if((nCurSendSeq <= 0) || (nCurSendSeq < lpFrontHeader->seq)) {
			nCurSendSeq = lpFrontHeader->seq;
		}
		/////////////////////////////////////////////////////////////////////////////////
		// 环形队列最小序号 => min_id => lpFrontHeader->seq
		// 环形队列最大序号 => max_id => nCurPackSeq
		/////////////////////////////////////////////////////////////////////////////////
		// 将要发送的数据包序号不能小于最前面包的序列号...
		ASSERT( nCurSendSeq >= lpFrontHeader->seq );
		ASSERT( nCurSendSeq <= nCurPackSeq );
		// 两者之差就是要发送数据包的头指针位置...
		nSendPos = (nCurSendSeq - lpFrontHeader->seq) * nPerPackSize;
		////////////////////////////////////////////////////////////////////////////////////////////////////
		// 注意：不能用简单的指针操作，环形队列可能会回还，必须用接口 => 从指定相对位置拷贝指定长度数据...
		// 获取将要发送数据包的包头位置和有效数据长度...
		////////////////////////////////////////////////////////////////////////////////////////////////////
		memset(szPacketBuffer, 0, nPerPackSize);
		circlebuf_read(&cur_circle, nSendPos, szPacketBuffer, nPerPackSize);
		lpSendHeader = (rtp_hdr_t*)szPacketBuffer;
		// 如果要发送的数据位置越界或无效，直接返回...
		if( lpSendHeader == NULL || lpSendHeader->seq <= 0 )
			break;
		// 获取有效的数据区长度 => 包头 + 数据...
		nSendSize = sizeof(rtp_hdr_t) + lpSendHeader->psize;
		// 注意：推流端只能有一个服务器方向的发包路线...
		ASSERT(m_dt_to_dir == DT_TO_SERVER);
		theErr = m_lpUDPSocket->SendTo((void*)lpSendHeader, nSendSize);
		// 如果有错误发生，打印出来...
		(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;

		// 累加总体、音视频输出总字节数...
		m_total_output_bytes += nSendSize;
		m_audio_output_bytes += (bIsAudio ? nSendSize : 0);
		m_video_output_bytes += (bIsAudio ? 0 : nSendSize);

		/////////////////////////////////////////////////////////////////////////////////
		// 实验：随机丢包...
		/////////////////////////////////////////////////////////////////////////////////
		/*if( nCurSendSeq % 3 != 2 ) {
			// 调用套接字接口，直接发送RTP数据包...
			theErr = m_lpUDPSocket->SendTo((void*)lpSendHeader, nSendSize);
			(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
		}*/
		/////////////////////////////////////////////////////////////////////////////////

		// 成功发送数据包 => 累加发送序列号...
		++nCurSendSeq;
		// 修改休息状态 => 已经有发包，不能休息...
		m_bNeedSleep = false;

		// 打印调试信息 => 刚刚发送的数据包...
		//int nZeroSize = DEF_MTU_SIZE - lpSendHeader->psize;
		//blog(LOG_INFO, "%s Size: %d, Type: %d, Seq: %lu, TS: %lu, pst: %d, ped: %d, Slice: %d, Zero: %d", m_strInnerName.c_str(), nSendSize, lpSendHeader->pt, lpSendHeader->seq, lpSendHeader->ts, lpSendHeader->pst, lpSendHeader->ped, lpSendHeader->psize, nZeroSize);
	} while (false);
	// 对环形队列相关资源互斥保护结束...
	pthread_mutex_unlock(&m_Mutex);
}

void CSmartSendThread::doRecvPacket()
{
	if( m_lpUDPSocket == NULL )
		return;
	GM_Error theErr = GM_NoErr;
	UInt32   outRecvLen = 0;
	UInt32   outRemoteAddr = 0;
	UInt16   outRemotePort = 0;
	UInt32   inBufLen = MAX_BUFF_LEN;
	char     ioBuffer[MAX_BUFF_LEN] = {0};
	// 调用接口从网络层获取数据包 => 这里是异步套接字，会立即返回 => 不管错误...
	theErr = m_lpUDPSocket->RecvFrom(&outRemoteAddr, &outRemotePort, ioBuffer, inBufLen, &outRecvLen);
	// 注意：这里不用打印错误信息，没有收到数据就立即返回...
	if( theErr != GM_NoErr || outRecvLen <= 0 )
		return;
	// 修改休息状态 => 已经成功收包，不能休息...
	m_bNeedSleep = false;

	// 判断最大接收数据长度 => DEF_MTU_SIZE + rtp_hdr_t
	UInt32 nMaxSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	if (outRecvLen > nMaxSize) {
		blog(LOG_INFO, "[Send-Error] Max => %lu, Addr => %lu:%d, Size => %lu", nMaxSize, outRemoteAddr, outRemotePort, outRecvLen);
		return;
	}

	// 对环形队列相关资源进行互斥保护...
	pthread_mutex_lock(&m_Mutex);

	// 获取第一个字节的高4位，得到数据包类型...
    uint8_t ptTag = (ioBuffer[0] >> 4) & 0x0F;

	// 对收到命令包进行类型分发...
	switch( ptTag )
	{
	case PT_TAG_CREATE:  this->doTagCreateProcess(ioBuffer, outRecvLen); break;
	case PT_TAG_HEADER:  this->doTagHeaderProcess(ioBuffer, outRecvLen); break;

	case PT_TAG_DETECT:	 this->doTagDetectProcess(ioBuffer, outRecvLen); break;
	case PT_TAG_SUPPLY:  this->doTagSupplyProcess(ioBuffer, outRecvLen); break;
	}

	// 对环形队列相关资源互斥保护结束...
	pthread_mutex_unlock(&m_Mutex);
}

void CSmartSendThread::doTagCreateProcess(char * lpBuffer, int inRecvLen)
{
	// 通过 rtp_hdr_t 做为载体发送过来的...
	if( m_lpUDPSocket == NULL || lpBuffer == NULL || inRecvLen <= 0 || inRecvLen < sizeof(rtp_hdr_t) )
		return;
	// 获取数据包结构体...
	rtp_hdr_t rtpHdr = {0};
	memcpy(&rtpHdr, lpBuffer, sizeof(rtpHdr));
	// 判断数据包的有效性 => 必须是服务器反馈的 Create 命令...
	if( rtpHdr.tm != TM_TAG_SERVER || rtpHdr.id != ID_TAG_SERVER || rtpHdr.pt != PT_TAG_CREATE )
		return;
	// 更新直播编号到相关配置当中...
	m_rtp_create.liveID = rtpHdr.noset;
	m_rtp_delete.liveID = rtpHdr.noset;
	// 修改命令状态 => 开始发送序列头...
	m_nCmdState = kCmdSendHeader;
	// 打印收到服务器反馈的创建命令包 => 注意打印直播编号字段...
	blog(LOG_INFO, "%s Recv Create from Server, LiveID: %u", m_strInnerName.c_str(), rtpHdr.noset);
}

void CSmartSendThread::doTagHeaderProcess(char * lpBuffer, int inRecvLen)
{
	// 通过 rtp_hdr_t 做为载体发送过来的...
	if( m_lpUDPSocket == NULL || lpBuffer == NULL || inRecvLen <= 0 || inRecvLen < sizeof(rtp_hdr_t) )
		return;
	// 获取数据包结构体...
	rtp_hdr_t rtpHdr = {0};
	memcpy(&rtpHdr, lpBuffer, sizeof(rtpHdr));
	// 判断数据包的有效性 => 必须是服务器反馈的序列头命令...
	if( rtpHdr.tm != TM_TAG_SERVER || rtpHdr.id != ID_TAG_SERVER || rtpHdr.pt != PT_TAG_HEADER )
		return;
	// 修改命令状态 => 可以发送数据包了...
	m_nCmdState = kCmdSendAVPack;
	// 打印收到服务器反馈的序列头命令包...
	blog(LOG_INFO, "%s Recv Header from Server", m_strInnerName.c_str());
	// 立即通知上层可以捕捉音视频数据了，之后才会产生压缩数据帧...
	obs_output_begin_data_capture(m_lpObsOutput, 0);
}

void CSmartSendThread::doTagSupplyProcess(char * lpBuffer, int inRecvLen)
{
	if( m_lpUDPSocket == NULL || lpBuffer == NULL || inRecvLen <= 0 || inRecvLen < sizeof(rtp_supply_t) )
		return;
    // 通过第一个字节的低2位，判断终端类型...
    uint8_t tmTag = lpBuffer[0] & 0x03;
    // 获取第一个字节的中2位，得到终端身份...
    uint8_t idTag = (lpBuffer[0] >> 2) & 0x03;
    // 获取第一个字节的高4位，得到数据包类型...
    uint8_t ptTag = (lpBuffer[0] >> 4) & 0x0F;
	// 注意：只处理 服务器端 发出的补包命令 => 推流数据包在服务器被缓存...
	if( tmTag != TM_TAG_SERVER || idTag != ID_TAG_SERVER )
		return;
	// 获取补包命令包内容...
	rtp_supply_t rtpSupply = {0};
	int nHeadSize = sizeof(rtp_supply_t);
	memcpy(&rtpSupply, lpBuffer, nHeadSize);
	if( (rtpSupply.suSize <= 0) || ((nHeadSize + rtpSupply.suSize) != inRecvLen) )
		return;
	// 根据数据包类型，找到丢包集合...
	GM_MapLose & theMapLose = (rtpSupply.suType == PT_TAG_AUDIO) ? m_AudioMapLose : m_VideoMapLose;
	// 获取需要补包的序列号，加入到补包队列当中...
	char * lpDataPtr = lpBuffer + nHeadSize;
	int    nDataSize = rtpSupply.suSize;
	while( nDataSize > 0 ) {
		uint32_t   nLoseSeq = 0;
		rtp_lose_t rtpLose = {0};
		// 获取补包序列号...
		memcpy(&nLoseSeq, lpDataPtr, sizeof(int));
		// 如果序列号已经存在，增加补包次数，不存在，增加新记录...
		if( theMapLose.find(nLoseSeq) != theMapLose.end() ) {
			rtp_lose_t & theFind = theMapLose[nLoseSeq];
			theFind.lose_type = rtpSupply.suType;
			theFind.lose_seq = nLoseSeq;
			++theFind.resend_count;
		} else {
			rtpLose.lose_seq = nLoseSeq;
			rtpLose.lose_type = rtpSupply.suType;
			rtpLose.resend_time = (uint32_t)(os_gettime_ns() / 1000000);
			theMapLose[rtpLose.lose_seq] = rtpLose;
		}
		// 移动数据区指针位置...
		lpDataPtr += sizeof(int);
		nDataSize -= sizeof(int);
	}
	// 打印已收到补包命令...
	blog(LOG_INFO, "%s Supply Recv => Count: %d, Type: %d", m_strInnerName.c_str(), rtpSupply.suSize / sizeof(int), rtpSupply.suType);
}

void CSmartSendThread::doProcMaxConSeq(bool bIsAudio, uint32_t inMaxConSeq)
{
	// 根据数据包类型，找到环形队列、最大播放序号、当前拥塞点...
	circlebuf & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;
	uint32_t & nCurSendSeq = bIsAudio ? m_nAudioCurSendSeq : m_nVideoCurSendSeq;
	uint32_t & nCurPackSeq = bIsAudio ? m_nAudioCurPackSeq : m_nVideoCurPackSeq;
	// 如果输入的最大连续包号无效或环形队列为空，直接返回...
	if( inMaxConSeq <= 0 || cur_circle.size <= 0 )
		return;
	// 先找到环形队列中最前面数据包的头指针 => 最小序号...
	rtp_hdr_t * lpFrontHeader = NULL;
	/////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：千万不能在环形队列当中进行指针操作，当start_pos > end_pos时，可能会有越界情况...
	// 所以，一定要用接口读取完整的数据包之后，再进行操作；如果用指针，一旦发生回还，就会错误...
	/////////////////////////////////////////////////////////////////////////////////////////////////
	const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	char szPacketBuffer[nPerPackSize] = {0};
	circlebuf_peek_front(&cur_circle, szPacketBuffer, nPerPackSize);
	lpFrontHeader = (rtp_hdr_t*)szPacketBuffer;
	// 如果要删除的数据包序号比最小序号还要小 => 数据已经删除了，直接返回...
	if( inMaxConSeq < lpFrontHeader->seq )
		return;
	// 注意：当前已发送包号是一个超前包号，是指下一个要发送的包号...
	// 服务器收到的最大连续包号相等或大于当前已发送包号 => 直接返回...
	if( inMaxConSeq >= nCurSendSeq )
		return;
	// 注意：环形队列当中的序列号一定是连续的...
	// 注意：服务器收到的最大连续包号一定比当前已发送包号小...
	// 两者之差加1就是要删除的数据长度 => 要包含最大连续包本身的删除...
	uint32_t nPopSize = (inMaxConSeq - lpFrontHeader->seq + 1) * nPerPackSize;
	circlebuf_pop_front(&cur_circle, NULL, nPopSize);
	// 注意：环形队列当中的数据块大小是连续的，是一样大的...
	// 打印环形队列删除结果，计算环形队列剩余的数据包个数...
	uint32_t nRemainCount = cur_circle.size / nPerPackSize;
	blog(LOG_INFO, "%s Detect Erase Success => %s, MaxConSeq: %lu, MinSeq: %lu, CurSendSeq: %lu, CurPackSeq: %lu, Circle: %lu", 
	     m_strInnerName.c_str(), bIsAudio ? "Audio" : "Video", inMaxConSeq, lpFrontHeader->seq, nCurSendSeq, nCurPackSeq, nRemainCount );
}

void CSmartSendThread::doTagDetectProcess(char * lpBuffer, int inRecvLen)
{
	GM_Error theErr = GM_NoErr;
	if( m_lpUDPSocket == NULL || lpBuffer == NULL || inRecvLen <= 0 || inRecvLen < sizeof(rtp_detect_t) )
		return;
    // 通过第一个字节的低2位，判断终端类型...
    uint8_t tmTag = lpBuffer[0] & 0x03;
    // 获取第一个字节的中2位，得到终端身份...
    uint8_t idTag = (lpBuffer[0] >> 2) & 0x03;
    // 获取第一个字节的高4位，得到数据包类型...
    uint8_t ptTag = (lpBuffer[0] >> 4) & 0x0F;
	// 如果是 服务器 发出的探测包，将收到的探测数据包原样返回给服务器端...
	if (tmTag == TM_TAG_SERVER && idTag == ID_TAG_SERVER) {
		rtp_detect_t * lpDetect = (rtp_detect_t*)lpBuffer;
		theErr = m_lpUDPSocket->SendTo(lpBuffer, inRecvLen);
		// 累加总的输出字节数，便于计算输出平均码流...
		m_total_output_bytes += inRecvLen;
		// 先处理服务器收到音频最大连续序号包...
		this->doProcMaxConSeq(true, lpDetect->maxAConSeq);
		// 再处理服务器收到视频最大连续序号包...
		this->doProcMaxConSeq(false, lpDetect->maxVConSeq);
		return;
	}
	// 如果是 推流端 自己发出的探测包，计算网络延时...
	if (tmTag == this->GetTmTag() && idTag == this->GetIdTag()) {
		// 获取收到的探测数据包...
		rtp_detect_t rtpDetect = { 0 };
		memcpy(&rtpDetect, lpBuffer, sizeof(rtpDetect));
		// 只有一条探测路线 => 服务器探测方向...
		if (rtpDetect.dtDir != DT_TO_SERVER)
			return;
		ASSERT(rtpDetect.dtDir == DT_TO_SERVER);
		// 当前时间转换成毫秒，计算网络延时 => 当前时间 - 探测时间...
		uint32_t cur_time_ms = (uint32_t)(os_gettime_ns() / 1000000);
		int keep_rtt = cur_time_ms - rtpDetect.tsSrc;
		// 防止网络突发延迟增大, 借鉴 TCP 的 RTT 遗忘衰减的算法...
		if (m_server_rtt_ms < 0) { m_server_rtt_ms = keep_rtt; }
		else { m_server_rtt_ms = (7 * m_server_rtt_ms + keep_rtt) / 8; }
		// 计算网络抖动的时间差值 => RTT的修正值...
		if (m_server_rtt_var_ms < 0) { m_server_rtt_var_ms = abs(m_server_rtt_ms - keep_rtt); }
		else { m_server_rtt_var_ms = (m_server_rtt_var_ms * 3 + abs(m_server_rtt_ms - keep_rtt)) / 4; }
		// 打印探测结果 => 探测序号 | 网络延时(毫秒)...
		blog(LOG_INFO, "%s Recv Detect => Dir: %d, dtNum: %d, rtt: %d ms, rtt_var: %d ms", m_strInnerName.c_str(), rtpDetect.dtDir, rtpDetect.dtNum, m_server_rtt_ms, m_server_rtt_var_ms);
	}
}
///////////////////////////////////////////////////////
// 注意：没有发包，也没有收包，需要进行休息...
///////////////////////////////////////////////////////
void CSmartSendThread::doSleepTo()
{
	// 如果不能休息，直接返回...
	if( !m_bNeedSleep )
		return;
	// 计算要休息的时间 => 最大休息毫秒数...
	uint64_t delta_ns = MAX_SLEEP_MS * 1000000;
	uint64_t cur_time_ns = os_gettime_ns();
	// 调用系统工具函数，进行sleep休息...
	os_sleepto_ns(cur_time_ns + delta_ns);
}