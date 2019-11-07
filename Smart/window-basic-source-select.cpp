/******************************************************************************
    Copyright (C) 2014 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <QMessageBox>
#include "window-basic-main.hpp"
#include "window-basic-source-select.hpp"
#include "qt-wrappers.hpp"
#include "obs-app.hpp"

struct AddSourceData {
	obs_source_t *source;
	bool visible;
};

bool OBSBasicSourceSelect::EnumSources(void *data, obs_source_t *source)
{
	OBSBasicSourceSelect *window = static_cast<OBSBasicSourceSelect*>(data);
	const char *name = obs_source_get_name(source);
	const char *id = obs_source_get_id(source);

	// 如果数据源已经存在，显示在列表框中 => 不是学生屏幕...
	if (strcmp(id, window->id) == 0 && !window->m_bIsScreen) {
		window->ui->sourceList->addItem(QT_UTF8(name));
	}
	// 如果数据源是麦克风数据源 => 设置标志...
	if (astrcmpi(id, App()->InputAudioSource()) == 0) {
		window->m_bHasMicSource = true;
	}
	// 如果当前添加的是学生屏幕分享，只能添加一个...
	if (astrcmpi(id, "slideshow") == 0 && window->m_bIsScreen) {
		obs_data_t * settings = obs_source_get_settings(source);
		if (obs_data_get_bool(settings, "screen_slide")) {
			window->m_bHasScreenSource = true;
		}
		obs_data_release(settings);
	}
	return true;
}

bool OBSBasicSourceSelect::EnumGroups(void *data, obs_source_t *source)
{
	OBSBasicSourceSelect *window =
		static_cast<OBSBasicSourceSelect *>(data);
	const char *name = obs_source_get_name(source);
	const char *id = obs_source_get_id(source);

	if (strcmp(id, window->id) == 0) {
		OBSBasic *main =
			reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
		OBSScene scene = main->GetCurrentScene();

		obs_sceneitem_t *existing = obs_scene_get_group(scene, name);
		if (!existing)
			window->ui->sourceList->addItem(QT_UTF8(name));
	}

	return true;
}

void OBSBasicSourceSelect::OBSSourceAdded(void *data, calldata_t *calldata)
{
	OBSBasicSourceSelect *window =
		static_cast<OBSBasicSourceSelect *>(data);
	obs_source_t *source = (obs_source_t *)calldata_ptr(calldata, "source");

	QMetaObject::invokeMethod(window, "SourceAdded",
				  Q_ARG(OBSSource, source));
}

void OBSBasicSourceSelect::OBSSourceRemoved(void *data, calldata_t *calldata)
{
	OBSBasicSourceSelect *window =
		static_cast<OBSBasicSourceSelect *>(data);
	obs_source_t *source = (obs_source_t *)calldata_ptr(calldata, "source");

	QMetaObject::invokeMethod(window, "SourceRemoved",
				  Q_ARG(OBSSource, source));
}

void OBSBasicSourceSelect::SourceAdded(OBSSource source)
{
	const char *name = obs_source_get_name(source);
	const char *sourceId = obs_source_get_id(source);

	if (strcmp(sourceId, id) != 0)
		return;

	ui->sourceList->addItem(name);
}

void OBSBasicSourceSelect::SourceRemoved(OBSSource source)
{
	const char *name = obs_source_get_name(source);
	const char *sourceId = obs_source_get_id(source);

	if (strcmp(sourceId, id) != 0)
		return;

	QList<QListWidgetItem *> items =
		ui->sourceList->findItems(name, Qt::MatchFixedString);

	if (!items.count())
		return;

	delete items[0];
}

static void AddSource(void *_data, obs_scene_t *scene)
{
	AddSourceData *data = (AddSourceData *)_data;
	obs_sceneitem_t *sceneitem;

	sceneitem = obs_scene_add(scene, data->source);
	obs_sceneitem_set_visible(sceneitem, data->visible);

	// 设置非0点位置，避免混乱...
	vec2 vPos = { 0 };
	vec2_set(&vPos, 20.0f, 20.0f);
	obs_sceneitem_set_pos(sceneitem, &vPos);

	// 调用主窗口接口 => 只对当前场景资源进行位置重排，两行（1行1列，1行5列）
	OBSBasic *main = reinterpret_cast<OBSBasic*>(App()->GetMainWindow());
	main->doSceneItemLayout(sceneitem);

	// 创建学生端麦克风按钮并重排所有麦克风按钮...
	main->doBuildStudentBtnMic(sceneitem);

	// 如果新数据源是视频捕捉设备，在混音中隐藏音频数据源...
	main->doHideDShowAudioMixer(sceneitem);
}

static char *get_new_source_name(const char *name)
{
	struct dstr new_name = {0};
	int inc = 0;

	dstr_copy(&new_name, name);

	for (;;) {
		obs_source_t *existing_source =
			obs_get_source_by_name(new_name.array);
		if (!existing_source)
			break;

		obs_source_release(existing_source);

		dstr_printf(&new_name, "%s %d", name, ++inc + 1);
	}

	return new_name.array;
}

static void AddExisting(const char *name, bool visible, bool duplicate)
{
	OBSBasic *main = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
	OBSScene scene = main->GetCurrentScene();
	if (!scene)
		return;

	obs_source_t *source = obs_get_source_by_name(name);
	if (source) {
		if (duplicate) {
			obs_source_t *from = source;
			char *new_name = get_new_source_name(name);
			source = obs_source_duplicate(from, new_name, false);
			bfree(new_name);
			obs_source_release(from);

			if (!source)
				return;
		}

		AddSourceData data;
		data.source = source;
		data.visible = visible;

		obs_enter_graphics();
		obs_scene_atomic_update(scene, AddSource, &data);
		obs_leave_graphics();

		obs_source_release(source);
	}
}

bool AddNew(QWidget *parent, const char *id, const char *name,
	    const bool visible, OBSSource &newSource)
{
	OBSBasic *main = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
	OBSScene scene = main->GetCurrentScene();
	bool success = false;
	if (!scene)
		return false;

	obs_source_t *source = obs_get_source_by_name(name);
	if (source) {
		OBSMessageBox::information(parent, QTStr("NameExists.Title"),
					   QTStr("NameExists.Text"));

	} else {
		source = obs_source_create(id, name, NULL, nullptr);

		if (source) {
			AddSourceData data;
			data.source = source;
			data.visible = visible;

			obs_enter_graphics();
			obs_scene_atomic_update(scene, AddSource, &data);
			obs_leave_graphics();

			newSource = source;

			/* set monitoring if source monitors by default */
			uint32_t flags = obs_source_get_output_flags(source);
			if ((flags & OBS_SOURCE_MONITOR_BY_DEFAULT) != 0) {
				obs_source_set_monitoring_type(
					source,
					OBS_MONITORING_TYPE_MONITOR_ONLY);
			}

			success = true;
		}
	}

	obs_source_release(source);
	return success;
}

obs_source_t * OBSBasicSourceSelect::AddNewSmartSource(const char *name)
{
	const char *id = App()->InteractSmartSource();
	obs_source_t *source = obs_source_create(id, name, NULL, nullptr);
	OBSBasic *main = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
	OBSScene scene = main->GetCurrentScene();

	if (source != NULL) {
		AddSourceData data;
		data.source = source;
		data.visible = true;

		obs_enter_graphics();
		obs_scene_atomic_update(scene, AddSource, &data);
		obs_leave_graphics();

		// 新添加资源是互动教室 => 需要监视并输出，开启本地监视...
		// 轨道1 => 输出给直播使用，始终屏蔽互动教室的声音...
		// 轨道2 => 输出给录像使用，当互动教室处于焦点状态时录制声音...
		obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
		blog(LOG_INFO, "User changed audio monitoring for source '%s' to: %s", obs_source_get_name(source), "monitor and output");
	}
	obs_source_release(source);
	return source;
}

void OBSBasicSourceSelect::doSaveScreenPath(obs_data_t * settings)
{
	char path[512] = { 0 };
	if (GetConfigPath(path, sizeof(path), "obs-smart/screen") <= 0)
		return;
	QString strQScreenPath = QString("%1").arg(path);
	obs_data_array *array = obs_data_array_create();
	obs_data_t *arrayItem = obs_data_create();
	obs_data_set_string(arrayItem, "value", QT_TO_UTF8(strQScreenPath));
	obs_data_set_bool(arrayItem, "selected", true);
	obs_data_set_bool(arrayItem, "hidden", false);
	obs_data_array_push_back(array, arrayItem);
	obs_data_release(arrayItem);
	obs_data_set_array(settings, "files", array);
	obs_data_array_release(array);
}

void OBSBasicSourceSelect::on_buttonBox_accepted()
{
	bool useExisting = ui->selectExisting->isChecked();
	bool visible = ui->sourceVisible->isChecked();

	// 当前新添加的资源是否是 => rtp_source => 互动教室...
	bool bIsNewRtpSource = ((astrcmpi(id, App()->InteractSmartSource()) == 0) ? true : false);

	// 当前新添加的资源是否是 学生屏幕分享...
	bool bIsNewScreenSource = ((astrcmpi(id, "slideshow") == 0) && m_bIsScreen);

	// 如果已经有了学生屏幕资源，就不能再添加新的学生屏幕资源了...
	if (m_bHasScreenSource && bIsNewScreenSource) {
		OBSMessageBox::information(this, 
			QTStr("SingleScreenSource.Title"), 
			QTStr("SingleScreenSource.Text"));
		return;
	}

	// 当前新添加的资源是否是 => InputAudioSource => 麦克风输入...
	bool bIsNewMicSource = ((astrcmpi(id, App()->InputAudioSource()) == 0) ? true : false);
	// 如果已经有了麦克风资源，就不能再添加新的麦克风数据源了...
	if (m_bHasMicSource && bIsNewMicSource) {
		OBSMessageBox::information(this,
			QTStr("SingleMicSource.Title"),
			QTStr("SingleMicSource.Text"));
		return;
	}

	if (useExisting) {
		QListWidgetItem *item = ui->sourceList->currentItem();
		if (!item)
			return;

		AddExisting(QT_TO_UTF8(item->text()), visible, false);
	} else {
		if (ui->sourceName->text().isEmpty()) {
			OBSMessageBox::warning(this,
					       QTStr("NoNameEntered.Title"),
					       QTStr("NoNameEntered.Text"));
			return;
		}
		// 如果添加新的场景资源失败，直接返回...
		if (!AddNew(this, id, QT_TO_UTF8(ui->sourceName->text()), visible, newSource))
			return;
		// 如果是 学生屏幕分享 数据源，设置状态标志参数和默认的路径...
		if (bIsNewScreenSource) {
			obs_data_t * settings = obs_source_get_settings(newSource);
			obs_data_set_bool(settings, "screen_slide", true);
			this->doSaveScreenPath(settings);
			obs_source_update(newSource, settings);
			obs_data_release(settings);
		}
		// 如果新添加资源是互动教室 => 需要监视并输出，开启本地监视，添加噪音抑制过滤器...
		// 轨道1 => 输出给直播使用，始终屏蔽互动教室的声音...
		// 轨道2 => 输出给录像使用，当互动教室处于焦点状态时录制声音...
		if (bIsNewRtpSource) {
			obs_source_set_monitoring_type(newSource, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
			blog(LOG_INFO, "User changed audio monitoring for source '%s' to: %s", obs_source_get_name(newSource), "monitor and output");
			// 给互动教室数据源，添加噪音抑制过滤器...
			AddFilterToSourceByID(newSource, App()->GetNSFilter());
		}
		// 给麦克风音频输入数据源添加噪音抑制过滤器...
		if (bIsNewMicSource) {
			AddFilterToSourceByID(newSource, App()->GetNSFilter());
		}
	}

	done(DialogCode::Accepted);
}

void OBSBasicSourceSelect::AddFilterToSourceByID(obs_source_t *source, const char * lpFilterID)
{
	////////////////////////////////////////////////////////
	// 2019.06.20 - 去掉系统自带的噪声抑制功能，效果一般...
	////////////////////////////////////////////////////////
	// 通过id名称查找过滤器资源名称...
	/*string strFilterName = obs_source_get_display_name(lpFilterID);
	obs_source_t * existing_filter = obs_source_get_filter_by_name(source, strFilterName.c_str());
	// 如果资源上已经挂载了相同名称的过滤器，直接返回...
	if (existing_filter != nullptr)
		return;
	// 创建一个新的过滤器资源对象，创建失败，直接返回...
	obs_source_t * new_filter = obs_source_create(lpFilterID, strFilterName.c_str(), nullptr, nullptr);
	if (new_filter == nullptr)
		return;
	// 获取资源名称，打印信息，挂载过滤器到当前资源，释放过滤器的引用计数...
	const char *sourceName = obs_source_get_name(source);
	blog(LOG_INFO, "User added filter '%s' (%s) to source '%s'", strFilterName.c_str(), lpFilterID, sourceName);
	obs_source_filter_add(source, new_filter);
	obs_source_release(new_filter);*/
}

void OBSBasicSourceSelect::on_buttonBox_rejected()
{
	done(DialogCode::Rejected);
}

static inline const char *GetSourceDisplayName(const char *id, bool bIsScreen)
{
	if (strcmp(id, "scene") == 0)
		return Str("Basic.Scene");
	if (strcmp(id, "slideshow") == 0 && bIsScreen)
		return Str("Basic.Student.Screen");
	return obs_source_get_display_name(id);
}

Q_DECLARE_METATYPE(OBSScene);

template<typename T> static inline T GetOBSRef(QListWidgetItem *item)
{
	return item->data(static_cast<int>(QtDataRole::OBSRef)).value<T>();
}

OBSBasicSourceSelect::OBSBasicSourceSelect(OBSBasic *parent, const char *id_, bool bIsScreen/* = false*/)
  : QDialog (parent),
	ui (new Ui::OBSBasicSourceSelect),
	id (id_),
	m_bIsScreen(bIsScreen)
{
	m_bHasMicSource = false;

	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	ui->setupUi(this);

	ui->sourceList->setAttribute(Qt::WA_MacShowFocusRect, false);

	QString placeHolderText{QT_UTF8(GetSourceDisplayName(id, m_bIsScreen))};

	QString text{placeHolderText};
	int i = 2;
	obs_source_t *source = nullptr;
	while ((source = obs_get_source_by_name(QT_TO_UTF8(text)))) {
		obs_source_release(source);
		text = QString("%1 %2").arg(placeHolderText).arg(i++);
	}

	ui->sourceName->setText(text);
	ui->sourceName->setFocus(); //Fixes deselect of text.
	ui->sourceName->selectAll();

	installEventFilter(CreateShortcutFilter());

	if (strcmp(id_, "scene") == 0) {
		OBSBasic *main = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
		OBSSource curSceneSource = main->GetCurrentSceneSource();

		ui->selectExisting->setChecked(true);
		ui->createNew->setChecked(false);
		ui->createNew->setEnabled(false);
		ui->sourceName->setEnabled(false);

		int count = main->ui->scenes->count();
		for (int i = 0; i < count; i++) {
			QListWidgetItem *item = main->ui->scenes->item(i);
			OBSScene scene = GetOBSRef<OBSScene>(item);
			OBSSource sceneSource = obs_scene_get_source(scene);

			if (curSceneSource == sceneSource)
				continue;

			const char *name = obs_source_get_name(sceneSource);
			ui->sourceList->addItem(QT_UTF8(name));
		}
	} else if (strcmp(id_, "group") == 0) {
		obs_enum_sources(EnumGroups, this);
	} else {
		obs_enum_sources(EnumSources, this);
	}
}

void OBSBasicSourceSelect::SourcePaste(const char *name, bool visible, bool dup)
{
	AddExisting(name, visible, dup);
}
