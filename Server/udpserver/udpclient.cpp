
#include "app.h"
#include "room.h"
#include "udpclient.h"

CUDPClient::CUDPClient(int inUdpListenFD, uint8_t tmTag, uint8_t idTag, uint32_t inHostAddr, uint16_t inHostPort)
  : m_nUdpListenFD(inUdpListenFD)
  , m_nHostAddr(inHostAddr)
  , m_nHostPort(inHostPort)
  , m_server_rtt_var_ms(-1)
  , m_server_rtt_ms(-1)
  , m_lpRoom(NULL)
  , m_tmTag(tmTag)
  , m_idTag(idTag)
  , m_nRoomID(0)
{
  assert(m_nUdpListenFD > 0);
  m_nStartTime = time(NULL);
  memset(&m_rtp_create, 0, sizeof(rtp_create_t));
  // 初始化序列头和探测命令包...
  m_strSeqHeader.clear();
  memset(&m_server_rtp_detect, 0, sizeof(rtp_detect_t));
  // 初始化推流端音视频环形队列...
  circlebuf_init(&m_audio_circle);
  circlebuf_init(&m_video_circle);
  // 如果是推流端，预分配环形队列空间...
  if( this->GetIdTag() == ID_TAG_PUSHER ) {
    circlebuf_reserve(&m_audio_circle, 64 * 1024);
    circlebuf_reserve(&m_video_circle, 64 * 1024);
  }
  // 打印UDP终端被创建信息 => 终端类型与端口信息...
  log_trace("[UDP-%s-%s-Create] HostPort: %d", get_tm_tag(m_tmTag), get_id_tag(m_idTag), m_nHostPort);
}

CUDPClient::~CUDPClient()
{
  // 打印UDP终端端被删除信息...
  log_trace("[UDP-%s-%s-Delete] HostPort: %d, LiveID: %d", get_tm_tag(m_tmTag), get_id_tag(m_idTag), m_nHostPort, this->GetDBCameraID());
  // 房间对象和老师端对象都有效，在房间中注销老师对象...
  if( m_lpRoom != NULL && m_tmTag == TM_TAG_TEACHER ) {
    m_lpRoom->doUdpDeleteTeacher(this);
  }
  // 房间对象和学生端对象有效，在房间中注销学生对象...
  if( m_lpRoom != NULL && m_tmTag == TM_TAG_STUDENT ) {
    m_lpRoom->doUdpDeleteStudent(this);
  }
  // 释放推流端音视频环形队列空间...
  circlebuf_free(&m_audio_circle);
  circlebuf_free(&m_video_circle);
}

void CUDPClient::ResetTimeout()
{
  m_nStartTime = time(NULL);
}

bool CUDPClient::IsTimeout()
{
  time_t nDeltaTime = time(NULL) - m_nStartTime;
  return ((nDeltaTime >= PLAY_TIME_OUT) ? true : false);  
}

bool CUDPClient::doProcessUdpEvent(uint8_t ptTag, char * lpBuffer, int inBufSize)
{
  // 收到网络数据包，重置超时计数器...
  this->ResetTimeout();

  // 对网络数据包，进行分发操作...  
  bool bResult = false;
  switch( ptTag )
  {
    case PT_TAG_DETECT:  bResult = this->doTagDetect(lpBuffer, inBufSize); break;
    case PT_TAG_CREATE:  bResult = this->doTagCreate(lpBuffer, inBufSize); break;
    case PT_TAG_DELETE:  bResult = this->doTagDelete(lpBuffer, inBufSize); break;
    case PT_TAG_SUPPLY:  bResult = this->doTagSupply(lpBuffer, inBufSize); break;
    case PT_TAG_HEADER:  bResult = this->doTagHeader(lpBuffer, inBufSize); break;
    case PT_TAG_READY:   bResult = this->doTagReady(lpBuffer, inBufSize);  break;
    case PT_TAG_AUDIO:   bResult = this->doTagAudio(lpBuffer, inBufSize);  break;
    case PT_TAG_VIDEO:   bResult = this->doTagVideo(lpBuffer, inBufSize);  break;
  }
  return bResult;
}

bool CUDPClient::doTagDetect(char * lpBuffer, int inBufSize)
{
  // 观看者 => 只有一种探测包 => 观看者自己的探测包...
  // 观看者 => 将探测包原样返回给自己，计算网络往返延时...
  //if( this->GetIdTag() == ID_TAG_LOOKER ) {
  //  return GetApp()->doUdpDetectForLooker(m_nRoomID, this, lpBuffer, inBufSize);
  //}
  // 推流者 => 会收到两种探测反馈包 => 推流者自己 和 服务器...
  // 注意：需要通过分析探测包来判断发送者，做出不同的操作...
  assert( this->GetIdTag() == ID_TAG_PUSHER );
  // 通过第一个字节的低2位，判断终端类型...
  uint8_t tmTag = lpBuffer[0] & 0x03;
  // 获取第一个字节的中2位，得到终端身份...
  uint8_t idTag = (lpBuffer[0] >> 2) & 0x03;
  // 获取第一个字节的高4位，得到数据包类型...
  uint8_t ptTag = (lpBuffer[0] >> 4) & 0x0F;
  // 如果是 推流者 自己发出的探测包，原样反馈给推流者...
  if( idTag == ID_TAG_PUSHER ) {
    return this->doTransferToFrom(lpBuffer, inBufSize);
  }
  // 只有推流者，服务器才会发送主动探测包...
  // 如果是 服务器 发出的探测包，计算网络延时...
  if( tmTag == TM_TAG_SERVER && idTag == ID_TAG_SERVER ) {
    // 获取收到的探测数据包...
    rtp_detect_t rtpDetect = { 0 };
    memcpy(&rtpDetect, lpBuffer, sizeof(rtpDetect));
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
    //log_debug("[%s-%s] Recv Detect => dtNum: %d, rtt: %d ms, rtt_var: %d ms",
    //          get_tm_tag(this->GetTmTag()), get_id_tag(this->GetIdTag()),
    //          rtpDetect.dtNum, m_server_rtt_ms, m_server_rtt_var_ms);    
  }
  return true;
}
  
bool CUDPClient::doTagCreate(char * lpBuffer, int inBufSize)
{
  bool bResult = false;
  // 更新创建命令包内容，创建或更新房间...
  memcpy(&m_rtp_create, lpBuffer, sizeof(m_rtp_create));
  if (m_rtp_create.roomID <= 0)
    return bResult;
  // 保存房间编号到终端对象...
  m_nRoomID = m_rtp_create.roomID;
  // 更新创建命令包内容，创建或更新房间，更新房间里的老师端...
  m_lpRoom = GetApp()->doCreateRoom(m_nRoomID);
  // 如果是讲师终端类型，加入到房间里面对应的讲师端...
  if (this->GetTmTag() == TM_TAG_TEACHER ) {
    m_lpRoom->doUdpCreateTeacher(this);
  }
  // 如果是讲师终端类型，加入到房间里面对应的学生端...
  if (this->GetTmTag() == TM_TAG_STUDENT ) {
    m_lpRoom->doUdpCreateStudent(this);
  }
  // 回复推流端 => 房间已经创建成功，不要再发创建命令了...
  if( this->GetIdTag() == ID_TAG_PUSHER ) {
    bResult = this->doCreateForPusher(lpBuffer, inBufSize);
  }
  // 回复观看端 => 将推流端的序列头转发给观看端 => 由于没有P2P模式，观看端不用发送准备就绪命令...
  if( this->GetIdTag() == ID_TAG_LOOKER ) {
    bResult = this->doCreateForLooker(lpBuffer, inBufSize);
  }
  // 返回执行结果...
  return bResult;
}

// 回复推流端 => 房间已经创建成功，不要再发创建命令了...
bool CUDPClient::doCreateForPusher(char * lpBuffer, int inBufSize)
{
  // 构造反馈的数据包...
  rtp_hdr_t rtpHdr = {0};
  rtpHdr.tm = TM_TAG_SERVER;
  rtpHdr.id = ID_TAG_SERVER;
  rtpHdr.pt = PT_TAG_CREATE;
  // 回复推流端 => 房间已经创建成功，不要再发创建命令了...
  return this->doTransferToFrom((char*)&rtpHdr, sizeof(rtpHdr));
}

// 回复观看端 => 将推流端的序列头转发给观看端 => 由于没有P2P模式，观看端不用发送准备就绪命令...
bool CUDPClient::doCreateForLooker(char * lpBuffer, int inBufSize)
{
  // 如果没有房间，直接返回...
  if( m_lpRoom == NULL )
    return false;
  // 获取房间里指定编号的推流者对象 => 无推流者，直接返回...
  int nDBCameraID = this->GetDBCameraID();
  CUDPClient * lpUdpPusher = m_lpRoom->doFindUdpPusher(nDBCameraID);
  if( lpUdpPusher == NULL )
    return false;
  // 获取推流者的序列头信息 => 序列头为空，直接返回...
  string & strSeqHeader = lpUdpPusher->GetSeqHeader();
  if( strSeqHeader.size() <= 0 )
    return false;
  // 回复观看端 => 将推流端的序列头转发给观看端...
  return this->doTransferToFrom((char*)strSeqHeader.c_str(), strSeqHeader.size());
}

bool CUDPClient::doTagDelete(char * lpBuffer, int inBufSize)
{
  // 注意：删除命令已经在CApp::doTagDelete()中拦截处理了...
  return true;
}

bool CUDPClient::doTagSupply(char * lpBuffer, int inBufSize)
{
  return true;
}

bool CUDPClient::doTagHeader(char * lpBuffer, int inBufSize)
{
  return true;
}

bool CUDPClient::doTagReady(char * lpBuffer, int inBufSize)
{
  return true;
}

bool CUDPClient::doTagAudio(char * lpBuffer, int inBufSize)
{
  return true;
}

bool CUDPClient::doTagVideo(char * lpBuffer, int inBufSize)
{
  return true;
}

// 原路返回的转发接口 => 观看者|推流者都可以原路返回...
bool CUDPClient::doTransferToFrom(char * lpBuffer, int inBufSize)
{
  // 判断输入的缓冲区是否有效...
  if( lpBuffer == NULL || inBufSize <= 0 )
    return false;
   // 获取需要的相关变量信息...
  uint32_t nHostAddr = this->GetHostAddr();
  uint16_t nHostPort = this->GetHostPort();
  int listen_fd = m_nUdpListenFD;
  if( listen_fd <= 0 || nHostAddr <= 0 || nHostPort <= 0 )
    return false;
  // 构造返回的接收地址...
	sockaddr_in addrFrom = {0};
	addrFrom.sin_family = AF_INET;
	addrFrom.sin_port = htons(nHostPort);
	addrFrom.sin_addr.s_addr = htonl(nHostAddr);
  // 将数据信息转发给学生观看者对象...
  if( sendto(listen_fd, lpBuffer, inBufSize, 0, (sockaddr*)&addrFrom, sizeof(addrFrom)) < 0 ) {
    log_trace("sendto error(code:%d, %s)", errno, strerror(errno));
    return false;
  }
  //log_debug("[Transfer] Size: %d", inBufSize);
  // 发送成功...
  return true; 
}
