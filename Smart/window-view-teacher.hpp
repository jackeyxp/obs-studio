
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
	obs_source_t     *  m_lpDefaultSource = nullptr;   // Ĭ��ͼƬ����Դ...
	obs_sceneitem_t  *  m_teacherSceneItem = nullptr;  // ��ʦ������Դ...
	CStudentWindow   *  m_lpStudentWindow = nullptr;   // �����ڶ���...
};
