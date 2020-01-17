
#pragma once

#include "obs-app.hpp"

#define WIN_MANIFEST_URL    DEF_WEB_CENTER "/update_studio/smart/manifest.json"
#define WIN_DXSETUP_URL     DEF_WEB_CENTER "/update_studio/dxwebsetup.exe"
#define WIN_UPDATER_URL     DEF_WEB_CENTER "/update_studio/updater.exe"
#define APP_DXSETUP_PATH    "obs-smart\\updates\\dxwebsetup.exe"
#define APP_MANIFEST_PATH   "obs-smart\\updates\\manifest.json"
#define APP_UPDATER_PATH    "obs-smart\\updates\\updater.exe"
#define APP_NAME            L"smart"

void doCloseMainWindow();
