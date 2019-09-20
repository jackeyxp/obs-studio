
#include "UDPSocket.h"
#include "SocketUtils.h"
#include "UDPPlayThread.h"
#include "smart-recv-thread.h"

CSmartRecvThread::CSmartRecvThread(CLIENT_TYPE inType, int nTCPSockFD, int nDBRoomID, int nLiveID)
  : m_lpUDPSocket(NULL)
  , m_lpObsSource(NULL)
  , m_lpPlaySDL(NULL)
  , m_bNeedSleep(false)
  , m_HostServerPort(0)
  , m_HostServerAddr(0)
  , m_bFirstAudioSeq(false)
  , m_bFirstVideoSeq(false)
  , m_nAudioMaxPlaySeq(0)
  , m_nVideoMaxPlaySeq(0)
  , m_nMaxResendCount(0)
  , m_next_create_ns(-1)
  , m_next_detect_ns(-1)
  , m_login_zero_ns(-1)
  , m_sys_zero_ns(-1)
  , m_server_cache_time_ms(-1)
  , m_server_rtt_var_ms(-1)
  , m_server_rtt_ms(-1)
{
	m_nClientType = inType;
	m_idTag = ID_TAG_LOOKER;
	// 保存数据源终端的内部名称...
	switch (m_nClientType) {
	case kClientStudent:
		m_strInnerName = ST_RECV_NAME;
		m_tmTag = TM_TAG_STUDENT;
		break;
	case kClientTeacher:
		m_strInnerName = TH_RECV_NAME;
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
	// 设置终端类型和结构体类型 => m_rtp_header => 等待网络填充 => 老师观看者身份...
	m_rtp_detect.tm = m_rtp_create.tm = m_rtp_delete.tm = m_rtp_supply.tm = m_tmTag;
	m_rtp_detect.id = m_rtp_create.id = m_rtp_delete.id = m_rtp_supply.id = m_idTag;
	m_rtp_detect.pt = PT_TAG_DETECT;
	m_rtp_create.pt = PT_TAG_CREATE;
	m_rtp_delete.pt = PT_TAG_DELETE;
	m_rtp_supply.pt = PT_TAG_SUPPLY;
	// 填充房间号和直播通道号...
	m_rtp_create.roomID = nDBRoomID;
	m_rtp_create.liveID = nLiveID;
	m_rtp_delete.roomID = nDBRoomID;
	m_rtp_delete.liveID = nLiveID;
	// 填充与远程关联的TCP套接字...
	m_rtp_create.tcpSock = nTCPSockFD;
}

CSmartRecvThread::~CSmartRecvThread()
{
	blog(LOG_INFO, "%s == [~CSmartRecvThread Thread] - Exit Start ==", m_strInnerName.c_str());
	// 停止线程，等待退出...
	this->StopAndWaitForThread();
	// 关闭UDPSocket对象...
	this->CloseSocket();
	// 删除音视频播放线程...
	this->ClosePlayer();
	// 释放音视频环形队列空间...
	circlebuf_free(&m_audio_circle);
	circlebuf_free(&m_video_circle);
	blog(LOG_INFO, "%s == [~CSmartRecvThread Thread] - Exit End ==", m_strInnerName.c_str());
}

void CSmartRecvThread::ClosePlayer()
{
	if( m_lpPlaySDL != NULL ) {
		delete m_lpPlaySDL;
		m_lpPlaySDL = NULL;
	}
}

void CSmartRecvThread::CloseSocket()
{
	if( m_lpUDPSocket != NULL ) {
		m_lpUDPSocket->Close();
		delete m_lpUDPSocket;
		m_lpUDPSocket = NULL;
	}
}

bool CSmartRecvThread::InitThread(obs_source_t * lpObsSource, const char * lpUdpAddr, int nUdpPort)
{
	// 保存obs资源对象...
	m_lpObsSource = lpObsSource;
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
	// 设定系统登录计时0点位置...
	m_login_zero_ns = os_gettime_ns();
	// 启动组播接收线程...
	this->Start();
	// 返回执行结果...
	return true;
}

void CSmartRecvThread::Entry()
{
	while( !this->IsStopRequested() ) {
		// 设置休息标志 => 只要有发包或收包就不能休息...
		m_bNeedSleep = true;
		// 发送创建房间和直播通道命令包...
		this->doSendCreateCmd();
		// 发送探测命令包...
		this->doSendDetectCmd();
		// 接收一个到达的服务器反馈包...
		this->doRecvPacket();
		// 先发送音频补包命令...
		this->doSendSupplyCmd(true);
		// 再发送视频补包命令...
		this->doSendSupplyCmd(false);
		// 从环形队列中抽取完整一帧音频放入播放器...
		this->doParseFrame(true);
		// 从环形队列中抽取完整一帧视频放入播放器...
		this->doParseFrame(false);
		// 等待发送或接收下一个数据包...
		this->doSleepTo();
	}
	// 只发送一次删除命令包...
	this->doSendDeleteCmd();
}

void CSmartRecvThread::doSendDeleteCmd()
{
	GM_Error theErr = GM_NoErr;
	if( m_lpUDPSocket == NULL )
		return;
	// 套接字有效，直接发送删除命令...
	ASSERT( m_lpUDPSocket != NULL );
	theErr = m_lpUDPSocket->SendTo((void*)&m_rtp_delete, sizeof(m_rtp_delete));
	(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
	// 打印已发送删除命令包...
	blog(LOG_INFO, "%s Send Delete RoomID: %lu, LiveID: %d", m_strInnerName.c_str(), m_rtp_delete.roomID, m_rtp_delete.liveID);
}

void CSmartRecvThread::doSendCreateCmd()
{
	if (m_lpUDPSocket == NULL)
		return;
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
	// 发送一个创建房间命令 => 相当于登录注册...
	GM_Error theErr = m_lpUDPSocket->SendTo((void*)&m_rtp_create, sizeof(m_rtp_create));
	(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
	// 打印已发送创建命令包 => 第一个包有可能没有发送出去，也返回正常...
	blog(LOG_INFO, "%s Send Create RoomID: %lu, LiveID: %d", m_strInnerName.c_str(), m_rtp_create.roomID, m_rtp_create.liveID);
	// 计算下次发送创建命令的时间戳...
	m_next_create_ns = os_gettime_ns() + period_ns;
	// 修改休息状态 => 已经有发包，不能休息...
	m_bNeedSleep = false;
	//////////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：如果收到第一个包或第一个命令的延时，会累加到播放层...
	// 因此，在发出每一个Create命令的时候都更新系统0点时刻，相当于提前设置了系统0点时刻...
	// 相当于在发出创建命令就认为是第一帧数据已经准备好可以播放的时刻点，这样受网络波动延时的影响最小；
	//////////////////////////////////////////////////////////////////////////////////////////////////////
	m_sys_zero_ns = os_gettime_ns();
	blog(LOG_INFO, "%s Set System Zero Time By Create => %I64d ms", m_strInnerName.c_str(), m_sys_zero_ns/1000000);
}

void CSmartRecvThread::doSendDetectCmd()
{
	if (m_lpUDPSocket == NULL)
		return;
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
	// 计算已收到音视频最大连续包号 => 服务器并没有使用...
	m_rtp_detect.maxAConSeq = this->doCalcMaxConSeq(true);
	m_rtp_detect.maxVConSeq = this->doCalcMaxConSeq(false);
	// 调用接口发送探测命令包 => 由于新增扩展音频，不能用P2P模式进行数据包发送...
	GM_Error theErr = m_lpUDPSocket->SendTo((void*)&m_rtp_detect, sizeof(m_rtp_detect));
	(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
	// 打印已发送探测命令包...
	//blog(LOG_INFO, "%s Send Detect dtNum: %d", m_strInnerName.c_str(), m_rtp_detect.dtNum);
	// 计算下次发送探测命令的时间戳...
	m_next_detect_ns = os_gettime_ns() + period_ns;
	// 修改休息状态 => 已经有发包，不能休息...
	m_bNeedSleep = false;
}

uint32_t CSmartRecvThread::doCalcMaxConSeq(bool bIsAudio)
{
	// 根据数据包类型，找到丢包集合、环形队列、最大播放包...
	GM_MapLose & theMapLose = bIsAudio ? m_AudioMapLose : m_VideoMapLose;
	circlebuf  & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;
	uint32_t  & nMaxPlaySeq = bIsAudio ? m_nAudioMaxPlaySeq : m_nVideoMaxPlaySeq;
	// 发生丢包的计算 => 等于最小丢包号 - 1
	if( theMapLose.size() > 0 ) {
		return (theMapLose.begin()->first - 1);
	}
	// 没有丢包 => 环形队列为空 => 返回最大播放序列号...
	if(  cur_circle.size <= 0  )
		return nMaxPlaySeq;
	// 没有丢包 => 已收到的最大包号 => 环形队列中最大序列号...
	const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	char szPacketBuffer[nPerPackSize] = {0};
	circlebuf_peek_back(&cur_circle, szPacketBuffer, nPerPackSize);
	rtp_hdr_t * lpMaxHeader = (rtp_hdr_t*)szPacketBuffer;
	return lpMaxHeader->seq;
}

// 由于新增扩展音频，不能用P2P模式进行数据包发送...
/*void CSmartRecvThread::doSendReadyCmd()
{
	if (m_lpUDPSocket == NULL)
		return;
	////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：观看端必须收到服务器转发的准备就绪命令之后，才能停止发送准备就绪命令...
	// 因为，要获取到推流者的映射地址和映射端口...
	// 如果命令状态不是准备就绪命令，不发送命令，直接返回...
	////////////////////////////////////////////////////////////////////////////////////////////
	if( m_nCmdState != kCmdSendReady )
		return;
	// 注意：这时音视频序列头已经接收完毕...
	// 每隔100毫秒发送就绪命令包 => 必须转换成有符号...
	int64_t cur_time_ns = os_gettime_ns();
	int64_t period_ns = 100 * 1000000;
	// 如果发包时间还没到，直接返回...
	if( m_next_ready_ns > cur_time_ns )
		return;
	ASSERT( cur_time_ns >= m_next_ready_ns );
	// 准备临时的准备就绪命令结构体...
	rtp_ready_t rtpReady = {0};
	rtpReady.tm = TM_TAG_TEACHER;
	rtpReady.id = ID_TAG_LOOKER;
	rtpReady.pt = PT_TAG_READY;
	// 发送一个准备就绪命令包 => 通知学生推流端 => 可以开始发送音视频数据包...
	GM_Error theErr = m_lpUDPSocket->SendTo((void*)&rtpReady, sizeof(rtpReady));
	(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
	// 打印已发送准备就绪命令包...
	blog(LOG_INFO, "%s Send Ready command", TM_RECV_NAME);
	// 计算下次发送创建命令的时间戳...
	m_next_ready_ns = os_gettime_ns() + period_ns;
	// 修改休息状态 => 已经有发包，不能休息...
	m_bNeedSleep = false;
}*/

void CSmartRecvThread::doSendSupplyCmd(bool bIsAudio)
{
	if (m_lpUDPSocket == NULL)
		return;
	// 根据数据包类型，找到丢包集合...
	GM_MapLose & theMapLose = bIsAudio ? m_AudioMapLose : m_VideoMapLose;
	// 如果丢包集合队列为空，直接返回...
	if( theMapLose.size() <= 0 )
		return;
	ASSERT( theMapLose.size() > 0 );
	// 定义最大的补包缓冲区...
	const int nHeadSize = sizeof(m_rtp_supply);
	const int nPerPackSize = DEF_MTU_SIZE + nHeadSize;
	char szPacket[nPerPackSize] = {0};
	char * lpData = szPacket + nHeadSize;
	// 获取当前时间的毫秒值 => 小于或等于当前时间的丢包都需要通知发送端再次发送...
	uint32_t cur_time_ms = (uint32_t)(os_gettime_ns() / 1000000);
	// 需要对网络往返延迟值进行线路选择 => 只有一条服务器线路...
	int cur_rtt_ms = m_server_rtt_ms;
	// 重置补报长度为0 => 重新计算需要补包的个数...
	m_rtp_supply.suType = bIsAudio ? PT_TAG_AUDIO : PT_TAG_VIDEO;
	m_rtp_supply.suSize = 0;
	int nCalcMaxResend = 0;
	// 遍历丢包队列，找出需要补包的丢包序列号...
	GM_MapLose::iterator itorItem = theMapLose.begin();
	while( itorItem != theMapLose.end() ) {
		rtp_lose_t & rtpLose = itorItem->second;
		if( rtpLose.resend_time <= cur_time_ms ) {
			// 如果补包缓冲超过设定的最大值，跳出循环 => 最多补包200个...
			if( (nHeadSize + m_rtp_supply.suSize) >= nPerPackSize )
				break;
			// 累加补包长度和指针，拷贝补包序列号...
			memcpy(lpData, &rtpLose.lose_seq, sizeof(uint32_t));
			m_rtp_supply.suSize += sizeof(uint32_t);
			lpData += sizeof(uint32_t);
			// 累加重发次数...
			++rtpLose.resend_count;
			// 计算最大丢包重发次数...
			nCalcMaxResend = max(nCalcMaxResend, rtpLose.resend_count);
			// 注意：同时发送的补包，下次也同时发送，避免形成多个散列的补包命令...
			// 注意：如果一个网络往返延时都没有收到补充包，需要再次发起这个包的补包命令...
			// 注意：这里要避免 网络抖动时间差 为负数的情况 => 还没有完成第一次探测的情况，也不能为0，会猛烈发包...
			// 修正下次重传时间点 => cur_time + rtt => 丢包时的当前时间 + 网络往返延迟值 => 需要进行线路选择...
			rtpLose.resend_time = cur_time_ms + max(cur_rtt_ms, MAX_SLEEP_MS);
			// 如果补包次数大于1，下次补包不要太快，追加一个休息周期..
			rtpLose.resend_time += ((rtpLose.resend_count > 1) ? MAX_SLEEP_MS : 0);
		}
		// 累加丢包算子对象...
		++itorItem;
	}
	// 如果补充包缓冲为空，直接返回...
	if( m_rtp_supply.suSize <= 0 )
		return;
	// 保存计算出来的最大丢包重发次数...
	m_nMaxResendCount = nCalcMaxResend;
	// 更新补包命令头内容块...
	memcpy(szPacket, &m_rtp_supply, nHeadSize);
	// 如果补包缓冲不为空，才进行补包命令发送...
	GM_Error theErr = GM_NoErr;
	int nDataSize = nHeadSize + m_rtp_supply.suSize;
	////////////////////////////////////////////////////////////////////////////////////////
	// 调用套接字接口，直接发送RTP数据包 => 目前只有一条服务器发包路线...
	////////////////////////////////////////////////////////////////////////////////////////
	ASSERT( m_dt_to_dir == DT_TO_SERVER );
	theErr = m_lpUDPSocket->SendTo(szPacket, nDataSize);
	// 如果有错误发生，打印出来...
	((theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL);
	// 修改休息状态 => 已经有发包，不能休息...
	m_bNeedSleep = false;
	// 打印已发送补包命令...
	blog(LOG_INFO, "%s Supply Send => PType: %d, Dir: %d, Count: %d, MaxResend: %d", m_strInnerName.c_str(),
	     m_rtp_supply.suType, m_dt_to_dir, m_rtp_supply.suSize/sizeof(uint32_t), nCalcMaxResend);
}

void CSmartRecvThread::doRecvPacket()
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
		blog(LOG_INFO, "[Recv-Error] Max => %lu, Addr => %lu:%d, Size => %lu", nMaxSize, outRemoteAddr, outRemotePort, outRecvLen);
		return;
	}
    // 获取第一个字节的高4位，得到数据包类型...
    uint8_t ptTag = (ioBuffer[0] >> 4) & 0x0F;
	// 对收到命令包进行类型分发...
	switch( ptTag )
	{
	case PT_TAG_HEADER:	 this->doTagHeaderProcess(ioBuffer, outRecvLen); break;
	case PT_TAG_DETECT:	 this->doTagDetectProcess(ioBuffer, outRecvLen); break;
	case PT_TAG_AUDIO:	 this->doTagAVPackProcess(ioBuffer, outRecvLen); break;
	case PT_TAG_VIDEO:	 this->doTagAVPackProcess(ioBuffer, outRecvLen); break;
	}
}

void CSmartRecvThread::doTagHeaderProcess(char * lpBuffer, int inRecvLen)
{
	// 通过 rtp_header_t 做为载体发送过来的 => 服务器直接原样转发的学生推流端的序列头结构体...
	if( m_lpUDPSocket == NULL || lpBuffer == NULL || inRecvLen < 0 || inRecvLen < sizeof(rtp_header_t) )
		return;
	// 如果不是创建命令状态，或者，已经收到了序列头，直接返回...
	if( m_nCmdState != kCmdSendCreate || m_rtp_header.pt == PT_TAG_HEADER || m_rtp_header.hasAudio || m_rtp_header.hasVideo )
		return;
    // 通过第一个字节的低2位，判断终端类型...
    uint8_t tmTag = lpBuffer[0] & 0x03;
    // 获取第一个字节的中2位，得到终端身份...
    uint8_t idTag = (lpBuffer[0] >> 2) & 0x03;
    // 获取第一个字节的高4位，得到数据包类型...
    uint8_t ptTag = (lpBuffer[0] >> 4) & 0x0F;
	// 如果发送者不是 学生推流端 => 直接返回...
	if( tmTag != TM_TAG_STUDENT || idTag != ID_TAG_PUSHER )
		return;
	// 如果发现长度不够，直接返回...
	memcpy(&m_rtp_header, lpBuffer, sizeof(m_rtp_header));
	int nNeedSize = m_rtp_header.spsSize + m_rtp_header.ppsSize + sizeof(m_rtp_header);
	if( nNeedSize != inRecvLen ) {
		blog(LOG_INFO, "%s Recv Header error, RecvLen: %d", m_strInnerName.c_str(), inRecvLen);
		memset(&m_rtp_header, 0, sizeof(m_rtp_header));
		return;
	}
	// 读取SPS和PPS格式头信息...
	char * lpData = lpBuffer + sizeof(m_rtp_header);
	if( m_rtp_header.spsSize > 0 ) {
		m_strSPS.assign(lpData, m_rtp_header.spsSize);
		lpData += m_rtp_header.spsSize;
	}
	// 保存 PPS 格式头...
	if( m_rtp_header.ppsSize > 0 ) {
		m_strPPS.assign(lpData, m_rtp_header.ppsSize);
		lpData += m_rtp_header.ppsSize;
	}
	// 修改命令状态 => 不向服务器准备就绪命令包...
	m_nCmdState = kCmdConnectOK;
	// 打印收到序列头结构体信息...
	blog(LOG_INFO, "%s Recv Header SPS: %d, PPS: %d", m_strInnerName.c_str(), m_strSPS.size(), m_strPPS.size());
	// 如果播放器已经创建，直接返回...
	if( m_lpPlaySDL != NULL )
		return;
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 开始重建本地播放器对象，设定观看端的0点时刻...
	// 注意：这里非常重要，服务器开始转发推流者音视频数据了，必须设定观看者的系统计时起点，0点时刻...
	// 0点时刻：系统认为第一帧数据的真正系统时间起点，第一帧数据就应该在这个时刻点已经被准备好...
	// 但是，由于网络延时，根本不可能在这个时刻点被准备好，如果在收到第一帧数据之后再设定0点时刻，就会产生永久的接入延时...
	// 注意：如果收到第一个包或第一个命令的延时，会累加到播放层...
	// 注意：最终采用的方案 => 发送Create命令设置系统0点时刻...
	// 计算系统0点时刻 => 第一个数据包到达时间就是系统0点时刻...
	// 有可能第一个数据包还没有到达，就立即用当前系统时刻做为0点时刻...
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/*if( m_sys_zero_ns < 0 ) {
		m_sys_zero_ns = os_gettime_ns() - 100 * 1000000;
		blog("%s Set System Zero Time By Header => %I64d ms", TM_RECV_NAME, m_sys_zero_ns/1000000);
	}*/
	// 计算默认的网络缓冲评估时间 => 使用帧率来计算...
	m_server_cache_time_ms = 1000 / ((m_rtp_header.fpsNum > 0) ? m_rtp_header.fpsNum : 25); 
	// 新建播放器，初始化音视频线程...
	m_lpPlaySDL = new CPlaySDL(m_lpObsSource, m_sys_zero_ns, m_strInnerName);
	// 如果有视频，初始化视频线程...
	if( m_rtp_header.hasVideo ) {
		int nPicFPS = m_rtp_header.fpsNum;
		int nPicWidth = m_rtp_header.picWidth;
		int nPicHeight = m_rtp_header.picHeight;
		m_lpPlaySDL->InitVideo(m_strSPS, m_strPPS, nPicWidth, nPicHeight, nPicFPS);
	} 
	// 如果有音频，初始化音频线程...
	if( m_rtp_header.hasAudio ) {
		int nRateIndex = m_rtp_header.rateIndex;
		int nChannelNum = m_rtp_header.channelNum;
		m_lpPlaySDL->InitAudio(nRateIndex, nChannelNum);
	}
}

// 注意：观看端必须收到服务器转发的准备就绪命令之后，才能停止发送准备就绪命令，因为要获取到推流者的映射地址和映射端口...
/*void CSmartRecvThread::doProcServerReady(char * lpBuffer, int inRecvLen)
{
	// 接收到来自学生推流端要求停止发送准备就绪命令包...
	if( m_lpUDPSocket == NULL || lpBuffer == NULL || inRecvLen < 0 || inRecvLen < sizeof(rtp_ready_t) )
		return;
	// 如果不是准备就绪命令状态，或者，已经收到了推流端反馈的准备就绪命令，直接返回...
	if( m_nCmdState != kCmdSendReady || m_rtp_ready.pt == PT_TAG_READY )
		return;
    // 通过第一个字节的低2位，判断终端类型...
    uint8_t tmTag = lpBuffer[0] & 0x03;
    // 获取第一个字节的中2位，得到终端身份...
    uint8_t idTag = (lpBuffer[0] >> 2) & 0x03;
    // 获取第一个字节的高4位，得到数据包类型...
    uint8_t ptTag = (lpBuffer[0] >> 4) & 0x0F;
	// 如果不是 学生推流端 发出的准备就绪包，直接返回...
	if( tmTag != TM_TAG_STUDENT || idTag != ID_TAG_PUSHER )
		return;
	// 修改命令状态 => 接入完毕，不要再发生准备就绪命令了...
	m_nCmdState = kCmdConnectOK;
	// 保存学生推流端发送的准备就绪数据包内容...
	memcpy(&m_rtp_ready, lpBuffer, sizeof(m_rtp_ready));
	// 打印收到准备就绪命令包 => 将地址转换成字符串...
	string strAddr = SocketUtils::ConvertAddrToString(m_rtp_ready.recvAddr);
	blog(LOG_INFO, "%s Recv Ready from %s:%d", TM_RECV_NAME, strAddr.c_str(), m_rtp_ready.recvPort);
}*/

bool CSmartRecvThread::IsLoginTimeout()
{
	// 如果已经是登录成功状态，直接返回...
	if (m_nCmdState == kCmdConnectOK)
		return false;
	ASSERT(m_nCmdState != kCmdConnectOK);
	// 计算已尝试登录时间，如果超时返回true...
	uint32_t cur_elapse_ms = (uint32_t)((os_gettime_ns() - m_login_zero_ns) / 1000000);
	return ((cur_elapse_ms >= DEF_TIMEOUT_MS) ? true : false);
}

void CSmartRecvThread::doTagDetectProcess(char * lpBuffer, int inRecvLen)
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
	// 老师观看端 自己发出的探测包，计算网络延时...
	if( tmTag == TM_TAG_TEACHER && idTag == ID_TAG_LOOKER ) {
		// 获取收到的探测数据包...
		rtp_detect_t rtpDetect = {0};
		memcpy(&rtpDetect, lpBuffer, sizeof(rtpDetect));
		// 目前只处理来自服务器方向的探测结果...
		if (rtpDetect.dtDir != DT_TO_SERVER)
			return;
		ASSERT(rtpDetect.dtDir == DT_TO_SERVER);
		// 网络极端情况下避免服务器补包失败...
		// 先处理服务器回传的音频最小序号包...
		this->doServerMinSeq(true, rtpDetect.maxAConSeq);
		// 再处理服务器回传的视频最小序号包...
		this->doServerMinSeq(false, rtpDetect.maxVConSeq);
		// 当前时间转换成毫秒，计算网络延时 => 当前时间 - 探测时间...
		uint32_t cur_time_ms = (uint32_t)(os_gettime_ns() / 1000000);
		int keep_rtt = cur_time_ms - rtpDetect.tsSrc;
		// 如果音频和视频都没有丢包，直接设定最大重发次数为0...
		if( m_AudioMapLose.size() <= 0 && m_VideoMapLose.size() <= 0 ) {
			m_nMaxResendCount = 0;
		}
		// 防止网络突发延迟增大, 借鉴 TCP 的 RTT 遗忘衰减的算法...
		if( m_server_rtt_ms < 0 ) { m_server_rtt_ms = keep_rtt; }
		else { m_server_rtt_ms = (7 * m_server_rtt_ms + keep_rtt) / 8; }
		// 计算网络抖动的时间差值 => RTT的修正值...
		if( m_server_rtt_var_ms < 0 ) { m_server_rtt_var_ms = abs(m_server_rtt_ms - keep_rtt); }
		else { m_server_rtt_var_ms = (m_server_rtt_var_ms * 3 + abs(m_server_rtt_ms - keep_rtt)) / 4; }
		// 计算缓冲评估时间 => 如果没有丢包，使用网络往返延时+网络抖动延时之和...
		if( m_nMaxResendCount > 0 ) {
			m_server_cache_time_ms = (2 * m_nMaxResendCount + 1) * (m_server_rtt_ms + m_server_rtt_var_ms) / 2;
		} else {
			m_server_cache_time_ms = m_server_rtt_ms + m_server_rtt_var_ms;
		}
		// 打印探测结果 => 探测序号 | 网络延时(毫秒)...
		int nADFrame = ((m_lpPlaySDL != NULL) ? m_lpPlaySDL->GetAFrameSize() : 0);
		int nVDFrame = ((m_lpPlaySDL != NULL) ? m_lpPlaySDL->GetVFrameSize() : 0);
		const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
		blog(LOG_INFO, "%s Recv Detect => LiveID: %d, Dir: %d, dtNum: %d, rtt: %d ms, rtt_var: %d ms, cache_time: %d ms, ACircle: %d:%d, VCircle: %d:%d",
			 m_strInnerName.c_str(), m_rtp_create.liveID, rtpDetect.dtDir, rtpDetect.dtNum, m_server_rtt_ms, m_server_rtt_var_ms, m_server_cache_time_ms,
			 m_audio_circle.size / nPerPackSize, nADFrame, m_video_circle.size / nPerPackSize, nVDFrame);
		// 打印播放器底层的缓存状态信息...
		/*if (m_lpPlaySDL != NULL) {
			blog(LOG_INFO, "%s Recv Detect => APacket: %d, VPacket: %d, AFrame: %d, VFrame: %d", TM_RECV_NAME,
				m_lpPlaySDL->GetAPacketSize(), m_lpPlaySDL->GetVPacketSize(), m_lpPlaySDL->GetAFrameSize(), m_lpPlaySDL->GetVFrameSize());
		}*/
		/////////////////////////////////////////////////////////////////////////
		// 注意：由于要靠服务器缓存，改进成永远是DT_TO_SERVER...
		// 对补包线路进行选择 => 选择已联通的最小rtt为补包通知线路...
		/////////////////////////////////////////////////////////////////////////
		// 如果P2P线路有效，并且更快，设定为P2P线路，否则，设定为服务器线路...
		/*if((m_p2p_rtt_ms >= 0) && (m_p2p_rtt_ms < m_server_rtt_ms)) {
			m_dt_to_dir = DT_TO_P2P;
		} else {
			m_dt_to_dir = DT_TO_SERVER;
		}*/
	}
}
// 本地接收缓存数据包的清理是靠解析数据帧，不是靠服务器的最小数据包...
// 但是，在网络极端情况下，这种主动删除的方式就会起作用，避免服务器补包失败...
void CSmartRecvThread::doServerMinSeq(bool bIsAudio, uint32_t inMinSeq)
{
	// 如果输入的最小包号无效，直接返回...
	if (inMinSeq <= 0)
		return;
	// 根据数据包类型，找到丢包集合、环形队列、最大播放包...
	GM_MapLose & theMapLose = bIsAudio ? m_AudioMapLose : m_VideoMapLose;
	circlebuf  & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;
	uint32_t  & nMaxPlaySeq = bIsAudio ? m_nAudioMaxPlaySeq : m_nVideoMaxPlaySeq;
	// 丢包队列有效，才进行丢包查询工作...
	if (theMapLose.size() > 0) {
		// 遍历丢包队列，凡是小于服务器端最小序号的丢包，都要扔掉...
		GM_MapLose::iterator itorItem = theMapLose.begin();
		while (itorItem != theMapLose.end()) {
			rtp_lose_t & rtpLose = itorItem->second;
			// 如果要补的包号，不小于最小包号，可以补包...
			if (rtpLose.lose_seq >= inMinSeq) {
				itorItem++;
				continue;
			}
			// 如果要补的包号，比最小包号还要小，直接丢弃，已经过期了...
			theMapLose.erase(itorItem++);
		}
	}
	// 如果环形队列为空，无需清理，直接返回...
	if (cur_circle.size <= 0)
		return;
	// 获取环形队列当中最小序号和最大序号，找到清理边界...
	const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	char szPacketBuffer[nPerPackSize] = { 0 };
	rtp_hdr_t * lpCurHeader = NULL;
	uint32_t    min_seq = 0, max_seq = 0;
	// 读取最小的数据包的内容，获取最小序列号...
	circlebuf_peek_front(&cur_circle, szPacketBuffer, nPerPackSize);
	lpCurHeader = (rtp_hdr_t*)szPacketBuffer;
	min_seq = lpCurHeader->seq;
	// 读取最大的数据包的内容，获取最大序列号...
	circlebuf_peek_back(&cur_circle, szPacketBuffer, nPerPackSize);
	lpCurHeader = (rtp_hdr_t*)szPacketBuffer;
	max_seq = lpCurHeader->seq;
	// 如果环形队列中的最小序号包比服务器端的最小序号包大，不用清理缓存...
	if (min_seq >= inMinSeq)
		return;
	// 打印环形队列清理前的各个变量状态值...
	blog(LOG_INFO, "%s Clear => LiveID: %d, Audio: %d, ServerMin: %lu, MinSeq: %lu, MaxSeq: %lu, MaxPlaySeq: %lu",
		m_strInnerName.c_str(), m_rtp_create.liveID, bIsAudio, inMinSeq, min_seq, max_seq, nMaxPlaySeq);
	// 如果环形队列中的最大序号包比服务器端的最小序号包小，清理全部缓存...
	if (max_seq < inMinSeq) {
		inMinSeq = max_seq + 1;
	}
	// 缓存清理范围 => [min_seq, inMinSeq]...
	int nConsumeSize = (inMinSeq - min_seq) * nPerPackSize;
	circlebuf_pop_front(&cur_circle, NULL, nConsumeSize);
	// 将最大播放包设定为 => 最小序号包 - 1...
	nMaxPlaySeq = inMinSeq - 1;
}

#ifdef DEBUG_FRAME
static void DoSaveRecvFile(uint32_t inPTS, int inType, bool bIsKeyFrame, string & strFrame)
{
	static char szBuf[MAX_PATH] = {0};
	char * lpszPath = "F:/MP4/Dst/recv.txt";
	FILE * pFile = fopen(lpszPath, "a+");
	sprintf(szBuf, "PTS: %lu, Type: %d, Key: %d, Size: %d\n", inPTS, inType, bIsKeyFrame, strFrame.size());
	fwrite(szBuf, 1, strlen(szBuf), pFile);
	fwrite(strFrame.c_str(), 1, strFrame.size(), pFile);
	fclose(pFile);
}

static void DoSaveRecvSeq(uint32_t inPSeq, int inPSize, bool inPST, bool inPED, uint32_t inPTS)
{
	static char szBuf[MAX_PATH] = {0};
	char * lpszPath = "F:/MP4/Dst/recv_seq.txt";
	FILE * pFile = fopen(lpszPath, "a+");
	sprintf(szBuf, "PSeq: %lu, PSize: %d, PST: %d, PED: %d, PTS: %lu\n", inPSeq, inPSize, inPST, inPED, inPTS);
	fwrite(szBuf, 1, strlen(szBuf), pFile);
	fclose(pFile);
}
#endif // DEBUG_FRAME

void CSmartRecvThread::doParseFrame(bool bIsAudio)
{
	/*////////////////////////////////////////////////////////////////////////////
	// 注意：环形队列至少要有一个数据包存在，否则，在发生丢包时，无法发现...
	// 测试 => 打印收到的有效包序号，并从环形队列当中删除...
	////////////////////////////////////////////////////////////////////////////
	const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	char szPacketCheck[nPerPackSize] = {0};
	if( m_circle.size <= nPerPackSize )
		return;
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：千万不能在环形队列当中进行指针操作，当start_pos > end_pos时，可能会有越界情况...
	// 所以，一定要用接口读取完整的数据包之后，再进行操作；如果用指针，一旦发生回还，就会错误...
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	circlebuf_peek_front(&m_circle, szPacketCheck, nPerPackSize);
	rtp_hdr_t * lpFirstHeader = (rtp_hdr_t*)szPacketCheck;
	if( lpFirstHeader->pt == PT_TAG_LOSE )
		return;
	//log_trace( "[Teacher-Looker] Seq: %lu, Type: %d, Key: %d, Size: %d, TS: %lu",
	//		lpFirstHeader->seq, lpFirstHeader->pt, lpFirstHeader->pk, lpFirstHeader->psize, lpFirstHeader->ts);
	// 如果收到的有效序号不是连续的，打印错误...
	if( (m_nMaxPlaySeq + 1) != lpFirstHeader->seq ) {
		log_trace("[Teacher-Looker] Error => PlaySeq: %lu, CurSeq: %lu", m_nMaxPlaySeq, lpFirstHeader->seq);
	}
	// 保留当前播放序号，移除环形队列...
	m_nMaxPlaySeq = lpFirstHeader->seq;
	circlebuf_pop_front(&m_circle, NULL, nPerPackSize);*/

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：千万不能在环形队列当中进行指针操作，当start_pos > end_pos时，可能会有越界情况...
	// 验证是否丢包的实验...
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/*const int nPerTestSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	char szPacketCheck[nPerPackSize] = {0};
	if( m_circle.size <= nPerTestSize )
		return;
	circlebuf_peek_front(&m_circle, szPacketCheck, nPerPackSize);
	rtp_hdr_t * lpTestHeader = (rtp_hdr_t*)szPacketCheck;
	if( lpTestHeader->pt == PT_TAG_LOSE )
		return;
	DoSaveRecvSeq(lpTestHeader->seq, lpTestHeader->psize, lpTestHeader->pst, lpTestHeader->ped, lpTestHeader->ts);
	circlebuf_pop_front(&m_circle, NULL, nPerTestSize);
	return;*/

	//////////////////////////////////////////////////////////////////////////////////
	// 如果登录还没有收到服务器反馈或播放器为空，都直接返回，继续等待...
	//////////////////////////////////////////////////////////////////////////////////
	if( m_nCmdState <= kCmdSendCreate ) {
		//blog(LOG_INFO, "%s Wait For Player => Audio: %d, Video: %d", TM_RECV_NAME, m_audio_circle.size/nPerPackSize, m_video_circle.size/nPerPackSize );
		return;
	}

	// 音视频使用不同的打包对象和变量...
	uint32_t & nMaxPlaySeq = bIsAudio ? m_nAudioMaxPlaySeq : m_nVideoMaxPlaySeq;
	circlebuf & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：环形队列至少要有一个数据包存在，否则，在发生丢包时，无法发现，即大号包先到，小号包后到，就会被扔掉...
	// 注意：环形队列在抽取完整音视频数据帧之后，也可能被抽干，所以，必须在 doFillLosePack 中对环形队列为空时做特殊处理...
	// 如果环形队列为空或播放器对象无效，直接返回...
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 准备解析抽包组帧过程中需要的变量和空间对象...
	const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	char szPacketCurrent[nPerPackSize] = {0};
	char szPacketFront[nPerPackSize] = {0};
	rtp_hdr_t * lpFrontHeader = NULL;
	if( cur_circle.size <= nPerPackSize )
		return;
	/////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：千万不能在环形队列当中进行指针操作，当start_pos > end_pos时，可能会有越界情况...
	// 所以，一定要用接口读取完整的数据包之后，再进行操作；如果用指针，一旦发生回还，就会错误...
	/////////////////////////////////////////////////////////////////////////////////////////////////
	// 先找到环形队列中最前面数据包的头指针 => 最小序号...
	circlebuf_peek_front(&cur_circle, szPacketFront, nPerPackSize);
	lpFrontHeader = (rtp_hdr_t*)szPacketFront;
	// 如果最小序号包是需要补充的丢包 => 返回休息等待...
	if( lpFrontHeader->pt == PT_TAG_LOSE )
		return;
	// 如果最小序号包不是音视频数据帧的开始包 => 删除这个数据包，继续找...
	if( lpFrontHeader->pst <= 0 ) {
		// 更新当前最大播放序列号并保存起来...
		nMaxPlaySeq = lpFrontHeader->seq;
		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// 注意：在删除数据包之前，可以进行存盘测试操作...
		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef DEBUG_RAME
		DoSaveRecvSeq(lpFrontHeader->seq, lpFrontHeader->psize, lpFrontHeader->pst, lpFrontHeader->ped, lpFrontHeader->ts);
#endif // DEBUG_FRAME
		// 删除这个数据包，返回不休息，继续找...
		circlebuf_pop_front(&cur_circle, NULL, nPerPackSize);
		// 修改休息状态 => 已经抽取数据包，不能休息...
		m_bNeedSleep = false;
		// 打印抽帧失败信息 => 没有找到数据帧的开始标记...
		blog(LOG_INFO, "%s Error => Frame start code not find, Seq: %lu, Type: %d, Key: %d, PTS: %lu", 
			m_strInnerName.c_str(), lpFrontHeader->seq, lpFrontHeader->pt, lpFrontHeader->pk, lpFrontHeader->ts );
		return;
	}
	ASSERT( lpFrontHeader->pst );
	// 开始正式从环形队列中抽取音视频数据帧...
	int         pt_type = lpFrontHeader->pt;
	bool        is_key = ((lpFrontHeader->pk > 0) ? true : false);
	uint32_t    ts_ms = lpFrontHeader->ts;
	uint32_t    min_seq = lpFrontHeader->seq;
	uint32_t    cur_seq = lpFrontHeader->seq;
	rtp_hdr_t * lpCurHeader = lpFrontHeader;
	uint32_t    nConsumeSize = nPerPackSize;
	string      strFrame;
	// 完整数据帧 => 序号连续，以pst开始，以ped结束...
	while( true ) {
		// 将数据包的有效载荷保存起来...
		char * lpData = (char*)lpCurHeader + sizeof(rtp_hdr_t);
		strFrame.append(lpData, lpCurHeader->psize);
		// 如果数据包是帧的结束包 => 数据帧组合完毕...
		if( lpCurHeader->ped > 0 )
			break;
		// 如果数据包不是帧的结束包 => 继续寻找...
		ASSERT( lpCurHeader->ped <= 0 );
		// 累加包序列号，并通过序列号找到包头位置...
		uint32_t nPosition = (++cur_seq - min_seq) * nPerPackSize;
		// 如果包头定位位置超过了环形队列总长度 => 说明已经到达环形队列末尾 => 直接返回，休息等待...
		if( nPosition >= cur_circle.size )
			return;
		////////////////////////////////////////////////////////////////////////////////////////////////////
		// 注意：不能用简单的指针操作，环形队列可能会回还，必须用接口 => 从指定相对位置拷贝指定长度数据...
		// 找到指定包头位置的头指针结构体...
		////////////////////////////////////////////////////////////////////////////////////////////////////
		circlebuf_read(&cur_circle, nPosition, szPacketCurrent, nPerPackSize);
		lpCurHeader = (rtp_hdr_t*)szPacketCurrent;
		// 如果新的数据包不是有效音视频数据包 => 返回等待补包...
		if( lpCurHeader->pt == PT_TAG_LOSE )
			return;
		ASSERT( lpCurHeader->pt != PT_TAG_LOSE );
		// 如果新的数据包不是连续序号包 => 返回等待...
		if( cur_seq != lpCurHeader->seq )
			return;
		ASSERT( cur_seq == lpCurHeader->seq );
		// 如果又发现了帧开始标记 => 清空已解析数据帧 => 这个数据帧不完整，需要丢弃...
		// 同时，需要更新临时存放的数据帧相关信息，重新开始组帧...
		if( lpCurHeader->pst > 0 ) {
			is_key = ((lpCurHeader->pk > 0) ? true : false);
			pt_type = lpCurHeader->pt;
			ts_ms = lpCurHeader->ts;
			strFrame.clear();
		}
		// 累加已解析的数据包总长度...
		nConsumeSize += nPerPackSize;
	}
	// 如果没有解析到数据帧 => 打印错误信息...
	if( strFrame.size() <= 0 ) {
		blog(LOG_INFO, "%s Error => Frame size is Zero, PlaySeq: %lu, Type: %d, Key: %d", m_strInnerName.c_str(), nMaxPlaySeq, pt_type, is_key);
		return;
	}
	// 注意：环形队列被抽干后，必须在 doFillLosePack 中对环形队列为空时做特殊处理...
	// 如果环形队列被全部抽干 => 也没关系，在收到新包当中对环形队列为空时做了特殊处理...
	/*if( nConsumeSize >= m_circle.size ) {
		blog(LOG_INFO, "%s Error => Circle Empty, PlaySeq: %lu, CurSeq: %lu", m_strInnerName.c_str(), m_nMaxPlaySeq, cur_seq);
	}*/

	// 注意：已解析的序列号是已经被删除的序列号...
	// 当前已解析的序列号保存为当前最大播放序列号...
	nMaxPlaySeq = cur_seq;
	
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 测试代码 => 将要删除的数据包信息保存到文件当中...
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef DEBUG_FRAME
	uint32_t nTestSeq = min_seq;
	while( nTestSeq <= cur_seq ) {
		DoSaveRecvSeq(lpFrontHeader->seq, lpFrontHeader->psize, lpFrontHeader->pst, lpFrontHeader->ped, lpFrontHeader->ts);
		uint32_t nPosition = (++nTestSeq - min_seq) * nPerPackSize;
		if( nPosition >= cur_circle.size ) break;
		// 注意：这里必须用接口，不能用指针偏移，环形队列可能会回还...
		circlebuf_read(&cur_circle, nPosition, szPacketCurrent, nPerPackSize);
		lpFrontHeader = (rtp_hdr_t*)szPacketCurrent;
	}
	// 进行数据的存盘验证测试...
	DoSaveRecvFile(ts_ms, pt_type, is_key, strFrame);
#endif // DEBUG_FRAME

	// 删除已解析完毕的环形队列数据包 => 回收缓冲区...
	circlebuf_pop_front(&cur_circle, NULL, nConsumeSize);
	// 需要对网络缓冲评估延时时间进行线路选择 => 目前只有一条服务器路线...
	int cur_cache_ms = m_server_cache_time_ms;
	// 将解析到的有效数据帧推入播放对象当中...
	if( m_lpPlaySDL != NULL ) {
		m_lpPlaySDL->PushPacket(cur_cache_ms, strFrame, pt_type, is_key, ts_ms);
	}
	// 打印已投递的完整数据帧信息...
	//blog(LOG_INFO, "%s Frame => Type: %d, Key: %d, PTS: %lu, Size: %d, PlaySeq: %lu, CircleSize: %d", 
	//	   m_strInnerName.c_str(), pt_type, is_key, ts_ms, strFrame.size(), m_nMaxPlaySeq, m_circle.size/nPerPackSize );
	// 修改休息状态 => 已经抽取完整音视频数据帧，不能休息...
	m_bNeedSleep = false;
}

void CSmartRecvThread::doEraseLoseSeq(uint8_t inPType, uint32_t inSeqID)
{
	// 根据数据包类型，找到丢包集合...
	GM_MapLose & theMapLose = (inPType == PT_TAG_AUDIO) ? m_AudioMapLose : m_VideoMapLose;
	// 如果没有找到指定的序列号，直接返回...
	GM_MapLose::iterator itorItem = theMapLose.find(inSeqID);
	if( itorItem == theMapLose.end() )
		return;
	// 删除检测到的丢包节点...
	rtp_lose_t & rtpLose = itorItem->second;
	uint32_t nResendCount = rtpLose.resend_count;
	theMapLose.erase(itorItem);
	// 打印已收到的补包信息，还剩下的未补包个数...
	//blog(LOG_INFO, "%s Supply Erase => LoseSeq: %lu, ResendCount: %lu, LoseSize: %lu, Type: %d",
	//     m_strInnerName.c_str(), inSeqID, nResendCount, theMapLose.size(), inPType);
}
//
// 给丢失数据包预留环形队列缓存空间...
void CSmartRecvThread::doFillLosePack(uint8_t inPType, uint32_t nStartLoseID, uint32_t nEndLoseID)
{
	// 根据数据包类型，找到丢包集合...
	circlebuf & cur_circle = (inPType == PT_TAG_AUDIO) ? m_audio_circle : m_video_circle;
	GM_MapLose & theMapLose = (inPType == PT_TAG_AUDIO) ? m_AudioMapLose : m_VideoMapLose;
	// 需要对网络抖动时间差进行线路选择 => 只有一条服务器补包路线...
	int cur_rtt_var_ms = m_server_rtt_var_ms;
	// 准备数据包结构体并进行初始化 => 连续丢包，设置成相同的重发时间点，否则，会连续发非常多的补包命令...
	uint32_t cur_time_ms = (uint32_t)(os_gettime_ns() / 1000000);
	uint32_t sup_id = nStartLoseID;
	rtp_hdr_t rtpDis = {0};
	rtpDis.pt = PT_TAG_LOSE;
	// 注意：是闭区间 => [nStartLoseID, nEndLoseID]
	while( sup_id <= nEndLoseID ) {
		// 给当前丢包预留缓冲区...
		rtpDis.seq = sup_id;
		circlebuf_push_back(&cur_circle, &rtpDis, sizeof(rtpDis));
		circlebuf_push_back_zero(&cur_circle, DEF_MTU_SIZE);
		// 将丢包序号加入丢包队列当中 => 毫秒时刻点...
		rtp_lose_t rtpLose = {0};
		rtpLose.resend_count = 0;
		rtpLose.lose_seq = sup_id;
		rtpLose.lose_type = inPType;
		// 注意：需要对网络抖动时间差进行线路选择 => 目前只有一条服务器补包路线...
		// 注意：这里要避免 网络抖动时间差 为负数的情况 => 还没有完成第一次探测的情况，也不能为0，会猛烈发包...
		// 重发时间点 => cur_time + rtt_var => 丢包时的当前时间 + 丢包时的网络抖动时间差 => 避免不是丢包，只是乱序的问题...
		rtpLose.resend_time = cur_time_ms + max(cur_rtt_var_ms, MAX_SLEEP_MS);
		theMapLose[sup_id] = rtpLose;
		// 打印已丢包信息，丢包队列长度...
		//blog(LOG_INFO, "%s Lose Seq: %lu, LoseSize: %lu, Type: %d", m_strInnerName.c_str(), sup_id, theMapLose.size(), inPType);
		// 累加当前丢包序列号...
		++sup_id;
	}
}
//
// 将获取的音视频数据包存入环形队列当中...
void CSmartRecvThread::doTagAVPackProcess(char * lpBuffer, int inRecvLen)
{
	// 判断输入数据包的有效性 => 不能小于数据包的头结构长度...
	const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	if( lpBuffer == NULL || inRecvLen < sizeof(rtp_hdr_t) || inRecvLen > nPerPackSize ) {
		blog(LOG_INFO, "%s Error => RecvLen: %d, Max: %d", m_strInnerName.c_str(), inRecvLen, nPerPackSize);
		return;
	}
	/////////////////////////////////////////////////////////////////////////////////
	// 注意：这里只需要判断序列头是否到达，不用判断推流端的准备就绪包是否到达...
	// 注意：因为可能音视频数据包会先于准备就绪包到达，那样的话就会造成丢包...
	/////////////////////////////////////////////////////////////////////////////////
	// 如果没有收到序列头，说明接入还没有完成，直接返回...
	/*if( m_rtp_header.pt != PT_TAG_HEADER ) {
		blog(LOG_INFO, "%s Discard => No Header, Connect not complete", m_strInnerName.c_str());
		return;
	}
	// 既没有音频，也没有视频，直接返回...
	if( !m_rtp_header.hasAudio && !m_rtp_header.hasVideo ) {
		blog(LOG_INFO, "%s Discard => No Audio and No Video", m_strInnerName.c_str());
		return;
	}*/

	///////////////////////////////////////////////////////////////////////////////////////////
	// 注意：不要阻止音视频数据包的接收，只要有数据到达就放入环形队列，并计算0点时刻...
	// 注意：有可能观看端已经在服务器上被创建，但是观看端还没有收到反馈，数据就会先到达...
	///////////////////////////////////////////////////////////////////////////////////////////

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：最终采用的方案 => 发送Create命令设置系统0点时刻...
	// 设定播放器的0点时刻 => 系统认为的第一帧应该已经准备好的系统时刻点...
	// 但由于网络延时，根本不可能在这个时刻点被准备好，一定会产生延时...
	// 因此，这里采用收到第一个数据包就设定为系统0点时刻，尽量降低网络传输延时...
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/*if( m_sys_zero_ns < 0 ) {
		m_sys_zero_ns = CUtilTool::os_gettime_ns() - 100 * 1000000;
		blog(LOG_INFO, "%s Set System Zero Time By First Data => %I64d ms", TM_RECV_NAME, m_sys_zero_ns/1000000);
	}*/

	// 如果收到的缓冲区长度不够 或 填充量为负数，直接丢弃...
	rtp_hdr_t * lpNewHeader = (rtp_hdr_t*)lpBuffer;
	int nDataSize = lpNewHeader->psize + sizeof(rtp_hdr_t);
	int nZeroSize = DEF_MTU_SIZE - lpNewHeader->psize;
	uint8_t  pt_tag = lpNewHeader->pt;
	uint32_t new_id = lpNewHeader->seq;
	uint32_t max_id = new_id;
	uint32_t min_id = new_id;
	// 出现打包错误，丢掉错误包，打印错误信息...
	if( inRecvLen != nDataSize || nZeroSize < 0 ) {
		blog(LOG_INFO, "%s Error => RecvLen: %d, DataSize: %d, ZeroSize: %d", m_strInnerName.c_str(), inRecvLen, nDataSize, nZeroSize);
		return;
	}
	// 音视频使用不同的打包对象和变量...
	uint32_t & nMaxPlaySeq = (pt_tag == PT_TAG_AUDIO) ? m_nAudioMaxPlaySeq : m_nVideoMaxPlaySeq;
	bool   &  bFirstSeqSet = (pt_tag == PT_TAG_AUDIO) ? m_bFirstAudioSeq : m_bFirstVideoSeq;
	circlebuf & cur_circle = (pt_tag == PT_TAG_AUDIO) ? m_audio_circle : m_video_circle;
	// 注意：观看端后接入时，最大播放包序号不是从0开始的...
	// 如果最大播放序列包是0，说明是第一个包，需要保存为最大播放包 => 当前包号 - 1 => 最大播放包是已删除包，当前包序号从1开始...
	if( nMaxPlaySeq <= 0 && !bFirstSeqSet ) {
		bFirstSeqSet = true;
		nMaxPlaySeq = new_id - 1;
		blog(LOG_INFO, "%s First Packet => Seq: %lu, Key: %d, PTS: %lu, PStart: %d, Type: %d",
			 m_strInnerName.c_str(), new_id, lpNewHeader->pk, lpNewHeader->ts, lpNewHeader->pst, pt_tag);
	}
	// 如果收到的补充包比当前最大播放包还要小 => 说明是多次补包的冗余包，直接扔掉...
	// 注意：即使相等也要扔掉，因为最大播放序号包本身已经投递到了播放层，已经被删除了...
	if( new_id <= nMaxPlaySeq ) {
		//blog(LOG_INFO, "%s Supply Discard => Seq: %lu, MaxPlaySeq: %lu, Type: %d", m_strInnerName.c_str(), new_id, nMaxPlaySeq, pt_tag);
		return;
	}
	// 打印收到的音频数据包信息 => 包括缓冲区填充量 => 每个数据包都是统一大小 => rtp_hdr_t + slice + Zero
	//log_trace("%s Seq: %lu, TS: %lu, Type: %d, pst: %d, ped: %d, Slice: %d, ZeroSize: %d",
	//          m_strInnerName.c_str(), lpNewHeader->seq, lpNewHeader->ts, lpNewHeader->pt,
	//          lpNewHeader->pst, lpNewHeader->ped, lpNewHeader->psize, nZeroSize);
	// 首先，将当前包序列号从丢包队列当中删除...
	this->doEraseLoseSeq(pt_tag, new_id);
	//////////////////////////////////////////////////////////////////////////////////////////////////
	// 注意：每个环形队列中的数据包大小是一样的 => rtp_hdr_t + slice + Zero
	//////////////////////////////////////////////////////////////////////////////////////////////////
	char szPacketBuffer[nPerPackSize] = {0};
	// 如果环形队列为空 => 需要对丢包做提前预判并进行处理...
	if( cur_circle.size < nPerPackSize ) {
		// 新到序号包与最大播放包之间有空隙，说明有丢包...
		// 丢包闭区间 => [nMaxPlaySeq + 1, new_id - 1]
		if( new_id > (nMaxPlaySeq + 1) ) {
			this->doFillLosePack(pt_tag, nMaxPlaySeq + 1, new_id - 1);
		}
		// 把最新序号包直接追加到环形队列的最后面，如果与最大播放包之间有空隙，已经在前面的步骤中补充好了...
		// 先加入包头和数据内容...
		circlebuf_push_back(&cur_circle, lpBuffer, inRecvLen);
		// 再加入填充数据内容，保证数据总是保持一个MTU单元大小...
		if( nZeroSize > 0 ) {
			circlebuf_push_back_zero(&cur_circle, nZeroSize);
		}
		// 打印新追加的序号包 => 不管有没有丢包，都要追加这个新序号包...
		//blog(LOG_INFO, "%s Max Seq: %lu, Cricle: Zero", m_strInnerName.c_str(), new_id);
		return;
	}
	// 环形队列中至少要有一个数据包...
	ASSERT( cur_circle.size >= nPerPackSize );
	// 获取环形队列中最小序列号...
	circlebuf_peek_front(&cur_circle, szPacketBuffer, nPerPackSize);
	rtp_hdr_t * lpMinHeader = (rtp_hdr_t*)szPacketBuffer;
	min_id = lpMinHeader->seq;
	// 获取环形队列中最大序列号...
	circlebuf_peek_back(&cur_circle, szPacketBuffer, nPerPackSize);
	rtp_hdr_t * lpMaxHeader = (rtp_hdr_t*)szPacketBuffer;
	max_id = lpMaxHeader->seq;
	// 发生丢包条件 => max_id + 1 < new_id
	// 丢包闭区间 => [max_id + 1, new_id - 1];
	if( max_id + 1 < new_id ) {
		this->doFillLosePack(pt_tag, max_id + 1, new_id - 1);
	}
	// 如果是丢包或正常序号包，放入环形队列，返回...
	if( max_id + 1 <= new_id ) {
		// 先加入包头和数据内容...
		circlebuf_push_back(&cur_circle, lpBuffer, inRecvLen);
		// 再加入填充数据内容，保证数据总是保持一个MTU单元大小...
		if( nZeroSize > 0 ) {
			circlebuf_push_back_zero(&cur_circle, nZeroSize);
		}
		// 打印新加入的最大序号包...
		//blog(LOG_INFO, "%s Max Seq: %lu, Circle: %d", m_strInnerName.c_str(), new_id, m_circle.size/nPerPackSize-1);
		return;
	}
	// 如果是丢包后的补充包 => max_id > new_id
	if( max_id > new_id ) {
		// 如果最小序号大于丢包序号 => 打印错误，直接丢掉这个补充包...
		if( min_id > new_id ) {
			//blog(LOG_INFO, "%s Supply Discard => Seq: %lu, Min-Max: [%lu, %lu], Type: %d", m_strInnerName.c_str(), new_id, min_id, max_id, pt_tag);
			return;
		}
		// 最小序号不能比丢包序号小...
		ASSERT( min_id <= new_id );
		// 计算缓冲区更新位置...
		uint32_t nPosition = (new_id - min_id) * nPerPackSize;
		// 将获取的数据内容更新到指定位置...
		circlebuf_place(&cur_circle, nPosition, lpBuffer, inRecvLen);
		// 打印补充包信息...
		//blog(LOG_INFO, "%s Supply Success => Seq: %lu, Min-Max: [%lu, %lu], Type: %d", m_strInnerName.c_str(), new_id, min_id, max_id, pt_tag);
		return;
	}
	// 如果是其它未知包，打印信息...
	blog(LOG_INFO, "%s Supply Unknown => Seq: %lu, Slice: %d, Min-Max: [%lu, %lu], Type: %d", m_strInnerName.c_str(), new_id, lpNewHeader->psize, min_id, max_id, pt_tag);
}
///////////////////////////////////////////////////////
// 注意：没有发包，也没有收包，需要进行休息...
///////////////////////////////////////////////////////
void CSmartRecvThread::doSleepTo()
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
