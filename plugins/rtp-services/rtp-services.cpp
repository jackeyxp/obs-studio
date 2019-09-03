
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

//void RegisterTeacherSource();
void RegisterTeacherOutput();
//void RegisterStudentSource();
//void RegisterStudentOutput();

bool obs_module_load(void)
{
	// ��ʼ��ģ��...
	InitModule();
	// ע������Դ�����...
	//RegisterTeacherSource();
	RegisterTeacherOutput();
	//RegisterStudentSource();
	//RegisterStudentOutput();
	return true;
}

void obs_module_unload(void)
{
	UnInitModule();
}

void InitModule()
{
	// ��ʼ���׽���...
	WORD	wsVersion = MAKEWORD(2, 2);
	WSADATA	wsData = { 0 };
	(void)::WSAStartup(wsVersion, &wsData);
	// ��ʼ�����硢�߳�...
	OSThread::Initialize();
	SocketUtils::Initialize();
}

void UnInitModule()
{
	// �ͷŷ����ϵͳ��Դ...
	SocketUtils::UnInitialize();
	OSThread::UnInitialize();
}