
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
	// 获取传递过来的输出对象和配置信息...
	teacher_output_t * lpRtpStream = (teacher_output_t*)data;
	lpRtpStream->room_id = (int)obs_data_get_int(settings, "room_id");
	lpRtpStream->udp_addr = obs_data_get_string(settings, "udp_addr");
	lpRtpStream->udp_port = (int)obs_data_get_int(settings, "udp_port");
	lpRtpStream->tcp_socket = (int)obs_data_get_int(settings, "tcp_socket");
	// 如果推流线程已经创建，直接删除...
	if (lpRtpStream->sendThread != NULL) {
		delete lpRtpStream->sendThread;
		lpRtpStream->sendThread = NULL;
	}
	ASSERT(lpRtpStream->sendThread == NULL);
	// 判断房间信息和传递参数是否有效，无效直接返回...
	if (lpRtpStream->room_id <= 0 || lpRtpStream->tcp_socket <= 0 ||
		lpRtpStream->udp_port <= 0 || lpRtpStream->udp_addr == NULL) {
		blog(LOG_INFO, "%s teacher_output_update => Failed, RoomID: %d, TCPSock: %d, %s:%d", TH_SEND_NAME,
			lpRtpStream->room_id, lpRtpStream->tcp_socket, lpRtpStream->udp_addr, lpRtpStream->udp_port);
		return;
	}
	// 创建新的推流线程对象，延时初始化，将推流线程对象保存到输出管理器当中...
	lpRtpStream->sendThread = new CTeacherSendThread(lpRtpStream->tcp_socket, lpRtpStream->room_id);
	// 打印成功创建接收线程信息 => room_id | udp_addr | udp_port | tcp_socket
	blog(LOG_INFO, "%s teacher_output_update => Success, RoomID: %d, TCPSock: %d, %s:%d", TH_SEND_NAME,
		lpRtpStream->room_id, lpRtpStream->tcp_socket, lpRtpStream->udp_addr, lpRtpStream->udp_port);
}

static void *teacher_output_create(obs_data_t *settings, obs_output_t *output)
{
	// 创建rtp数据源变量管理对象...
	teacher_output_t * lpRtpStream = (teacher_output_t*)bzalloc(sizeof(struct teacher_output_t));
	// 将传递过来的obs输出对象保存起来...
	lpRtpStream->output = output;
	// 先调用配置发生变化的处理...
	teacher_output_update(lpRtpStream, settings);
	// 返回输出变量管理对象...
	return lpRtpStream;
}

static void teacher_output_destroy(void *data)
{
	// 注意：不能再次调用 obs_output_end_data_capture，可能会引起压缩器崩溃...
	teacher_output_t * lpRtpStream = (teacher_output_t*)data;
	// 如果推流线程被创建过，需要被删除...
	if (lpRtpStream->sendThread != NULL) {
		delete lpRtpStream->sendThread;
		lpRtpStream->sendThread = NULL;
	}
	// 释放rtp输出管理器...
	bfree(lpRtpStream);
}

static bool teacher_output_start(void *data)
{
	teacher_output_t * lpRtpStream = (teacher_output_t*)data;
	// 判断上层是否可以进行数据捕捉 => 查询失败，打印错误信息...
	if (!obs_output_can_begin_data_capture(lpRtpStream->output, 0)) {
		blog(LOG_INFO, "%s begin capture failed!", TH_SEND_NAME);
		return false;
	}
	// 通知上层初始化压缩器 => 启动失败，打印错误信息...
	if (!obs_output_initialize_encoders(lpRtpStream->output, 0)) {
		blog(LOG_INFO, "%s init encoder failed!", TH_SEND_NAME);
		return false;
	}
	// 如果推流线程无效，直接返回...
	if (lpRtpStream->sendThread == NULL) {
		blog(LOG_INFO, "%s send thread failed!", TH_SEND_NAME);
		return false;
	}
	// 用音视频格式头初始化推流线程 => 初始化失败，直接返回...
	if (!lpRtpStream->sendThread->InitThread(lpRtpStream->output, lpRtpStream->udp_addr, lpRtpStream->udp_port)) {
		blog(LOG_INFO, "%s start thread failed!", TH_SEND_NAME);
		return false;
	}
	// 先不要启动数据采集压缩过程，等推流线程准备好之后，再启动，以免数据丢失，造成启动延时...
	// CTeacherSendThread::doProcServerHeader => 推流线程连接服务器成功之后，才启动数据采集压缩...
	return true;
}

static void teacher_output_stop(void *data, uint64_t ts)
{
	teacher_output_t * lpRtpStream = (teacher_output_t*)data;
	// OBS_OUTPUT_SUCCESS 会调用 obs_output_end_data_capture...
	obs_output_signal_stop(lpRtpStream->output, OBS_OUTPUT_SUCCESS);
	// 上层停止产生数据之后，删除推流线程对象...
	if (lpRtpStream->sendThread != NULL) {
		delete lpRtpStream->sendThread;
		lpRtpStream->sendThread = NULL;
	}
}

static void teacher_output_data(void *data, struct encoder_packet *packet)
{
	teacher_output_t * lpRtpStream = (teacher_output_t*)data;
	// 将数据帧投递给推流线程 => 投递原始数据，不用做任何处理...
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
