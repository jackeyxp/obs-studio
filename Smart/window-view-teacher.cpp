
#include "window-view-teacher.hpp"
#include "display-helpers.hpp"
#include "window-student.h"
#include "qt-wrappers.hpp"
#include "platform.hpp"
#include "obs-app.hpp"

#include <QPainter>
#include <QResource>

CViewTeacher::CViewTeacher(QWidget *parent, Qt::WindowFlags flags)
	: OBSQTDisplay(parent, flags)
{
	m_lpStudentWindow = static_cast<CStudentWindow *>(parent);
	assert(m_lpStudentWindow != nullptr);
	//this->setMouseTracking(true);
	this->initWindow();
}

CViewTeacher::~CViewTeacher()
{
	// Ƶ��0 => ���ͷ�������������������Դ����...
	if (m_teacherSceneItem != nullptr) {
		obs_sceneitem_remove(m_teacherSceneItem);
		m_teacherSceneItem = nullptr;
	}
	// �ͷ�Ĭ�ϵ�����ͷͼƬ����Դ...
	if (m_lpImageSource != nullptr) {
		obs_source_release(m_lpImageSource);
		m_lpImageSource = nullptr;
	}
	if (m_lpTextSource != nullptr) {
		obs_source_release(m_lpTextSource);
		m_lpTextSource = nullptr;
	}
}

void CViewTeacher::initWindow()
{
}

void CViewTeacher::doRemoveDrawCallback()
{
	obs_display_remove_draw_callback(this->GetDisplay(), CViewTeacher::doDrawTeacherPreview, this);
}

void CViewTeacher::doUpdatedSmartSource(OBSSource source)
{
	obs_source_t * lpTeacherSource = obs_sceneitem_get_source(m_teacherSceneItem);
	if (lpTeacherSource != source)
		return;
	obs_data_t * settings = obs_source_get_settings(lpTeacherSource);
	const char * lpLabel = obs_data_get_string(settings, "notice");
	uint32_t nCalcPTS = (uint32_t)obs_data_get_int(settings, "pts");
	bool bIsDrawImage = obs_data_get_bool(settings, "render");
	obs_data_release(settings);
	QString strNotice = QTStr(lpLabel);
	if (strNotice.size() <= 0)
		return;
	// ����Ƕ�֡����Ϣ����Ҫ����ʱ�������...
	if (astrcmpi(lpLabel, "Render.Window.DropVideoFrame") == 0) {
		strNotice = QString(QTStr(lpLabel)).arg(nCalcPTS);
	}
	// �����Ƿ��ܹ�����ͼƬ�ı�־...
	m_bIsDrawImage = bIsDrawImage;
	// ��QString�ַ���ת����UTF8��ʽ...
	m_strUTF8TextLabel = strNotice.toUtf8().toStdString();
	// �����º��UTF8�ַ������µ���ʾ��Ϣ����Դ����...
	const char * lpTxtNotice = m_strUTF8TextLabel.c_str();
	obs_data_t * lpTxtSettings = obs_source_get_settings(m_lpTextSource);
	// ����������Ϣ��ͨ����ǰ����Դ�����ýṹ...
	obs_data_set_string(lpTxtSettings, "text", lpTxtNotice);
	// ���µ���Դ����Ӧ�õ���ǰtext��Դ������...
	obs_source_update(m_lpTextSource, lpTxtSettings);
	// ע�⣺��������ֶ��������ü������٣����򣬻�����ڴ�й©...
	obs_data_release(lpTxtSettings);
}

void CViewTeacher::onUpdateTeacherLabel(const char * lpLabelName)
{
	string strNewUTF8Label = Str(lpLabelName);
	if (strNewUTF8Label.size() <= 0 || m_lpTextSource == nullptr)
		return;
	m_strUTF8TextLabel = strNewUTF8Label;
	const char * lpStrNotice = m_strUTF8TextLabel.c_str();
	obs_data_t * settings = obs_source_get_settings(m_lpTextSource);
	// ����������Ϣ��ͨ����ǰ����Դ�����ýṹ...
	obs_data_set_string(settings, "text", lpStrNotice);
	// ���µ���Դ����Ӧ�õ���ǰtext��Դ������...
	obs_source_update(m_lpTextSource, settings);
	// ע�⣺��������ֶ��������ü������٣����򣬻�����ڴ�й©...
	obs_data_release(settings);
}

bool CViewTeacher::doLoadNoTeacherLabel()
{
	m_strUTF8TextLabel = Str("Render.Window.TeacherNotice");
	const char *lpStrNotice = m_strUTF8TextLabel.c_str();
	const char *text_source_id = "text_gdiplus";
	obs_data_t *settings = obs_data_create();
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", "Arial");
	obs_data_set_int(font, "flags", 1); // Bold text
	obs_data_set_int(font, "size", 40);
	obs_data_set_obj(settings, "font", font);
	obs_data_set_string(settings, "text", lpStrNotice);
	obs_data_set_bool(settings, "outline", false);
	// �ı�����Դ����ɫ˳������ڲ�ת�� => rgb_to_bgr()
	obs_data_set_int(settings, "color", QColor(0, 255, 255).rgb());

	const char * lpStrName = obs_source_get_display_name(text_source_id);
	obs_source_t * txtSource = obs_source_create_private(text_source_id, lpStrName, settings);

	obs_data_release(font);
	obs_data_release(settings);

	m_lpTextSource = txtSource;

	return true;
}

// ����Ĭ�ϵ���ʦ��ͼƬ����Դ����...
bool CViewTeacher::doLoadNoTeacherImage()
{
	string strNoTeacherPath;
	// ͨ���ⲿ��Դ�ķ�ʽ��ȡĬ������ͷ��ȫ·�� => ������qrc��Դ...
	if (!GetDataFilePath("images/class.png", strNoTeacherPath))
		return false;
	assert(strNoTeacherPath.size() > 0);
	obs_data_t * settings = obs_data_create();
	obs_data_set_string(settings, "file", strNoTeacherPath.c_str());
	obs_source_t * source = obs_source_create_private("image_source", NULL, settings);
	obs_data_release(settings);
	m_lpImageSource = source;
	return true;
}

void CViewTeacher::doCreateTeacherSource()
{
	// ��ʦ������Դ�Ѿ�������ֱ�ӷ���...
	if (m_teacherSceneItem != nullptr)
		return;
	// ����һ���µĽ�ʦ������Դ���ἤ��AddSceneItem�¼�...
	obs_scene_t * lpObsScene = m_lpStudentWindow->GetObsScene();
	const char * lpSmartID = App()->InteractSmartSource();
	const char * lpSmartName = obs_source_get_display_name(lpSmartID);
	obs_source_t * lpTeacherSource = obs_source_create(lpSmartID, lpSmartName, NULL, nullptr);
	// ���û�л�ȡ����Ĭ�ϵ�Smart����Դ => ֱ�ӷ��� => ע�⴦����̱�־...
	if (lpObsScene == nullptr || lpSmartID == nullptr && lpTeacherSource == nullptr)
		return;
	// ��ʼ��Ĭ������Դ...
	obs_enter_graphics();
	vec2 vPos = { 120.0f, 120.0f };
	// �����λ�����������MainTexure��λ�� => �����趨Ϊ0��λ��...
	obs_sceneitem_t * lpTeacherItem = obs_scene_add(lpObsScene, lpTeacherSource);
	// ע�⣺����ἤ���ź��¼�AddSceneItem => ��Ҫ�������ü�����...
	obs_source_release(lpTeacherSource);
	// ��������ͷ�ɼ����Լ��趨��ʼλ��...
	// ע�⣺0��λ�õ�����Դ����Ҫ�������������Դ...
	obs_sceneitem_set_visible(lpTeacherItem, true);
	obs_sceneitem_set_pos(lpTeacherItem, &vPos);
	obs_leave_graphics();
	// ������Ҫ������ʦ������Դ����ƵΪֻ���Ų����״̬��Ĭ����NONE���Ȳ����Ҳ�����ز���...
	obs_source_set_monitoring_type(lpTeacherSource, OBS_MONITORING_TYPE_MONITOR_ONLY);
	blog(LOG_INFO, "User changed audio monitoring for source '%s' to: %s", obs_source_get_name(lpTeacherSource), "monitor only");
	// ���潲ʦ�˳�������Դ���� => û�в��ü���AddSceneItem�¼��ķ�ʽ...
	m_teacherSceneItem = lpTeacherItem;
}

// ��ʼ������ͷ���� => ��������Դ...
bool CViewTeacher::doInitTeacher()
{
	// ���ȼ���Ĭ�ϵ��޽�ʦͼƬ����Դ...
	this->doLoadNoTeacherImage();
	this->doLoadNoTeacherLabel();
	// ������ʦ������Դ����...
	this->doCreateTeacherSource();
	// ������ʾ�ص��ӿں���...
	auto addTeacherDrawCallback = [this](OBSQTDisplay *window) {
		// ע�⣺��������˱����޸ģ�������ɱ�����ɫ���ҵ�����...
		this->SetDisplayBackgroundColor(QColor(46, 48, 55));
		// ������ǰ��Ļ��ƺ����ӿ� => �� show() ��ʱ����ܵ�������...
		obs_display_add_draw_callback(window->GetDisplay(), CViewTeacher::doDrawTeacherPreview, this);
	};
	// ���� DisplayCreated ��Ӧ����ʾ�ص������ӿ�...
	connect(this, &OBSQTDisplay::DisplayCreated, addTeacherDrawCallback);
	return true;
}

void CViewTeacher::doRenderTeacherSource(uint32_t cx, uint32_t cy)
{
	obs_source_t * lpTeacherSource = obs_sceneitem_get_source(m_teacherSceneItem);
	if (!m_bIsDrawImage || lpTeacherSource == nullptr)
		return;
	int x, y;
	int newCX, newCY;
	float scale = 1.0f;
	uint32_t sourceCX = max(obs_source_get_width(lpTeacherSource), 1u);
	uint32_t sourceCY = max(obs_source_get_height(lpTeacherSource), 1u);
	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(lpTeacherSource);
}

void CViewTeacher::doRenderNoTeacherImage(uint32_t cx, uint32_t cy)
{
	obs_source_t * lpTeacherSource = m_lpImageSource;
	if (m_bIsDrawImage || lpTeacherSource == nullptr)
		return;
	int x, y;
	int newCX, newCY;
	float scale = 1.0f;
	uint32_t sourceCX = max(obs_source_get_width(lpTeacherSource), 1u);
	uint32_t sourceCY = max(obs_source_get_height(lpTeacherSource), 1u);
	GetCenterPosFromFixedScale(sourceCX, sourceCY, cx, cy, x, y, scale);
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));
	gs_set_viewport(x, y - 120, newCX, newCY);
	obs_source_video_render(lpTeacherSource);
}

void CViewTeacher::doRenderNoTeacherLabel(uint32_t cx, uint32_t cy)
{
	obs_source_t * lpTeacherSource = m_lpTextSource;
	if (m_bIsDrawImage || lpTeacherSource == nullptr)
		return;
	int x, y;
	int newCX, newCY;
	float scale = 1.0f;
	uint32_t sourceCX = max(obs_source_get_width(lpTeacherSource), 1u);
	uint32_t sourceCY = max(obs_source_get_height(lpTeacherSource), 1u);
	GetCenterPosFromFixedScale(sourceCX, sourceCY, cx, cy, x, y, scale);
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(lpTeacherSource);
}

void CViewTeacher::doRenderAllSource(uint32_t cx, uint32_t cy)
{
	gs_viewport_push();
	gs_projection_push();

	this->doRenderTeacherSource(cx, cy);
	this->doRenderNoTeacherImage(cx, cy);
	this->doRenderNoTeacherLabel(cx, cy);

	gs_projection_pop();
	gs_viewport_pop();
}

void CViewTeacher::doDrawTeacherPreview(void *data, uint32_t cx, uint32_t cy)
{
	CViewTeacher * window = static_cast<CViewTeacher *>(data);
	window->doRenderAllSource(cx, cy);
}

void CViewTeacher::onRemoteLiveOnLine(int nLiveID, bool bIsLiveOnLine)
{
	// ����Ҳིʦ������Դ��Ч��ֱ�ӷ���...
	obs_source_t * lpTeacherSource = obs_sceneitem_get_source(m_teacherSceneItem);
	if (lpTeacherSource == nullptr || m_teacherSceneItem == nullptr)
		return;
	// �������ڳ������ӵ����ֱ�ǩ��Ϣ...
	this->onUpdateTeacherLabel(bIsLiveOnLine ? "Render.Window.ConnectServer" : "Render.Window.TeacherNotice");
	// �������߻������ߣ���Ҫ������ʾ������ʾ��Ϣ...
	m_bIsDrawImage = false;
	// ������ʦ������Դ�Ƿ����ߣ��Ա���ʾʹ��...
	m_bTeacherOnLine = bIsLiveOnLine;
	// ���Դ����Ҳ���ʦ�˲��Ż������...
	obs_data_t * lpSettings = obs_source_get_settings(lpTeacherSource);
	int nRoomID = atoi(App()->GetRoomIDStr().c_str());
	obs_data_set_int(lpSettings, "room_id", nRoomID);
	obs_data_set_int(lpSettings, "live_id", nLiveID);
	obs_data_set_bool(lpSettings, "live_on", bIsLiveOnLine);
	obs_data_set_int(lpSettings, "udp_port", App()->GetUdpPort());
	obs_data_set_string(lpSettings, "udp_addr", App()->GetUdpAddr().c_str());
	obs_data_set_int(lpSettings, "tcp_socket", App()->GetRemoteTcpSockFD());
	obs_data_set_int(lpSettings, "client_type", App()->GetClientType());
	// ���µ���Դ����Ӧ�õ���ǰsmart_source��Դ������...
	obs_source_update(lpTeacherSource, lpSettings);
	// ע�⣺��������ֶ��������ü������٣����򣬻�����ڴ�й©...
	obs_data_release(lpSettings);
}