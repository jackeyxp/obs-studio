# This file will be configured to contain variables for CPack. These variables
# should be set in the CMake list file of the project before CPack module is
# included. The list of available CPACK_xxx variables and their associated
# documentation may be obtained using
#  cpack --help-variable-list
#
# Some variables are common to all generators (e.g. CPACK_PACKAGE_NAME)
# and some are specific to a generator
# (e.g. CPACK_NSIS_EXTRA_INSTALL_COMMANDS). The generator specific variables
# usually begin with CPACK_<GENNAME>_xxxx.


set(CPACK_BINARY_7Z "")
set(CPACK_BINARY_BUNDLE "")
set(CPACK_BINARY_CYGWIN "")
set(CPACK_BINARY_DEB "")
set(CPACK_BINARY_DRAGNDROP "")
set(CPACK_BINARY_FREEBSD "")
set(CPACK_BINARY_IFW "")
set(CPACK_BINARY_NSIS "")
set(CPACK_BINARY_OSXX11 "")
set(CPACK_BINARY_PACKAGEMAKER "")
set(CPACK_BINARY_PRODUCTBUILD "")
set(CPACK_BINARY_RPM "")
set(CPACK_BINARY_STGZ "")
set(CPACK_BINARY_TBZ2 "")
set(CPACK_BINARY_TGZ "")
set(CPACK_BINARY_TXZ "")
set(CPACK_BINARY_TZ "")
set(CPACK_BINARY_WIX "")
set(CPACK_BINARY_ZIP "")
set(CPACK_BUILD_SOURCE_DIRS "F:/obs-studio;F:/obs-studio/vsbuild")
set(CPACK_BUNDLE_ICON "F:/obs-studio/cmake/osxbundle/obs.icns")
set(CPACK_BUNDLE_NAME "OBS")
set(CPACK_BUNDLE_PLIST "F:/obs-studio/cmake/osxbundle/Info.plist")
set(CPACK_BUNDLE_STARTUP_COMMAND "F:/obs-studio/cmake/osxbundle/obslaunch.sh")
set(CPACK_CMAKE_GENERATOR "Visual Studio 14 2015")
set(CPACK_COMPONENTS_ALL "")
set(CPACK_COMPONENT_UNSPECIFIED_HIDDEN "TRUE")
set(CPACK_COMPONENT_UNSPECIFIED_REQUIRED "TRUE")
set(CPACK_CREATE_DESKTOP_LINKS "obs32")
set(CPACK_GENERATOR "WIX;ZIP")
set(CPACK_INSTALL_CMAKE_PROJECTS "F:/obs-studio/vsbuild;obs-studio;ALL;/")
set(CPACK_INSTALL_PREFIX "C:/Program Files/obs-studio")
set(CPACK_MODULE_PATH "F:/obs-studio/cmake/Modules/")
set(CPACK_NSIS_DISPLAY_NAME "OBS Studio (32bit)")
set(CPACK_NSIS_INSTALLER_ICON_CODE "")
set(CPACK_NSIS_INSTALLER_MUI_ICON_CODE "")
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES")
set(CPACK_NSIS_PACKAGE_NAME "OBS Studio (32bit)")
set(CPACK_OUTPUT_CONFIG_FILE "F:/obs-studio/vsbuild/CPackConfig.cmake")
set(CPACK_PACKAGE_DEFAULT_LOCATION "/")
set(CPACK_PACKAGE_DESCRIPTION_FILE "E:/cmake-3.11.1-win32-x86/share/cmake-3.11/Templates/CPack.GenericDescription.txt")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OBS - Live video and audio streaming and recording software")
set(CPACK_PACKAGE_EXECUTABLES "obs32;OBS Studio")
set(CPACK_PACKAGE_FILE_NAME "obs-studio-x86-aae1f0e")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "OBS Studio (32bit)")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "OBSStudio32")
set(CPACK_PACKAGE_NAME "OBS Studio (32bit)")
set(CPACK_PACKAGE_RELOCATABLE "true")
set(CPACK_PACKAGE_VENDOR "obsproject.com")
set(CPACK_PACKAGE_VERSION "0.0.1")
set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "1")
set(CPACK_RESOURCE_FILE_LICENSE "F:/obs-studio/UI/data/license/gplv2.txt")
set(CPACK_RESOURCE_FILE_README "E:/cmake-3.11.1-win32-x86/share/cmake-3.11/Templates/CPack.GenericDescription.txt")
set(CPACK_RESOURCE_FILE_WELCOME "E:/cmake-3.11.1-win32-x86/share/cmake-3.11/Templates/CPack.GenericWelcome.txt")
set(CPACK_SET_DESTDIR "OFF")
set(CPACK_SOURCE_7Z "ON")
set(CPACK_SOURCE_CYGWIN "")
set(CPACK_SOURCE_GENERATOR "7Z;ZIP")
set(CPACK_SOURCE_OUTPUT_CONFIG_FILE "F:/obs-studio/vsbuild/CPackSourceConfig.cmake")
set(CPACK_SOURCE_RPM "")
set(CPACK_SOURCE_TBZ2 "")
set(CPACK_SOURCE_TGZ "")
set(CPACK_SOURCE_TXZ "")
set(CPACK_SOURCE_TZ "")
set(CPACK_SOURCE_ZIP "ON")
set(CPACK_SYSTEM_NAME "win32")
set(CPACK_TOPLEVEL_TAG "win32")
set(CPACK_WIX_PRODUCT_GUID "8e24982d-b0ab-4f66-9c90-f726f3b64682")
set(CPACK_WIX_SIZEOF_VOID_P "4")
set(CPACK_WIX_TEMPLATE "F:/obs-studio/cmake/Modules/WIX.template.in")
set(CPACK_WIX_UPGRADE_GUID "a26acea4-6190-4470-9fb9-f6d32f3ba030")

if(NOT CPACK_PROPERTIES_FILE)
  set(CPACK_PROPERTIES_FILE "F:/obs-studio/vsbuild/CPackProperties.cmake")
endif()

if(EXISTS ${CPACK_PROPERTIES_FILE})
  include(${CPACK_PROPERTIES_FILE})
endif()
