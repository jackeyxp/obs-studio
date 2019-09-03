
#include <obs-avc.h>
#include <obs-module.h>
#include "teacher-send-thread.h"

struct teacher_output_t {
	obs_output_t   * output;
	int              room_id;
	const char     * udp_addr;
	int              udp_port;
	int              tcp_socket;
	CTeacherSendThread * sendThread;
};

static const char *teacher_output_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("TeacherOutput");
}

static void teacher_output_update(void *data, obs_data_t *settings)
{
	// ��ȡ���ݹ�������������������Ϣ...
	teacher_output_t * lpRtpStream = (teacher_output_t*)data;
	lpRtpStream->room_id = (int)obs_data_get_int(settings, "room_id");
	lpRtpStream->udp_addr = obs_data_get_string(settings, "udp_addr");
	lpRtpStream->udp_port = (int)obs_data_get_int(settings, "udp_port");
	lpRtpStream->tcp_socket = (int)obs_data_get_int(settings, "tcp_socket");
	// ��������߳��Ѿ�������ֱ��ɾ��...
	if (lpRtpStream->sendThread != NULL) {
		delete lpRtpStream->sendThread;
		lpRtpStream->sendThread = NULL;
	}
	ASSERT(lpRtpStream->sendThread == NULL);
	// �жϷ�����Ϣ�ʹ��ݲ����Ƿ���Ч����Чֱ�ӷ���...
	if (lpRtpStream->room_id <= 0 || lpRtpStream->tcp_socket <= 0 ||
		lpRtpStream->udp_port <= 0 || lpRtpStream->udp_addr == NULL) {
		blog(LOG_INFO, "%s teacher_output_update => Failed, RoomID: %d, TCPSock: %d, %s:%d", TH_SEND_NAME,
			lpRtpStream->room_id, lpRtpStream->tcp_socket, lpRtpStream->udp_addr, lpRtpStream->udp_port);
		return;
	}
	// �����µ������̶߳�����ʱ��ʼ�����������̶߳��󱣴浽�������������...
	lpRtpStream->sendThread = new CTeacherSendThread(lpRtpStream->tcp_socket, lpRtpStream->room_id);
	// ��ӡ�ɹ����������߳���Ϣ => room_id | udp_addr | udp_port | tcp_socket
	blog(LOG_INFO, "%s teacher_output_update => Success, RoomID: %d, TCPSock: %d, %s:%d", TH_SEND_NAME,
		lpRtpStream->room_id, lpRtpStream->tcp_socket, lpRtpStream->udp_addr, lpRtpStream->udp_port);
}

static void *teacher_output_create(obs_data_t *settings, obs_output_t *output)
{
	// ����rtp����Դ�����������...
	teacher_output_t * lpRtpStream = (teacher_output_t*)bzalloc(sizeof(struct teacher_output_t));
	// �����ݹ�����obs������󱣴�����...
	lpRtpStream->output = output;
	// �ȵ������÷����仯�Ĵ���...
	teacher_output_update(lpRtpStream, settings);
	// ������������������...
	return lpRtpStream;
}

static void teacher_output_destroy(void *data)
{
	// ע�⣺�����ٴε��� obs_output_end_data_capture�����ܻ�����ѹ��������...
	teacher_output_t * lpRtpStream = (teacher_output_t*)data;
	// ��������̱߳�����������Ҫ��ɾ��...
	if (lpRtpStream->sendThread != NULL) {
		delete lpRtpStream->sendThread;
		lpRtpStream->sendThread = NULL;
	}
	// �ͷ�rtp���������...
	bfree(lpRtpStream);
}

static bool teacher_output_start(void *data)
{
	teacher_output_t * lpRtpStream = (teacher_output_t*)data;
	// �ж��ϲ��Ƿ���Խ������ݲ�׽ => ��ѯʧ�ܣ���ӡ������Ϣ...
	if (!obs_output_can_begin_data_capture(lpRtpStream->output, 0)) {
		blog(LOG_INFO, "%s begin capture failed!", TH_SEND_NAME);
		return false;
	}
	// ֪ͨ�ϲ��ʼ��ѹ���� => ����ʧ�ܣ���ӡ������Ϣ...
	if (!obs_output_initialize_encoders(lpRtpStream->output, 0)) {
		blog(LOG_INFO, "%s init encoder failed!", TH_SEND_NAME);
		return false;
	}
	// ��������߳���Ч��ֱ�ӷ���...
	if (lpRtpStream->sendThread == NULL) {
		blog(LOG_INFO, "%s send thread failed!", TH_SEND_NAME);
		return false;
	}
	// ������Ƶ��ʽͷ��ʼ�������߳� => ��ʼ��ʧ�ܣ�ֱ�ӷ���...
	if (!lpRtpStream->sendThread->InitThread(lpRtpStream->output, lpRtpStream->udp_addr, lpRtpStream->udp_port)) {
		blog(LOG_INFO, "%s start thread failed!", TH_SEND_NAME);
		return false;
	}
	// �Ȳ�Ҫ�������ݲɼ�ѹ�����̣��������߳�׼����֮�����������������ݶ�ʧ�����������ʱ...
	// CTeacherSendThread::doProcServerHeader => �����߳����ӷ������ɹ�֮�󣬲��������ݲɼ�ѹ��...
	return true;
}

static void teacher_output_stop(void *data, uint64_t ts)
{
	teacher_output_t * lpRtpStream = (teacher_output_t*)data;
	// OBS_OUTPUT_SUCCESS ����� obs_output_end_data_capture...
	obs_output_signal_stop(lpRtpStream->output, OBS_OUTPUT_SUCCESS);
	// �ϲ�ֹͣ��������֮��ɾ�������̶߳���...
	if (lpRtpStream->sendThread != NULL) {
		delete lpRtpStream->sendThread;
		lpRtpStream->sendThread = NULL;
	}
}

static void teacher_output_data(void *data, struct encoder_packet *packet)
{
	teacher_output_t * lpRtpStream = (teacher_output_t*)data;
	// ������֡Ͷ�ݸ������߳� => Ͷ��ԭʼ���ݣ��������κδ���...
	if (lpRtpStream->sendThread != NULL) {
		lpRtpStream->sendThread->PushFrame(packet);
	}
}

static void teacher_output_defaults(obs_data_t *defaults)
{
}

static obs_properties_t *teacher_output_properties(void *unused)
{
	return NULL;
}

static uint64_t teacher_output_total_bytes_sent(void *data)
{
	return 0;
}

static int teacher_output_connect_time(void *data)
{
	return 0;
}

static int teacher_output_dropped_frames(void *data)
{
	return 0;
}

void RegisterTeacherOutput()
{
	obs_output_info th_output = {};
	th_output.id = "teacher_output";
	th_output.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED;
	th_output.encoded_video_codecs = "h264";
	th_output.encoded_audio_codecs = "aac";
	th_output.get_name = teacher_output_getname;
	th_output.create = teacher_output_create;
	th_output.update = teacher_output_update;
	th_output.destroy = teacher_output_destroy;
	th_output.start = teacher_output_start;
	th_output.stop = teacher_output_stop;
	th_output.encoded_packet = teacher_output_data;
	th_output.get_defaults = teacher_output_defaults;
	th_output.get_properties = teacher_output_properties;
	th_output.get_total_bytes = teacher_output_total_bytes_sent;
	th_output.get_connect_time_ms = teacher_output_connect_time;
	th_output.get_dropped_frames = teacher_output_dropped_frames;
	obs_register_output(&th_output);
}
