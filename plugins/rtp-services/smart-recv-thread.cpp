
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
	// ��������Դ�ն˵��ڲ�����...
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
	// ��ʼ������·�� => ����������...
	m_dt_to_dir = DT_TO_SERVER;
	// ��ʼ������״̬...
	m_nCmdState = kCmdSendCreate;
	// ��ʼ��rtp����ͷ�ṹ��...
	memset(&m_rtp_detect, 0, sizeof(m_rtp_detect));
	memset(&m_rtp_create, 0, sizeof(m_rtp_create));
	memset(&m_rtp_delete, 0, sizeof(m_rtp_delete));
	memset(&m_rtp_header, 0, sizeof(m_rtp_header));
	// ��ʼ������Ƶ���ζ��У�Ԥ����ռ�...
	circlebuf_init(&m_audio_circle);
	circlebuf_init(&m_video_circle);
	circlebuf_reserve(&m_audio_circle, DEF_CIRCLE_SIZE);
	circlebuf_reserve(&m_video_circle, DEF_CIRCLE_SIZE);
	// �����ն����ͺͽṹ������ => m_rtp_header => �ȴ�������� => ��ʦ�ۿ������...
	m_rtp_detect.tm = m_rtp_create.tm = m_rtp_delete.tm = m_rtp_supply.tm = m_tmTag;
	m_rtp_detect.id = m_rtp_create.id = m_rtp_delete.id = m_rtp_supply.id = m_idTag;
	m_rtp_detect.pt = PT_TAG_DETECT;
	m_rtp_create.pt = PT_TAG_CREATE;
	m_rtp_delete.pt = PT_TAG_DELETE;
	m_rtp_supply.pt = PT_TAG_SUPPLY;
	// ��䷿��ź�ֱ��ͨ����...
	m_rtp_create.roomID = nDBRoomID;
	m_rtp_create.liveID = nLiveID;
	m_rtp_delete.roomID = nDBRoomID;
	m_rtp_delete.liveID = nLiveID;
	// �����Զ�̹�����TCP�׽���...
	m_rtp_create.tcpSock = nTCPSockFD;
}

CSmartRecvThread::~CSmartRecvThread()
{
	blog(LOG_INFO, "%s == [~CSmartRecvThread Thread] - Exit Start ==", m_strInnerName.c_str());
	// ֹͣ�̣߳��ȴ��˳�...
	this->StopAndWaitForThread();
	// �ر�UDPSocket����...
	this->CloseSocket();
	// ɾ������Ƶ�����߳�...
	this->ClosePlayer();
	// �ͷ�����Ƶ���ζ��пռ�...
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
	// ����obs��Դ����...
	m_lpObsSource = lpObsSource;
	// ���ȣ��ر�socket...
	this->CloseSocket();
	// ���½�socket...
	ASSERT( m_lpUDPSocket == NULL );
	m_lpUDPSocket = new UDPSocket();
	// ����UDP,��������Ƶ����,����ָ��...
	GM_Error theErr = GM_NoErr;
	theErr = m_lpUDPSocket->Open();
	if( theErr != GM_NoErr ) {
		MsgLogGM(theErr);
		return false;
	}
	// �����첽ģʽ...
	m_lpUDPSocket->NonBlocking();
	// �����ظ�ʹ�ö˿�...
	m_lpUDPSocket->ReuseAddr();
	// ���÷��ͺͽ��ջ�����...
	m_lpUDPSocket->SetSocketSendBufSize(128 * 1024);
	m_lpUDPSocket->SetSocketRecvBufSize(128 * 1024);
	// ����TTL���紩Խ��ֵ...
	m_lpUDPSocket->SetTTL(64);
	// ��ȡ��������ַ��Ϣ => ����������Ϣ����һ��IPV4����...
	const char * lpszAddr = lpUdpAddr;
	hostent * lpHost = gethostbyname(lpszAddr);
	if( lpHost != NULL && lpHost->h_addr_list != NULL ) {
		lpszAddr = inet_ntoa(*(in_addr*)lpHost->h_addr_list[0]);
	}
	// �����������ַ����SendTo����......
	m_lpUDPSocket->SetRemoteAddr(lpszAddr, nUdpPort);
	// ��������ַ�Ͷ˿�ת����host��ʽ����������...
	m_HostServerPort = nUdpPort;
	m_HostServerAddr = ntohl(inet_addr(lpszAddr));
	// �趨ϵͳ��¼��ʱ0��λ��...
	m_login_zero_ns = os_gettime_ns();
	// �����鲥�����߳�...
	this->Start();
	// ����ִ�н��...
	return true;
}

void CSmartRecvThread::Entry()
{
	while( !this->IsStopRequested() ) {
		// ������Ϣ��־ => ֻҪ�з������հ��Ͳ�����Ϣ...
		m_bNeedSleep = true;
		// ���ʹ��������ֱ��ͨ�������...
		this->doSendCreateCmd();
		// ����̽�������...
		this->doSendDetectCmd();
		// ����һ������ķ�����������...
		this->doRecvPacket();
		// �ȷ�����Ƶ��������...
		this->doSendSupplyCmd(true);
		// �ٷ�����Ƶ��������...
		this->doSendSupplyCmd(false);
		// �ӻ��ζ����г�ȡ����һ֡��Ƶ���벥����...
		this->doParseFrame(true);
		// �ӻ��ζ����г�ȡ����һ֡��Ƶ���벥����...
		this->doParseFrame(false);
		// �ȴ����ͻ������һ�����ݰ�...
		this->doSleepTo();
	}
	// ֻ����һ��ɾ�������...
	this->doSendDeleteCmd();
}

void CSmartRecvThread::doSendDeleteCmd()
{
	GM_Error theErr = GM_NoErr;
	if( m_lpUDPSocket == NULL )
		return;
	// �׽�����Ч��ֱ�ӷ���ɾ������...
	ASSERT( m_lpUDPSocket != NULL );
	theErr = m_lpUDPSocket->SendTo((void*)&m_rtp_delete, sizeof(m_rtp_delete));
	(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
	// ��ӡ�ѷ���ɾ�������...
	blog(LOG_INFO, "%s Send Delete RoomID: %lu, LiveID: %d", m_strInnerName.c_str(), m_rtp_delete.roomID, m_rtp_delete.liveID);
}

void CSmartRecvThread::doSendCreateCmd()
{
	if (m_lpUDPSocket == NULL)
		return;
	// �������״̬���Ǵ���������������ֱ�ӷ���...
	if( m_nCmdState != kCmdSendCreate )
		return;
	// ÿ��100���뷢�ʹ�������� => ����ת�����з���...
	int64_t cur_time_ns = os_gettime_ns();
	int64_t period_ns = 100 * 1000000;
	// �������ʱ�仹û����ֱ�ӷ���...
	if( m_next_create_ns > cur_time_ns )
		return;
	ASSERT( cur_time_ns >= m_next_create_ns );
	// ����һ�������������� => �൱�ڵ�¼ע��...
	GM_Error theErr = m_lpUDPSocket->SendTo((void*)&m_rtp_create, sizeof(m_rtp_create));
	(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
	// ��ӡ�ѷ��ʹ�������� => ��һ�����п���û�з��ͳ�ȥ��Ҳ��������...
	blog(LOG_INFO, "%s Send Create RoomID: %lu, LiveID: %d", m_strInnerName.c_str(), m_rtp_create.roomID, m_rtp_create.liveID);
	// �����´η��ʹ��������ʱ���...
	m_next_create_ns = os_gettime_ns() + period_ns;
	// �޸���Ϣ״̬ => �Ѿ��з�����������Ϣ...
	m_bNeedSleep = false;
	//////////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺����յ���һ�������һ���������ʱ�����ۼӵ����Ų�...
	// ��ˣ��ڷ���ÿһ��Create�����ʱ�򶼸���ϵͳ0��ʱ�̣��൱����ǰ������ϵͳ0��ʱ��...
	// �൱���ڷ��������������Ϊ�ǵ�һ֡�����Ѿ�׼���ÿ��Բ��ŵ�ʱ�̵㣬���������粨����ʱ��Ӱ����С��
	//////////////////////////////////////////////////////////////////////////////////////////////////////
	m_sys_zero_ns = os_gettime_ns();
	blog(LOG_INFO, "%s Set System Zero Time By Create => %I64d ms", m_strInnerName.c_str(), m_sys_zero_ns/1000000);
}

void CSmartRecvThread::doSendDetectCmd()
{
	if (m_lpUDPSocket == NULL)
		return;
	// ÿ��1�뷢��һ��̽������� => ����ת�����з���...
	int64_t cur_time_ns = os_gettime_ns();
	int64_t period_ns = 1000 * 1000000;
	// ��һ��̽�����ʱ1/3�뷢�� => �����һ��̽����ȵ�����������������ؽ�����...
	if( m_next_detect_ns < 0 ) { 
		m_next_detect_ns = cur_time_ns + period_ns / 3;
	}
	// �������ʱ�仹û����ֱ�ӷ���...
	if( m_next_detect_ns > cur_time_ns )
		return;
	ASSERT( cur_time_ns >= m_next_detect_ns );
	// ͨ����������ת��̽�� => ��̽�����ʱ��ת���ɺ��룬�ۼ�̽�������...
	m_rtp_detect.tsSrc  = (uint32_t)(cur_time_ns / 1000000);
	m_rtp_detect.dtDir  = DT_TO_SERVER;
	m_rtp_detect.dtNum += 1;
	// �������յ�����Ƶ����������� => ��������û��ʹ��...
	m_rtp_detect.maxAConSeq = this->doCalcMaxConSeq(true);
	m_rtp_detect.maxVConSeq = this->doCalcMaxConSeq(false);
	// ���ýӿڷ���̽������� => ����������չ��Ƶ��������P2Pģʽ�������ݰ�����...
	GM_Error theErr = m_lpUDPSocket->SendTo((void*)&m_rtp_detect, sizeof(m_rtp_detect));
	(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
	// ��ӡ�ѷ���̽�������...
	//blog(LOG_INFO, "%s Send Detect dtNum: %d", m_strInnerName.c_str(), m_rtp_detect.dtNum);
	// �����´η���̽�������ʱ���...
	m_next_detect_ns = os_gettime_ns() + period_ns;
	// �޸���Ϣ״̬ => �Ѿ��з�����������Ϣ...
	m_bNeedSleep = false;
}

uint32_t CSmartRecvThread::doCalcMaxConSeq(bool bIsAudio)
{
	// �������ݰ����ͣ��ҵ��������ϡ����ζ��С���󲥷Ű�...
	GM_MapLose & theMapLose = bIsAudio ? m_AudioMapLose : m_VideoMapLose;
	circlebuf  & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;
	uint32_t  & nMaxPlaySeq = bIsAudio ? m_nAudioMaxPlaySeq : m_nVideoMaxPlaySeq;
	// ���������ļ��� => ������С������ - 1
	if( theMapLose.size() > 0 ) {
		return (theMapLose.begin()->first - 1);
	}
	// û�ж��� => ���ζ���Ϊ�� => ������󲥷����к�...
	if(  cur_circle.size <= 0  )
		return nMaxPlaySeq;
	// û�ж��� => ���յ��������� => ���ζ�����������к�...
	const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	char szPacketBuffer[nPerPackSize] = {0};
	circlebuf_peek_back(&cur_circle, szPacketBuffer, nPerPackSize);
	rtp_hdr_t * lpMaxHeader = (rtp_hdr_t*)szPacketBuffer;
	return lpMaxHeader->seq;
}

// ����������չ��Ƶ��������P2Pģʽ�������ݰ�����...
/*void CSmartRecvThread::doSendReadyCmd()
{
	if (m_lpUDPSocket == NULL)
		return;
	////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺�ۿ��˱����յ�������ת����׼����������֮�󣬲���ֹͣ����׼����������...
	// ��Ϊ��Ҫ��ȡ�������ߵ�ӳ���ַ��ӳ��˿�...
	// �������״̬����׼������������������ֱ�ӷ���...
	////////////////////////////////////////////////////////////////////////////////////////////
	if( m_nCmdState != kCmdSendReady )
		return;
	// ע�⣺��ʱ����Ƶ����ͷ�Ѿ��������...
	// ÿ��100���뷢�;�������� => ����ת�����з���...
	int64_t cur_time_ns = os_gettime_ns();
	int64_t period_ns = 100 * 1000000;
	// �������ʱ�仹û����ֱ�ӷ���...
	if( m_next_ready_ns > cur_time_ns )
		return;
	ASSERT( cur_time_ns >= m_next_ready_ns );
	// ׼����ʱ��׼����������ṹ��...
	rtp_ready_t rtpReady = {0};
	rtpReady.tm = TM_TAG_TEACHER;
	rtpReady.id = ID_TAG_LOOKER;
	rtpReady.pt = PT_TAG_READY;
	// ����һ��׼����������� => ֪ͨѧ�������� => ���Կ�ʼ��������Ƶ���ݰ�...
	GM_Error theErr = m_lpUDPSocket->SendTo((void*)&rtpReady, sizeof(rtpReady));
	(theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL;
	// ��ӡ�ѷ���׼�����������...
	blog(LOG_INFO, "%s Send Ready command", TM_RECV_NAME);
	// �����´η��ʹ��������ʱ���...
	m_next_ready_ns = os_gettime_ns() + period_ns;
	// �޸���Ϣ״̬ => �Ѿ��з�����������Ϣ...
	m_bNeedSleep = false;
}*/

void CSmartRecvThread::doSendSupplyCmd(bool bIsAudio)
{
	if (m_lpUDPSocket == NULL)
		return;
	// �������ݰ����ͣ��ҵ���������...
	GM_MapLose & theMapLose = bIsAudio ? m_AudioMapLose : m_VideoMapLose;
	// ����������϶���Ϊ�գ�ֱ�ӷ���...
	if( theMapLose.size() <= 0 )
		return;
	ASSERT( theMapLose.size() > 0 );
	// �������Ĳ���������...
	const int nHeadSize = sizeof(m_rtp_supply);
	const int nPerPackSize = DEF_MTU_SIZE + nHeadSize;
	char szPacket[nPerPackSize] = {0};
	char * lpData = szPacket + nHeadSize;
	// ��ȡ��ǰʱ��ĺ���ֵ => С�ڻ���ڵ�ǰʱ��Ķ�������Ҫ֪ͨ���Ͷ��ٴη���...
	uint32_t cur_time_ms = (uint32_t)(os_gettime_ns() / 1000000);
	// ��Ҫ�����������ӳ�ֵ������·ѡ�� => ֻ��һ����������·...
	int cur_rtt_ms = m_server_rtt_ms;
	// ���ò�������Ϊ0 => ���¼�����Ҫ�����ĸ���...
	m_rtp_supply.suType = bIsAudio ? PT_TAG_AUDIO : PT_TAG_VIDEO;
	m_rtp_supply.suSize = 0;
	int nCalcMaxResend = 0;
	// �����������У��ҳ���Ҫ�����Ķ������к�...
	GM_MapLose::iterator itorItem = theMapLose.begin();
	while( itorItem != theMapLose.end() ) {
		rtp_lose_t & rtpLose = itorItem->second;
		if( rtpLose.resend_time <= cur_time_ms ) {
			// ����������峬���趨�����ֵ������ѭ�� => ��ಹ��200��...
			if( (nHeadSize + m_rtp_supply.suSize) >= nPerPackSize )
				break;
			// �ۼӲ������Ⱥ�ָ�룬�����������к�...
			memcpy(lpData, &rtpLose.lose_seq, sizeof(uint32_t));
			m_rtp_supply.suSize += sizeof(uint32_t);
			lpData += sizeof(uint32_t);
			// �ۼ��ط�����...
			++rtpLose.resend_count;
			// ������󶪰��ط�����...
			nCalcMaxResend = max(nCalcMaxResend, rtpLose.resend_count);
			// ע�⣺ͬʱ���͵Ĳ������´�Ҳͬʱ���ͣ������γɶ��ɢ�еĲ�������...
			// ע�⣺���һ������������ʱ��û���յ����������Ҫ�ٴη���������Ĳ�������...
			// ע�⣺����Ҫ���� ���綶��ʱ��� Ϊ��������� => ��û����ɵ�һ��̽��������Ҳ����Ϊ0�������ҷ���...
			// �����´��ش�ʱ��� => cur_time + rtt => ����ʱ�ĵ�ǰʱ�� + ���������ӳ�ֵ => ��Ҫ������·ѡ��...
			rtpLose.resend_time = cur_time_ms + max(cur_rtt_ms, MAX_SLEEP_MS);
			// ���������������1���´β�����Ҫ̫�죬׷��һ����Ϣ����..
			rtpLose.resend_time += ((rtpLose.resend_count > 1) ? MAX_SLEEP_MS : 0);
		}
		// �ۼӶ������Ӷ���...
		++itorItem;
	}
	// ������������Ϊ�գ�ֱ�ӷ���...
	if( m_rtp_supply.suSize <= 0 )
		return;
	// ��������������󶪰��ط�����...
	m_nMaxResendCount = nCalcMaxResend;
	// ���²�������ͷ���ݿ�...
	memcpy(szPacket, &m_rtp_supply, nHeadSize);
	// ����������岻Ϊ�գ��Ž��в��������...
	GM_Error theErr = GM_NoErr;
	int nDataSize = nHeadSize + m_rtp_supply.suSize;
	////////////////////////////////////////////////////////////////////////////////////////
	// �����׽��ֽӿڣ�ֱ�ӷ���RTP���ݰ� => Ŀǰֻ��һ������������·��...
	////////////////////////////////////////////////////////////////////////////////////////
	ASSERT( m_dt_to_dir == DT_TO_SERVER );
	theErr = m_lpUDPSocket->SendTo(szPacket, nDataSize);
	// ����д���������ӡ����...
	((theErr != GM_NoErr) ? MsgLogGM(theErr) : NULL);
	// �޸���Ϣ״̬ => �Ѿ��з�����������Ϣ...
	m_bNeedSleep = false;
	// ��ӡ�ѷ��Ͳ�������...
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
	// ���ýӿڴ�������ȡ���ݰ� => �������첽�׽��֣����������� => ���ܴ���...
	theErr = m_lpUDPSocket->RecvFrom(&outRemoteAddr, &outRemotePort, ioBuffer, inBufLen, &outRecvLen);
	// ע�⣺���ﲻ�ô�ӡ������Ϣ��û���յ����ݾ���������...
	if( theErr != GM_NoErr || outRecvLen <= 0 )
		return;
	// �޸���Ϣ״̬ => �Ѿ��ɹ��հ���������Ϣ...
	m_bNeedSleep = false;
	// �ж����������ݳ��� => DEF_MTU_SIZE + rtp_hdr_t
	UInt32 nMaxSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	if (outRecvLen > nMaxSize) {
		blog(LOG_INFO, "[Recv-Error] Max => %lu, Addr => %lu:%d, Size => %lu", nMaxSize, outRemoteAddr, outRemotePort, outRecvLen);
		return;
	}
    // ��ȡ��һ���ֽڵĸ�4λ���õ����ݰ�����...
    uint8_t ptTag = (ioBuffer[0] >> 4) & 0x0F;
	// ���յ�������������ͷַ�...
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
	// ͨ�� rtp_header_t ��Ϊ���巢�͹����� => ������ֱ��ԭ��ת����ѧ�������˵�����ͷ�ṹ��...
	if( m_lpUDPSocket == NULL || lpBuffer == NULL || inRecvLen < 0 || inRecvLen < sizeof(rtp_header_t) )
		return;
	// ������Ǵ�������״̬�����ߣ��Ѿ��յ�������ͷ��ֱ�ӷ���...
	if( m_nCmdState != kCmdSendCreate || m_rtp_header.pt == PT_TAG_HEADER || m_rtp_header.hasAudio || m_rtp_header.hasVideo )
		return;
    // ͨ����һ���ֽڵĵ�2λ���ж��ն�����...
    uint8_t tmTag = lpBuffer[0] & 0x03;
    // ��ȡ��һ���ֽڵ���2λ���õ��ն����...
    uint8_t idTag = (lpBuffer[0] >> 2) & 0x03;
    // ��ȡ��һ���ֽڵĸ�4λ���õ����ݰ�����...
    uint8_t ptTag = (lpBuffer[0] >> 4) & 0x0F;
	// ��������߲��� ѧ�������� => ֱ�ӷ���...
	if( tmTag != TM_TAG_STUDENT || idTag != ID_TAG_PUSHER )
		return;
	// ������ֳ��Ȳ�����ֱ�ӷ���...
	memcpy(&m_rtp_header, lpBuffer, sizeof(m_rtp_header));
	int nNeedSize = m_rtp_header.spsSize + m_rtp_header.ppsSize + sizeof(m_rtp_header);
	if( nNeedSize != inRecvLen ) {
		blog(LOG_INFO, "%s Recv Header error, RecvLen: %d", m_strInnerName.c_str(), inRecvLen);
		memset(&m_rtp_header, 0, sizeof(m_rtp_header));
		return;
	}
	// ��ȡSPS��PPS��ʽͷ��Ϣ...
	char * lpData = lpBuffer + sizeof(m_rtp_header);
	if( m_rtp_header.spsSize > 0 ) {
		m_strSPS.assign(lpData, m_rtp_header.spsSize);
		lpData += m_rtp_header.spsSize;
	}
	// ���� PPS ��ʽͷ...
	if( m_rtp_header.ppsSize > 0 ) {
		m_strPPS.assign(lpData, m_rtp_header.ppsSize);
		lpData += m_rtp_header.ppsSize;
	}
	// �޸�����״̬ => ���������׼�����������...
	m_nCmdState = kCmdConnectOK;
	// ��ӡ�յ�����ͷ�ṹ����Ϣ...
	blog(LOG_INFO, "%s Recv Header SPS: %d, PPS: %d", m_strInnerName.c_str(), m_strSPS.size(), m_strPPS.size());
	// ����������Ѿ�������ֱ�ӷ���...
	if( m_lpPlaySDL != NULL )
		return;
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ��ʼ�ؽ����ز����������趨�ۿ��˵�0��ʱ��...
	// ע�⣺����ǳ���Ҫ����������ʼת������������Ƶ�����ˣ������趨�ۿ��ߵ�ϵͳ��ʱ��㣬0��ʱ��...
	// 0��ʱ�̣�ϵͳ��Ϊ��һ֡���ݵ�����ϵͳʱ����㣬��һ֡���ݾ�Ӧ�������ʱ�̵��Ѿ���׼����...
	// ���ǣ�����������ʱ�����������������ʱ�̵㱻׼���ã�������յ���һ֡����֮�����趨0��ʱ�̣��ͻ�������õĽ�����ʱ...
	// ע�⣺����յ���һ�������һ���������ʱ�����ۼӵ����Ų�...
	// ע�⣺���ղ��õķ��� => ����Create��������ϵͳ0��ʱ��...
	// ����ϵͳ0��ʱ�� => ��һ�����ݰ�����ʱ�����ϵͳ0��ʱ��...
	// �п��ܵ�һ�����ݰ���û�е���������õ�ǰϵͳʱ����Ϊ0��ʱ��...
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/*if( m_sys_zero_ns < 0 ) {
		m_sys_zero_ns = os_gettime_ns() - 100 * 1000000;
		blog("%s Set System Zero Time By Header => %I64d ms", TM_RECV_NAME, m_sys_zero_ns/1000000);
	}*/
	// ����Ĭ�ϵ����绺������ʱ�� => ʹ��֡��������...
	m_server_cache_time_ms = 1000 / ((m_rtp_header.fpsNum > 0) ? m_rtp_header.fpsNum : 25); 
	// �½�����������ʼ������Ƶ�߳�...
	m_lpPlaySDL = new CPlaySDL(m_lpObsSource, m_sys_zero_ns, m_strInnerName);
	// �������Ƶ����ʼ����Ƶ�߳�...
	if( m_rtp_header.hasVideo ) {
		int nPicFPS = m_rtp_header.fpsNum;
		int nPicWidth = m_rtp_header.picWidth;
		int nPicHeight = m_rtp_header.picHeight;
		m_lpPlaySDL->InitVideo(m_strSPS, m_strPPS, nPicWidth, nPicHeight, nPicFPS);
	} 
	// �������Ƶ����ʼ����Ƶ�߳�...
	if( m_rtp_header.hasAudio ) {
		int nRateIndex = m_rtp_header.rateIndex;
		int nChannelNum = m_rtp_header.channelNum;
		m_lpPlaySDL->InitAudio(nRateIndex, nChannelNum);
	}
}

// ע�⣺�ۿ��˱����յ�������ת����׼����������֮�󣬲���ֹͣ����׼�����������ΪҪ��ȡ�������ߵ�ӳ���ַ��ӳ��˿�...
/*void CSmartRecvThread::doProcServerReady(char * lpBuffer, int inRecvLen)
{
	// ���յ�����ѧ��������Ҫ��ֹͣ����׼�����������...
	if( m_lpUDPSocket == NULL || lpBuffer == NULL || inRecvLen < 0 || inRecvLen < sizeof(rtp_ready_t) )
		return;
	// �������׼����������״̬�����ߣ��Ѿ��յ��������˷�����׼���������ֱ�ӷ���...
	if( m_nCmdState != kCmdSendReady || m_rtp_ready.pt == PT_TAG_READY )
		return;
    // ͨ����һ���ֽڵĵ�2λ���ж��ն�����...
    uint8_t tmTag = lpBuffer[0] & 0x03;
    // ��ȡ��һ���ֽڵ���2λ���õ��ն����...
    uint8_t idTag = (lpBuffer[0] >> 2) & 0x03;
    // ��ȡ��һ���ֽڵĸ�4λ���õ����ݰ�����...
    uint8_t ptTag = (lpBuffer[0] >> 4) & 0x0F;
	// ������� ѧ�������� ������׼����������ֱ�ӷ���...
	if( tmTag != TM_TAG_STUDENT || idTag != ID_TAG_PUSHER )
		return;
	// �޸�����״̬ => ������ϣ���Ҫ�ٷ���׼������������...
	m_nCmdState = kCmdConnectOK;
	// ����ѧ�������˷��͵�׼���������ݰ�����...
	memcpy(&m_rtp_ready, lpBuffer, sizeof(m_rtp_ready));
	// ��ӡ�յ�׼����������� => ����ַת�����ַ���...
	string strAddr = SocketUtils::ConvertAddrToString(m_rtp_ready.recvAddr);
	blog(LOG_INFO, "%s Recv Ready from %s:%d", TM_RECV_NAME, strAddr.c_str(), m_rtp_ready.recvPort);
}*/

bool CSmartRecvThread::IsLoginTimeout()
{
	// ����Ѿ��ǵ�¼�ɹ�״̬��ֱ�ӷ���...
	if (m_nCmdState == kCmdConnectOK)
		return false;
	ASSERT(m_nCmdState != kCmdConnectOK);
	// �����ѳ��Ե�¼ʱ�䣬�����ʱ����true...
	uint32_t cur_elapse_ms = (uint32_t)((os_gettime_ns() - m_login_zero_ns) / 1000000);
	return ((cur_elapse_ms >= DEF_TIMEOUT_MS) ? true : false);
}

void CSmartRecvThread::doTagDetectProcess(char * lpBuffer, int inRecvLen)
{
	GM_Error theErr = GM_NoErr;
	if( m_lpUDPSocket == NULL || lpBuffer == NULL || inRecvLen <= 0 || inRecvLen < sizeof(rtp_detect_t) )
		return;
    // ͨ����һ���ֽڵĵ�2λ���ж��ն�����...
    uint8_t tmTag = lpBuffer[0] & 0x03;
    // ��ȡ��һ���ֽڵ���2λ���õ��ն����...
    uint8_t idTag = (lpBuffer[0] >> 2) & 0x03;
    // ��ȡ��һ���ֽڵĸ�4λ���õ����ݰ�����...
    uint8_t ptTag = (lpBuffer[0] >> 4) & 0x0F;
	// ��ʦ�ۿ��� �Լ�������̽���������������ʱ...
	if( tmTag == TM_TAG_TEACHER && idTag == ID_TAG_LOOKER ) {
		// ��ȡ�յ���̽�����ݰ�...
		rtp_detect_t rtpDetect = {0};
		memcpy(&rtpDetect, lpBuffer, sizeof(rtpDetect));
		// Ŀǰֻ�������Է����������̽����...
		if (rtpDetect.dtDir != DT_TO_SERVER)
			return;
		ASSERT(rtpDetect.dtDir == DT_TO_SERVER);
		// ���缫������±������������ʧ��...
		// �ȴ���������ش�����Ƶ��С��Ű�...
		this->doServerMinSeq(true, rtpDetect.maxAConSeq);
		// �ٴ���������ش�����Ƶ��С��Ű�...
		this->doServerMinSeq(false, rtpDetect.maxVConSeq);
		// ��ǰʱ��ת���ɺ��룬����������ʱ => ��ǰʱ�� - ̽��ʱ��...
		uint32_t cur_time_ms = (uint32_t)(os_gettime_ns() / 1000000);
		int keep_rtt = cur_time_ms - rtpDetect.tsSrc;
		// �����Ƶ����Ƶ��û�ж�����ֱ���趨����ط�����Ϊ0...
		if( m_AudioMapLose.size() <= 0 && m_VideoMapLose.size() <= 0 ) {
			m_nMaxResendCount = 0;
		}
		// ��ֹ����ͻ���ӳ�����, ��� TCP �� RTT ����˥�����㷨...
		if( m_server_rtt_ms < 0 ) { m_server_rtt_ms = keep_rtt; }
		else { m_server_rtt_ms = (7 * m_server_rtt_ms + keep_rtt) / 8; }
		// �������綶����ʱ���ֵ => RTT������ֵ...
		if( m_server_rtt_var_ms < 0 ) { m_server_rtt_var_ms = abs(m_server_rtt_ms - keep_rtt); }
		else { m_server_rtt_var_ms = (m_server_rtt_var_ms * 3 + abs(m_server_rtt_ms - keep_rtt)) / 4; }
		// ���㻺������ʱ�� => ���û�ж�����ʹ������������ʱ+���綶����ʱ֮��...
		if( m_nMaxResendCount > 0 ) {
			m_server_cache_time_ms = (2 * m_nMaxResendCount + 1) * (m_server_rtt_ms + m_server_rtt_var_ms) / 2;
		} else {
			m_server_cache_time_ms = m_server_rtt_ms + m_server_rtt_var_ms;
		}
		// ��ӡ̽���� => ̽����� | ������ʱ(����)...
		int nADFrame = ((m_lpPlaySDL != NULL) ? m_lpPlaySDL->GetAFrameSize() : 0);
		int nVDFrame = ((m_lpPlaySDL != NULL) ? m_lpPlaySDL->GetVFrameSize() : 0);
		const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
		blog(LOG_INFO, "%s Recv Detect => LiveID: %d, Dir: %d, dtNum: %d, rtt: %d ms, rtt_var: %d ms, cache_time: %d ms, ACircle: %d:%d, VCircle: %d:%d",
			 m_strInnerName.c_str(), m_rtp_create.liveID, rtpDetect.dtDir, rtpDetect.dtNum, m_server_rtt_ms, m_server_rtt_var_ms, m_server_cache_time_ms,
			 m_audio_circle.size / nPerPackSize, nADFrame, m_video_circle.size / nPerPackSize, nVDFrame);
		// ��ӡ�������ײ�Ļ���״̬��Ϣ...
		/*if (m_lpPlaySDL != NULL) {
			blog(LOG_INFO, "%s Recv Detect => APacket: %d, VPacket: %d, AFrame: %d, VFrame: %d", TM_RECV_NAME,
				m_lpPlaySDL->GetAPacketSize(), m_lpPlaySDL->GetVPacketSize(), m_lpPlaySDL->GetAFrameSize(), m_lpPlaySDL->GetVFrameSize());
		}*/
		/////////////////////////////////////////////////////////////////////////
		// ע�⣺����Ҫ�����������棬�Ľ�����Զ��DT_TO_SERVER...
		// �Բ�����·����ѡ�� => ѡ������ͨ����СrttΪ����֪ͨ��·...
		/////////////////////////////////////////////////////////////////////////
		// ���P2P��·��Ч�����Ҹ��죬�趨ΪP2P��·�������趨Ϊ��������·...
		/*if((m_p2p_rtt_ms >= 0) && (m_p2p_rtt_ms < m_server_rtt_ms)) {
			m_dt_to_dir = DT_TO_P2P;
		} else {
			m_dt_to_dir = DT_TO_SERVER;
		}*/
	}
}
// ���ؽ��ջ������ݰ��������ǿ���������֡�����ǿ�����������С���ݰ�...
// ���ǣ������缫������£���������ɾ���ķ�ʽ�ͻ������ã��������������ʧ��...
void CSmartRecvThread::doServerMinSeq(bool bIsAudio, uint32_t inMinSeq)
{
	// ����������С������Ч��ֱ�ӷ���...
	if (inMinSeq <= 0)
		return;
	// �������ݰ����ͣ��ҵ��������ϡ����ζ��С���󲥷Ű�...
	GM_MapLose & theMapLose = bIsAudio ? m_AudioMapLose : m_VideoMapLose;
	circlebuf  & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;
	uint32_t  & nMaxPlaySeq = bIsAudio ? m_nAudioMaxPlaySeq : m_nVideoMaxPlaySeq;
	// ����������Ч���Ž��ж�����ѯ����...
	if (theMapLose.size() > 0) {
		// �����������У�����С�ڷ���������С��ŵĶ�������Ҫ�ӵ�...
		GM_MapLose::iterator itorItem = theMapLose.begin();
		while (itorItem != theMapLose.end()) {
			rtp_lose_t & rtpLose = itorItem->second;
			// ���Ҫ���İ��ţ���С����С���ţ����Բ���...
			if (rtpLose.lose_seq >= inMinSeq) {
				itorItem++;
				continue;
			}
			// ���Ҫ���İ��ţ�����С���Ż�ҪС��ֱ�Ӷ������Ѿ�������...
			theMapLose.erase(itorItem++);
		}
	}
	// ������ζ���Ϊ�գ���������ֱ�ӷ���...
	if (cur_circle.size <= 0)
		return;
	// ��ȡ���ζ��е�����С��ź������ţ��ҵ�����߽�...
	const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	char szPacketBuffer[nPerPackSize] = { 0 };
	rtp_hdr_t * lpCurHeader = NULL;
	uint32_t    min_seq = 0, max_seq = 0;
	// ��ȡ��С�����ݰ������ݣ���ȡ��С���к�...
	circlebuf_peek_front(&cur_circle, szPacketBuffer, nPerPackSize);
	lpCurHeader = (rtp_hdr_t*)szPacketBuffer;
	min_seq = lpCurHeader->seq;
	// ��ȡ�������ݰ������ݣ���ȡ������к�...
	circlebuf_peek_back(&cur_circle, szPacketBuffer, nPerPackSize);
	lpCurHeader = (rtp_hdr_t*)szPacketBuffer;
	max_seq = lpCurHeader->seq;
	// ������ζ����е���С��Ű��ȷ������˵���С��Ű��󣬲���������...
	if (min_seq >= inMinSeq)
		return;
	// ��ӡ���ζ�������ǰ�ĸ�������״ֵ̬...
	blog(LOG_INFO, "%s Clear => LiveID: %d, Audio: %d, ServerMin: %lu, MinSeq: %lu, MaxSeq: %lu, MaxPlaySeq: %lu",
		m_strInnerName.c_str(), m_rtp_create.liveID, bIsAudio, inMinSeq, min_seq, max_seq, nMaxPlaySeq);
	// ������ζ����е������Ű��ȷ������˵���С��Ű�С������ȫ������...
	if (max_seq < inMinSeq) {
		inMinSeq = max_seq + 1;
	}
	// ��������Χ => [min_seq, inMinSeq]...
	int nConsumeSize = (inMinSeq - min_seq) * nPerPackSize;
	circlebuf_pop_front(&cur_circle, NULL, nConsumeSize);
	// ����󲥷Ű��趨Ϊ => ��С��Ű� - 1...
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
	// ע�⣺���ζ�������Ҫ��һ�����ݰ����ڣ������ڷ�������ʱ���޷�����...
	// ���� => ��ӡ�յ�����Ч����ţ����ӻ��ζ��е���ɾ��...
	////////////////////////////////////////////////////////////////////////////
	const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	char szPacketCheck[nPerPackSize] = {0};
	if( m_circle.size <= nPerPackSize )
		return;
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺ǧ�����ڻ��ζ��е��н���ָ���������start_pos > end_posʱ�����ܻ���Խ�����...
	// ���ԣ�һ��Ҫ�ýӿڶ�ȡ���������ݰ�֮���ٽ��в����������ָ�룬һ�������ػ����ͻ����...
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	circlebuf_peek_front(&m_circle, szPacketCheck, nPerPackSize);
	rtp_hdr_t * lpFirstHeader = (rtp_hdr_t*)szPacketCheck;
	if( lpFirstHeader->pt == PT_TAG_LOSE )
		return;
	//log_trace( "[Teacher-Looker] Seq: %lu, Type: %d, Key: %d, Size: %d, TS: %lu",
	//		lpFirstHeader->seq, lpFirstHeader->pt, lpFirstHeader->pk, lpFirstHeader->psize, lpFirstHeader->ts);
	// ����յ�����Ч��Ų��������ģ���ӡ����...
	if( (m_nMaxPlaySeq + 1) != lpFirstHeader->seq ) {
		log_trace("[Teacher-Looker] Error => PlaySeq: %lu, CurSeq: %lu", m_nMaxPlaySeq, lpFirstHeader->seq);
	}
	// ������ǰ������ţ��Ƴ����ζ���...
	m_nMaxPlaySeq = lpFirstHeader->seq;
	circlebuf_pop_front(&m_circle, NULL, nPerPackSize);*/

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺ǧ�����ڻ��ζ��е��н���ָ���������start_pos > end_posʱ�����ܻ���Խ�����...
	// ��֤�Ƿ񶪰���ʵ��...
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
	// �����¼��û���յ������������򲥷���Ϊ�գ���ֱ�ӷ��أ������ȴ�...
	//////////////////////////////////////////////////////////////////////////////////
	if( m_nCmdState <= kCmdSendCreate ) {
		//blog(LOG_INFO, "%s Wait For Player => Audio: %d, Video: %d", TM_RECV_NAME, m_audio_circle.size/nPerPackSize, m_video_circle.size/nPerPackSize );
		return;
	}

	// ����Ƶʹ�ò�ͬ�Ĵ������ͱ���...
	uint32_t & nMaxPlaySeq = bIsAudio ? m_nAudioMaxPlaySeq : m_nVideoMaxPlaySeq;
	circlebuf & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺���ζ�������Ҫ��һ�����ݰ����ڣ������ڷ�������ʱ���޷����֣�����Ű��ȵ���С�Ű��󵽣��ͻᱻ�ӵ�...
	// ע�⣺���ζ����ڳ�ȡ��������Ƶ����֮֡��Ҳ���ܱ���ɣ����ԣ������� doFillLosePack �жԻ��ζ���Ϊ��ʱ�����⴦��...
	// ������ζ���Ϊ�ջ򲥷���������Ч��ֱ�ӷ���...
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ׼�����������֡��������Ҫ�ı����Ϳռ����...
	const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	char szPacketCurrent[nPerPackSize] = {0};
	char szPacketFront[nPerPackSize] = {0};
	rtp_hdr_t * lpFrontHeader = NULL;
	if( cur_circle.size <= nPerPackSize )
		return;
	/////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺ǧ�����ڻ��ζ��е��н���ָ���������start_pos > end_posʱ�����ܻ���Խ�����...
	// ���ԣ�һ��Ҫ�ýӿڶ�ȡ���������ݰ�֮���ٽ��в����������ָ�룬һ�������ػ����ͻ����...
	/////////////////////////////////////////////////////////////////////////////////////////////////
	// ���ҵ����ζ�������ǰ�����ݰ���ͷָ�� => ��С���...
	circlebuf_peek_front(&cur_circle, szPacketFront, nPerPackSize);
	lpFrontHeader = (rtp_hdr_t*)szPacketFront;
	// �����С��Ű�����Ҫ����Ķ��� => ������Ϣ�ȴ�...
	if( lpFrontHeader->pt == PT_TAG_LOSE )
		return;
	// �����С��Ű���������Ƶ����֡�Ŀ�ʼ�� => ɾ��������ݰ���������...
	if( lpFrontHeader->pst <= 0 ) {
		// ���µ�ǰ��󲥷����кŲ���������...
		nMaxPlaySeq = lpFrontHeader->seq;
		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// ע�⣺��ɾ�����ݰ�֮ǰ�����Խ��д��̲��Բ���...
		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef DEBUG_RAME
		DoSaveRecvSeq(lpFrontHeader->seq, lpFrontHeader->psize, lpFrontHeader->pst, lpFrontHeader->ped, lpFrontHeader->ts);
#endif // DEBUG_FRAME
		// ɾ��������ݰ������ز���Ϣ��������...
		circlebuf_pop_front(&cur_circle, NULL, nPerPackSize);
		// �޸���Ϣ״̬ => �Ѿ���ȡ���ݰ���������Ϣ...
		m_bNeedSleep = false;
		// ��ӡ��֡ʧ����Ϣ => û���ҵ�����֡�Ŀ�ʼ���...
		blog(LOG_INFO, "%s Error => Frame start code not find, Seq: %lu, Type: %d, Key: %d, PTS: %lu", 
			m_strInnerName.c_str(), lpFrontHeader->seq, lpFrontHeader->pt, lpFrontHeader->pk, lpFrontHeader->ts );
		return;
	}
	ASSERT( lpFrontHeader->pst );
	// ��ʼ��ʽ�ӻ��ζ����г�ȡ����Ƶ����֡...
	int         pt_type = lpFrontHeader->pt;
	bool        is_key = ((lpFrontHeader->pk > 0) ? true : false);
	uint32_t    ts_ms = lpFrontHeader->ts;
	uint32_t    min_seq = lpFrontHeader->seq;
	uint32_t    cur_seq = lpFrontHeader->seq;
	rtp_hdr_t * lpCurHeader = lpFrontHeader;
	uint32_t    nConsumeSize = nPerPackSize;
	string      strFrame;
	// ��������֡ => �����������pst��ʼ����ped����...
	while( true ) {
		// �����ݰ�����Ч�غɱ�������...
		char * lpData = (char*)lpCurHeader + sizeof(rtp_hdr_t);
		strFrame.append(lpData, lpCurHeader->psize);
		// ������ݰ���֡�Ľ����� => ����֡������...
		if( lpCurHeader->ped > 0 )
			break;
		// ������ݰ�����֡�Ľ����� => ����Ѱ��...
		ASSERT( lpCurHeader->ped <= 0 );
		// �ۼӰ����кţ���ͨ�����к��ҵ���ͷλ��...
		uint32_t nPosition = (++cur_seq - min_seq) * nPerPackSize;
		// �����ͷ��λλ�ó����˻��ζ����ܳ��� => ˵���Ѿ����ﻷ�ζ���ĩβ => ֱ�ӷ��أ���Ϣ�ȴ�...
		if( nPosition >= cur_circle.size )
			return;
		////////////////////////////////////////////////////////////////////////////////////////////////////
		// ע�⣺�����ü򵥵�ָ����������ζ��п��ܻ�ػ��������ýӿ� => ��ָ�����λ�ÿ���ָ����������...
		// �ҵ�ָ����ͷλ�õ�ͷָ��ṹ��...
		////////////////////////////////////////////////////////////////////////////////////////////////////
		circlebuf_read(&cur_circle, nPosition, szPacketCurrent, nPerPackSize);
		lpCurHeader = (rtp_hdr_t*)szPacketCurrent;
		// ����µ����ݰ�������Ч����Ƶ���ݰ� => ���صȴ�����...
		if( lpCurHeader->pt == PT_TAG_LOSE )
			return;
		ASSERT( lpCurHeader->pt != PT_TAG_LOSE );
		// ����µ����ݰ�����������Ű� => ���صȴ�...
		if( cur_seq != lpCurHeader->seq )
			return;
		ASSERT( cur_seq == lpCurHeader->seq );
		// ����ַ�����֡��ʼ��� => ����ѽ�������֡ => �������֡����������Ҫ����...
		// ͬʱ����Ҫ������ʱ��ŵ�����֡�����Ϣ�����¿�ʼ��֡...
		if( lpCurHeader->pst > 0 ) {
			is_key = ((lpCurHeader->pk > 0) ? true : false);
			pt_type = lpCurHeader->pt;
			ts_ms = lpCurHeader->ts;
			strFrame.clear();
		}
		// �ۼ��ѽ��������ݰ��ܳ���...
		nConsumeSize += nPerPackSize;
	}
	// ���û�н���������֡ => ��ӡ������Ϣ...
	if( strFrame.size() <= 0 ) {
		blog(LOG_INFO, "%s Error => Frame size is Zero, PlaySeq: %lu, Type: %d, Key: %d", m_strInnerName.c_str(), nMaxPlaySeq, pt_type, is_key);
		return;
	}
	// ע�⣺���ζ��б���ɺ󣬱����� doFillLosePack �жԻ��ζ���Ϊ��ʱ�����⴦��...
	// ������ζ��б�ȫ����� => Ҳû��ϵ�����յ��°����жԻ��ζ���Ϊ��ʱ�������⴦��...
	/*if( nConsumeSize >= m_circle.size ) {
		blog(LOG_INFO, "%s Error => Circle Empty, PlaySeq: %lu, CurSeq: %lu", m_strInnerName.c_str(), m_nMaxPlaySeq, cur_seq);
	}*/

	// ע�⣺�ѽ��������к����Ѿ���ɾ�������к�...
	// ��ǰ�ѽ��������кű���Ϊ��ǰ��󲥷����к�...
	nMaxPlaySeq = cur_seq;
	
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ���Դ��� => ��Ҫɾ�������ݰ���Ϣ���浽�ļ�����...
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef DEBUG_FRAME
	uint32_t nTestSeq = min_seq;
	while( nTestSeq <= cur_seq ) {
		DoSaveRecvSeq(lpFrontHeader->seq, lpFrontHeader->psize, lpFrontHeader->pst, lpFrontHeader->ped, lpFrontHeader->ts);
		uint32_t nPosition = (++nTestSeq - min_seq) * nPerPackSize;
		if( nPosition >= cur_circle.size ) break;
		// ע�⣺��������ýӿڣ�������ָ��ƫ�ƣ����ζ��п��ܻ�ػ�...
		circlebuf_read(&cur_circle, nPosition, szPacketCurrent, nPerPackSize);
		lpFrontHeader = (rtp_hdr_t*)szPacketCurrent;
	}
	// �������ݵĴ�����֤����...
	DoSaveRecvFile(ts_ms, pt_type, is_key, strFrame);
#endif // DEBUG_FRAME

	// ɾ���ѽ�����ϵĻ��ζ������ݰ� => ���ջ�����...
	circlebuf_pop_front(&cur_circle, NULL, nConsumeSize);
	// ��Ҫ�����绺��������ʱʱ�������·ѡ�� => Ŀǰֻ��һ��������·��...
	int cur_cache_ms = m_server_cache_time_ms;
	// ������������Ч����֡���벥�Ŷ�����...
	if( m_lpPlaySDL != NULL ) {
		m_lpPlaySDL->PushPacket(cur_cache_ms, strFrame, pt_type, is_key, ts_ms);
	}
	// ��ӡ��Ͷ�ݵ���������֡��Ϣ...
	//blog(LOG_INFO, "%s Frame => Type: %d, Key: %d, PTS: %lu, Size: %d, PlaySeq: %lu, CircleSize: %d", 
	//	   m_strInnerName.c_str(), pt_type, is_key, ts_ms, strFrame.size(), m_nMaxPlaySeq, m_circle.size/nPerPackSize );
	// �޸���Ϣ״̬ => �Ѿ���ȡ��������Ƶ����֡��������Ϣ...
	m_bNeedSleep = false;
}

void CSmartRecvThread::doEraseLoseSeq(uint8_t inPType, uint32_t inSeqID)
{
	// �������ݰ����ͣ��ҵ���������...
	GM_MapLose & theMapLose = (inPType == PT_TAG_AUDIO) ? m_AudioMapLose : m_VideoMapLose;
	// ���û���ҵ�ָ�������кţ�ֱ�ӷ���...
	GM_MapLose::iterator itorItem = theMapLose.find(inSeqID);
	if( itorItem == theMapLose.end() )
		return;
	// ɾ����⵽�Ķ����ڵ�...
	rtp_lose_t & rtpLose = itorItem->second;
	uint32_t nResendCount = rtpLose.resend_count;
	theMapLose.erase(itorItem);
	// ��ӡ���յ��Ĳ�����Ϣ����ʣ�µ�δ��������...
	//blog(LOG_INFO, "%s Supply Erase => LoseSeq: %lu, ResendCount: %lu, LoseSize: %lu, Type: %d",
	//     m_strInnerName.c_str(), inSeqID, nResendCount, theMapLose.size(), inPType);
}
//
// ����ʧ���ݰ�Ԥ�����ζ��л���ռ�...
void CSmartRecvThread::doFillLosePack(uint8_t inPType, uint32_t nStartLoseID, uint32_t nEndLoseID)
{
	// �������ݰ����ͣ��ҵ���������...
	circlebuf & cur_circle = (inPType == PT_TAG_AUDIO) ? m_audio_circle : m_video_circle;
	GM_MapLose & theMapLose = (inPType == PT_TAG_AUDIO) ? m_AudioMapLose : m_VideoMapLose;
	// ��Ҫ�����綶��ʱ��������·ѡ�� => ֻ��һ������������·��...
	int cur_rtt_var_ms = m_server_rtt_var_ms;
	// ׼�����ݰ��ṹ�岢���г�ʼ�� => �������������ó���ͬ���ط�ʱ��㣬���򣬻��������ǳ���Ĳ�������...
	uint32_t cur_time_ms = (uint32_t)(os_gettime_ns() / 1000000);
	uint32_t sup_id = nStartLoseID;
	rtp_hdr_t rtpDis = {0};
	rtpDis.pt = PT_TAG_LOSE;
	// ע�⣺�Ǳ����� => [nStartLoseID, nEndLoseID]
	while( sup_id <= nEndLoseID ) {
		// ����ǰ����Ԥ��������...
		rtpDis.seq = sup_id;
		circlebuf_push_back(&cur_circle, &rtpDis, sizeof(rtpDis));
		circlebuf_push_back_zero(&cur_circle, DEF_MTU_SIZE);
		// ��������ż��붪�����е��� => ����ʱ�̵�...
		rtp_lose_t rtpLose = {0};
		rtpLose.resend_count = 0;
		rtpLose.lose_seq = sup_id;
		rtpLose.lose_type = inPType;
		// ע�⣺��Ҫ�����綶��ʱ��������·ѡ�� => Ŀǰֻ��һ������������·��...
		// ע�⣺����Ҫ���� ���綶��ʱ��� Ϊ��������� => ��û����ɵ�һ��̽��������Ҳ����Ϊ0�������ҷ���...
		// �ط�ʱ��� => cur_time + rtt_var => ����ʱ�ĵ�ǰʱ�� + ����ʱ�����綶��ʱ��� => ���ⲻ�Ƕ�����ֻ�����������...
		rtpLose.resend_time = cur_time_ms + max(cur_rtt_var_ms, MAX_SLEEP_MS);
		theMapLose[sup_id] = rtpLose;
		// ��ӡ�Ѷ�����Ϣ���������г���...
		//blog(LOG_INFO, "%s Lose Seq: %lu, LoseSize: %lu, Type: %d", m_strInnerName.c_str(), sup_id, theMapLose.size(), inPType);
		// �ۼӵ�ǰ�������к�...
		++sup_id;
	}
}
//
// ����ȡ������Ƶ���ݰ����뻷�ζ��е���...
void CSmartRecvThread::doTagAVPackProcess(char * lpBuffer, int inRecvLen)
{
	// �ж��������ݰ�����Ч�� => ����С�����ݰ���ͷ�ṹ����...
	const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
	if( lpBuffer == NULL || inRecvLen < sizeof(rtp_hdr_t) || inRecvLen > nPerPackSize ) {
		blog(LOG_INFO, "%s Error => RecvLen: %d, Max: %d", m_strInnerName.c_str(), inRecvLen, nPerPackSize);
		return;
	}
	/////////////////////////////////////////////////////////////////////////////////
	// ע�⣺����ֻ��Ҫ�ж�����ͷ�Ƿ񵽴�����ж������˵�׼���������Ƿ񵽴�...
	// ע�⣺��Ϊ��������Ƶ���ݰ�������׼����������������Ļ��ͻ���ɶ���...
	/////////////////////////////////////////////////////////////////////////////////
	// ���û���յ�����ͷ��˵�����뻹û����ɣ�ֱ�ӷ���...
	/*if( m_rtp_header.pt != PT_TAG_HEADER ) {
		blog(LOG_INFO, "%s Discard => No Header, Connect not complete", m_strInnerName.c_str());
		return;
	}
	// ��û����Ƶ��Ҳû����Ƶ��ֱ�ӷ���...
	if( !m_rtp_header.hasAudio && !m_rtp_header.hasVideo ) {
		blog(LOG_INFO, "%s Discard => No Audio and No Video", m_strInnerName.c_str());
		return;
	}*/

	///////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺��Ҫ��ֹ����Ƶ���ݰ��Ľ��գ�ֻҪ�����ݵ���ͷ��뻷�ζ��У�������0��ʱ��...
	// ע�⣺�п��ܹۿ����Ѿ��ڷ������ϱ����������ǹۿ��˻�û���յ����������ݾͻ��ȵ���...
	///////////////////////////////////////////////////////////////////////////////////////////

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺���ղ��õķ��� => ����Create��������ϵͳ0��ʱ��...
	// �趨��������0��ʱ�� => ϵͳ��Ϊ�ĵ�һ֡Ӧ���Ѿ�׼���õ�ϵͳʱ�̵�...
	// ������������ʱ�����������������ʱ�̵㱻׼���ã�һ���������ʱ...
	// ��ˣ���������յ���һ�����ݰ����趨Ϊϵͳ0��ʱ�̣������������紫����ʱ...
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/*if( m_sys_zero_ns < 0 ) {
		m_sys_zero_ns = CUtilTool::os_gettime_ns() - 100 * 1000000;
		blog(LOG_INFO, "%s Set System Zero Time By First Data => %I64d ms", TM_RECV_NAME, m_sys_zero_ns/1000000);
	}*/

	// ����յ��Ļ��������Ȳ��� �� �����Ϊ������ֱ�Ӷ���...
	rtp_hdr_t * lpNewHeader = (rtp_hdr_t*)lpBuffer;
	int nDataSize = lpNewHeader->psize + sizeof(rtp_hdr_t);
	int nZeroSize = DEF_MTU_SIZE - lpNewHeader->psize;
	uint8_t  pt_tag = lpNewHeader->pt;
	uint32_t new_id = lpNewHeader->seq;
	uint32_t max_id = new_id;
	uint32_t min_id = new_id;
	// ���ִ�����󣬶������������ӡ������Ϣ...
	if( inRecvLen != nDataSize || nZeroSize < 0 ) {
		blog(LOG_INFO, "%s Error => RecvLen: %d, DataSize: %d, ZeroSize: %d", m_strInnerName.c_str(), inRecvLen, nDataSize, nZeroSize);
		return;
	}
	// ����Ƶʹ�ò�ͬ�Ĵ������ͱ���...
	uint32_t & nMaxPlaySeq = (pt_tag == PT_TAG_AUDIO) ? m_nAudioMaxPlaySeq : m_nVideoMaxPlaySeq;
	bool   &  bFirstSeqSet = (pt_tag == PT_TAG_AUDIO) ? m_bFirstAudioSeq : m_bFirstVideoSeq;
	circlebuf & cur_circle = (pt_tag == PT_TAG_AUDIO) ? m_audio_circle : m_video_circle;
	// ע�⣺�ۿ��˺����ʱ����󲥷Ű���Ų��Ǵ�0��ʼ��...
	// �����󲥷����а���0��˵���ǵ�һ��������Ҫ����Ϊ��󲥷Ű� => ��ǰ���� - 1 => ��󲥷Ű�����ɾ��������ǰ����Ŵ�1��ʼ...
	if( nMaxPlaySeq <= 0 && !bFirstSeqSet ) {
		bFirstSeqSet = true;
		nMaxPlaySeq = new_id - 1;
		blog(LOG_INFO, "%s First Packet => Seq: %lu, Key: %d, PTS: %lu, PStart: %d, Type: %d",
			 m_strInnerName.c_str(), new_id, lpNewHeader->pk, lpNewHeader->ts, lpNewHeader->pst, pt_tag);
	}
	// ����յ��Ĳ�����ȵ�ǰ��󲥷Ű���ҪС => ˵���Ƕ�β������������ֱ���ӵ�...
	// ע�⣺��ʹ���ҲҪ�ӵ�����Ϊ��󲥷���Ű������Ѿ�Ͷ�ݵ��˲��Ų㣬�Ѿ���ɾ����...
	if( new_id <= nMaxPlaySeq ) {
		//blog(LOG_INFO, "%s Supply Discard => Seq: %lu, MaxPlaySeq: %lu, Type: %d", m_strInnerName.c_str(), new_id, nMaxPlaySeq, pt_tag);
		return;
	}
	// ��ӡ�յ�����Ƶ���ݰ���Ϣ => ��������������� => ÿ�����ݰ�����ͳһ��С => rtp_hdr_t + slice + Zero
	//log_trace("%s Seq: %lu, TS: %lu, Type: %d, pst: %d, ped: %d, Slice: %d, ZeroSize: %d",
	//          m_strInnerName.c_str(), lpNewHeader->seq, lpNewHeader->ts, lpNewHeader->pt,
	//          lpNewHeader->pst, lpNewHeader->ped, lpNewHeader->psize, nZeroSize);
	// ���ȣ�����ǰ�����кŴӶ������е���ɾ��...
	this->doEraseLoseSeq(pt_tag, new_id);
	//////////////////////////////////////////////////////////////////////////////////////////////////
	// ע�⣺ÿ�����ζ����е����ݰ���С��һ���� => rtp_hdr_t + slice + Zero
	//////////////////////////////////////////////////////////////////////////////////////////////////
	char szPacketBuffer[nPerPackSize] = {0};
	// ������ζ���Ϊ�� => ��Ҫ�Զ�������ǰԤ�в����д���...
	if( cur_circle.size < nPerPackSize ) {
		// �µ���Ű�����󲥷Ű�֮���п�϶��˵���ж���...
		// ���������� => [nMaxPlaySeq + 1, new_id - 1]
		if( new_id > (nMaxPlaySeq + 1) ) {
			this->doFillLosePack(pt_tag, nMaxPlaySeq + 1, new_id - 1);
		}
		// ��������Ű�ֱ��׷�ӵ����ζ��е�����棬�������󲥷Ű�֮���п�϶���Ѿ���ǰ��Ĳ����в������...
		// �ȼ����ͷ����������...
		circlebuf_push_back(&cur_circle, lpBuffer, inRecvLen);
		// �ټ�������������ݣ���֤�������Ǳ���һ��MTU��Ԫ��С...
		if( nZeroSize > 0 ) {
			circlebuf_push_back_zero(&cur_circle, nZeroSize);
		}
		// ��ӡ��׷�ӵ���Ű� => ������û�ж�������Ҫ׷���������Ű�...
		//blog(LOG_INFO, "%s Max Seq: %lu, Cricle: Zero", m_strInnerName.c_str(), new_id);
		return;
	}
	// ���ζ���������Ҫ��һ�����ݰ�...
	ASSERT( cur_circle.size >= nPerPackSize );
	// ��ȡ���ζ�������С���к�...
	circlebuf_peek_front(&cur_circle, szPacketBuffer, nPerPackSize);
	rtp_hdr_t * lpMinHeader = (rtp_hdr_t*)szPacketBuffer;
	min_id = lpMinHeader->seq;
	// ��ȡ���ζ�����������к�...
	circlebuf_peek_back(&cur_circle, szPacketBuffer, nPerPackSize);
	rtp_hdr_t * lpMaxHeader = (rtp_hdr_t*)szPacketBuffer;
	max_id = lpMaxHeader->seq;
	// ������������ => max_id + 1 < new_id
	// ���������� => [max_id + 1, new_id - 1];
	if( max_id + 1 < new_id ) {
		this->doFillLosePack(pt_tag, max_id + 1, new_id - 1);
	}
	// ����Ƕ�����������Ű������뻷�ζ��У�����...
	if( max_id + 1 <= new_id ) {
		// �ȼ����ͷ����������...
		circlebuf_push_back(&cur_circle, lpBuffer, inRecvLen);
		// �ټ�������������ݣ���֤�������Ǳ���һ��MTU��Ԫ��С...
		if( nZeroSize > 0 ) {
			circlebuf_push_back_zero(&cur_circle, nZeroSize);
		}
		// ��ӡ�¼���������Ű�...
		//blog(LOG_INFO, "%s Max Seq: %lu, Circle: %d", m_strInnerName.c_str(), new_id, m_circle.size/nPerPackSize-1);
		return;
	}
	// ����Ƕ�����Ĳ���� => max_id > new_id
	if( max_id > new_id ) {
		// �����С��Ŵ��ڶ������ => ��ӡ����ֱ�Ӷ�����������...
		if( min_id > new_id ) {
			//blog(LOG_INFO, "%s Supply Discard => Seq: %lu, Min-Max: [%lu, %lu], Type: %d", m_strInnerName.c_str(), new_id, min_id, max_id, pt_tag);
			return;
		}
		// ��С��Ų��ܱȶ������С...
		ASSERT( min_id <= new_id );
		// ���㻺��������λ��...
		uint32_t nPosition = (new_id - min_id) * nPerPackSize;
		// ����ȡ���������ݸ��µ�ָ��λ��...
		circlebuf_place(&cur_circle, nPosition, lpBuffer, inRecvLen);
		// ��ӡ�������Ϣ...
		//blog(LOG_INFO, "%s Supply Success => Seq: %lu, Min-Max: [%lu, %lu], Type: %d", m_strInnerName.c_str(), new_id, min_id, max_id, pt_tag);
		return;
	}
	// ���������δ֪������ӡ��Ϣ...
	blog(LOG_INFO, "%s Supply Unknown => Seq: %lu, Slice: %d, Min-Max: [%lu, %lu], Type: %d", m_strInnerName.c_str(), new_id, lpNewHeader->psize, min_id, max_id, pt_tag);
}
///////////////////////////////////////////////////////
// ע�⣺û�з�����Ҳû���հ�����Ҫ������Ϣ...
///////////////////////////////////////////////////////
void CSmartRecvThread::doSleepTo()
{
	// ���������Ϣ��ֱ�ӷ���...
	if( !m_bNeedSleep )
		return;
	// ����Ҫ��Ϣ��ʱ�� => �����Ϣ������...
	uint64_t delta_ns = MAX_SLEEP_MS * 1000000;
	uint64_t cur_time_ns = os_gettime_ns();
	// ����ϵͳ���ߺ���������sleep��Ϣ...
	os_sleepto_ns(cur_time_ns + delta_ns);
}
