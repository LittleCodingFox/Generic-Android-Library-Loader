LOCAL_PATH := $(call my-dir)
LOCAL_PATH_MINUS_ONE_LEVEL := $(LOCAL_PATH)/../

include $(CLEAR_VARS)

LOCAL_MODULE := GALL
LOCAL_SRC_FILES := GALL.cpp
LOCAL_LDLIBS := -llog -landroid -ldl

include $(BUILD_SHARED_LIBRARY)
