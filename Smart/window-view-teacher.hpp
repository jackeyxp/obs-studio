
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
	QRect               m_rcNoramlRect;                // 窗口的全屏前的矩形区域...
	string              m_strUTF8TextLabel;            // UTF8文字标签提示信息...
	bool                m_bIsDrawImage = false;        // 是否正在绘制讲师画面标志...
	bool                m_bTeacherOnLine = false;      // 右侧老师端数据源是否在线...
	obs_source_t     *  m_lpTextSource = nullptr;      // 默认文字数据源...
	obs_source_t     *  m_lpImageSource = nullptr;     // 默认图片数据源...
	obs_sceneitem_t  *  m_teacherSceneItem = nullptr;  // 老师端数据源...
	CStudentWindow   *  m_lpStudentWindow = nullptr;   // 主窗口对象...
};
