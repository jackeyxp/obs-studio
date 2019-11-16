
#pragma once

#include "qt-display.hpp"

class CStudentWindow;
class CViewCamera : public OBSQTDisplay
{
	Q_OBJECT
public:
	CViewCamera(QWidget *parent, Qt::WindowFlags flags = 0);
	~CViewCamera();
public:
	bool  doInitCamera();
	void  doRemoveDrawCallback();
public:
	uint32_t  doGetCameraWidth();
	uint32_t  doGetCameraHeight();
private:
	void  initWindow();
	void  onFullScreenAction();
	bool  doLoadNoDShowImage();
	void  doCreateDShowCamera();
	void  doCreateDShowMicphone();
	void  doRenderAllSource(uint32_t cx, uint32_t cy);
	void  doRenderDShowCamera(uint32_t cx, uint32_t cy);
	void  doRenderNoDShowImage(uint32_t cx, uint32_t cy);
	void  doResetAudioDevice(const char *sourceId, const char *deviceId, const char *deviceDesc, int channel);
private:
	static void doDrawDShowPreview(void *data, uint32_t cx, uint32_t cy);
	static bool doHasAudioDevices(const char *source_id);
protected:
	void  closeEvent(QCloseEvent *event) override;
	void  keyPressEvent(QKeyEvent *event) override;
	void  mouseDoubleClickEvent(QMouseEvent *event) override;
private:
	QRect               m_rcNoramlRect;                // 窗口的全屏前的矩形区域...
	int                 m_nMicInputChannel = 3;        // 麦克风频道编号...
	bool                m_bIsDrawImage = false;        // 是否正在绘制画面标志...
	obs_source_t     *  m_lpMicInputSource = nullptr;  // 麦克风输入数据源...
	obs_source_t     *  m_lpImageSource = nullptr;     // 默认图片...
	obs_sceneitem_t  *  m_dshowCameraItem = nullptr;   // 本地摄像头...
	CStudentWindow   *  m_lpStudentWindow = nullptr;   // 主窗口对象...
};
