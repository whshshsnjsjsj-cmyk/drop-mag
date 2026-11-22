LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE    := DropMag
LOCAL_SRC_FILES := main.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)          # taruh folder "mod/" (amlmod.h, logger.h) dari repo AML di sini
LOCAL_CFLAGS    := -O2 -std=c++17 -DNDEBUG
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
LOCAL_CFLAGS    += -mfloat-abi=softfp
endif
LOCAL_LDLIBS    := -llog -lOpenSLES
include $(BUILD_SHARED_LIBRARY)
