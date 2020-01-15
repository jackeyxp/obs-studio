
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

// ����smart_source��Դ���÷����仯���¼�֪ͨ...
static void smart_source_update(void *data, obs_data_t *settings)
{
	smart_source_t * lpRtpSource = (smart_source_t*)data;
	bool bIsLiveOnLine = obs_data_get_bool(settings, "live_on");
	// ��ȡ����Դ�����������Ϣ...
	lpRtpSource->room_id = (int)obs_data_get_int(settings, "room_id");
	lpRtpSource->live_id = (int)obs_data_get_int(settings, "live_id");
	lpRtpSource->udp_addr = obs_data_get_string(settings, "udp_addr");
	lpRtpSource->udp_port = (int)obs_data_get_int(settings, "udp_port");
	lpRtpSource->tcp_socket = (int)obs_data_get_int(settings, "tcp_socket");
	lpRtpSource->client_type = (CLIENT_TYPE)obs_data_get_int(settings, "client_type");
	// ��������߳��Ѿ�������ֱ��ɾ��...
	if (lpRtpSource->recvThread != NULL) {
		delete lpRtpSource->recvThread;
		lpRtpSource->recvThread = NULL;
	}
	ASSERT(lpRtpSource->recvThread == NULL);
	// ��������ƹ�������Ϊ�ǻ״̬���ȴ��µĻ��浽��...
	obs_source_output_video(lpRtpSource->source, NULL);
	// �жϷ�����Ϣ�ʹ��ݲ����Ƿ���Ч����Чֱ�ӷ��أ�����״̬Ҳ������Ч�����...
	if (!bIsLiveOnLine || lpRtpSource->live_id <= 0 || lpRtpSource->room_id <= 0 || lpRtpSource->tcp_socket <= 0 ||
		lpRtpSource->udp_port <= 0 || lpRtpSource->udp_addr == NULL || lpRtpSource->client_type <= 0) {
		blog(LOG_INFO, "smart_source_update => Failed, OnLine: %d, ClientType: %d, RoomID: %d, LiveID: %d, TCPSock: %d, %s:%d",
			bIsLiveOnLine, lpRtpSource->client_type, lpRtpSource->room_id, lpRtpSource->live_id, lpRtpSource->tcp_socket,
			lpRtpSource->udp_addr, lpRtpSource->udp_port);
		return;
	}
	// ��������Դ�ն˵��ڲ�����...
	switch (lpRtpSource->client_type) {
	case kClientStudent: lpRtpSource->inner_name = ST_RECV_NAME; break;
	case kClientTeacher: lpRtpSource->inner_name = TH_RECV_NAME; break;
	}
	// ʹ����֤������Ч�������ؽ��������󣬲���ʼ�����������̶߳��󱣴浽����Դ����������...
	lpRtpSource->recvThread = new CSmartRecvThread(lpRtpSource->client_type, lpRtpSource->tcp_socket, lpRtpSource->room_id, lpRtpSource->live_id);
	lpRtpSource->recvThread->InitThread(lpRtpSource->source, lpRtpSource->udp_addr, lpRtpSource->udp_port);
	// �ò��Ų㲻Ҫ������֡���л��棬ֱ����첥�� => �������Ҫ��������Ч���Ͳ�����ʱ...
	obs_source_set_async_unbuffered(lpRtpSource->source, true);
	//obs_source_set_async_decoupled(lpRtpSource->source, true);
	// ��ӡ�ɹ����������߳���Ϣ => client_type | room_id | udp_addr | udp_port | live_id | tcp_socket
	blog(LOG_INFO, "smart_source_update => Success, OnLine: %d, ClientType: %s, RoomID: %d, LiveID: %d, TCPSock: %d, %s:%d",
		bIsLiveOnLine, lpRtpSource->inner_name, lpRtpSource->room_id, lpRtpSource->live_id, lpRtpSource->tcp_socket,
		lpRtpSource->udp_addr, lpRtpSource->udp_port);
}

static void *smart_source_create(obs_data_t *settings, obs_source_t *source)
{
	// ����smart����Դ�����������...
	smart_source_t * lpRtpSource = (smart_source_t*)bzalloc(sizeof(struct smart_source_t));
	// ���������߳�δ������־ => ���rtp�����������÷�ʽ...
	obs_data_set_bool(settings, "live_on", false);
	// �����ݹ�����obs��Դ���󱣴�����...
	lpRtpSource->source = source;
	// ������Դ�����������...
	return lpRtpSource;
}

static void smart_source_destroy(void *data)
{
	// ��������̱߳�����������Ҫ��ɾ��...
	smart_source_t * lpRtpSource = (smart_source_t*)data;
	if (lpRtpSource->recvThread != NULL) {
		delete lpRtpSource->recvThread;
		lpRtpSource->recvThread = NULL;
	}
	// �ͷ�rtp��Դ������...
	bfree(lpRtpSource);
}

static void smart_source_defaults(obs_data_t *defaults)
{
}

static obs_properties_t *smart_source_properties(void *unused)
{
	return NULL;
}

// �ϲ�ϵͳ���ݹ�������������...
static void smart_source_tick(void *data, float seconds)
{
	// �����߳������Ч��ֱ�ӷ���...
	smart_source_t * lpRtpSource = (smart_source_t*)data;
	obs_data_t * lpSettings = obs_source_get_settings(lpRtpSource->source);
	do {
		if (lpRtpSource->recvThread == NULL) {
			obs_data_set_bool(lpSettings, "live_on", false);
			break;
		}
		// ����Ѿ�������¼��ʱ��ɾ�������߳�...
		if (lpRtpSource->recvThread->IsLoginTimeout()) {
			delete lpRtpSource->recvThread;
			lpRtpSource->recvThread = NULL;
			// �����߳��Ѿ�ɾ���������������߱�־...
			obs_data_set_bool(lpSettings, "live_on", false);
			break;
		}
	} while (false);
	// ע�⣺��������ֶ��������ü������٣����򣬻�����ڴ�й© => obs_source_get_settings ���������ü���...
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
