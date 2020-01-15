
#include <obs-module.h>
#include "smart-recv-thread.h"

struct smart_source_t {
	obs_source_t   * source;
	int              room_id;
	int              live_id;
	const char     * udp_addr;
	int              udp_port;
	int              tcp_socket;
	CLIENT_TYPE      client_type;
	const char     * inner_name;
	CSmartRecvThread * recvThread;
};

static const char *smart_source_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("SmartSource");
}

// 处理smart_source资源配置发生变化的事件通知...
static void smart_source_update(void *data, obs_data_t *settings)
{
	smart_source_t * lpRtpSource = (smart_source_t*)data;
	bool bIsLiveOnLine = obs_data_get_bool(settings, "live_on");
	// 获取数据源里面的配置信息...
	lpRtpSource->room_id = (int)obs_data_get_int(settings, "room_id");
	lpRtpSource->live_id = (int)obs_data_get_int(settings, "live_id");
	lpRtpSource->udp_addr = obs_data_get_string(settings, "udp_addr");
	lpRtpSource->udp_port = (int)obs_data_get_int(settings, "udp_port");
	lpRtpSource->tcp_socket = (int)obs_data_get_int(settings, "tcp_socket");
	lpRtpSource->client_type = (CLIENT_TYPE)obs_data_get_int(settings, "client_type");
	// 如果拉流线程已经创建，直接删除...
	if (lpRtpSource->recvThread != NULL) {
		delete lpRtpSource->recvThread;
		lpRtpSource->recvThread = NULL;
	}
	ASSERT(lpRtpSource->recvThread == NULL);
	// 将画面绘制过程设置为非活动状态，等待新的画面到达...
	obs_source_output_video(lpRtpSource->source, NULL);
	// 判断房间信息和传递参数是否有效，无效直接返回，离线状态也属于无效的情况...
	if (!bIsLiveOnLine || lpRtpSource->live_id <= 0 || lpRtpSource->room_id <= 0 || lpRtpSource->tcp_socket <= 0 ||
		lpRtpSource->udp_port <= 0 || lpRtpSource->udp_addr == NULL || lpRtpSource->client_type <= 0) {
		blog(LOG_INFO, "smart_source_update => Failed, OnLine: %d, ClientType: %d, RoomID: %d, LiveID: %d, TCPSock: %d, %s:%d",
			bIsLiveOnLine, lpRtpSource->client_type, lpRtpSource->room_id, lpRtpSource->live_id, lpRtpSource->tcp_socket,
			lpRtpSource->udp_addr, lpRtpSource->udp_port);
		return;
	}
	// 保存数据源终端的内部名称...
	switch (lpRtpSource->client_type) {
	case kClientStudent: lpRtpSource->inner_name = ST_RECV_NAME; break;
	case kClientTeacher: lpRtpSource->inner_name = TH_RECV_NAME; break;
	}
	// 使用验证过的有效参数，重建拉流对象，并初始化，将接收线程对象保存到数据源管理器当中...
	lpRtpSource->recvThread = new CSmartRecvThread(lpRtpSource->client_type, lpRtpSource->tcp_socket, lpRtpSource->room_id, lpRtpSource->live_id);
	lpRtpSource->recvThread->InitThread(lpRtpSource->source, lpRtpSource->udp_addr, lpRtpSource->udp_port);
	// 让播放层不要对数据帧进行缓存，直接最快播放 => 这个很重要，可以有效降低播放延时...
	obs_source_set_async_unbuffered(lpRtpSource->source, true);
	//obs_source_set_async_decoupled(lpRtpSource->source, true);
	// 打印成功创建接收线程信息 => client_type | room_id | udp_addr | udp_port | live_id | tcp_socket
	blog(LOG_INFO, "smart_source_update => Success, OnLine: %d, ClientType: %s, RoomID: %d, LiveID: %d, TCPSock: %d, %s:%d",
		bIsLiveOnLine, lpRtpSource->inner_name, lpRtpSource->room_id, lpRtpSource->live_id, lpRtpSource->tcp_socket,
		lpRtpSource->udp_addr, lpRtpSource->udp_port);
}

static void *smart_source_create(obs_data_t *settings, obs_source_t *source)
{
	// 创建smart数据源变量管理对象...
	smart_source_t * lpRtpSource = (smart_source_t*)bzalloc(sizeof(struct smart_source_t));
	// 设置拉流线程未创建标志 => 多个rtp，必须用配置方式...
	obs_data_set_bool(settings, "live_on", false);
	// 将传递过来的obs资源对象保存起来...
	lpRtpSource->source = source;
	// 返回资源变量管理对象...
	return lpRtpSource;
}

static void smart_source_destroy(void *data)
{
	// 如果接收线程被创建过，需要被删除...
	smart_source_t * lpRtpSource = (smart_source_t*)data;
	if (lpRtpSource->recvThread != NULL) {
		delete lpRtpSource->recvThread;
		lpRtpSource->recvThread = NULL;
	}
	// 释放rtp资源管理器...
	bfree(lpRtpSource);
}

static void smart_source_defaults(obs_data_t *defaults)
{
}

static obs_properties_t *smart_source_properties(void *unused)
{
	return NULL;
}

// 上层系统传递过来的心跳过程...
static void smart_source_tick(void *data, float seconds)
{
	// 接收线程如果无效，直接返回...
	smart_source_t * lpRtpSource = (smart_source_t*)data;
	obs_data_t * lpSettings = obs_source_get_settings(lpRtpSource->source);
	do {
		if (lpRtpSource->recvThread == NULL) {
			obs_data_set_bool(lpSettings, "live_on", false);
			break;
		}
		// 如果已经发生登录超时，删除接收线程...
		if (lpRtpSource->recvThread->IsLoginTimeout()) {
			delete lpRtpSource->recvThread;
			lpRtpSource->recvThread = NULL;
			// 拉流线程已经删除，设置拉流离线标志...
			obs_data_set_bool(lpSettings, "live_on", false);
			break;
		}
	} while (false);
	// 注意：这里必须手动进行引用计数减少，否则，会造成内存泄漏 => obs_source_get_settings 会增加引用计数...
	obs_data_release(lpSettings);
}

static void smart_source_activate(void *data)
{
}

static void smart_source_deactivate(void *data)
{
}

void RegisterSmartSource()
{
	obs_source_info st_source = {};
	st_source.id = "smart_source";
	st_source.type = OBS_SOURCE_TYPE_INPUT;
	st_source.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
	st_source.get_name = smart_source_getname;
	st_source.create = smart_source_create;
	st_source.update = smart_source_update;
	st_source.destroy = smart_source_destroy;
	st_source.get_defaults = smart_source_defaults;
	st_source.get_properties = smart_source_properties;
	st_source.activate = smart_source_activate;
	st_source.deactivate = smart_source_deactivate;
	st_source.video_tick = smart_source_tick;
	obs_register_source(&st_source);
}
