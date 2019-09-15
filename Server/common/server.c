
#include "server.h"
#include "bmem.h"
#include "rtp.h"

uint64_t os_gettime_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec);
}

bool os_sleepto_ns(uint64_t time_target)
{
	uint64_t current = os_gettime_ns();
	if (time_target < current)
		return false;

	time_target -= current;

	struct timespec req, remain;
	memset(&req, 0, sizeof(req));
	memset(&remain, 0, sizeof(remain));
	req.tv_sec = time_target/1000000000;
	req.tv_nsec = time_target%1000000000;

	while (nanosleep(&req, &remain)) {
		req = remain;
		memset(&remain, 0, sizeof(remain));
	}

	return true;
}

void os_sleep_ms(uint32_t duration)
{
	usleep(duration*1000);
}

static inline void add_ms_to_ts(struct timespec *ts, unsigned long milliseconds)
{
	ts->tv_sec += milliseconds/1000;
	ts->tv_nsec += (milliseconds%1000)*1000000;
	if (ts->tv_nsec > 1000000000) {
		ts->tv_sec += 1;
		ts->tv_nsec -= 1000000000;
	}
}

int os_sem_init(os_sem_t **sem, int value)
{
	sem_t new_sem;
	int ret = sem_init(&new_sem, 0, value);
	if (ret != 0)
		return ret;

	*sem = (os_sem_t*)bzalloc(sizeof(struct os_sem_data));
	(*sem)->sem = new_sem;
	return 0;
}

void os_sem_destroy(os_sem_t *sem)
{
	if (sem) {
		sem_destroy(&sem->sem);
		bfree(sem);
	}
}

int os_sem_post(os_sem_t *sem)
{
	if (!sem) return -1;
	return sem_post(&sem->sem);
}

int os_sem_wait(os_sem_t *sem)
{
	if (!sem) return -1;
	return sem_wait(&sem->sem);
}

int os_sem_timedwait(os_sem_t *sem, unsigned long milliseconds)
{
  if (!sem) return -1;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  add_ms_to_ts(&ts, milliseconds);
  return sem_timedwait(&sem->sem, &ts);
}

// 获取交互终端类型...
const char * get_tm_tag(int tmTag)
{
  switch(tmTag)
  {
    case TM_TAG_STUDENT: return "Student";
    case TM_TAG_TEACHER: return "Teacher";
    case TM_TAG_SERVER:  return "Server";
  }
  return "unknown";
}

// 获取交互终端身份...
const char * get_id_tag(int idTag)
{
  switch(idTag)
  {
    case ID_TAG_PUSHER: return "Pusher";
    case ID_TAG_LOOKER: return "Looker";
    case ID_TAG_SERVER: return "Server";
  }
  return "unknown";
}

// 获取用户类型...
const char * get_client_type(int inType)
{
  switch(inType)
  {
    case kClientPHP:       return "PHP";
    case kClientStudent:   return "Student";
    case kClientTeacher:   return "Teacher";
    case kClientUdpServer: return "UdpServer";
    case kClientScreen:    return "Screen";
  }
  return "unknown";
}

// 获取命令类型...
const char * get_command_name(int inCmd)
{
  switch(inCmd)
  {
    case kCmd_Smart_Login:          return "Smart_Login";
    case kCmd_Smart_OnLine:         return "Smart_OnLine";
    case kCmd_UdpServer_Login:      return "UdpServer_Login";
    case kCmd_UdpServer_OnLine:     return "UdpServer_OnLine";
    case kCmd_UdpServer_AddTeacher: return "UdpServer_AddTeacher";
    case kCmd_UdpServer_DelTeacher: return "UdpServer_DelTeacher";
    case kCmd_UdpServer_AddStudent: return "UdpServer_AddStudent";
    case kCmd_UdpServer_DelStudent: return "UdpServer_DelStudent";
    case kCmd_PHP_GetUdpServer:     return "PHP_GetUdpServer";
    case kCmd_PHP_GetAllServer:     return "PHP_GetAllServer";
    case kCmd_PHP_GetAllClient:     return "PHP_GetAllClient";
    case kCmd_PHP_GetRoomList:      return "PHP_GetRoomList";
    case kCmd_PHP_GetPlayerList:    return "PHP_GetPlayerList";
    case kCmd_PHP_Bind_Mini:        return "PHP_Bind_Mini";
    case kCmd_PHP_GetRoomFlow:      return "PHP_GetRoomFlow";
    case kCmd_Screen_Login:         return "Screen_Login";
    case kCmd_Screen_OnLine:        return "Screen_OnLine";
    case kCmd_Screen_Packet:        return "Screen_Packet";
    case kCmd_Screen_Finish:        return "Screen_Finish";
  }
  return "unknown";
}

void long2buff(int64_t n, char *buff)
{
	unsigned char *p;
	p = (unsigned char *)buff;
	*p++ = (n >> 56) & 0xFF;
	*p++ = (n >> 48) & 0xFF;
	*p++ = (n >> 40) & 0xFF;
	*p++ = (n >> 32) & 0xFF;
	*p++ = (n >> 24) & 0xFF;
	*p++ = (n >> 16) & 0xFF;
	*p++ = (n >> 8) & 0xFF;
	*p++ = n & 0xFF;
}

int64_t buff2long(const char *buff)
{
	unsigned char *p;
	p = (unsigned char *)buff;
	return  (((int64_t)(*p)) << 56) | \
		(((int64_t)(*(p+1))) << 48) |  \
		(((int64_t)(*(p+2))) << 40) |  \
		(((int64_t)(*(p+3))) << 32) |  \
		(((int64_t)(*(p+4))) << 24) |  \
		(((int64_t)(*(p+5))) << 16) |  \
		(((int64_t)(*(p+6))) << 8) | \
		((int64_t)(*(p+7)));
}
