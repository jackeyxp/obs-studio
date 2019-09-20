
#pragma once

#include "OSThread.h"
#include "HYDefine.h"

class CPlaySDL;
class UDPSocket;
class CSmartRecvThread : public OSThread
{
public:
	CSmartRecvThread(CLIENT_TYPE inType, int nTCPSockFD, int nDBRoomID, int nLiveID);
	virtual ~CSmartRecvThread();
	virtual void Entry();
public:
	bool			InitThread(obs_source_t * lpObsSource, const char * lpUdpAddr, int nUdpPort);
	bool            IsLoginTimeout();
public:
	int             GetDBRoomID() { return m_rtp_create.roomID; }
	int             GetLiveID() { return m_rtp_create.liveID; }
	uint8_t         GetTmTag() { return m_tmTag; }
	uint8_t         GetIdTag() { return m_idTag; }
private:
	void			ClosePlayer();
	void			CloseSocket();
	void			doSendCreateCmd();
	void			doSendDeleteCmd();
	void			doSendSupplyCmd(bool bIsAudio);
	void			doSendDetectCmd();
	void			doRecvPacket();
	void			doSleepTo();

	void			doTagHeaderProcess(char * lpBuffer, int inRecvLen);
	void			doTagDetectProcess(char * lpBuffer, int inRecvLen);
	void			doTagAVPackProcess(char * lpBuffer, int inRecvLen);

	void			doFillLosePack(uint8_t inPType, uint32_t nStartLoseID, uint32_t nEndLoseID);
	void			doEraseLoseSeq(uint8_t inPType, uint32_t inSeqID);
	void			doParseFrame(bool bIsAudio);

	uint32_t		doCalcMaxConSeq(bool bIsAudio);
	void            doServerMinSeq(bool bIsAudio, uint32_t inMinSeq);
private:
	enum {
		kCmdSendCreate	= 0,				// ��ʼ���� => ��������״̬
		kCmdConnectOK	= 1,				// ���뽻����ϣ��Ѿ���ʼ��������Ƶ������
	} m_nCmdState;							// ����״̬����...

	string			m_strSPS;				// ��Ƶsps
	string			m_strPPS;				// ��Ƶpps

	CLIENT_TYPE     m_nClientType;          // �ն��ڲ�����...
	string          m_strInnerName;         // �ն��ڲ�����...
	uint8_t         m_tmTag;                // �ն�����
	uint8_t         m_idTag;                // �ն˱�ʶ

	UDPSocket    *  m_lpUDPSocket;			// UDP����
	obs_source_t *  m_lpObsSource;			// obs��Դ����

	uint16_t		m_HostServerPort;		// �������˿� => host
	uint32_t	    m_HostServerAddr;		// ��������ַ => host

	bool			m_bNeedSleep;			// ��Ϣ��־ => ֻҪ�з������հ��Ͳ�����Ϣ...
	int				m_dt_to_dir;			// ����·�߷��� => TO_SERVER | TO_P2P
	int				m_server_rtt_ms;		// Server => ���������ӳ�ֵ => ����
	int				m_server_rtt_var_ms;	// Server => ���綶��ʱ��� => ����
	int				m_server_cache_time_ms;	// Server => ��������ʱ��   => ���� => ���ǲ�����ʱʱ��
	int				m_nMaxResendCount;		// ��ǰ��������ط�����
	
	rtp_detect_t	m_rtp_detect;			// RTP̽������ṹ��
	rtp_create_t	m_rtp_create;			// RTP���������ֱ���ṹ��
	rtp_delete_t	m_rtp_delete;			// RTPɾ�������ֱ���ṹ��
	rtp_supply_t	m_rtp_supply;			// RTP��������ṹ��

	rtp_header_t	m_rtp_header;			// RTP����ͷ�ṹ��   => ���� => ����������...

	int64_t			m_login_zero_ns;		// ϵͳ��¼��ʱ0��ʱ��...
	int64_t			m_sys_zero_ns;			// ϵͳ��ʱ��� => ��һ�����ݰ������ϵͳʱ�̵� => ����...
	int64_t			m_next_create_ns;		// �´η��ʹ�������ʱ��� => ���� => ÿ��100���뷢��һ��...
	int64_t			m_next_detect_ns;		// �´η���̽�����ʱ��� => ���� => ÿ��1�뷢��һ��...

	circlebuf		m_audio_circle;			// ��Ƶ���ζ���
	circlebuf		m_video_circle;			// ��Ƶ���ζ���

	bool			m_bFirstAudioSeq;		// ��Ƶ��һ�����ݰ����յ���־...
	bool			m_bFirstVideoSeq;		// ��Ƶ��һ�����ݰ����յ���־...
	uint32_t		m_nAudioMaxPlaySeq;		// ��ƵRTP��ǰ��󲥷����к� => ���������Ч���к�...
	uint32_t		m_nVideoMaxPlaySeq;		// ��ƵRTP��ǰ��󲥷����к� => ���������Ч���к�...

	GM_MapLose		m_AudioMapLose;			// ��Ƶ��⵽�Ķ������϶���...
	GM_MapLose		m_VideoMapLose;			// ��Ƶ��⵽�Ķ������϶���...
	
	CPlaySDL    *   m_lpPlaySDL;            // SDL���Ź�����...
};