#include "VRBase.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

static bool vr_platform[VR_PLATFORM_MAX];
static engine_t vr_engine;
int vr_initialized = 0;

extern void VRLog(const char* msg);  // defined in PPSSPPVR.cpp

void VR_Init( void* system, const char* name, int version ) {
	if (vr_initialized)
		return;

	VRLog("[VR_Init] calling XRLoad...");
	if (!XRLoad()) {
		VRLog("[VR_Init] XRLoad failed");
		return;
	}
	VRLog("[VR_Init] XRLoad OK");

	ovrApp_Clear(&vr_engine.appState);
	VRLog("[VR_Init] ovrApp_Clear OK");

#ifdef ANDROID
	PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
	xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);
	if (xrInitializeLoaderKHR != NULL) {
		ovrJava* java = (ovrJava*)system;
		XrLoaderInitInfoAndroidKHR loaderInitializeInfo;
		memset(&loaderInitializeInfo, 0, sizeof(loaderInitializeInfo));
		loaderInitializeInfo.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
		loaderInitializeInfo.next = NULL;
		loaderInitializeInfo.applicationVM = java->Vm;
		loaderInitializeInfo.applicationContext = java->ActivityObject;
		xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*)&loaderInitializeInfo);
	}
#endif

	std::vector<const char *> extensions;
#if XR_USE_GRAPHICS_API_OPENGL && !defined(ANDROID)
	extensions.push_back(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
#elif defined(XR_USE_GRAPHICS_API_OPENGL_ES)
	extensions.push_back(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
#endif
#ifdef ANDROID
	extensions.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
#endif
#ifdef ANDROID
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_INSTANCE)) {
		extensions.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
	}
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PASSTHROUGH)) {
		extensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
	}
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PERFORMANCE)) {
		extensions.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
		extensions.push_back(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME);
	}
#endif

	// Check for depth composition layer extension (all platforms)
	{
		uint32_t extCount = 0;
		xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
		if (extCount > 0) {
			std::vector<XrExtensionProperties> extProps(extCount);
			for (uint32_t i = 0; i < extCount; i++) {
				extProps[i].type = XR_TYPE_EXTENSION_PROPERTIES;
				extProps[i].next = nullptr;
			}
			xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, extProps.data());
			for (const auto& ext : extProps) {
				if (strcmp(ext.extensionName, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME) == 0) {
					extensions.push_back(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
					VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_DEPTH, true);
					VRLog("[VR_Init] Depth composition layer extension available and enabled");
					break;
				}
			}
		}
	}

	// Create the OpenXR instance.
	XrApplicationInfo appInfo;
	memset(&appInfo, 0, sizeof(appInfo));
	strcpy(appInfo.applicationName, name);
	strcpy(appInfo.engineName, name);
	appInfo.applicationVersion = version;
	appInfo.engineVersion = version;
	appInfo.apiVersion = XR_API_VERSION_1_0;

	XrInstanceCreateInfo instanceCreateInfo;
	memset(&instanceCreateInfo, 0, sizeof(instanceCreateInfo));
	instanceCreateInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.next = NULL;
	instanceCreateInfo.createFlags = 0;
	instanceCreateInfo.applicationInfo = appInfo;
#ifdef OPENXR_VALIDATION
	const char* enabledApiLayers[] = { "XR_APILAYER_LUNARG_core_validation" };
	instanceCreateInfo.enabledApiLayerCount = 1;
	instanceCreateInfo.enabledApiLayerNames = enabledApiLayers;
#else
	instanceCreateInfo.enabledApiLayerCount = 0;
	instanceCreateInfo.enabledApiLayerNames = NULL;
#endif
	instanceCreateInfo.enabledExtensionCount = (uint32_t)extensions.size();
	instanceCreateInfo.enabledExtensionNames = extensions.data();

#ifdef ANDROID
	XrInstanceCreateInfoAndroidKHR instanceCreateInfoAndroid = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_INSTANCE)) {
		ovrJava* java = (ovrJava*)system;
		instanceCreateInfoAndroid.applicationVM = java->Vm;
		instanceCreateInfoAndroid.applicationActivity = java->ActivityObject;
		instanceCreateInfo.next = (XrBaseInStructure*)&instanceCreateInfoAndroid;
	}
#endif

	VRLog("[VR_Init] calling xrCreateInstance...");
	XrResult initResult;
	OXR(initResult = xrCreateInstance(&instanceCreateInfo, &vr_engine.appState.Instance));
	if (initResult != XR_SUCCESS) {
		VRLog("[VR_Init] xrCreateInstance FAILED");
		ALOGE("Failed to create XR instance: %d.", initResult);
#if XR_USE_PLATFORM_WIN32
		// Graceful fallback: no VR runtime available, continue as non-VR
		return;
#else
		exit(1);
#endif
	}
	VRLog("[VR_Init] xrCreateInstance OK");

	XRLoadInstanceFunctions(vr_engine.appState.Instance);
	VRLog("[VR_Init] XRLoadInstanceFunctions OK");

	XrInstanceProperties instanceInfo;
	instanceInfo.type = XR_TYPE_INSTANCE_PROPERTIES;
	instanceInfo.next = NULL;
	OXR(xrGetInstanceProperties(vr_engine.appState.Instance, &instanceInfo));
	VRLog("[VR_Init] xrGetInstanceProperties OK");
	ALOGV(
			"Runtime %s: Version : %u.%u.%u",
			instanceInfo.runtimeName,
			XR_VERSION_MAJOR(instanceInfo.runtimeVersion),
			XR_VERSION_MINOR(instanceInfo.runtimeVersion),
			XR_VERSION_PATCH(instanceInfo.runtimeVersion));

	XrSystemGetInfo systemGetInfo;
	memset(&systemGetInfo, 0, sizeof(systemGetInfo));
	systemGetInfo.type = XR_TYPE_SYSTEM_GET_INFO;
	systemGetInfo.next = NULL;
	systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	VRLog("[VR_Init] calling xrGetSystem...");
	XrSystemId systemId;
	OXR(initResult = xrGetSystem(vr_engine.appState.Instance, &systemGetInfo, &systemId));
	if (initResult != XR_SUCCESS) {
		VRLog("[VR_Init] xrGetSystem FAILED");
		ALOGE("Failed to get system.");
#if XR_USE_PLATFORM_WIN32
		// Graceful fallback: no headset connected, continue as non-VR
		xrDestroyInstance(vr_engine.appState.Instance);
		vr_engine.appState.Instance = XR_NULL_HANDLE;
		return;
#else
		exit(1);
#endif
	}
	VRLog("[VR_Init] xrGetSystem OK");

	// Get the graphics requirements.
#if XR_USE_GRAPHICS_API_OPENGL && !defined(ANDROID)
	VRLog("[VR_Init] calling xrGetOpenGLGraphicsRequirementsKHR...");
	PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetOpenGLGraphicsRequirementsKHR = NULL;
	OXR(xrGetInstanceProcAddr(
			vr_engine.appState.Instance,
			"xrGetOpenGLGraphicsRequirementsKHR",
			(PFN_xrVoidFunction*)(&pfnGetOpenGLGraphicsRequirementsKHR)));

	XrGraphicsRequirementsOpenGLKHR graphicsRequirements = {};
	graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
	OXR(pfnGetOpenGLGraphicsRequirementsKHR(vr_engine.appState.Instance, systemId, &graphicsRequirements));
	VRLog("[VR_Init] graphics requirements OK");
#elif defined(XR_USE_GRAPHICS_API_OPENGL_ES)
	PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetOpenGLESGraphicsRequirementsKHR = NULL;
	OXR(xrGetInstanceProcAddr(
			vr_engine.appState.Instance,
			"xrGetOpenGLESGraphicsRequirementsKHR",
			(PFN_xrVoidFunction*)(&pfnGetOpenGLESGraphicsRequirementsKHR)));

	XrGraphicsRequirementsOpenGLESKHR graphicsRequirements = {};
	graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
	OXR(pfnGetOpenGLESGraphicsRequirementsKHR(vr_engine.appState.Instance, systemId, &graphicsRequirements));
#endif

#ifdef ANDROID
	vr_engine.appState.MainThreadTid = gettid();
#endif
	vr_engine.appState.SystemId = systemId;
	vr_initialized = 1;
}

void VR_Destroy( engine_t* engine ) {
	if (engine == &vr_engine) {
		xrDestroyInstance(engine->appState.Instance);
		ovrApp_Destroy(&engine->appState);
	}
}

#if XR_USE_PLATFORM_WIN32
void VR_EnterVR( engine_t* engine, HDC hDC, HGLRC hGLRC ) {
#else
void VR_EnterVR( engine_t* engine ) {
#endif

	if (engine->appState.Session) {
		ALOGE("VR_EnterVR called with existing session");
		return;
	}

	// Create the OpenXR Session.
	XrSessionCreateInfo sessionCreateInfo = {};
	memset(&sessionCreateInfo, 0, sizeof(sessionCreateInfo));

#if XR_USE_PLATFORM_WIN32
	XrGraphicsBindingOpenGLWin32KHR graphicsBindingGL = {};
	graphicsBindingGL.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
	graphicsBindingGL.next = NULL;
	graphicsBindingGL.hDC = hDC;
	graphicsBindingGL.hGLRC = hGLRC;
	sessionCreateInfo.next = &graphicsBindingGL;
#elif defined(ANDROID)
	XrGraphicsBindingOpenGLESAndroidKHR graphicsBindingGL = {};
	graphicsBindingGL.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR;
	graphicsBindingGL.next = NULL;
	graphicsBindingGL.display = eglGetCurrentDisplay();
	graphicsBindingGL.config = NULL;
	graphicsBindingGL.context = eglGetCurrentContext();
	sessionCreateInfo.next = &graphicsBindingGL;
#endif
	sessionCreateInfo.type = XR_TYPE_SESSION_CREATE_INFO;
	sessionCreateInfo.createFlags = 0;
	sessionCreateInfo.systemId = engine->appState.SystemId;

	XrResult initResult;
	OXR(initResult = xrCreateSession(engine->appState.Instance, &sessionCreateInfo, &engine->appState.Session));
	if (initResult != XR_SUCCESS) {
		ALOGE("Failed to create XR session: %d.", initResult);
#if XR_USE_PLATFORM_WIN32
		// Graceful fallback: session creation failed, continue as non-VR
		return;
#else
		exit(1);
#endif
	}

	// Create a space to the first path
	XrReferenceSpaceCreateInfo spaceCreateInfo = {};
	spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
	OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.HeadSpace));
}

void VR_LeaveVR( engine_t* engine ) {
	if (engine->appState.Session) {
		OXR(xrDestroySpace(engine->appState.HeadSpace));
		// StageSpace is optional.
		if (engine->appState.StageSpace != XR_NULL_HANDLE) {
			OXR(xrDestroySpace(engine->appState.StageSpace));
		}
		OXR(xrDestroySpace(engine->appState.FakeStageSpace));
		engine->appState.CurrentSpace = XR_NULL_HANDLE;
		OXR(xrDestroySession(engine->appState.Session));
		engine->appState.Session = XR_NULL_HANDLE;
	}
}

engine_t* VR_GetEngine( void ) {
	return &vr_engine;
}

bool VR_GetPlatformFlag(VRPlatformFlag flag) {
	return vr_platform[flag];
}

void VR_SetPlatformFLag(VRPlatformFlag flag, bool value) {
	vr_platform[flag] = value;
}
