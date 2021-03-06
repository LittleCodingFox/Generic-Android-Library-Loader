////////////////////////////////////////////////////////////
//
// GALL - Generic Android Library Loader
// Adapted from public domain code provided by Jonathan De Wachter (dewachter.jonathan@gmail.com)
// Copyright (C) 2014 Nuno Silva (little.coding.fox@gmail.com)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it freely,
// subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented;
//    you must not claim that you wrote the original software.
//    If you use this software in a product, an acknowledgment
//    in the product documentation would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such,
//    and must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
////////////////////////////////////////////////////////////

#include <string>
#include <vector>
#include <android/native_activity.h>
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <jni.h>

#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_INFO, "GALL", __VA_ARGS__))

namespace {
    typedef void (*activityOnCreatePointer)(ANativeActivity*, void*, size_t);
}

std::string getMetadata(JNIEnv* lJNIEnv, jobject& objectActivityInfo, const char *metaName)
{
    // This function reads the value of meta-data "loader.app.lib_name"
    // found in the Android Manifest file and returns it. It performs the
    // following java code using the JNI interface:
    //
    // ai.metaData.getString(metaName);

    // Get metaData instance from the ActivityInfo object
    jclass classActivityInfo = lJNIEnv->FindClass("android/content/pm/ActivityInfo");
    jfieldID fieldMetaData = lJNIEnv->GetFieldID(classActivityInfo, "metaData", "Landroid/os/Bundle;");
    jobject objectMetaData = lJNIEnv->GetObjectField(objectActivityInfo, fieldMetaData);

    // Create a java string object containing the metaname
    jobject objectName = lJNIEnv->NewStringUTF(metaName);

    // Get the value of meta-data named "gall.dependencies"
    jclass classBundle = lJNIEnv->FindClass("android/os/Bundle");
    jmethodID methodGetString = lJNIEnv->GetMethodID(classBundle, "getString", "(Ljava/lang/String;)Ljava/lang/String;");
    jstring valueString = (jstring)lJNIEnv->CallObjectMethod(objectMetaData, methodGetString, objectName);

    // No meta-data "gall.dependencies" was found so we abord and inform the user
    if (valueString == NULL)
    {
        LOGE("No meta-data '%s' found in AndroidManifest.xml file", metaName);
        exit(1);
    }

    // Convert the application name to a C++ string and return it
    const jsize applicationNameLength = lJNIEnv->GetStringUTFLength(valueString);
    const char* applicationName = lJNIEnv->GetStringUTFChars(valueString, NULL);
    std::string ret(applicationName, applicationNameLength);
    lJNIEnv->ReleaseStringUTFChars(valueString, applicationName);

    return ret;
}

void* loadLibrary(const char* libraryName, JNIEnv* lJNIEnv, jobject& ObjectActivityInfo)
{
    // Find out the absolute path of the library
    jclass ClassActivityInfo = lJNIEnv->FindClass("android/content/pm/ActivityInfo");
    jfieldID FieldApplicationInfo = lJNIEnv->GetFieldID(ClassActivityInfo, "applicationInfo", "Landroid/content/pm/ApplicationInfo;");
    jobject ObjectApplicationInfo = lJNIEnv->GetObjectField(ObjectActivityInfo, FieldApplicationInfo);

    jclass ClassApplicationInfo = lJNIEnv->FindClass("android/content/pm/ApplicationInfo");
    jfieldID FieldNativeLibraryDir = lJNIEnv->GetFieldID(ClassApplicationInfo, "nativeLibraryDir", "Ljava/lang/String;");

    jobject ObjectDirPath = lJNIEnv->GetObjectField(ObjectApplicationInfo, FieldNativeLibraryDir);

    jclass ClassSystem = lJNIEnv->FindClass("java/lang/System");
    jmethodID StaticMethodMapLibraryName = lJNIEnv->GetStaticMethodID(ClassSystem, "mapLibraryName", "(Ljava/lang/String;)Ljava/lang/String;");

    jstring LibNameObject = lJNIEnv->NewStringUTF(libraryName);
    jobject ObjectName = lJNIEnv->CallStaticObjectMethod(ClassSystem, StaticMethodMapLibraryName, LibNameObject);

    jclass ClassFile = lJNIEnv->FindClass("java/io/File");
    jmethodID FileConstructor = lJNIEnv->GetMethodID(ClassFile, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V");
    jobject ObjectFile = lJNIEnv->NewObject(ClassFile, FileConstructor, ObjectDirPath, ObjectName);

    // Get the library absolute path and convert it
    jmethodID MethodGetPath = lJNIEnv->GetMethodID(ClassFile, "getPath", "()Ljava/lang/String;");
    jstring javaLibraryPath = static_cast<jstring>(lJNIEnv->CallObjectMethod(ObjectFile, MethodGetPath));
    const char* libraryPath = lJNIEnv->GetStringUTFChars(javaLibraryPath, NULL);
	
	LOGE("Loading %s ('%s')", libraryName, libraryPath);

    // Manually load the library
    void * handle = dlopen(libraryPath, RTLD_NOW | RTLD_GLOBAL);

    if (!handle)
    {
        LOGE("dlopen(\"%s\"): %s", libraryPath, dlerror());
        exit(1);
    }
	
	LOGE("Successfully loaded '%s'", libraryName);

    // Release the Java string
    lJNIEnv->ReleaseStringUTFChars(javaLibraryPath, libraryPath);

    return handle;
}

void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize)
{
    // Before we can load a library, we need to find out its location. As
    // we're powerless here in C/C++, we need the JNI interface to communicate
    // with the attached Java virtual machine and perform some Java calls in
    // order to retrieve the absolute path of our libraries.
    //
    // Here's the snippet of Java code it performs:
    // --------------------------------------------
    // ai = getPackageManager().getActivityInfo(getIntent().getComponent(), PackageManager.GET_META_DATA);
    // File libraryFile = new File(ai.applicationInfo.nativeLibraryDir, System.mapLibraryName(libname));
    // String path = libraryFile.getPath();
    //
    // With libname being the library name such as "jpeg".

    // Retrieve JNI environment and JVM instance
    JavaVM* lJavaVM = activity->vm;
    JNIEnv* lJNIEnv = activity->env;

    // Retrieve the NativeActivity
    jobject ObjectNativeActivity = activity->clazz;
    jclass ClassNativeActivity = lJNIEnv->GetObjectClass(ObjectNativeActivity);

    // Retrieve the ActivityInfo
    jmethodID MethodGetPackageManager = lJNIEnv->GetMethodID(ClassNativeActivity, "getPackageManager", "()Landroid/content/pm/PackageManager;");
    jobject ObjectPackageManager = lJNIEnv->CallObjectMethod(ObjectNativeActivity, MethodGetPackageManager);

    jmethodID MethodGetIndent = lJNIEnv->GetMethodID(ClassNativeActivity, "getIntent", "()Landroid/content/Intent;");
    jobject ObjectIntent = lJNIEnv->CallObjectMethod(ObjectNativeActivity, MethodGetIndent);

    jclass ClassIntent = lJNIEnv->FindClass("android/content/Intent");
    jmethodID MethodGetComponent = lJNIEnv->GetMethodID(ClassIntent, "getComponent", "()Landroid/content/ComponentName;");

    jobject ObjectComponentName = lJNIEnv->CallObjectMethod(ObjectIntent, MethodGetComponent);

    jclass ClassPackageManager = lJNIEnv->FindClass("android/content/pm/PackageManager");

    jfieldID FieldGET_META_DATA = lJNIEnv->GetStaticFieldID(ClassPackageManager, "GET_META_DATA", "I");
    jint GET_META_DATA = lJNIEnv->GetStaticIntField(ClassPackageManager, FieldGET_META_DATA);

    jmethodID MethodGetActivityInfo = lJNIEnv->GetMethodID(ClassPackageManager, "getActivityInfo", "(Landroid/content/ComponentName;I)Landroid/content/pm/ActivityInfo;");
    jobject ObjectActivityInfo = lJNIEnv->CallObjectMethod(ObjectPackageManager, MethodGetActivityInfo, ObjectComponentName, GET_META_DATA);

    std::string dependenciesString = getMetadata(lJNIEnv, ObjectActivityInfo, "gall.dependencies");
	
	std::vector<std::string> dependencies;
	
	size_t offset = dependenciesString.find("|"), lastOffset = 0;
	
	while(offset != std::string::npos)
	{
		dependencies.push_back(dependenciesString.substr(lastOffset, offset - lastOffset));

		LOGE("Adding dependency '%s'", dependencies.back().c_str());
		
		lastOffset = offset + 1;
		
		offset = dependenciesString.find("|", lastOffset);
	}
	
	if(lastOffset == 0 || (offset == std::string::npos && lastOffset != dependenciesString.length() - 1))
	{
		dependencies.push_back(dependenciesString.substr(lastOffset, dependenciesString.length() - lastOffset));

		LOGE("Adding dependency '%s'", dependencies.back().c_str());
	}
	
	for(int i = 0; i < dependencies.size(); i++)
	{
		loadLibrary(dependencies[i].c_str(), lJNIEnv, ObjectActivityInfo);
	}

    std::string libName = getMetadata(lJNIEnv, ObjectActivityInfo, "gall.target");
	
	LOGE("Loading Target '%s'", libName.c_str());
	
    void* handle = loadLibrary(libName.c_str(), lJNIEnv, ObjectActivityInfo);

    // Call the original ANativeActivity_onCreate function
    activityOnCreatePointer ANativeActivity_onCreate = (activityOnCreatePointer)dlsym(handle, "ANativeActivity_onCreate");

    if (!ANativeActivity_onCreate)
    {
        LOGE("Undefined symbol ANativeActivity_onCreate");
        exit(1);
    }
	
	LOGE("Starting Target!");

    ANativeActivity_onCreate(activity, savedState, savedStateSize);
}
