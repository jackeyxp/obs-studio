
#pragma once

#include "qt-display.hpp"

class CStudentWindow;
class CViewTeacher : public OBSQTDisplay
{
	Q_OBJECT
public:
	CViewTeacher(QWidget *parent, Qt::WindowFlags flags = 0);
	~CViewTeacher();
public:
	bool  doInitTeacher();
	void  doRemoveDrawCallback();
private:
	void  initWindow();
	bool  doLoadNoTeacherSource();
private:
	static void doDrawTeacherPreview(void *data, uint32_t cx, uint32_t cy);
private:
	obs_source_t     *  m_lpDefaultSource = nullptr;   // 默认图片数据源...
	obs_sceneitem_t  *  m_teacherSceneItem = nullptr;  // 老师端数据源...
	CStudentWindow   *  m_lpStudentWindow = nullptr;   // 主窗口对象...
};
