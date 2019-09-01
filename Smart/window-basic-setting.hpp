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

class OBSBasic;
class QAbstractButton;
class QComboBox;
class QCheckBox;
class QLabel;

#include "ui_OBSBasicSetting.h"

class OBSBasicSetting : public QDialog {
	Q_OBJECT
	// 需要修改 UI/data/themes 下面的 .qss 文件，只能分开写样式...
	Q_PROPERTY(QIcon generalIcon READ GetGeneralIcon WRITE SetGeneralIcon DESIGNABLE true)
	Q_PROPERTY(QIcon audioIcon READ GetAudioIcon WRITE SetAudioIcon DESIGNABLE true)
	Q_PROPERTY(QIcon videoIcon READ GetVideoIcon WRITE SetVideoIcon DESIGNABLE true)
private:
	OBSBasic * main;
	std::unique_ptr<Ui::OBSBasicSetting> ui;

	bool generalChanged = false;
	bool audioChanged = false;
	bool videoChanged = false;
	int  pageIndex = 0;
	bool loading = true;

	inline bool Changed() const {
		return generalChanged || audioChanged || videoChanged;
	}
	inline void EnableApplyButton(bool en) {
		ui->buttonBox->button(QDialogButtonBox::Apply)->setEnabled(en);
	}
	inline void ClearChanged() {
		generalChanged = false;
		audioChanged = false;
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

	void LoadSettings(bool changedOnly);
	void LoadGeneralSettings();
	void LoadAudioSettings();
	void LoadVideoSettings();

	/* general */
	void LoadLanguageList();
private:
	void SaveGeneralSettings();
	void SaveAudioSettings();
	void SaveVideoSettings();
	void SaveSettings();

	QIcon generalIcon;
	QIcon audioIcon;
	QIcon videoIcon;

	QIcon GetGeneralIcon() const { return generalIcon; }
	QIcon GetAudioIcon() const { return audioIcon; }
	QIcon GetVideoIcon() const { return videoIcon; }
private slots:
    void OnSetupLoad();
	void GeneralChanged();
	void AudioChanged();
	void VideoChanged();
	void on_listWidget_itemSelectionChanged();
	void on_buttonBox_clicked(QAbstractButton *button);
	void SetGeneralIcon(const QIcon &icon) { generalIcon = icon; }
	void SetAudioIcon(const QIcon &icon) { audioIcon = icon; }
	void SetVideoIcon(const QIcon &icon) { videoIcon = icon; }
protected:
	virtual void closeEvent(QCloseEvent *event);
public:
	OBSBasicSetting(QWidget *parent);
	~OBSBasicSetting();
};
