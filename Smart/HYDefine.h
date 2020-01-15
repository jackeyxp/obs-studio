
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
typedef map<int, GM_MapData>		GM_MapNodeCamera;     // int  => 是指数据库DBCameraID

enum CAMERA_TYPE {            // 学生端专用类型
	kCameraSoft		= 0,      // 左侧本地摄像头
	kCameraTeacher	= 1,      // 右侧老师摄像头
	kCameraIPC		= 2,      // 左侧IPC摄像头
};
