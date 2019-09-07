
#include "app.h"
#include "udpclient.h"

CUDPClient::CUDPClient(int inUdpListenFD, uint8_t tmTag, uint8_t idTag, uint32_t inHostAddr, uint16_t inHostPort)
  : m_nUdpListenFD(inUdpListenFD)
  , m_nHostAddr(inHostAddr)
  , m_nHostPort(inHostPort)
  , m_server_rtt_var_ms(-1)
  , m_server_rtt_ms(-1)
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
  // 释放推流端音视频环形队列空间...
  circlebuf_free(&m_audio_circle);
  circlebuf_free(&m_video_circle);
  // 调用接口删除房间里对应的对象...
  GetApp()->doUdpClientDelete(m_nRoomID, this);
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
  return true;
}
  
bool CUDPClient::doTagCreate(char * lpBuffer, int inBufSize)
{
  // 更新创建命令包内容，创建或更新房间...
  memcpy(&m_rtp_create, lpBuffer, sizeof(m_rtp_create));
  if (m_rtp_create.roomID <= 0)
    return false;
  // 保存房间编号到终端对象...
  m_nRoomID = m_rtp_create.roomID;
  // 将当前UDP终端更新到指定的房间对象当中...
  return GetApp()->doUdpClientCreate(m_nRoomID, this, lpBuffer, inBufSize);
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
