
#pragma once

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <assert.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef ASSERT
#define ASSERT assert 
#endif // ASSERT

typedef unsigned char		UInt8;
typedef signed char			SInt8;
typedef unsigned short		UInt16;
typedef signed short		SInt16;
typedef unsigned long		UInt32;
typedef signed long			SInt32;
typedef LONGLONG			SInt64;
typedef ULONGLONG			UInt64;
typedef float				Float32;
typedef double				Float64;
typedef UInt16				Bool16;

#pragma warning(disable: 4786)

#include <map>
#include <string>

using namespace std;

#include <obs.h>
#include <util/platform.h>
#include <util/circlebuf.h>
#include <GMError.h>
#include <rtp.h>

#define TH_RECV_NAME       "[Teacher-Looker]"  // 老师观看端...
#define TH_SEND_NAME       "[Teacher-Pusher]"  // 老师推流端...
#define ST_RECV_NAME       "[Student-Looker]"  // 学生观看端...
#define ST_SEND_NAME       "[Student-Pusher]"  // 学生推流端...

#define LOCAL_ADDRESS_V4   "127.0.0.1"         // 本地地址定义-IPV4
#define LINGER_TIME         500                // SOCKET停止时数据链路层BUFF清空的最大延迟时间
#define DEF_MTU_SIZE        800                // 默认MTU分片大小(字节)...
#define LOG_MAX_SIZE        2048               // 单条日志最大长度(字节)...
#define MAX_BUFF_LEN        1024               // 最大报文长度(字节)...
#define MAX_SLEEP_MS		20				   // 最大休息时间(毫秒)
#define ADTS_HEADER_SIZE	7				   // AAC音频数据包头长度(字节)...
#define DEF_CIRCLE_SIZE		128	* 1024		   // 默认环形队列长度(字节)...
#define DEF_TIMEOUT_MS		5 * 1000		   // 默认超时时间(毫秒)...

#define MsgLogGM(nErr) blog(LOG_ERROR, "Error: %lu, %s(%d)", nErr, __FILE__, __LINE__)
