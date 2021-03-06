
#pragma once

#include "../common/server.h"
#include "../common/rtp.h"

#define DEF_CENTER_PORT             27027          // 默认中心服务器端口...
#define MAX_OPEN_FILE                2048          // 最大打开文件句柄数(个)...
#define MAX_EPOLL_SIZE               1024          // EPOLL队列最大值...
#define MAX_LISTEN_SIZE              1024          // 监听队列最大值...
#define CHECK_TIME_OUT                 10          // 超时检测周期 => 每隔10秒，检测一次超时...
#define APP_SLEEP_MS                  500          // 应用层休息时间(毫秒)...

using namespace std;

class CApp;
class CTCPRoom;
class CUdpServer;
typedef map<int, int>             GM_MapInt;        // RoomID => RoomID
typedef map<int, CTCPRoom *>      GM_MapRoom;       // RoomID => CTCPRoom *
typedef map<int, CUdpServer *>    GM_MapServer;     // socket => CUdpServer *
typedef map<uint32_t, rtp_lose_t> GM_MapLose;       // 定义检测到的丢包队列 => 序列号 : 丢包结构体...

// 获取全局的App对象...
CApp * GetApp();

//////////////////////////////////////////////////////////////////////////
// 以下是有关TCP中转服务器的相关变量和类型定义...
//////////////////////////////////////////////////////////////////////////
class CTCPClient;
typedef map<int, CTCPClient*>   GM_MapTCPConn;    // connfd     => CTCPClient*
typedef map<string, string>     GM_MapJson;       // key        => value => JSON map object...
