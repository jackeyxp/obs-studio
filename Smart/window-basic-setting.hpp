/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>
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

#pragma once

#include <util/util.hpp>
#include <QPushButton>
#include <QDialog>
#include <QPointer>
#include <memory>
#include <string>

#include <obs.hpp>

class QLabel;
class OBSBasic;
class QComboBox;
class QCheckBox;
class QAbstractButton;
class OBSPropertiesView;

#include "ui_OBSBasicSetting.h"

class OBSBasicSetting : public QDialog {
	Q_OBJECT
	// 需要修改 UI/data/themes 下面的 .qss 文件，只能分开写样式...
	Q_PROPERTY(QIcon generalIcon READ GetGeneralIcon WRITE SetGeneralIcon DESIGNABLE true)
	Q_PROPERTY(QIcon videoIcon READ GetVideoIcon WRITE SetVideoIcon DESIGNABLE true)
private:
	OBSBasic * main;
	std::unique_ptr<Ui::OBSBasicSetting> ui;

	bool generalChanged = false;
	bool videoChanged = false;
	int  pageIndex = 0;
	bool loading = true;

	uint32_t outputCX = 0;
	uint32_t outputCY = 0;

	OBSPropertiesView *streamEncoderProps = nullptr;
	QString curAdvStreamEncoder;

	void SaveCombo(QComboBox *widget, const char *section, const char *value);
	void SaveEdit(QLineEdit *widget, const char *section, const char *value);
	void SaveSpinBox(QSpinBox *widget, const char *section, const char *value);
	void SaveComboData(QComboBox *widget, const char *section, const char *value);
	void SaveCheckBox(QAbstractButton *widget, const char *section, const char *value, bool invert = false);

	inline bool Changed() const {
		return generalChanged || videoChanged;
	}
	inline void EnableApplyButton(bool en) {
		ui->buttonBox->button(QDialogButtonBox::Apply)->setEnabled(en);
	}
	inline void ClearChanged() {
		generalChanged = false;
		videoChanged = false;
		EnableApplyButton(false);
	}

#ifdef _WIN32
	bool aeroWasDisabled = false;
	QCheckBox *toggleAero = nullptr;
	void ToggleDisableAero(bool checked);
#endif

	void HookWidget(QWidget *widget, const char *signal, const char *slot);
	bool QueryChanges();

	void LoadEncoderTypes();
	void LoadAdvOutputStreamingEncoderProperties();

	void LoadSettings(bool changedOnly);
	void LoadGeneralSettings();
	void LoadGeneralExtern();
	void LoadVideoSettings();

	OBSPropertiesView * CreateEncoderPropertyView(const char *encoder, const char *path, bool changed = false);

	/* general */
	void LoadLanguageList();

	/* video */
	void LoadRendererList();
	void LoadDownscaleFilters();
	void LoadResolutionLists();
	void LoadFPSData();

	void ResetDownscales(uint32_t cx, uint32_t cy);
	void RecalcOutputResPixels(const char *resText);

private:
	void SaveGeneralExtern();
	void SaveGeneralSettings();
	void SaveVideoSettings();
	void SaveSettings();

	QIcon generalIcon;
	QIcon videoIcon;

	QIcon GetGeneralIcon() const { return generalIcon; }
	QIcon GetVideoIcon() const { return videoIcon; }
private slots:
    void OnSetupLoad();
	void GeneralChanged();
	void VideoChanged();
	void VideoChangedRestart();
	void VideoChangedResolution();
	void on_listWidget_itemSelectionChanged();
	void on_advOutEncoder_currentIndexChanged(int idx);
	void on_outputResolution_editTextChanged(const QString &text);
	void on_baseResolution_editTextChanged(const QString &text);
	void on_buttonBox_clicked(QAbstractButton *button);
	void SetGeneralIcon(const QIcon &icon) { generalIcon = icon; }
	void SetVideoIcon(const QIcon &icon) { videoIcon = icon; }
protected:
	virtual void closeEvent(QCloseEvent *event);
public:
	OBSBasicSetting(QWidget *parent);
	~OBSBasicSetting();
};
