set(SOURCES VideoBuffer.cpp)
set(HEADERS VideoBuffer.h)

if("gbm" IN_LIST CORE_PLATFORM_NAME_LC OR "wayland" IN_LIST CORE_PLATFORM_NAME_LC)
  list(APPEND SOURCES VideoBufferDMA.cpp
                      VideoBufferDRMPRIME.cpp
                      VideoBufferPoolDMA.cpp)
  list(APPEND HEADERS VideoBufferDMA.h
                      VideoBufferDRMPRIME.h
                      VideoBufferPoolDMA.h)
endif()

if(MMAL_FOUND)
  list(APPEND SOURCES VideoBufferMMAL.cpp
                      VideoBufferPoolMMAL.cpp)
  list(APPEND HEADERS VideoBufferMMAL.h
                      VideoBufferPoolMMAL.h)
endif()

core_add_library(videoplayer-buffers)
