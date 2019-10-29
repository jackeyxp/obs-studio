
#include <obs-avc.h>
#include <obs-module.h>
#include "smart-send-thread.h"

struct smart_output_t {
	obs_output_t   * output;
	int              room_id;
	const char     * udp_addr;
	int              udp_port;
	int              tcp_socket;
	CLIENT_TYPE      client_type;
	const char     * inner_name;
	CSmartSendThread * sendThread;
};

static const char *smart_output_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("SmartOutput");
}

static void smart_output_update(void *data, obs_data_t *settings)
{
	// 获取传递过来的输出对象和配置信息...
	smart_output_t * lpRtpStream = (smart_output_t*)data;
	lpRtpStream->room_id = (int)obs_data_get_int(settings, "room_id");
	lpRtpStream->udp_addr = obs_data_get_string(settings, "udp_addr");
	lpRtpStream->udp_port = (int)obs_data_get_int(settings, "udp_port");
	lpRtpStream->tcp_socket = (int)obs_data_get_int(settings, "tcp_socket");
	lpRtpStream->client_type = (CLIENT_TYPE)obs_data_get_int(settings, "client_type");
	// 保存数据源终端的内部名称...
	switch (lpRtpStream->client_type) {
	case kClientStudent: lpRtpStream->inner_name = ST_SEND_NAME; break;
	case kClientTeacher: lpRtpStream->inner_name = TH_SEND_NAME; break;
	}
	// 如果推流线程已经创建，直接删除...
	if (lpRtpStream->sendThread != NULL) {
		delete lpRtpStream->sendThread;
		lpRtpStream->sendThread = NULL;
	}
	ASSERT(lpRtpStream->sendThread == NULL);
	// 判断房间信息和传递参数是否有效，无效直接返回...
	if (lpRtpStream->room_id <= 0 || lpRtpStream->tcp_socket <= 0 || lpRtpStream->udp_port <= 0 || lpRtpStream->udp_addr == NULL || lpRtpStream->client_type <= 0) {
		blog(LOG_INFO, "smart_output_update => Failed, ClientType: %d, RoomID: %d, TCPSock: %d, %s:%d",
			lpRtpStream->client_type, lpRtpStream->room_id, lpRtpStream->tcp_socket,
			lpRtpStream->udp_addr, lpRtpStream->udp_port);
		return;
	}
	// 创建新的推流线程对象，延时初始化，将推流线程对象保存到输出管理器当中...
	lpRtpStream->sendThread = new CSmartSendThread(lpRtpStream->client_type, lpRtpStream->tcp_socket, lpRtpStream->room_id);
	// 打印成功创建接收线程信息 => client_type | room_id | udp_addr | udp_port | tcp_socket
	blog(LOG_INFO, "smart_output_update => Success, ClientType: %s, RoomID: %d, TCPSock: %d, %s:%d",
		lpRtpStream->inner_name, lpRtpStream->room_id, lpRtpStream->tcp_socket,
		lpRtpStream->udp_addr, lpRtpStream->udp_port);
}

static void *smart_output_create(obs_data_t *settings, obs_output_t *output)
{
	// 创建rtp数据源变量管理对象...
	smart_output_t * lpRtpStream = (smart_output_t*)bzalloc(sizeof(struct smart_output_t));
	// 将传递过来的obs输出对象保存起来...
	lpRtpStream->output = output;
	// 注意：不要调用配置发生变化的处理，没有意义...
	//smart_output_update(lpRtpStream, settings);
	// 返回输出变量管理对象...
	return lpRtpStream;
}

static void smart_output_destroy(void *data)
{
	// 注意：不能再次调用 obs_output_end_data_capture，可能会引起压缩器崩溃...
	smart_output_t * lpRtpStream = (smart_output_t*)data;
	// 如果推流线程被创建过，需要被删除...
	if (lpRtpStream->sendThread != NULL) {
		delete lpRtpStream->sendThread;
		lpRtpStream->sendThread = NULL;
	}
	// 释放rtp输出管理器...
	bfree(lpRtpStream);
}

static bool smart_output_start(void *data)
{
	smart_output_t * lpRtpStream = (smart_output_t*)data;
	// 判断上层是否可以进行数据捕捉 => 查询失败，打印错误信息...
	if (!obs_output_can_begin_data_capture(lpRtpStream->output, 0)) {
		blog(LOG_INFO, "%s begin capture failed!", lpRtpStream->inner_name);
		return false;
	}
	// 通知上层初始化压缩器 => 启动失败，打印错误信息...
	if (!obs_output_initialize_encoders(lpRtpStream->output, 0)) {
		blog(LOG_INFO, "%s init encoder failed!", lpRtpStream->inner_name);
		return false;
	}
	// 如果推流线程无效，直接返回...
	if (lpRtpStream->sendThread == NULL) {
		blog(LOG_INFO, "%s send thread failed!", lpRtpStream->inner_name);
		return false;
	}
	// 用音视频格式头初始化推流线程 => 初始化失败，直接返回...
	if (!lpRtpStream->sendThread->InitThread(lpRtpStream->output, lpRtpStream->udp_addr, lpRtpStream->udp_port)) {
		blog(LOG_INFO, "%s start thread failed!", lpRtpStream->inner_name);
		return false;
	}
	// 先不要启动数据采集压缩过程，等推流线程准备好之后，再启动，以免数据丢失，造成启动延时...
	// CSmartSendThread::doProcServerHeader => 推流线程连接服务器成功之后，才启动数据采集压缩...
	return true;
}

static void smart_output_stop(void *data, uint64_t ts)
{
	smart_output_t * lpRtpStream = (smart_output_t*)data;
	// OBS_OUTPUT_SUCCESS 会调用 obs_output_end_data_capture...
	obs_output_signal_stop(lpRtpStream->output, OBS_OUTPUT_SUCCESS);
	// 上层停止产生数据之后，删除推流线程对象...
	if (lpRtpStream->sendThread != NULL) {
		delete lpRtpStream->sendThread;
		lpRtpStream->sendThread = NULL;
	}
}

static void smart_output_data(void *data, struct encoder_packet *packet)
{
	smart_output_t * lpRtpStream = (smart_output_t*)data;
	// 将数据帧投递给推流线程 => 投递原始数据，不用做任何处理...
	if (lpRtpStream->sendThread != NULL) {
		lpRtpStream->sendThread->PushFrame(packet);
	}
}

static void smart_output_defaults(obs_data_t *defaults)
{
}

static obs_properties_t *smart_output_properties(void *unused)
{
	return NULL;
}

static uint64_t smart_output_total_bytes_sent(void *data)
{
	return 0;
}

static int smart_output_connect_time(void *data)
{
	return 0;
}

static int smart_output_dropped_frames(void *data)
{
	return 0;
}

void RegisterSmartOutput()
{
	obs_output_info st_output = {};
	st_output.id = "smart_output";
	st_output.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED;
	st_output.encoded_video_codecs = "h264";
	st_output.encoded_audio_codecs = "aac";
	st_output.get_name = smart_output_getname;
	st_output.create = smart_output_create;
	st_output.update = smart_output_update;
	st_output.destroy = smart_output_destroy;
	st_output.start = smart_output_start;
	st_output.stop = smart_output_stop;
	st_output.encoded_packet = smart_output_data;
	st_output.get_defaults = smart_output_defaults;
	st_output.get_properties = smart_output_properties;
	st_output.get_total_bytes = smart_output_total_bytes_sent;
	st_output.get_connect_time_ms = smart_output_connect_time;
	st_output.get_dropped_frames = smart_output_dropped_frames;
	obs_register_output(&st_output);
}
