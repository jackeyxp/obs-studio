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
#include <sstream>

using namespace std;

static inline QString GetComboData(QComboBox *combo)
{
	int idx = combo->currentIndex();
	if (idx == -1)
		return QString();
	return combo->itemData(idx).toString();
}

static inline bool WidgetChanged(QWidget *widget)
{
	return widget->property("changed").toBool();
}

/* parses "[width]x[height]", string, i.e. 1024x768 */
static bool ConvertResText(const char *res, uint32_t &cx, uint32_t &cy)
{
	BaseLexer lex;
	base_token token;

	lexer_start(lex, res);

	/* parse width */
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (token.type != BASETOKEN_DIGIT)
		return false;

	cx = std::stoul(token.text.array);

	/* parse 'x' */
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (strref_cmpi(&token.text, "x") != 0)
		return false;

	/* parse height */
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (token.type != BASETOKEN_DIGIT)
		return false;

	cy = std::stoul(token.text.array);

	/* shouldn't be any more tokens after this */
	if (lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;

	return true;
}

/* clang-format off */
#define CBEDIT_CHANGED  SIGNAL(editTextChanged(const QString &))
#define COMBO_CHANGED   SIGNAL(currentIndexChanged(int))
#define CHECK_CHANGED   SIGNAL(clicked(bool))
#define SCROLL_CHANGED  SIGNAL(valueChanged(int))

#define GENERAL_CHANGED SLOT(GeneralChanged())
#define VIDEO_CHANGED   SLOT(VideoChanged())
#define VIDEO_RESTART   SLOT(VideoChangedRestart())
#define VIDEO_RES       SLOT(VideoChangedResolution())

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
	ui->listWidget->item(1)->setIcon(videoIcon);

	/* clang-format off */
	HookWidget(ui->language, COMBO_CHANGED, GENERAL_CHANGED);
	HookWidget(ui->enableAutoUpdates, CHECK_CHANGED, GENERAL_CHANGED);

	HookWidget(ui->baseResolution, CBEDIT_CHANGED, VIDEO_RES);
	HookWidget(ui->outputResolution, CBEDIT_CHANGED, VIDEO_RES);
	HookWidget(ui->downscaleFilter, COMBO_CHANGED, VIDEO_CHANGED);
	HookWidget(ui->fpsType, COMBO_CHANGED, VIDEO_CHANGED);
	HookWidget(ui->fpsCommon, COMBO_CHANGED, VIDEO_CHANGED);
	HookWidget(ui->fpsInteger, SCROLL_CHANGED, VIDEO_CHANGED);
	HookWidget(ui->fpsNumerator, SCROLL_CHANGED, VIDEO_CHANGED);
	HookWidget(ui->fpsDenominator, SCROLL_CHANGED, VIDEO_CHANGED);

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

void OBSBasicSetting::SaveVideoSettings()
{
	QString baseResolution = ui->baseResolution->currentText();
	QString outputResolution = ui->outputResolution->currentText();
	int fpsType = ui->fpsType->currentIndex();
	uint32_t cx = 0, cy = 0;

	/* ------------------- */

	if (WidgetChanged(ui->baseResolution) &&
		ConvertResText(QT_TO_UTF8(baseResolution), cx, cy)) {
		config_set_uint(main->Config(), "Video", "BaseCX", cx);
		config_set_uint(main->Config(), "Video", "BaseCY", cy);
	}

	if (WidgetChanged(ui->outputResolution) &&
		ConvertResText(QT_TO_UTF8(outputResolution), cx, cy)) {
		config_set_uint(main->Config(), "Video", "OutputCX", cx);
		config_set_uint(main->Config(), "Video", "OutputCY", cy);
	}

	if (WidgetChanged(ui->fpsType)) {
		config_set_uint(main->Config(), "Video", "FPSType", fpsType);
	}

	SaveCombo(ui->fpsCommon, "Video", "FPSCommon");
	SaveSpinBox(ui->fpsInteger, "Video", "FPSInt");
	SaveSpinBox(ui->fpsNumerator, "Video", "FPSNum");
	SaveSpinBox(ui->fpsDenominator, "Video", "FPSDen");
	SaveComboData(ui->downscaleFilter, "Video", "ScaleType");

#ifdef _WIN32
	if (toggleAero) {
		SaveCheckBox(toggleAero, "Video", "DisableAero");
		aeroWasDisabled = toggleAero->isChecked();
	}
#endif
}

void OBSBasicSetting::SaveCombo(QComboBox *widget, const char *section, const char *value)
{
	if (WidgetChanged(widget)) {
		config_set_string(main->Config(), section, value, QT_TO_UTF8(widget->currentText()));
	}
}

void OBSBasicSetting::SaveComboData(QComboBox *widget, const char *section, const char *value)
{
	if (WidgetChanged(widget)) {
		QString str = GetComboData(widget);
		config_set_string(main->Config(), section, value, QT_TO_UTF8(str));
	}
}

void OBSBasicSetting::SaveCheckBox(QAbstractButton *widget,
	const char *section, const char *value, bool invert)
{
	if (WidgetChanged(widget)) {
		bool checked = widget->isChecked();
		if (invert) checked = !checked;
		config_set_bool(main->Config(), section, value, checked);
	}
}

void OBSBasicSetting::SaveEdit(QLineEdit *widget, const char *section, const char *value)
{
	if (WidgetChanged(widget)) {
		config_set_string(main->Config(), section, value, QT_TO_UTF8(widget->text()));
	}
}

void OBSBasicSetting::SaveSpinBox(QSpinBox *widget, const char *section, const char *value)
{
	if (WidgetChanged(widget)) {
		config_set_int(main->Config(), section, value, widget->value());
	}
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

static string ResString(uint32_t cx, uint32_t cy)
{
	stringstream res;
	res << cx << "x" << cy;
	return res.str();
}

/* some nice default output resolution vals */
static const double vals[] = {
	1.0, 1.25, (1.0 / 0.75),
	1.5, (1.0 / 0.6), 1.75, 
	2.0, 2.25, 2.5, 2.75, 3.0
};

static const size_t numVals = sizeof(vals) / sizeof(double);

void OBSBasicSetting::ResetDownscales(uint32_t cx, uint32_t cy)
{
	QString oldOutputRes;
	string bestScale;
	int bestPixelDiff = 0x7FFFFFFF;
	uint32_t out_cx = outputCX;
	uint32_t out_cy = outputCY;

	//QString advRescale;
	//QString advRecRescale;
	//QString advFFRescale;
	//advRescale = ui->advOutRescale->lineEdit()->text();
	//advRecRescale = ui->advOutRecRescale->lineEdit()->text();
	//advFFRescale = ui->advOutFFRescale->lineEdit()->text();

	ui->outputResolution->blockSignals(true);
	ui->outputResolution->clear();

	//ui->advOutRescale->clear();
	//ui->advOutRecRescale->clear();
	//ui->advOutFFRescale->clear();

	if (!out_cx || !out_cy) {
		out_cx = cx; out_cy = cy;
		oldOutputRes = ui->baseResolution->lineEdit()->text();
	} else {
		oldOutputRes = QString::number(out_cx) + "x" + QString::number(out_cy);
	}

	for (size_t idx = 0; idx < numVals; idx++) {
		uint32_t downscaleCX = uint32_t(double(cx) / vals[idx]);
		uint32_t downscaleCY = uint32_t(double(cy) / vals[idx]);
		uint32_t outDownscaleCX = uint32_t(double(out_cx) / vals[idx]);
		uint32_t outDownscaleCY = uint32_t(double(out_cy) / vals[idx]);

		downscaleCX &= 0xFFFFFFFC;
		downscaleCY &= 0xFFFFFFFE;
		outDownscaleCX &= 0xFFFFFFFE;
		outDownscaleCY &= 0xFFFFFFFE;

		string res = ResString(downscaleCX, downscaleCY);
		string outRes = ResString(outDownscaleCX, outDownscaleCY);
		ui->outputResolution->addItem(res.c_str());
		//ui->advOutRescale->addItem(outRes.c_str());
		//ui->advOutRecRescale->addItem(outRes.c_str());
		//ui->advOutFFRescale->addItem(outRes.c_str());

		// always try to find the closest output resolution to the
		// previously set output resolution 
		int newPixelCount = int(downscaleCX * downscaleCY);
		int oldPixelCount = int(out_cx * out_cy);
		int diff = abs(newPixelCount - oldPixelCount);

		if (diff < bestPixelDiff) {
			bestScale = res;
			bestPixelDiff = diff;
		}
	}

	string res = ResString(cx, cy);

	float baseAspect = float(cx) / float(cy);
	float outputAspect = float(out_cx) / float(out_cy);

	bool closeAspect = close_float(baseAspect, outputAspect, 0.01f);
	if (closeAspect)
		ui->outputResolution->lineEdit()->setText(oldOutputRes);
	else
		ui->outputResolution->lineEdit()->setText(bestScale.c_str());

	ui->outputResolution->blockSignals(false);

	if (!closeAspect) {
		ui->outputResolution->setProperty("changed", QVariant(true));
		videoChanged = true;
	}

	/*if (advRescale.isEmpty())
		advRescale = res.c_str();
	if (advRecRescale.isEmpty())
		advRecRescale = res.c_str();
	if (advFFRescale.isEmpty())
		advFFRescale = res.c_str();*/

	//ui->advOutRescale->lineEdit()->setText(advRescale);
	//ui->advOutRecRescale->lineEdit()->setText(advRecRescale);
	//ui->advOutFFRescale->lineEdit()->setText(advFFRescale);
}

void OBSBasicSetting::LoadDownscaleFilters()
{
	ui->downscaleFilter->addItem(
		QTStr("Basic.Settings.Video.DownscaleFilter.Bilinear"),
		QT_UTF8("bilinear"));
	ui->downscaleFilter->addItem(
		QTStr("Basic.Settings.Video.DownscaleFilter.Area"),
		QT_UTF8("area"));
	ui->downscaleFilter->addItem(
		QTStr("Basic.Settings.Video.DownscaleFilter.Bicubic"),
		QT_UTF8("bicubic"));
	//ui->downscaleFilter->addItem(
	//	QTStr("Basic.Settings.Video.DownscaleFilter.Lanczos"),
	//	QT_UTF8("lanczos"));

	const char *scaleType = config_get_string(main->Config(), "Video", "ScaleType");

	if (astrcmpi(scaleType, "bilinear") == 0)
		ui->downscaleFilter->setCurrentIndex(0);
	//else if (astrcmpi(scaleType, "lanczos") == 0)
	//	ui->downscaleFilter->setCurrentIndex(3);
	else if (astrcmpi(scaleType, "area") == 0)
		ui->downscaleFilter->setCurrentIndex(1);
	else
		ui->downscaleFilter->setCurrentIndex(2);
}

void OBSBasicSetting::LoadResolutionLists()
{
	uint32_t cx = config_get_uint(main->Config(), "Video", "BaseCX");
	uint32_t cy = config_get_uint(main->Config(), "Video", "BaseCY");
	uint32_t out_cx = config_get_uint(main->Config(), "Video", "OutputCX");
	uint32_t out_cy = config_get_uint(main->Config(), "Video", "OutputCY");

	ui->baseResolution->clear();

	auto addRes = [this](int cx, int cy) {
		QString res = ResString(cx, cy).c_str();
		if (ui->baseResolution->findText(res) == -1)
			ui->baseResolution->addItem(res);
	};

	for (QScreen *screen : QGuiApplication::screens()) {
		QSize as = screen->size();
		addRes(as.width(), as.height());
	}

	addRes(1920, 1080);
	addRes(1280, 720);

	string outputResString = ResString(out_cx, out_cy);

	ui->baseResolution->lineEdit()->setText(ResString(cx, cy).c_str());

	this->RecalcOutputResPixels(outputResString.c_str());
	this->ResetDownscales(cx, cy);

	ui->outputResolution->lineEdit()->setText(outputResString.c_str());
}

#define INVALID_RES_STR "Basic.Settings.Video.InvalidResolution"

static bool ValidResolutions(Ui::OBSBasicSetting *ui)
{
	QString baseRes = ui->baseResolution->lineEdit()->text();
	QString outputRes = ui->outputResolution->lineEdit()->text();
	uint32_t cx, cy;

	if (!ConvertResText(QT_TO_UTF8(baseRes), cx, cy) ||
		!ConvertResText(QT_TO_UTF8(outputRes), cx, cy)) {
		ui->videoMsg->setText(QTStr(INVALID_RES_STR));
		return false;
	}

	ui->videoMsg->setText("");
	return true;
}

void OBSBasicSetting::RecalcOutputResPixels(const char *resText)
{
	uint32_t newCX;
	uint32_t newCY;

	ConvertResText(resText, newCX, newCY);
	if (newCX && newCY) {
		outputCX = newCX;
		outputCY = newCY;
	}
}
static inline void LoadFPSCommon(OBSBasic *main, Ui::OBSBasicSetting *ui)
{
	const char *val = config_get_string(main->Config(), "Video", "FPSCommon");

	int idx = ui->fpsCommon->findText(val);
	if (idx == -1) idx = 4;
	ui->fpsCommon->setCurrentIndex(idx);
}

static inline void LoadFPSInteger(OBSBasic *main, Ui::OBSBasicSetting *ui)
{
	uint32_t val = config_get_uint(main->Config(), "Video", "FPSInt");
	ui->fpsInteger->setValue(val);
}

static inline void LoadFPSFraction(OBSBasic *main, Ui::OBSBasicSetting *ui)
{
	uint32_t num = config_get_uint(main->Config(), "Video", "FPSNum");
	uint32_t den = config_get_uint(main->Config(), "Video", "FPSDen");

	ui->fpsNumerator->setValue(num);
	ui->fpsDenominator->setValue(den);
}

void OBSBasicSetting::LoadFPSData()
{
	LoadFPSCommon(main, ui.get());
	LoadFPSInteger(main, ui.get());
	LoadFPSFraction(main, ui.get());

	uint32_t fpsType = config_get_uint(main->Config(), "Video", "FPSType");
	if (fpsType > 2) fpsType = 0;

	ui->fpsType->setCurrentIndex(fpsType);
	ui->fpsTypes->setCurrentIndex(fpsType);
}

void OBSBasicSetting::LoadVideoSettings()
{
	loading = true;

	if (obs_video_active()) {
		ui->videoPage->setEnabled(false);
		ui->videoMsg->setText(QTStr("Basic.Settings.Video.CurrentlyActive"));
	}

	this->LoadResolutionLists();
	this->LoadFPSData();
	this->LoadDownscaleFilters();

#ifdef _WIN32
	if (toggleAero) {
		bool disableAero = config_get_bool(main->Config(), "Video", "DisableAero");
		toggleAero->setChecked(disableAero);
		aeroWasDisabled = disableAero;
	}
#endif

	loading = false;
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

void OBSBasicSetting::on_outputResolution_editTextChanged(const QString &text)
{
	if (!loading) {
		this->RecalcOutputResPixels(QT_TO_UTF8(text));
	}
}

void OBSBasicSetting::on_baseResolution_editTextChanged(const QString &text)
{
	if (!loading && ValidResolutions(ui.get())) {
		QString baseResolution = text;
		uint32_t cx, cy;

		ConvertResText(QT_TO_UTF8(baseResolution), cx, cy);
		this->ResetDownscales(cx, cy);
	}
}

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

void OBSBasicSetting::VideoChanged()
{
	if (!loading) {
		videoChanged = true;
		sender()->setProperty("changed", QVariant(true));
		EnableApplyButton(true);
	}
}

void OBSBasicSetting::VideoChangedResolution()
{
	if (!loading && ValidResolutions(ui.get())) {
		videoChanged = true;
		sender()->setProperty("changed", QVariant(true));
		EnableApplyButton(true);
	}
}

void OBSBasicSetting::VideoChangedRestart()
{
	if (!loading) {
		videoChanged = true;
		ui->videoMsg->setText(QTStr("Basic.Settings.ProgramRestart"));
		sender()->setProperty("changed", QVariant(true));
		EnableApplyButton(true);
	}
}
