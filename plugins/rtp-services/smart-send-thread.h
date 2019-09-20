
#pragma once

#include <util/threading.h>
#include "OSThread.h"
#include "HYDefine.h"

class UDPSocket;
class CSmartSendThread : public OSThread
{
public:
	CSmartSendThread(CLIENT_TYPE inType, int nTCPSockFD, int nDBRoomID);
	virtual ~CSmartSendThread();
	virtual void Entry();
public:
	BOOL			InitThread(obs_output_t * lpObsOutput, const char * lpUdpAddr, int nUdpPort);
	BOOL			PushFrame(encoder_packet * lpEncPacket);
protected:
	uint8_t         GetTmTag() { return m_tmTag; }
	uint8_t         GetIdTag() { return m_idTag; }
	BOOL			InitVideo(string & inSPS, string & inPPS, int nWidth, int nHeight, int nFPS);
	BOOL			InitAudio(int inAudioRate, int inAudioChannel);
	BOOL			ParseAVHeader();
private:
	void			CloseSocket();
	void			doCalcAVBitRate();
	void			doSendCreateCmd();
	void			doSendHeaderCmd();
	void			doSendDeleteCmd();
	void			doSendDetectCmd();
	void			doSendLosePacket(bool bIsAudio);
	void			doSendPacket(bool bIsAudio);
	void			doRecvPacket();
	void			doSleepTo();

	void			doTagCreateProcess(char * lpBuffer, int inRecvLen);
	void			doTagHeaderProcess(char * lpBuffer, int inRecvLen);
	void			doTagDetectProcess(char * lpBuffer, int inRecvLen);
	void			doTagSupplyProcess(char * lpBuffer, int inRecvLen);

	void			doProcMaxConSeq(bool bIsAudio, uint32_t inMaxConSeq);
private:
	enum {
		kCmdSendCreate	= 0,				// ��ʼ���� => ��������״̬
		kCmdSendHeader	= 1,				// ��ʼ���� => ����ͷ����״̬
		kCmdSendAVPack	= 2,				// ��ʼ���� => ����Ƶ���ݰ�״̬
		kCmdUnkownState = 3,				// δ֪״̬ => �߳��Ѿ��˳�		
	} m_nCmdState;							// ����״̬����...

	string			m_strSPS;				// ��Ƶsps
	string			m_strPPS;				// ��Ƶpps
	
	CLIENT_TYPE     m_nClientType;          // �ն��ڲ�����...
	string          m_strInnerName;         // �ն��ڲ�����...
	uint8_t         m_tmTag;                // �ն�����
	uint8_t         m_idTag;                // �ն˱�ʶ

	UDPSocket	 *  m_lpUDPSocket;			// UDP����
	obs_output_t *  m_lpObsOutput;			// obs�������
	pthread_mutex_t m_Mutex;                // ������� => �������ζ���...

	uint16_t		m_HostServerPort;		// �������˿� => host
	uint32_t	    m_HostServerAddr;		// ��������ַ => host

	bool			m_bNeedSleep;			// ��Ϣ��־ => ֻҪ�з������հ��Ͳ�����Ϣ...
	int32_t			m_start_dts_ms;			// ��һ������֡��dtsʱ�䣬0���ʱ...

	int				m_dt_to_dir;			// ����·�߷��� => TO_SERVER | TO_P2P
	int				m_server_rtt_ms;		// Server => ���������ӳ�ֵ => ����
	int				m_server_rtt_var_ms;	// Server => ���綶��ʱ��� => ����

	rtp_detect_t	m_rtp_detect;			// RTP̽������ṹ��
	rtp_create_t	m_rtp_create;			// RTP���������ֱ���ṹ��
	rtp_delete_t	m_rtp_delete;			// RTPɾ�������ֱ���ṹ��
	rtp_header_t	m_rtp_header;			// RTP����ͷ�ṹ��

	int64_t			m_next_create_ns;		// �´η��ʹ�������ʱ��� => ���� => ÿ��100���뷢��һ��...
	int64_t			m_next_header_ns;		// �´η�������ͷ����ʱ��� => ���� => ÿ��100���뷢��һ��...
	int64_t			m_next_detect_ns;		// �´η���̽�����ʱ��� => ���� => ÿ��1�뷢��һ��...

	circlebuf		m_audio_circle;			// ��Ƶ���ζ���
	circlebuf		m_video_circle;			// ��Ƶ���ζ���

	uint32_t		m_nAudioCurPackSeq;		// ��ƵRTP��ǰ������к�
	uint32_t		m_nAudioCurSendSeq;		// ��ƵRTP��ǰ�������к�

	uint32_t		m_nVideoCurPackSeq;		// ��ƵRTP��ǰ������к�
	uint32_t		m_nVideoCurSendSeq;		// ��ƵRTP��ǰ�������к�

	GM_MapLose		m_AudioMapLose;			// ��Ƶ��⵽�Ķ������϶���...
	GM_MapLose		m_VideoMapLose;			// ��Ƶ��⵽�Ķ������϶���...

	int64_t			m_start_time_ns;		// ������������ʱ��...
	int64_t			m_total_time_ms;		// �ܵĳ���������...

	int64_t			m_audio_input_bytes;	// ��Ƶ�������ֽ���...
	int64_t			m_video_input_bytes;	// ��Ƶ�������ֽ���...
	int				m_audio_input_kbps;		// ��Ƶ����ƽ������...
	int				m_video_input_kbps;		// ��Ƶ����ƽ������...

	int64_t			m_total_output_bytes;	// �ܵ�����ֽ���...
	int64_t			m_audio_output_bytes;	// ��Ƶ������ֽ���...
	int64_t			m_video_output_bytes;	// ��Ƶ������ֽ���...
	int				m_total_output_kbps;	// �ܵ����ƽ������...
	int				m_audio_output_kbps;	// ��Ƶ���ƽ������...
	int				m_video_output_kbps;	// ��Ƶ���ƽ������...
};