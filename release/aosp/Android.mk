# Modules staged by stage_native.py. Names match the V73 startup paths.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := gralloc.kindle
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := hal/gralloc/gralloc.cpp hal/gralloc/mapper.cpp hal/gralloc/framebuffer.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/common
LOCAL_CPPFLAGS := -std=gnu++14
LOCAL_SHARED_LIBRARIES := libcutils liblog libdl
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libGLES_kindleroid
LOCAL_SRC_FILES := gles/kd-gles-dispatch.c gles/kd-gles-trampolines.S
LOCAL_SHARED_LIBRARIES := libdl liblog
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := kdhealthd
LOCAL_SRC_FILES := hal/kdhealthd/kdhealthd.cpp
LOCAL_CPPFLAGS := -std=gnu++14
LOCAL_STATIC_LIBRARIES := libbatteryservice
LOCAL_SHARED_LIBRARIES := libbinder libutils libcutils liblog
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := libkdashmem
LOCAL_SRC_FILES := shims/kdashmem.c
LOCAL_CFLAGS := -fvisibility=hidden
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libkdnonwakealarm
LOCAL_SRC_FILES := shims/nonwake-alarm.c
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libkdkill
LOCAL_SRC_FILES := gles/kd-kill.c
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
