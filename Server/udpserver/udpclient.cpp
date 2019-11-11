
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
  log_trace("[UDP-%s-%s-Delete] HostPort: %d, LiveID: %d", get_tm_tag(m_tmTag), get_id_tag(m_idTag), m_nHostPort, this->GetLiveID());
  // 房间对象有效，在房间中注销，内部根据终端类型，自行分发处理...
  if( m_lpRoom != NULL ) {
    m_lpRoom->doUdpDeleteSmart(this);
  }
  // 如果是推流端，把自己从补包队列当中删除掉...
  if( this->GetIdTag() == ID_TAG_PUSHER ) {
    GetApp()->doDelSupplyForPusher(this);
  }
  // 如果是观看端，把自己从丢包队列当中删除掉...
  if( this->GetIdTag() == ID_TAG_LOOKER ) {
    GetApp()->doDelLoseForLooker(this);
  }
  // 打印自己所在的房间信息...
  if( m_lpRoom != NULL ) {
    m_lpRoom->doDumpRoomInfo();
  }
  // 释放推流端音视频环形队列空间...
  circlebuf_free(&m_audio_circle);
  circlebuf_free(&m_video_circle);
}

bool CUDPClient::doServerSendDetect()
{
  // 只有推流端，服务器才会主动发起探测命令...
  if( this->GetIdTag() != ID_TAG_PUSHER )
    return false;
  // 采用了新的拥塞处理 => 删除指定缓存时间点之前的音视频数据包...
  this->doCalcAVJamStatus(false);
  this->doCalcAVJamStatus(true);
  // 填充探测命令包 => 服务器主动发起...
  m_server_rtp_detect.tm     = TM_TAG_SERVER;
  m_server_rtp_detect.id     = ID_TAG_SERVER;
  m_server_rtp_detect.pt     = PT_TAG_DETECT;
  m_server_rtp_detect.tsSrc  = (uint32_t)(os_gettime_ns() / 1000000);
  m_server_rtp_detect.dtDir  = DT_TO_SERVER;
  m_server_rtp_detect.dtNum += 1;
  // 计算已收到音视频最大连续包号...
  m_server_rtp_detect.maxAConSeq = this->doCalcMaxConSeq(true);
  m_server_rtp_detect.maxVConSeq = this->doCalcMaxConSeq(false);
  int nFlowSize = sizeof(m_server_rtp_detect);
  // 将新构造的探测包转发给当前对象...
  return this->doTransferToFrom((char*)&m_server_rtp_detect, nFlowSize);
}

// 推流者 => 计算已接收到的最大连续的序号包...
uint32_t CUDPClient::doCalcMaxConSeq(bool bIsAudio)
{
  // 根据数据包类型，找到丢包集合、环形队列、最大播放包...
  GM_MapLose & theMapLose = bIsAudio ? m_AudioMapLose : m_VideoMapLose;
  circlebuf  & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;
  // 发生丢包的计算 => 等于最小丢包号 - 1
  if( theMapLose.size() > 0 ) {
    return (theMapLose.begin()->first - 1);
  }
  // 没有丢包 => 环形队列为空 => 返回0...
  if( cur_circle.size <= 0 )
    return 0;
  // 没有丢包 => 已收到的最大包号 => 环形队列中最大序列号 - 1...
  const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
  char szPacketBuffer[nPerPackSize] = {0};
  circlebuf_peek_back(&cur_circle, szPacketBuffer, nPerPackSize);
  rtp_hdr_t * lpMaxHeader = (rtp_hdr_t*)szPacketBuffer;
  return (lpMaxHeader->seq - 1);
}

void CUDPClient::doCalcAVJamStatus(bool bIsAudio)
{
  // 根据数据包类型，找到环形队列...
  circlebuf  & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;
  // 环形队列为空，没有拥塞，直接返回...
  if( cur_circle.size <= 0 )
    return;
  // 遍历环形队列，删除所有超过n秒的缓存数据包 => 不管是否是关键帧或完整包，只是为补包而存在...
  const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
  char szPacketBuffer[nPerPackSize] = {0};
  rtp_hdr_t * lpCurHeader = NULL;
  uint32_t    min_ts = 0, min_seq = 0;
  uint32_t    max_ts = 0, max_seq = 0;
  // 读取最大的数据包的内容，获取最大时间戳...
  circlebuf_peek_back(&cur_circle, szPacketBuffer, nPerPackSize);
  lpCurHeader = (rtp_hdr_t*)szPacketBuffer;
  max_seq = lpCurHeader->seq;
  max_ts = lpCurHeader->ts;
  // 遍历环形队列，查看是否有需要删除的数据包...
  while ( cur_circle.size > 0 ) {
    // 读取第一个数据包的内容，获取最小时间戳...
    circlebuf_peek_front(&cur_circle, szPacketBuffer, nPerPackSize);
    lpCurHeader = (rtp_hdr_t*)szPacketBuffer;
    // 计算环形队列当中总的缓存时间...
    uint32_t cur_buf_ms = max_ts - lpCurHeader->ts;
    // 如果总缓存时间不超过n秒，中断操作...
    if (cur_buf_ms < 6000)
      break;
    assert(cur_buf_ms >= 6000);
    // 保存删除的时间点，供音频参考...
    min_ts = lpCurHeader->ts;
    min_seq = lpCurHeader->seq;
    // 如果总缓存时间超过n秒，删除最小数据包，继续寻找...
    circlebuf_pop_front(&cur_circle, NULL, nPerPackSize);
  }
  // 如果没有发生删除，直接返回...
  if (min_ts <= 0 || min_seq <= 0 )
    return;
  // 打印网络拥塞情况 => 就是视频缓存的拥塞情况...
  //log_trace("[%s-%s] %s Jam => MinSeq: %u, MaxSeq: %u, Circle: %d",
  //          get_tm_tag(this->GetTmTag()), get_id_tag(this->GetIdTag()),
  //          bIsAudio ? "Audio" : "Video", min_seq, max_seq,
  //          cur_circle.size/nPerPackSize);
  // 2019.08.09 - 调整策略，音频也是由自己单独控制缓存清理，始终保留5秒数据...
  // 删除音频相关时间的数据包 => 包括这个时间戳之前的所有数据包都被删除...
  //this->doEarseAudioByPTS(min_ts);
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
  if( this->GetIdTag() == ID_TAG_LOOKER ) {
    return this->doDetectForLooker(lpBuffer, inBufSize);
  }
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
    //log_trace("[%s-%s] Recv Detect => dtNum: %d, rtt: %d ms, rtt_var: %d ms",
    //          get_tm_tag(this->GetTmTag()), get_id_tag(this->GetIdTag()),
    //          rtpDetect.dtNum, m_server_rtt_ms, m_server_rtt_var_ms);    
  }
  return true;
}

// 调用推流者的最小序号包接口...
bool CUDPClient::doDetectForLooker(char * lpBuffer, int inBufSize)
{
  // 如果没有房间，直接返回...
  if( m_lpRoom == NULL )
    return false;
  // 获取房间里指定编号的推流者对象 => 无推流者，直接返回...
  int nLiveID = this->GetLiveID();
  CUDPClient * lpUdpPusher = m_lpRoom->doFindUdpPusher(nLiveID);
  if( lpUdpPusher == NULL )
    return false;
  // 注意：音视频最小序号包不用管是否是有效包，只简单获取最小包号...
  // 将推流者当前最小的音视频数据包号更新到探测包当中...
  rtp_detect_t * lpDetect = (rtp_detect_t*)lpBuffer;
  lpDetect->maxAConSeq = lpUdpPusher->doCalcMinSeq(true);
  lpDetect->maxVConSeq = lpUdpPusher->doCalcMinSeq(false);
  return this->doTransferToFrom(lpBuffer, inBufSize);
}

// 方便观看者在极端网络下的缓存清理...
// 返回最小序号包，不用管是否是有效包号...
uint32_t CUDPClient::doCalcMinSeq(bool bIsAudio)
{
  // 音视频使用不同队列和变量...
  circlebuf & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;
  // 如果环形队列为空，直接返回0...
  if( cur_circle.size <= 0 )
    return 0;
  // 读取第一个数据包的内容，获取最小包序号...
  const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
  char szPacketBuffer[nPerPackSize] = { 0 };
  circlebuf_peek_front(&cur_circle, szPacketBuffer, nPerPackSize);
  rtp_hdr_t * lpCurHeader = (rtp_hdr_t*)szPacketBuffer;
  return lpCurHeader->seq;
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
  // 更新创建命令包内容，创建或更新房间，更新房间里的终端...
  m_lpRoom = GetApp()->doCreateRoom(m_nRoomID);
  // 加入到房间里面对应的终端当中...
  m_lpRoom->doUdpCreateSmart(this);
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
  // 打印推流对象创建信息 => 可能会打印多次信息 => 专门打印推流直播编号...
  log_trace("[UDP-%s-%s-Create] LiveID: %d", get_tm_tag(m_tmTag), get_id_tag(m_idTag), this->GetLiveID());
  // 构造反馈的数据包...
  rtp_hdr_t rtpHdr = {0};
  rtpHdr.tm = TM_TAG_SERVER;
  rtpHdr.id = ID_TAG_SERVER;
  rtpHdr.pt = PT_TAG_CREATE;
  // 新增反馈直播编号 => 服务器生成...
  rtpHdr.noset = this->GetLiveID();
  // 回复推流端 => 房间已经创建成功，不要再发创建命令了...
  return this->doTransferToFrom((char*)&rtpHdr, sizeof(rtpHdr));
}

// 回复观看端 => 将推流端的序列头转发给观看端 => 由于没有P2P模式，观看端不用发送准备就绪命令...
bool CUDPClient::doCreateForLooker(char * lpBuffer, int inBufSize)
{
  // 如果没有房间，直接返回...
  if( m_lpRoom == NULL )
    return false;
  // 打印观看对象创建信息 => 可能会打印多次信息 => 专门打印观看直播编号...
  log_trace("[UDP-%s-%s-Create] LiveID: %d", get_tm_tag(m_tmTag), get_id_tag(m_idTag), this->GetLiveID());
  // 获取房间里指定编号的推流者对象 => 无推流者，直接返回...
  int nLiveID = this->GetLiveID();
  CUDPClient * lpUdpPusher = m_lpRoom->doFindUdpPusher(nLiveID);
  if( lpUdpPusher == NULL )
    return false;
  // 获取推流者的序列头信息 => 序列头为空，直接返回...
  string & strSeqHeader = lpUdpPusher->GetSeqHeader();
  if( strSeqHeader.size() <= 0 )
    return false;
  // 回复观看端 => 将推流端的序列头转发给观看端...
  return this->doTransferToFrom((char*)strSeqHeader.c_str(), strSeqHeader.size());
}

bool CUDPClient::doTagHeader(char * lpBuffer, int inBufSize)
{
  // 只有推流者才会处理序列头命令...
  if( this->GetIdTag() != ID_TAG_PUSHER )
    return false;
  // 将序列头保存起来，等待观看端接入时，转发给观看端...
  m_strSeqHeader.assign(lpBuffer, inBufSize);
  // 注意：之前放在tagCreate当中，当丢包时会造成问题...
  // 通知房间里关联的终端可以进行拉流操作了...
  m_lpRoom->doUdpHeaderSmart(this);
  // 构造反馈的数据包...
  rtp_hdr_t rtpHdr = {0};
  rtpHdr.tm = TM_TAG_SERVER;
  rtpHdr.id = ID_TAG_SERVER;
  rtpHdr.pt = PT_TAG_HEADER;
  // 回复推流端 => 序列头已经收到，不要再发序列头命令了...
  return this->doTransferToFrom((char*)&rtpHdr, sizeof(rtpHdr));
}

bool CUDPClient::doTagDelete(char * lpBuffer, int inBufSize)
{
  // 注意：删除命令已经在CApp::doTagDelete()中拦截处理了...
  return true;
}

bool CUDPClient::doTagSupply(char * lpBuffer, int inBufSize)
{
  // 判断输入数据的内容是否有效，如果无效，直接返回...
  if( lpBuffer == NULL || inBufSize <= 0 || inBufSize < sizeof(rtp_supply_t) )
    return false;
  // 只有观看者才会发起补包命令...
  if( this->GetIdTag() != ID_TAG_LOOKER )
    return false;
  // 通过第一个字节的低2位，判断终端类型...
  uint8_t tmTag = lpBuffer[0] & 0x03;
  // 获取第一个字节的中2位，得到终端身份...
  uint8_t idTag = (lpBuffer[0] >> 2) & 0x03;
  // 获取第一个字节的高4位，得到数据包类型...
  uint8_t ptTag = (lpBuffer[0] >> 4) & 0x0F;
  // 注意：只处理 观看端 发出的补包命令 => 可能是老师|学生...
  if( idTag != ID_TAG_LOOKER )
    return false;
  // 注意：这里只是将需要补包的序号加入到丢包队列当中，让线程去补包...
  // 注意：观看端的补包从服务器上的推流者的缓存获取...
  rtp_supply_t rtpSupply = {0};
  int nHeadSize = sizeof(rtp_supply_t);
  memcpy(&rtpSupply, lpBuffer, nHeadSize);
  if( (rtpSupply.suSize <= 0) || ((nHeadSize + rtpSupply.suSize) != inBufSize) )
    return false;
  // 根据数据包类型，找到丢包集合...
  bool bIsAudio = (rtpSupply.suType == PT_TAG_AUDIO) ? true : false;
  GM_MapLose & theMapLose = bIsAudio ? m_AudioMapLose : m_VideoMapLose;
  // 获取需要补包的序列号，加入到丢包队列当中...
  char * lpDataPtr = lpBuffer + nHeadSize;
  int    nDataSize = rtpSupply.suSize;
  while( nDataSize > 0 ) {
    uint32_t   nLoseSeq = 0;
    rtp_lose_t rtpLose = {0};
    // 获取补包序列号...
    memcpy(&nLoseSeq, lpDataPtr, sizeof(uint32_t));
    // 移动数据区指针位置...
    lpDataPtr += sizeof(uint32_t);
    nDataSize -= sizeof(uint32_t);
    // 查看这个丢包号是否是服务器端也要补的包...
    // 服务器收到补包后会自动转发，这里就不用补了...
    if( this->doIsPusherLose(rtpSupply.suType, nLoseSeq) )
      continue;
    // 是观看端丢失的新包，需要进行补包队列处理...
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
  }
  // 如果补包队列为空 => 都是服务器端本身就需要补的包...
  if( theMapLose.size() <= 0 )
    return true;
  // 把自己加入到丢包对象列表当中...
  GetApp()->doAddLoseForLooker(this);
  // 打印已收到补包命令...
  log_trace("[%s-%s] Supply Recv => Count: %d, Type: %d, Lose: %d",
            get_tm_tag(this->GetTmTag()), get_id_tag(this->GetIdTag()),
            rtpSupply.suSize / sizeof(int), rtpSupply.suType, theMapLose.size());
  return true;
}

bool CUDPClient::doIsPusherLose(uint8_t inPType, uint32_t inLoseSeq)
{
  // 如果没有房间，直接返回...
  if( m_lpRoom == NULL )
    return false;
  // 获取房间里指定编号的推流者对象 => 无推流者，直接返回...
  int nLiveID = this->GetLiveID();
  CUDPClient * lpUdpPusher = m_lpRoom->doFindUdpPusher(nLiveID);
  if( lpUdpPusher == NULL )
    return false;
  // 在找到的推流者当中查找是否有包丢失...
  bool bIsAudio = ((inPType == PT_TAG_AUDIO) ? true : false);
  return lpUdpPusher->doIsLosePacket(bIsAudio, inLoseSeq);
}

bool CUDPClient::doIsLosePacket(bool bIsAudio, uint32_t inLoseSeq)
{
  GM_MapLose & theMapLose = bIsAudio ? m_AudioMapLose : m_VideoMapLose;
  return ((theMapLose.find(inLoseSeq) != theMapLose.end()) ? true : false);
}

bool CUDPClient::doTagReady(char * lpBuffer, int inBufSize)
{
  // 由于去掉了P2P模式，不会再处理准备就绪命令了...
  return true;
}

bool CUDPClient::doTagAudio(char * lpBuffer, int inBufSize)
{
  // 只有推流端才会处理音频包命令...
  if( this->GetIdTag() != ID_TAG_PUSHER )
    return false;
  // 将音频数据包缓存起来...
  this->doTagAVPackProcess(lpBuffer, inBufSize);
  // 转发音频数据包到房间里的所有的观看者...
  return this->doTransferToLooker(lpBuffer, inBufSize);
}

bool CUDPClient::doTagVideo(char * lpBuffer, int inBufSize)
{
  // 只有推流端才会处理视频包命令...
  if( this->GetIdTag() != ID_TAG_PUSHER )
    return false;
  // 将视频数据包缓存起来...
  this->doTagAVPackProcess(lpBuffer, inBufSize);
  // 转发视频数据包到房间里的所有的观看者...
  return this->doTransferToLooker(lpBuffer, inBufSize);
}

void CUDPClient::doTagAVPackProcess(char * lpBuffer, int inBufSize)
{
  const char * lpTMTag = get_tm_tag(this->GetTmTag());
  const char * lpIDTag = get_id_tag(this->GetIdTag());
  // 判断输入数据包的有效性 => 不能小于数据包的头结构长度...
  const int nPerPackSize = DEF_MTU_SIZE + sizeof(rtp_hdr_t);
  if( lpBuffer == NULL || inBufSize < sizeof(rtp_hdr_t) || inBufSize > nPerPackSize ) {
  	log_trace("[%s-%s] Error => RecvLen: %d, Max: %d", lpTMTag, lpIDTag, inBufSize, nPerPackSize);
  	return;
  }
  // 如果收到的缓冲区长度不够 或 填充量为负数，直接丢弃...
  rtp_hdr_t * lpNewHeader = (rtp_hdr_t*)lpBuffer;
  int nDataSize = lpNewHeader->psize + sizeof(rtp_hdr_t);
  int nZeroSize = DEF_MTU_SIZE - lpNewHeader->psize;
  uint8_t  pt_tag = lpNewHeader->pt;
  uint32_t new_id = lpNewHeader->seq;
  uint32_t max_id = new_id;
  uint32_t min_id = new_id;
  // 更新序列头里面的最新视频关键帧序号 => 视频帧标志+关键帧+帧开始...
  /*if((lpNewHeader->pt == PT_TAG_VIDEO) && (lpNewHeader->pk > 0) && (lpNewHeader->pst > 0)) {
    rtp_header_t * lpSeqHeader = (rtp_header_t*)m_strSeqHeader.c_str();
    lpSeqHeader->vk_seq = lpNewHeader->seq;
  }*/
  // 打印推流端发送数据的调试信息...
  //log_debug("[%s-%s] Size: %d, Type: %d, Seq: %u, TS: %u, pst: %d, ped: %d, Slice: %d, Zero: %d", lpTMTag, lpIDTag, inBufSize,
  //          lpNewHeader->pt, lpNewHeader->seq, lpNewHeader->ts, lpNewHeader->pst,
  //          lpNewHeader->ped, lpNewHeader->psize, nZeroSize);
  // 出现打包错误，丢掉错误包，打印错误信息...
  if( inBufSize != nDataSize || nZeroSize < 0 ) {
  	log_trace("[%s-%s] Error => RecvLen: %d, DataSize: %d, ZeroSize: %d", lpTMTag, lpIDTag, inBufSize, nDataSize, nZeroSize);
  	return;
  }
  // 音视频使用不同的打包对象和变量...
  circlebuf & cur_circle = (pt_tag == PT_TAG_AUDIO) ? m_audio_circle : m_video_circle;
  // 首先，将当前包序列号从丢包队列当中删除...
  this->doEraseLoseSeq(pt_tag, new_id);
  //////////////////////////////////////////////////////////////////////////////////////////////////
  // 注意：每个环形队列中的数据包大小是一样的 => rtp_hdr_t + slice + Zero
  //////////////////////////////////////////////////////////////////////////////////////////////////
  char szPacketBuffer[nPerPackSize] = {0};
  // 注意：环形队列只有最开始为空，因为始终有5秒的缓存供补包使用...
  // 如果环形队列为空 => 需要对丢包做提前预判并进行处理...
  if( cur_circle.size < nPerPackSize ) {
    // 新到序号包与最大播放包之间有空隙，说明有丢包...
    // 丢包闭区间 => [0 + 1, new_id - 1]
    if( new_id > (0 + 1) ) {
    	this->doFillLosePack(pt_tag, 0 + 1, new_id - 1);
    }
    // 把最新序号包直接追加到环形队列的最后面，如果与最大播放包之间有空隙，已经在前面的步骤中补充好了...
    // 先加入包头和数据内容...
    circlebuf_push_back(&cur_circle, lpBuffer, inBufSize);
    // 再加入填充数据内容，保证数据总是保持一个MTU单元大小...
    if( nZeroSize > 0 ) {
    	circlebuf_push_back_zero(&cur_circle, nZeroSize);
    }
    // 打印新追加的序号包 => 不管有没有丢包，都要追加这个新序号包...
    //log_trace("[%s-%s] Max Seq: %u, Cricle: Zero", lpTMTag, lpIDTag, new_id);
    return;
  }
  // 环形队列中至少要有一个数据包...
  assert( cur_circle.size >= nPerPackSize );
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
		circlebuf_push_back(&cur_circle, lpBuffer, inBufSize);
		// 再加入填充数据内容，保证数据总是保持一个MTU单元大小...
		if( nZeroSize > 0 ) {
			circlebuf_push_back_zero(&cur_circle, nZeroSize);
		}
		// 打印新加入的最大序号包...
		//log_trace("[%s-%s] Max Seq: %u, Circle: %d", lpTMTag, lpIDTag, new_id, cur_circle.size/nPerPackSize-1);
		return;
	}
	// 如果是丢包后的补充包 => max_id > new_id
	if( max_id > new_id ) {
		// 如果最小序号大于丢包序号 => 打印错误，直接丢掉这个补充包...
		if( min_id > new_id ) {
			//log_trace("[%s-%s] Supply Discard => Seq: %u, Min-Max: [%u, %u], Type: %d", lpTMTag, lpIDTag, new_id, min_id, max_id, pt_tag);
			return;
		}
		// 最小序号不能比丢包序号小...
		assert( min_id <= new_id );
		// 计算缓冲区更新位置...
		uint32_t nPosition = (new_id - min_id) * nPerPackSize;
		// 将获取的数据内容更新到指定位置...
		circlebuf_place(&cur_circle, nPosition, lpBuffer, inBufSize);
		// 打印补充包信息...
		//log_trace("[%s-%s] Supply Success => Seq: %u, Min-Max: [%u, %u], Type: %d", lpTMTag, lpIDTag, new_id, min_id, max_id, pt_tag);
		return;
	}
	// 如果是其它未知包，打印信息...
	log_trace("[%s-%s] Supply Unknown => Seq: %u, Slice: %d, Min-Max: [%u, %u], Type: %d",
             lpTMTag, lpIDTag, new_id, lpNewHeader->psize, min_id, max_id, pt_tag);  
}

// 查看当前包是否需要从丢包队列中删除...
void CUDPClient::doEraseLoseSeq(uint8_t inPType, uint32_t inSeqID)
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
	//log_trace("[%s-%s] Supply Erase => LoseSeq: %u, ResendCount: %u, LoseSize: %u, Type: %d",
  //          get_tm_tag(this->GetTmTag()), get_id_tag(this->GetIdTag()),
  //          inSeqID, nResendCount, theMapLose.size(), inPType);
}

// 给丢失数据包预留环形队列缓存空间...
void CUDPClient::doFillLosePack(uint8_t inPType, uint32_t nStartLoseID, uint32_t nEndLoseID)
{
	// 根据数据包类型，找到丢包集合...
	circlebuf & cur_circle = (inPType == PT_TAG_AUDIO) ? m_audio_circle : m_video_circle;
	GM_MapLose & theMapLose = (inPType == PT_TAG_AUDIO) ? m_AudioMapLose : m_VideoMapLose;
	// 需要对网络抖动时间差进行线路选择 => 只有一条服务器线路...
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
		// 注意：这里要避免 网络抖动时间差 为负数的情况 => 还没有完成第一次探测的情况，也不能为0，会猛烈发包...
		// 重发时间点 => cur_time + rtt_var => 丢包时的当前时间 + 丢包时的网络抖动时间差 => 避免不是丢包，只是乱序的问题...
		rtpLose.resend_time = cur_time_ms + max(cur_rtt_var_ms, MAX_SLEEP_MS);
		theMapLose[sup_id] = rtpLose;
		// 打印已丢包信息，丢包队列长度...
		//log_trace("[%s-%s] Lose Seq: %u, LoseSize: %u, Type: %d", get_tm_tag(this->GetTmTag()), get_id_tag(this->GetIdTag()), sup_id, theMapLose.size(), inPType);
		// 累加当前丢包序列号...
		++sup_id;
	}
  // 把自己加入到补包对象列表当中...
  GetApp()->doAddSupplyForPusher(this);
}

// 推流者才会有补包命令...
int CUDPClient::doServerSendSupply()
{
  // -1 => 音视频都没有补包...
  //  0 => 有补包，但不到补包时间...
  //  1 => 有补包，已经发送补包命令...
  // 如果不是推流者，直接返回没有补包数据...
  if( this->GetIdTag() != ID_TAG_PUSHER )
    return -1;
  // 音视频单独发送补包命令...
  int nRetAudio = this->doSendSupplyCmd(true);
  int nRetVideo = this->doSendSupplyCmd(false);
  // 如果音视频都小于0，返回-1...
  if( nRetAudio < 0 && nRetVideo < 0 )
    return -1;
  // 只要有一个大于0，返回1...
  if( nRetAudio > 0 || nRetVideo > 0 )
    return 1;
  // 其余的情况返回0...
  return 0;
}

// 推流者才会有补包命令...
int CUDPClient::doSendSupplyCmd(bool bIsAudio)
{
  // -1 => 没有补包...
  //  0 => 有补包，但不到补包时间...
  //  1 => 有补包，已经发送补包命令...
  
  const char * lpTMTag = get_tm_tag(this->GetTmTag());
  const char * lpIDTag = get_id_tag(this->GetIdTag());
  
  // 根据数据包类型，找到丢包集合...
  circlebuf & cur_circle = bIsAudio ? m_audio_circle : m_video_circle;
  GM_MapLose & theMapLose = bIsAudio ? m_AudioMapLose : m_VideoMapLose;
  // 如果丢包集合队列为空，直接返回...
  if( theMapLose.size() <= 0 )
    return -1;
  assert( theMapLose.size() > 0 );
  // 定义最大的补包缓冲区...
  const int nHeadSize = sizeof(rtp_supply_t);
  const int nPerPackSize = DEF_MTU_SIZE + nHeadSize;
  char szPacketBuffer[nPerPackSize] = {0};
  uint32_t min_id = 0;
  // 获取环形队列中最小序列号...
  if( cur_circle.size > nPerPackSize ) {
    circlebuf_peek_front(&cur_circle, szPacketBuffer, nPerPackSize);
    rtp_hdr_t * lpMinHeader = (rtp_hdr_t*)szPacketBuffer;
    min_id = lpMinHeader->seq;
  }
  // 获取补包缓冲区存放数据的头指针...
  char * lpData = szPacketBuffer + nHeadSize;
  // 获取当前时间的毫秒值 => 小于或等于当前时间的丢包都需要通知发送端再次发送...
  uint32_t cur_time_ms = (uint32_t)(os_gettime_ns() / 1000000);
  // 需要对网络往返延迟值进行线路选择 => 只有一条服务器线路...
  int cur_rtt_ms = m_server_rtt_ms;
  // 重置补报长度为0 => 重新计算需要补包的个数...
  // 需要设置为从服务器发出的补包命令...
  rtp_supply_t rtpSupply = {0};
  rtpSupply.tm = TM_TAG_SERVER;
  rtpSupply.id = ID_TAG_SERVER;
  rtpSupply.pt = PT_TAG_SUPPLY;
  rtpSupply.suSize = 0;
  rtpSupply.suType = bIsAudio ? PT_TAG_AUDIO : PT_TAG_VIDEO;
  // 遍历丢包队列，找出需要补包的丢包序列号...
  GM_MapLose::iterator itorItem = theMapLose.begin();
  while( itorItem != theMapLose.end() ) {
    rtp_lose_t & rtpLose = itorItem->second;
    // 如果要补的包号，比最小包号还要小，直接丢弃，已经过期了...
    if( rtpLose.lose_seq < min_id ) {
      log_trace("[%s-%s] Supply Discard => LoseSeq: %u, MinSeq: %u, Audio: %d", lpTMTag, lpIDTag, rtpLose.lose_seq, min_id, bIsAudio);
      theMapLose.erase(itorItem++);
      continue;
    }
    // 补包序号在有效范围之内...
    if( rtpLose.resend_time <= cur_time_ms ) {
      // 如果补包缓冲超过设定的最大值，跳出循环 => 最多补包200个...
      if( (nHeadSize + rtpSupply.suSize) >= nPerPackSize )
        break;
      // 累加补包长度和指针，拷贝补包序列号...
      memcpy(lpData, &rtpLose.lose_seq, sizeof(uint32_t));
      rtpSupply.suSize += sizeof(uint32_t);
      lpData += sizeof(uint32_t);
      // 累加重发次数...
      ++rtpLose.resend_count;
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
  // 如果补充包缓冲为空 => 补包时间未到...
  if( rtpSupply.suSize <= 0 )
    return 0;
  // 更新补包命令头内容块...
  memcpy(szPacketBuffer, &rtpSupply, nHeadSize);
  // 如果补包缓冲不为空，才进行补包命令发送...
  int nDataSize = nHeadSize + rtpSupply.suSize;
  // 打印已发送补包命令...
  log_trace("[%s-%s] Supply Send => Dir: %d, Count: %d, Audio: %d", lpTMTag, lpIDTag, DT_TO_SERVER, rtpSupply.suSize/sizeof(uint32_t), bIsAudio);
  // 将补包命令转发给当前老师推流者对象...
  return this->doTransferToFrom(szPacketBuffer, nDataSize);
}

// 观看者才会有丢包命令...
bool CUDPClient::doServerSendLose()
{
  // 如果不是观看者，直接返回没有丢包数据...
  if( this->GetIdTag() != ID_TAG_LOOKER )
    return false;
  // 发送观看端需要的音频丢失数据包...
  this->doSendLosePacket(true);
  // 发送观看端需要的视频丢失数据包...
  this->doSendLosePacket(false);
  // 如果音频和视频都没有丢包数据，返回false...
  if( m_AudioMapLose.size() <= 0 && m_VideoMapLose.size() <= 0 ) {
    return false;
  }
  // 音视频只要有一个还有补包序号，返回true...
  return true;
}

// 观看者才会有丢包命令...
void CUDPClient::doSendLosePacket(bool bIsAudio)
{
  const char * lpTMTag = get_tm_tag(this->GetTmTag());
  const char * lpIDTag = get_id_tag(this->GetIdTag());

  // 根据数据包类型，找到丢包集合、环形队列...
  GM_MapLose & theMapLose = bIsAudio ? m_AudioMapLose : m_VideoMapLose;
  // 丢包集合队列为空，直接返回...
  if( theMapLose.size() <= 0 )
    return;
  // 拿出一个丢包记录，无论是否发送成功，都要删除这个丢包记录...
  // 如果观看端，没有收到这个数据包，会再次发起补包命令...
  GM_MapLose::iterator itorItem = theMapLose.begin();
  rtp_lose_t rtpLose = itorItem->second;
  theMapLose.erase(itorItem);
  // 如果没有房间，直接返回...
  if( m_lpRoom == NULL )
    return;
  // 获取房间里指定编号的推流者对象 => 无推流者，直接返回...
  int nLiveID = this->GetLiveID();
  CUDPClient * lpUdpPusher = m_lpRoom->doFindUdpPusher(nLiveID);
  if( lpUdpPusher == NULL )
    return;
  // 获取学生推流者在服务器缓存的音频或视频环形队列对象...
  circlebuf & cur_circle = bIsAudio ? lpUdpPusher->m_audio_circle : lpUdpPusher->m_video_circle;
  // 如果环形队列为空，直接返回...
  if( cur_circle.size <= 0 )
    return;
  // 先找到环形队列中最前面数据包的头指针 => 最小序号...
  rtp_hdr_t * lpFrontHeader = NULL;
  rtp_hdr_t * lpSendHeader = NULL;
  int nSendPos = 0, nSendSize = 0;
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
    log_trace("[%s-%s] Lose Error => lose: %u, min: %u, Type: %d", lpTMTag, lpIDTag, rtpLose.lose_seq, lpFrontHeader->seq, rtpLose.lose_type);
    return;
  }
  assert( rtpLose.lose_seq >= lpFrontHeader->seq );
  // 注意：环形队列当中的序列号一定是连续的...
  // 两者之差就是要发送数据包的头指针位置...
  nSendPos = (rtpLose.lose_seq - lpFrontHeader->seq) * nPerPackSize;
  // 如果补包位置大于或等于环形队列长度 => 补包越界...
  if( nSendPos >= cur_circle.size ) {
    log_trace("[%s-%s] Lose Error => Position Excessed", lpTMTag, lpIDTag);
    return;
  }
  ////////////////////////////////////////////////////////////////////////////////////////////////////
  // 注意：不能用简单的指针操作，环形队列可能会回还，必须用接口 => 从指定相对位置拷贝指定长度数据...
  // 获取将要发送数据包的包头位置和有效数据长度...
  ////////////////////////////////////////////////////////////////////////////////////////////////////
  memset(szPacketBuffer, 0, nPerPackSize);
  circlebuf_read(&cur_circle, nSendPos, szPacketBuffer, nPerPackSize);
  lpSendHeader = (rtp_hdr_t*)szPacketBuffer;
  // 如果找到的序号位置不对 或 本身就是需要补的丢包...
  if((lpSendHeader->pt == PT_TAG_LOSE) || (lpSendHeader->seq != rtpLose.lose_seq)) {
    log_trace("[%s-%s] Lose Error => Seq: %u, Find: %u, Type: %d", lpTMTag, lpIDTag, rtpLose.lose_seq, lpSendHeader->seq, lpSendHeader->pt);
    return;
  }
  // 获取有效的数据区长度 => 包头 + 数据...
  nSendSize = sizeof(rtp_hdr_t) + lpSendHeader->psize;
  // 打印已经发送补包信息...
  //log_trace("[%s-%s] Lose Send => Seq: %u, TS: %u, Slice: %d, Type: %d",
  //          lpTMTag, lpIDTag, lpSendHeader->seq, lpSendHeader->ts,
  //          lpSendHeader->psize, lpSendHeader->pt);
  // 回复老师观看端 => 发送补包命令数据内容...
  this->doTransferToFrom((char*)lpSendHeader, nSendSize);
}

// 转发音频数据包到房间里的所有的观看者...
bool CUDPClient::doTransferToLooker(char * lpBuffer, int inBufSize)
{
  // 如果不是推流者终端，直接返回失败...
  if( this->GetIdTag() != ID_TAG_PUSHER )
    return false;
  // 遍历当前推流者挂载的所有观看者...
  GM_MapUDPConn::iterator itorItem;
  for(itorItem = m_MapUdpLooker.begin(); itorItem != m_MapUdpLooker.end(); ++itorItem) {
    CUDPClient * lpLooker = itorItem->second;
    if( lpLooker == NULL ) continue;
    // 观看者对象有效，转发数据包给这个观看者...
    lpLooker->doTransferToFrom(lpBuffer, inBufSize);
  }
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

void CUDPClient::doAddUdpLooker(CUDPClient * lpLooker)
{
  // 输入终端必须是有效的观看者...
  if (lpLooker == NULL || lpLooker->GetIdTag() != ID_TAG_LOOKER)
    return;
  // 当前终端必须是有效的推流者...
  if (this->GetIdTag() != ID_TAG_PUSHER)
    return;
  int nHostPort = lpLooker->GetHostPort();
  // 将观看者放入集合当中 => 有多个观看者...
  m_MapUdpLooker[nHostPort] = lpLooker;
}

void CUDPClient::doDelUdpLooker(CUDPClient * lpLooker)
{
  // 输入终端必须是有效的观看者...
  if (lpLooker == NULL || lpLooker->GetIdTag() != ID_TAG_LOOKER)
    return;
  // 当前终端必须是有效的推流者...
  if (this->GetIdTag() != ID_TAG_PUSHER)
    return;
  int nHostPort = lpLooker->GetHostPort();
  // 将观看者从集合当中删除 => 有多个观看者...
  m_MapUdpLooker.erase(nHostPort);
}
