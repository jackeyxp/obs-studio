
#pragma once

#include <rtp.h>
#include <assert.h>

#ifndef ASSERT
#define ASSERT assert 
#endif // ASSERT

#pragma warning(disable: 4786)

#include <map>
#include <string>
#include <functional>

using namespace std;

typedef	map<string, string>			GM_MapData;
typedef map<int, GM_MapData>		GM_MapNodeCamera;     // int  => ��ָ���ݿ�DBCameraID

enum CAMERA_TYPE {            // ѧ����ר������
	kCameraSoft		= 0,      // ��౾������ͷ
	kCameraTeacher	= 1,      // �Ҳ���ʦ����ͷ
	kCameraIPC		= 2,      // ���IPC����ͷ
};
