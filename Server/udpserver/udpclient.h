
#pragma once

#include "global.h"

class CRoom;
class CUDPClient
{
public:
  CUDPClient(int inUdpListenFD, uint8_t tmTag, uint8_t idTag, uint32_t inHostAddr, uint16_t inHostPort);
  ~CUDPClient();
public:
  bool          IsTimeout();
  void          ResetTimeout();
  uint8_t       GetTmTag() { return m_tmTag; }
  uint8_t       GetIdTag() { return m_idTag; }
  uint32_t      GetHostAddr() { return m_nHostAddr; }
  uint16_t      GetHostPort() { return m_nHostPort; }
  string    &   GetSeqHeader() { return m_strSeqHeader; }
  uint32_t      GetCurVPushSeq() { return m_nCurVPushSeq; }
  uint32_t      GetLiveID() { return m_rtp_create.liveID; }
  int           GetTCPSockID() { return m_rtp_create.tcpSock; }
  int           GetLookerCount() { return m_MapUdpLooker.size(); }
  void          SetCanDetect(bool bFlag) { m_bIsCanDetect = bFlag; }
public:
  void          doAddUdpLooker(CUDPClient * lpLooker);
  void          doDelUdpLooker(CUDPClient * lpLooker);
public:
  bool          doServerSendLose();
  bool          doServerSendDetect();
  int           doServerSendSupply();
  bool          doProcessUdpEvent(uint8_t ptTag, char * lpBuffer, int inBufSize);
protected:
  bool          doTagDetect(char * lpBuffer, int inBufSize);
  bool          doTagCreate(char * lpBuffer, int inBufSize);
  bool          doTagDelete(char * lpBuffer, int inBufSize);
  bool          doTagSupply(char * lpBuffer, int inBufSize);
  bool          doTagHeader(char * lpBuffer, int inBufSize);
  bool          doTagReady(char * lpBuffer, int inBufSize);
  bool          doTagAudio(char * lpBuffer, int inBufSize);
  bool          doTagVideo(char * lpBuffer, int inBufSize);
private:
  uint32_t      doCalcMinSeq(bool bIsAudio);
  uint32_t      doCalcMaxConSeq(bool bIsAudio);
  void          doCalcAVJamStatus(bool bIsAudio);
  void          doSendLosePacket(bool bIsAudio);
  int           doSendSupplyCmd(bool bIsAudio);

  bool          doIsLosePacket(bool bIsAudio, uint32_t inLoseSeq);
  bool          doIsPusherLose(uint8_t inPType, uint32_t inLoseSeq);
  bool          doCreateForPusher(char * lpBuffer, int inBufSize);
  bool          doCreateForLooker(char * lpBuffer, int inBufSize);
  bool          doDetectForLooker(char * lpBuffer, int inBufSize);

  bool          doTransferToFrom(char * lpBuffer, int inBufSize);
  bool          doTransferToLooker(char * lpBuffer, int inBufSize);

  void          doTagAVPackProcess(char * lpBuffer, int inBufSize);
  void          doEraseLoseSeq(uint8_t inPType, uint32_t inSeqID);
  void          doFillLosePack(uint8_t inPType, uint32_t nStartLoseID, uint32_t nEndLoseID);
protected:
  bool          m_bIsCanDetect;       // 服务器能否向推流端发送探测数据包标志...
  CRoom    *    m_lpRoom;             // 房间对象
  int           m_nRoomID;            // 房间编号
  int           m_nUdpListenFD;       // UDP监听套接字
  uint32_t      m_nCurVPushSeq;       // 推流者最新推送视频帧序号...
  uint32_t      m_nHostAddr;          // 映射地址
  uint16_t      m_nHostPort;          // 映射端口
  uint8_t       m_tmTag;              // 终端类型
  uint8_t       m_idTag;              // 终端标识
  time_t        m_nStartTime;         // 超时检测起点
  string        m_strSeqHeader;       // 推流端上传的序列头命令包...
  int           m_server_rtt_ms;      // Server => 网络往返延迟值 => 毫秒 => 推流时服务器探测延时
  int           m_server_rtt_var_ms;  // Server => 网络抖动时间差 => 毫秒 => 推流时服务器探测延时
  rtp_detect_t  m_server_rtp_detect;  // Server => 探测命令...
  rtp_create_t  m_rtp_create;         // 创建命令
  circlebuf     m_audio_circle;       // 推流端音频环形队列...
  circlebuf     m_video_circle;       // 推流端视频环形队列...
  GM_MapLose    m_AudioMapLose;			  // 推流端检测|观看端上报的音频丢包集合队列...
  GM_MapLose    m_VideoMapLose;			  // 推流端检测|观看端上报的视频丢包集合队列...
  GM_MapUDPConn m_MapUdpLooker;       // 多个UDP观看者...
  
  friend class CRoom;
};