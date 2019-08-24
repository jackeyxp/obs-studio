# Install script for directory: F:/obs-studio/plugins

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files/obs-studio")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("F:/obs-studio/vsbuild/plugins/coreaudio-encoder/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/win-wasapi/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/win-dshow/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/win-capture/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/decklink/win/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/win-mf/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/obs-qsv11/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/vlc-video/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/enc-amf/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/win-ivcam/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/obs-vst/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/image-source/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/obs-x264/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/obs-libfdk/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/obs-ffmpeg/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/obs-outputs/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/obs-filters/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/obs-transitions/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/obs-text/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/rtmp-services/cmake_install.cmake")
  include("F:/obs-studio/vsbuild/plugins/text-freetype2/cmake_install.cmake")

endif()

