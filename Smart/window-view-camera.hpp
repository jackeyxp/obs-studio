
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
	QRect               m_rcNoramlRect;                // ���ڵ�ȫ��ǰ�ľ�������...
	int                 m_nMicInputChannel = 3;        // ��˷�Ƶ�����...
	bool                m_bIsDrawImage = false;        // �Ƿ����ڻ��ƻ����־...
	obs_source_t     *  m_lpMicInputSource = nullptr;  // ��˷���������Դ...
	obs_source_t     *  m_lpImageSource = nullptr;     // Ĭ��ͼƬ...
	obs_sceneitem_t  *  m_dshowCameraItem = nullptr;   // ��������ͷ...
	CStudentWindow   *  m_lpStudentWindow = nullptr;   // �����ڶ���...
};
