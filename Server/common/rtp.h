
#pragma once

#define DEF_WEB_CENTER     "https://edu.ihaoyi.cn"   // 默认中心网站(443) => 必须是 https:// 兼容小程序接口...

// define student role type...
enum ROLE_TYPE
{
  kRoleWanRecv   = 0,      // 外网接收者角色
  kRoleMultiRecv = 1,      // 组播接收者角色
  kRoleMultiSend = 2,      // 组播发送者角色
};

// define client type...
enum {
  kClientPHP       = 1,       // 网站端链接...
  kClientSmart     = 2,       // Smart终端...
  kClientUdpServer = 3,       // UDP服务器...
  kClientScreen    = 4,       // 屏幕终端...
};

// 注意：定义与服务器交互命令编号...
// 注意：PHP 代码中也可以使用 __LINE__
const long CMD_LINE_START   = __LINE__ + 2;
enum {
  kCmd_Smart_Login          = __LINE__ - CMD_LINE_START,
  kCmd_Smart_OnLine         = __LINE__ - CMD_LINE_START,
  kCmd_UdpServer_Login      = __LINE__ - CMD_LINE_START,
  kCmd_UdpServer_OnLine     = __LINE__ - CMD_LINE_START,
  kCmd_UdpServer_AddTeacher = __LINE__ - CMD_LINE_START,
  kCmd_UdpServer_DelTeacher = __LINE__ - CMD_LINE_START,
  kCmd_UdpServer_AddStudent = __LINE__ - CMD_LINE_START,
  kCmd_UdpServer_DelStudent = __LINE__ - CMD_LINE_START,
  kCmd_PHP_GetUdpServer     = __LINE__ - CMD_LINE_START,
  kCmd_PHP_GetAllServer     = __LINE__ - CMD_LINE_START,
  kCmd_PHP_GetAllClient     = __LINE__ - CMD_LINE_START,
  kCmd_PHP_GetRoomList      = __LINE__ - CMD_LINE_START,
  kCmd_PHP_GetPlayerList    = __LINE__ - CMD_LINE_START,
  kCmd_PHP_Bind_Mini        = __LINE__ - CMD_LINE_START,
  kCmd_PHP_GetRoomFlow      = __LINE__ - CMD_LINE_START,
  kCmd_Screen_Login         = __LINE__ - CMD_LINE_START,
  kCmd_Screen_OnLine        = __LINE__ - CMD_LINE_START,
  kCmd_Screen_Packet        = __LINE__ - CMD_LINE_START,
  kCmd_Screen_Finish        = __LINE__ - CMD_LINE_START,
};

// 定义与服务器交互命令结构体...
typedef struct {
  int   m_pkg_len;    // body size...
  int   m_type;       // client type...
  int   m_cmd;        // command id...
  int   m_sock;       // php sock in transmit...
} Cmd_Header;

//
// 定义交互终端类型...
enum
{
  TM_TAG_STUDENT  = 0x01, // 学生端标记...
  TM_TAG_TEACHER  = 0x02, // 讲师端标记...
  TM_TAG_SERVER   = 0x03, // 服务器标记...
};
//
// 定义交互终端身份...
enum
{
  ID_TAG_PUSHER  = 0x01,  // 推流者身份 => 发送者...
  ID_TAG_LOOKER  = 0x02,  // 拉流者身份 => 观看者...
  ID_TAG_SERVER  = 0x03,  // 服务器身份
};
//
// 定义RTP载荷命令类型...
enum
{
  PT_TAG_DETECT   = 0x01,  // 探测命令标记...
  PT_TAG_CREATE   = 0x02,  // 创建命令标记...
  PT_TAG_DELETE   = 0x03,  // 删除命令标记...
  PT_TAG_SUPPLY   = 0x04,  // 补包命令标记...
  PT_TAG_HEADER   = 0x05,  // 音视频序列头...
  PT_TAG_READY    = 0x06,  // 准备就绪命令 => 已失效...
  PT_TAG_AUDIO    = 0x08,  // 音频包 => FLV_TAG_TYPE_AUDIO...
  PT_TAG_VIDEO    = 0x09,  // 视频包 => FLV_TAG_TYPE_VIDEO...
  PT_TAG_LOSE     = 0x0A,  // 已丢失数据包...
  PT_TAG_EX_AUDIO = 0x0B,  // 扩展音频包...
};
//
// 定义探测方向类型...
enum
{
  DT_TO_SERVER   = 0x01,  // 通过服务器中转的探测
  DT_TO_P2P      = 0x02,  // 通过端到端直接的探测 => 已失效...
};
//
// 定义探测命令结构体 => PT_TAG_DETECT
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_DETECT
  unsigned char   dtDir;        // 探测方向 => DT_TO_SERVER | DT_TO_P2P
  unsigned short  dtNum;        // 探测序号
  unsigned int    tsSrc;        // 探测发起时的时间戳 => 毫秒
  unsigned int    maxAConSeq;   // 接收端已收到音频最大连续序列号 => 告诉发送端：这个号码之前的音频数据包都可以删除了
  unsigned int    maxVConSeq;   // 接收端已收到视频最大连续序列号 => 告诉发送端：这个号码之前的视频数据包都可以删除了
  unsigned int    maxExAConSeq; // 接收端已收到扩展音频最大连续序列号 => 告诉发送端：这个号码之前的扩展音频数据包都可以删除了
}rtp_detect_t;
//
// 定义创建命令结构体 => PT_TAG_CREATE
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_CREATE
  unsigned char   noset;        // 保留 => 字节对齐
  unsigned short  liveID;       // 学生端摄像头编号
  unsigned int    roomID;       // 教室房间编号
  unsigned int    tcpSock;      // 关联的TCP套接字
}rtp_create_t;
//
// 定义删除命令结构体 => PT_TAG_DELETE
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_DELETE
  unsigned char   noset;        // 保留 => 字节对齐
  unsigned short  liveID;       // 学生端摄像头编号
  unsigned int    roomID;       // 教室房间编号
}rtp_delete_t;
//
// 定义补包命令结构体 => PT_TAG_SUPPLY
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_SUPPLY
  unsigned char   suType;       // 补包类型 => 0x08(音频)0x09(视频)0x0B(扩展音频)
  unsigned short  suSize;       // 补报长度 / 4 = 补包个数
  // unsigned int => 补包序号1
  // unsigned int => 补包序号2
  // unsigned int => 补包序号3
}rtp_supply_t;
//
// 定义音视频序列头结构体 => PT_TAG_HEADER
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_HEADER
  unsigned char   hasAudio:4;   // 是否有音频 => 0 or 1
  unsigned char   hasVideo:4;   // 是否有视频 => 0 or 1
  unsigned char   rateIndex:5;  // 音频采样率索引编号
  unsigned char   channelNum:3; // 音频通道数量
  unsigned char   fpsNum;       // 视频fps大小
  unsigned short  picWidth;     // 视频宽度
  unsigned short  picHeight;    // 视频高度
  unsigned short  spsSize;      // sps长度
  unsigned short  ppsSize;      // pps长度
  // .... => sps data           // sps数据区
  // .... => pps data           // pps数据区
}rtp_header_t;
//
// 定义准备就绪命令结构体 => PT_TAG_READY
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_READY
  unsigned char   noset;        // 保留 => 字节对齐
  unsigned short  recvPort;     // 接收者穿透端口 => 备用 => host
  unsigned int    recvAddr;     // 接收者穿透地址 => 备用 => host
}rtp_ready_t;
//
// 定义RTP数据包头结构体 => PT_TAG_AUDIO | PT_TAG_VIDEO
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_AUDIO | PT_TAG_VIDEO
  unsigned char   pk:4;         // payload is keyframe => 0 or 1
  unsigned char   pst:2;        // payload start flag => 0 or 1
  unsigned char   ped:2;        // payload end flag => 0 or 1
  unsigned short  psize;        // payload size => 不含包头，纯数据
  unsigned int    seq;          // rtp序列号 => 从1开始
  unsigned int    ts;           // 帧时间戳 => 毫秒
  unsigned int    noset;        // 保留 => 特殊用途
}rtp_hdr_t;
//
// 定义丢包结构体...
typedef struct {
  unsigned int    lose_seq;      // 检测到的丢包序列号
  unsigned int    resend_time;   // 重发时间点 => cur_time + rtt_var => 丢包时的当前时间 + 丢包时的网络抖动时间差
  unsigned short  resend_count;  // 重发次数值 => 当前丢失包的已重发次数
  unsigned char   lose_type;     // 丢包类型 => 0x08(音频)0x09(视频)0x0B(扩展音频)
  unsigned char   noset;         // 保留 => 字节对齐
}rtp_lose_t;
