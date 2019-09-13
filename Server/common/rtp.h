
#pragma once

#define DEF_WEB_CENTER     "https://edu.ihaoyi.cn"   // Ĭ��������վ(443) => ������ https:// ����С����ӿ�...

// define student role type...
enum ROLE_TYPE
{
  kRoleWanRecv   = 0,      // ���������߽�ɫ
  kRoleMultiRecv = 1,      // �鲥�����߽�ɫ
  kRoleMultiSend = 2,      // �鲥�����߽�ɫ
};

// define client type...
enum {
  kClientPHP       = 1,       // ��վ������...
  kClientSmart     = 2,       // Smart�ն�...
  kClientUdpServer = 3,       // UDP������...
  kClientScreen    = 4,       // ��Ļ�ն�...
};

// ע�⣺���������������������...
// ע�⣺PHP ������Ҳ����ʹ�� __LINE__
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

// �������������������ṹ��...
typedef struct {
  int   m_pkg_len;    // body size...
  int   m_type;       // client type...
  int   m_cmd;        // command id...
  int   m_sock;       // php sock in transmit...
} Cmd_Header;

//
// ���彻���ն�����...
enum
{
  TM_TAG_STUDENT  = 0x01, // ѧ���˱��...
  TM_TAG_TEACHER  = 0x02, // ��ʦ�˱��...
  TM_TAG_SERVER   = 0x03, // ���������...
};
//
// ���彻���ն����...
enum
{
  ID_TAG_PUSHER  = 0x01,  // ��������� => ������...
  ID_TAG_LOOKER  = 0x02,  // ��������� => �ۿ���...
  ID_TAG_SERVER  = 0x03,  // ���������
};
//
// ����RTP�غ���������...
enum
{
  PT_TAG_DETECT   = 0x01,  // ̽��������...
  PT_TAG_CREATE   = 0x02,  // ����������...
  PT_TAG_DELETE   = 0x03,  // ɾ��������...
  PT_TAG_SUPPLY   = 0x04,  // ����������...
  PT_TAG_HEADER   = 0x05,  // ����Ƶ����ͷ...
  PT_TAG_READY    = 0x06,  // ׼���������� => ��ʧЧ...
  PT_TAG_AUDIO    = 0x08,  // ��Ƶ�� => FLV_TAG_TYPE_AUDIO...
  PT_TAG_VIDEO    = 0x09,  // ��Ƶ�� => FLV_TAG_TYPE_VIDEO...
  PT_TAG_LOSE     = 0x0A,  // �Ѷ�ʧ���ݰ�...
  PT_TAG_EX_AUDIO = 0x0B,  // ��չ��Ƶ��...
};
//
// ����̽�ⷽ������...
enum
{
  DT_TO_SERVER   = 0x01,  // ͨ����������ת��̽��
  DT_TO_P2P      = 0x02,  // ͨ���˵���ֱ�ӵ�̽�� => ��ʧЧ...
};
//
// ����̽������ṹ�� => PT_TAG_DETECT
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_DETECT
  unsigned char   dtDir;        // ̽�ⷽ�� => DT_TO_SERVER | DT_TO_P2P
  unsigned short  dtNum;        // ̽�����
  unsigned int    tsSrc;        // ̽�ⷢ��ʱ��ʱ��� => ����
  unsigned int    maxAConSeq;   // ���ն����յ���Ƶ����������к� => ���߷��Ͷˣ��������֮ǰ����Ƶ���ݰ�������ɾ����
  unsigned int    maxVConSeq;   // ���ն����յ���Ƶ����������к� => ���߷��Ͷˣ��������֮ǰ����Ƶ���ݰ�������ɾ����
  unsigned int    maxExAConSeq; // ���ն����յ���չ��Ƶ����������к� => ���߷��Ͷˣ��������֮ǰ����չ��Ƶ���ݰ�������ɾ����
}rtp_detect_t;
//
// ���崴������ṹ�� => PT_TAG_CREATE
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_CREATE
  unsigned char   noset;        // ���� => �ֽڶ���
  unsigned short  liveID;       // ѧ��������ͷ���
  unsigned int    roomID;       // ���ҷ�����
  unsigned int    tcpSock;      // ������TCP�׽���
}rtp_create_t;
//
// ����ɾ������ṹ�� => PT_TAG_DELETE
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_DELETE
  unsigned char   noset;        // ���� => �ֽڶ���
  unsigned short  liveID;       // ѧ��������ͷ���
  unsigned int    roomID;       // ���ҷ�����
}rtp_delete_t;
//
// ���岹������ṹ�� => PT_TAG_SUPPLY
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_SUPPLY
  unsigned char   suType;       // �������� => 0x08(��Ƶ)0x09(��Ƶ)0x0B(��չ��Ƶ)
  unsigned short  suSize;       // �������� / 4 = ��������
  // unsigned int => �������1
  // unsigned int => �������2
  // unsigned int => �������3
}rtp_supply_t;
//
// ��������Ƶ����ͷ�ṹ�� => PT_TAG_HEADER
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_HEADER
  unsigned char   hasAudio:4;   // �Ƿ�����Ƶ => 0 or 1
  unsigned char   hasVideo:4;   // �Ƿ�����Ƶ => 0 or 1
  unsigned char   rateIndex:5;  // ��Ƶ�������������
  unsigned char   channelNum:3; // ��Ƶͨ������
  unsigned char   fpsNum;       // ��Ƶfps��С
  unsigned short  picWidth;     // ��Ƶ���
  unsigned short  picHeight;    // ��Ƶ�߶�
  unsigned short  spsSize;      // sps����
  unsigned short  ppsSize;      // pps����
  // .... => sps data           // sps������
  // .... => pps data           // pps������
}rtp_header_t;
//
// ����׼����������ṹ�� => PT_TAG_READY
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_READY
  unsigned char   noset;        // ���� => �ֽڶ���
  unsigned short  recvPort;     // �����ߴ�͸�˿� => ���� => host
  unsigned int    recvAddr;     // �����ߴ�͸��ַ => ���� => host
}rtp_ready_t;
//
// ����RTP���ݰ�ͷ�ṹ�� => PT_TAG_AUDIO | PT_TAG_VIDEO
typedef struct {
  unsigned char   tm:2;         // terminate type => TM_TAG_STUDENT | TM_TAG_TEACHER
  unsigned char   id:2;         // identify type => ID_TAG_PUSHER | ID_TAG_LOOKER
  unsigned char   pt:4;         // payload type => PT_TAG_AUDIO | PT_TAG_VIDEO
  unsigned char   pk:4;         // payload is keyframe => 0 or 1
  unsigned char   pst:2;        // payload start flag => 0 or 1
  unsigned char   ped:2;        // payload end flag => 0 or 1
  unsigned short  psize;        // payload size => ������ͷ��������
  unsigned int    seq;          // rtp���к� => ��1��ʼ
  unsigned int    ts;           // ֡ʱ��� => ����
  unsigned int    noset;        // ���� => ������;
}rtp_hdr_t;
//
// ���嶪���ṹ��...
typedef struct {
  unsigned int    lose_seq;      // ��⵽�Ķ������к�
  unsigned int    resend_time;   // �ط�ʱ��� => cur_time + rtt_var => ����ʱ�ĵ�ǰʱ�� + ����ʱ�����綶��ʱ���
  unsigned short  resend_count;  // �ط�����ֵ => ��ǰ��ʧ�������ط�����
  unsigned char   lose_type;     // �������� => 0x08(��Ƶ)0x09(��Ƶ)0x0B(��չ��Ƶ)
  unsigned char   noset;         // ���� => �ֽڶ���
}rtp_lose_t;
