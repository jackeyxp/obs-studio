
#pragma once

#include "qt-display.hpp"

using namespace std;

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
	void  doUpdatedSmartSource(OBSSource source);
	void  onRemoteLiveOnLine(int nLiveID, bool bIsLiveOnLine);
public:
	bool  IsDrawImage() { return m_bIsDrawImage; }
private:
	void  initWindow();
	void  onFullScreenAction();
	void  doCreateTeacherSource();
	bool  doLoadNoTeacherImage();
	bool  doLoadNoTeacherLabel();
	void  doRenderAllSource(uint32_t cx, uint32_t cy);
	void  doRenderTeacherSource(uint32_t cx, uint32_t cy);
	void  doRenderNoTeacherImage(uint32_t cx, uint32_t cy);
	void  doRenderNoTeacherLabel(uint32_t cx, uint32_t cy);
	void  onUpdateTeacherLabel(const char * lpLabelName);
private:
	static void doDrawTeacherPreview(void *data, uint32_t cx, uint32_t cy);
protected:
	void  closeEvent(QCloseEvent *event) override;
	void  keyPressEvent(QKeyEvent *event) override;
	void  mouseDoubleClickEvent(QMouseEvent *event) override;
private:
	QRect               m_rcNoramlRect;                // ���ڵ�ȫ��ǰ�ľ�������...
	string              m_strUTF8TextLabel;            // UTF8���ֱ�ǩ��ʾ��Ϣ...
	bool                m_bIsDrawImage = false;        // �Ƿ����ڻ��ƽ�ʦ�����־...
	bool                m_bTeacherOnLine = false;      // �Ҳ���ʦ������Դ�Ƿ�����...
	obs_source_t     *  m_lpTextSource = nullptr;      // Ĭ����������Դ...
	obs_source_t     *  m_lpImageSource = nullptr;     // Ĭ��ͼƬ����Դ...
	obs_sceneitem_t  *  m_teacherSceneItem = nullptr;  // ��ʦ������Դ...
	CStudentWindow   *  m_lpStudentWindow = nullptr;   // �����ڶ���...
};
