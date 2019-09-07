
#pragma once

#include "global.h"

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
  int           GetDBCameraID() { return m_rtp_create.liveID; }
  int           GetTCPSockID() { return m_rtp_create.tcpSock; }
  bool          doTransferToFrom(char * lpBuffer, int inBufSize);
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
protected:
  int           m_nUdpListenFD;       // UDP监听套接字
  int           m_nRoomID;            // 房间编号
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

  friend class CRoom;
};