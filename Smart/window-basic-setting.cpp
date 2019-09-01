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

using namespace std;

OBSBasicSetting::OBSBasicSetting(QWidget *parent)
	: QDialog(parent),
	  main(qobject_cast<OBSBasic *>(parent)),
	  ui(new Ui::OBSBasicSetting)
{
	EnableThreadedMessageBoxes(true);

	ui->setupUi(this);

	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	main->EnableOutputs(false);

	ui->listWidget->setAttribute(Qt::WA_MacShowFocusRect, false);

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

void OBSBasicSetting::LoadGeneralSettings()
{

}

void OBSBasicSetting::LoadAudioSettings()
{

}

void OBSBasicSetting::LoadVideoSettings()
{

}

void OBSBasicSetting::on_listWidget_itemSelectionChanged()
{
	int row = ui->listWidget->currentRow();
	//if (loading || row == pageIndex)
	//	return;
	pageIndex = row;
}

void OBSBasicSetting::on_buttonBox_clicked(QAbstractButton *button)
{
	QDialogButtonBox::ButtonRole val = ui->buttonBox->buttonRole(button);
}

OBSBasicSetting::~OBSBasicSetting()
{
	main->EnableOutputs(true);
	EnableThreadedMessageBoxes(false);
}

