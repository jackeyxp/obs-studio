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
private:
	OBSBasic * main;
	std::unique_ptr<Ui::OBSBasicSetting> ui;

	bool generalChanged = false;
	bool audioChanged = false;
	bool videoChanged = false;
	int  pageIndex = 0;
	bool loading = true;

	inline void EnableApplyButton(bool en) {
		ui->buttonBox->button(QDialogButtonBox::Apply)->setEnabled(en);
	}
	void LoadSettings(bool changedOnly);
	void LoadGeneralSettings();
	void LoadAudioSettings();
	void LoadVideoSettings();
private slots:
	void on_listWidget_itemSelectionChanged();
	void on_buttonBox_clicked(QAbstractButton *button);
public:
	OBSBasicSetting(QWidget *parent);
	~OBSBasicSetting();
};
