set(SOURCES DmxUtils.cpp
            DmxDPMSSupport.cpp
            VideoSyncDmx.cpp
            WinSystemDmx.cpp
            WinSystemDmxEGLContext.cpp)

set(HEADERS DmxUtils.h
            DmxDPMSSupport.h
            VideoSyncDmx.h
            WinSystemDmx.h
            WinSystemDmxEGLContext.h)

if(OPENGLES_FOUND)
  list(APPEND SOURCES WinSystemDmxGLESContext.cpp)
  list(APPEND HEADERS WinSystemDmxGLESContext.h)
endif()

core_add_library(windowing_dmx)