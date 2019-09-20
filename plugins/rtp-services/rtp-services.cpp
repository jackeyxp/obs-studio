
#include "OSThread.h"
#include "SocketUtils.h"
#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("rtp-services", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Windows rtp-services source/output";
}

void InitModule();
void UnInitModule();

void RegisterSmartOutput();
void RegisterSmartSource();

bool obs_module_load(void)
{
	// 初始化模块...
	InitModule();
	// 注册数据源和输出...
	RegisterSmartOutput();
	RegisterSmartSource();
	return true;
}

void obs_module_unload(void)
{
	UnInitModule();
}

void InitModule()
{
	// 初始化套接字...
	WORD	wsVersion = MAKEWORD(2, 2);
	WSADATA	wsData = { 0 };
	(void)::WSAStartup(wsVersion, &wsData);
	// 初始化网络、线程...
	OSThread::Initialize();
	SocketUtils::Initialize();
}

void UnInitModule()
{
	// 释放分配的系统资源...
	SocketUtils::UnInitialize();
	OSThread::UnInitialize();
}