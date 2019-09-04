
#pragma once

#include <unistd.h>
#include <sys/socket.h>     /* basic socket definitions */
#include <netinet/tcp.h>    /* 2017.07.26 - by jackey */
#include <netinet/in.h>     /* sockaddr_in{} and other Internet defns */
#include <arpa/inet.h>      /* inet(3) functions */
#include <sys/epoll.h>      /* epoll function */
#include <sys/types.h>      /* basic system data types */
#include <sys/resource.h>   /* setrlimit */
#include <sys/time.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <fcntl.h>          /* nonblocking */
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#include <algorithm> 
#include <string>
#include <list>
#include <map>

#define SERVER_MAJOR_VER 2
#define SERVER_MINOR_VER 0
#define SERVER_PATCH_VER 1

#ifndef _T
#define _T(x)	x
#endif

#define _chSTR(x)	 _T(#x)
#define chSTR(x)	 _chSTR(x)

#define SERVER_VERSION  chSTR(SERVER_MAJOR_VER) _T(".") chSTR(SERVER_MINOR_VER) _T(".") chSTR(SERVER_PATCH_VER)

#ifndef uint8_t
typedef unsigned char uint8_t;
#endif

#ifndef uint16_t
typedef unsigned short uint16_t;
#endif

#ifndef uint32_t
typedef unsigned int uint32_t;
#endif

#ifndef os_sem_t
struct os_sem_data {
	sem_t sem;
};
typedef struct os_sem_data os_sem_t;
#endif

uint64_t  os_gettime_ns(void);
void      os_sleep_ms(uint32_t duration);
bool      os_sleepto_ns(uint64_t time_target);

int       os_sem_init(os_sem_t **sem, int value);
void      os_sem_destroy(os_sem_t *sem);
int       os_sem_post(os_sem_t *sem);
int       os_sem_wait(os_sem_t *sem);
int       os_sem_timedwait(os_sem_t *sem, unsigned long milliseconds);

const char * get_client_type(int inType);
const char * get_command_name(int inCmd);
const char * get_tm_tag(int tmTag);
const char * get_id_tag(int idTag);
const char * get_abs_path();

int64_t buff2long(const char *buff);
void long2buff(int64_t n, char *buff);

// 定义日志处理函数和宏 => debug 模式只打印不写日志文件...
bool do_trace(const char * inFile, int inLine, bool bIsDebug, const char *msg, ...);
#define log_trace(msg, ...) do_trace(__FILE__, __LINE__, false, msg, ##__VA_ARGS__)
#define log_debug(msg, ...) do_trace(__FILE__, __LINE__, true, msg, ##__VA_ARGS__)

///////////////////////////////////////////////////////////
// Only for PHP transmit server...
//////////////////////////////////////////////////////////
typedef struct {
	char  pkg_len[8];  // body length, not including header
	char  cmd;         // command code
	char  status;      // status code for response
} TrackerHeader;

#define ERR_OK          0
#define ERR_NO_ROOM     10001
#define ERR_NO_SERVER   10002
#define ERR_MODE_MATCH  10003
#define ERR_NO_PARAM    10004
#define ERR_NO_TERMINAL 10005
#define ERR_TYPE_MATCH  10006
#define ERR_TIME_MATCH  10007
#define ERR_HAS_TEACHER 10008
