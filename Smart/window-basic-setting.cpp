/******************************************************************************
    Copyright (C) 2013-2014 by Hugh Bailey <obs.jim@gmail.com>
                               Philippe Groarke <philippe.groarke@gmail.com>

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

#include <obs.hpp>
#include <util/util.hpp>
#include <QCompleter>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMessageBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QDirIterator>
#include <QVariant>
#include <QTreeView>
#include <QScreen>
#include <QStandardItemModel>
#include <QSpacerItem>

#include "obs-app.hpp"
#include "platform.hpp"
#include "qt-wrappers.hpp"
#include "window-basic-main.hpp"
#include "window-basic-setting.hpp"
#include <util/profiler.hpp>

using namespace std;

static inline bool WidgetChanged(QWidget *widget)
{
	return widget->property("changed").toBool();
}

/* clang-format off */
#define COMBO_CHANGED   SIGNAL(currentIndexChanged(int))
#define CHECK_CHANGED   SIGNAL(clicked(bool))

#define GENERAL_CHANGED SLOT(GeneralChanged())
#define AUDIO_CHANGED   SLOT(AudioChanged())
#define VIDEO_CHANGED   SLOT(VideoChanged())

void OBSBasicSetting::HookWidget(QWidget *widget, const char *signal, const char *slot)
{
	QObject::connect(widget, signal, this, slot);
	widget->setProperty("changed", QVariant(false));
}

#ifdef _WIN32
void OBSBasicSetting::ToggleDisableAero(bool checked)
{
	SetAeroEnabled(!checked);
}
#endif

void OBSBasicSetting::GeneralChanged()
{
	if (!loading) {
		generalChanged = true;
		sender()->setProperty("changed", QVariant(true));
		EnableApplyButton(true);
	}
}

void OBSBasicSetting::AudioChanged()
{
	if (!loading) {
		audioChanged = true;
		sender()->setProperty("changed", QVariant(true));
		EnableApplyButton(true);
	}
}

void OBSBasicSetting::VideoChanged()
{
	if (!loading) {
		videoChanged = true;
		sender()->setProperty("changed", QVariant(true));
		EnableApplyButton(true);
	}
}

OBSBasicSetting::OBSBasicSetting(QWidget *parent)
	: QDialog(parent),
	main(qobject_cast<OBSBasic *>(parent)),
	ui(new Ui::OBSBasicSetting)
{
	ProfileScope("OBSBasicSetting::OBSBasicSetting");

	// 直接拉伸居中显示...
	this->resize(981, 748);

	main->EnableOutputs(false);
	EnableThreadedMessageBoxes(true);

	// 去掉右侧关闭按钮旁边的帮助按钮...
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	// 利用时钟进行延迟加载，可以获得更好的用户体验...
	QTimer::singleShot(20, this, SLOT(OnSetupLoad()));
}

OBSBasicSetting::~OBSBasicSetting()
{
	main->EnableOutputs(true);
	EnableThreadedMessageBoxes(false);
}

void OBSBasicSetting::OnSetupLoad()
{
	ProfileScope("OBSBasicSetting::OnSetupLoad");

	// 元素非常多时会比较慢...
	ui->setupUi(this);

	ui->listWidget->setAttribute(Qt::WA_MacShowFocusRect, false);

	// 由于延迟加载，需要在这里更新左侧列表栏对应图标...
	ui->listWidget->item(0)->setIcon(generalIcon);
	ui->listWidget->item(1)->setIcon(audioIcon);
	ui->listWidget->item(2)->setIcon(videoIcon);

	/* clang-format off */
	HookWidget(ui->language, COMBO_CHANGED, GENERAL_CHANGED);
	HookWidget(ui->enableAutoUpdates, CHECK_CHANGED, GENERAL_CHANGED);

#ifdef _WIN32
	uint32_t winVer = GetWindowsVersion();
	if (winVer > 0 && winVer < 0x602) {
		toggleAero = new QCheckBox(QTStr("Basic.Settings.Video.DisableAero"), this);
		QFormLayout *videoLayout = reinterpret_cast<QFormLayout *>(ui->videoPage->layout());
		videoLayout->addRow(nullptr, toggleAero);

		HookWidget(toggleAero, CHECK_CHANGED, VIDEO_CHANGED);
		connect(toggleAero, &QAbstractButton::toggled, this, &OBSBasicSetting::ToggleDisableAero);
	}
#endif

	//Apply button disabled until change.
	EnableApplyButton(false);

	LoadSettings(false);
}

void OBSBasicSetting::LoadSettings(bool changedOnly)
{
	if (!changedOnly || generalChanged)
		LoadGeneralSettings();
	if (!changedOnly || audioChanged)
		LoadAudioSettings();
	if (!changedOnly || videoChanged)
		LoadVideoSettings();
}

void OBSBasicSetting::SaveGeneralSettings()
{
	int languageIndex = ui->language->currentIndex();
	QVariant langData = ui->language->itemData(languageIndex);
	string language = langData.toString().toStdString();

	if (WidgetChanged(ui->language)) {
		config_set_string(GetGlobalConfig(), "General", "Language", language.c_str());
	}
	if (WidgetChanged(ui->enableAutoUpdates)) {
		config_set_bool(GetGlobalConfig(), "General", "EnableAutoUpdates", ui->enableAutoUpdates->isChecked());
	}
}

void OBSBasicSetting::SaveAudioSettings()
{

}

void OBSBasicSetting::SaveVideoSettings()
{

}

void OBSBasicSetting::LoadGeneralSettings()
{
	loading = true;

	LoadLanguageList();
	//LoadThemeList();

	bool enableAutoUpdates = config_get_bool(GetGlobalConfig(), "General", "EnableAutoUpdates");
	ui->enableAutoUpdates->setChecked(enableAutoUpdates);

	loading = false;
}

void OBSBasicSetting::LoadLanguageList()
{
	const char *currentLang = App()->GetLocale();

	ui->language->clear();

	for (const auto &locale : GetLocaleNames()) {
		int idx = ui->language->count();

		ui->language->addItem(QT_UTF8(locale.second.c_str()),
			QT_UTF8(locale.first.c_str()));

		if (locale.first == currentLang)
			ui->language->setCurrentIndex(idx);
	}

	ui->language->model()->sort(0);
}

/*void OBSBasicSetting::LoadThemeList()
{
	// Save theme if user presses Cancel 
	savedTheme = string(App()->GetTheme());

	ui->theme->clear();
	QSet<QString> uniqueSet;
	string themeDir;
	char userThemeDir[512];
	int ret = GetConfigPath(userThemeDir, sizeof(userThemeDir),	"obs-smart/themes/");
	GetDataFilePath("themes/", themeDir);

	// Check user dir first. 
	if (ret > 0) {
		QDirIterator it(QString(userThemeDir), QStringList() << "*.qss", QDir::Files);
		while (it.hasNext()) {
			it.next();
			QString name = it.fileName().section(".", 0, 0);
			ui->theme->addItem(name);
			uniqueSet.insert(name);
		}
	}

	QString defaultTheme;
	defaultTheme += DEFAULT_THEME;
	defaultTheme += " ";
	defaultTheme += QTStr("Default");

	// Check shipped themes. 
	QDirIterator uIt(QString(themeDir.c_str()), QStringList() << "*.qss", QDir::Files);
	while (uIt.hasNext()) {
		uIt.next();
		QString name = uIt.fileName().section(".", 0, 0);

		if (name == DEFAULT_THEME)
			name = defaultTheme;

		if (!uniqueSet.contains(name) && name != "Default")
			ui->theme->addItem(name);
	}

	std::string themeName = App()->GetTheme();

	if (themeName == DEFAULT_THEME)
		themeName = QT_TO_UTF8(defaultTheme);

	int idx = ui->theme->findText(themeName.c_str());
	if (idx != -1)
		ui->theme->setCurrentIndex(idx);
}*/

void OBSBasicSetting::LoadAudioSettings()
{

}

void OBSBasicSetting::LoadVideoSettings()
{

}

void OBSBasicSetting::on_listWidget_itemSelectionChanged()
{
	int row = ui->listWidget->currentRow();
	if (loading || row == pageIndex)
		return;
	pageIndex = row;
}

void OBSBasicSetting::on_buttonBox_clicked(QAbstractButton *button)
{
	QDialogButtonBox::ButtonRole val = ui->buttonBox->buttonRole(button);
	if (val == QDialogButtonBox::ApplyRole || val == QDialogButtonBox::AcceptRole) {
		// 应用保存之后，立即清理状态，避免点击确定后的二次保存...
		this->SaveSettings();
		this->ClearChanged();
	}

	if (val == QDialogButtonBox::AcceptRole || val == QDialogButtonBox::RejectRole) {
		if (val == QDialogButtonBox::RejectRole) {
			//屏蔽主题风格，会造成堵塞缓慢...
			//App()->SetTheme(savedTheme);
#ifdef _WIN32
			if (toggleAero)
				SetAeroEnabled(!aeroWasDisabled);
#endif
		}
		// 清理状态变化标志...
		this->ClearChanged();
		this->close();
	}
}

// 确定|取消|关闭，都会激发 closeEvent 事件执行...
void OBSBasicSetting::closeEvent(QCloseEvent *event)
{
	if (Changed() && !QueryChanges())
		event->ignore();
}

#define MINOR_SEPARATOR "------------------------------------------------"

static void AddChangedVal(std::string &changed, const char *str)
{
	if (changed.size())
		changed += ", ";
	changed += str;
}

void OBSBasicSetting::SaveSettings()
{
	if (generalChanged)
		SaveGeneralSettings();
	if (audioChanged)
		SaveAudioSettings();
	if (videoChanged)
		SaveVideoSettings();
	if (videoChanged)
		main->ResetVideo();

	config_save_safe(main->Config(), "tmp", nullptr);
	config_save_safe(GetGlobalConfig(), "tmp", nullptr);
	main->SaveProject();

	if (Changed()) {
		std::string changed;
		if (generalChanged)
			AddChangedVal(changed, "general");
		if (audioChanged)
			AddChangedVal(changed, "audio");
		if (videoChanged)
			AddChangedVal(changed, "video");
		blog(LOG_INFO, "Settings changed (%s)", changed.c_str());
		blog(LOG_INFO, MINOR_SEPARATOR);
	}
}

bool OBSBasicSetting::QueryChanges()
{
	QMessageBox::StandardButton button;

	button = OBSMessageBox::question(
		this, QTStr("Basic.Settings.ConfirmTitle"),
		QTStr("Basic.Settings.Confirm"),
		QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

	if (button == QMessageBox::Cancel) {
		return false;
	} else if (button == QMessageBox::Yes) {
		SaveSettings();
	} else {
		LoadSettings(true);
#ifdef _WIN32
		if (toggleAero)
			SetAeroEnabled(!aeroWasDisabled);
#endif
	}

	ClearChanged();
	return true;
}
