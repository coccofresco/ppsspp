#include "VRFramebuffer.h"

#if XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES

#include "Common/GPU/OpenGL/GLCommon.h"

#endif

#if XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_GL_IMAGE XrSwapchainImageOpenGLESKHR
#define XR_GL_SWAPCHAIN XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR
#else
#define XR_GL_IMAGE XrSwapchainImageOpenGLKHR
#define XR_GL_SWAPCHAIN XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <cassert>
#include <vector>

extern void VRLog(const char* msg);

#if !defined(_WIN32)
#include <pthread.h>
#endif

double FromXrTime(const XrTime time) {
	return (time * 1e-9);
}

/*
================================================================================

ovrFramebuffer

================================================================================
*/


void ovrFramebuffer_Clear(ovrFramebuffer* frameBuffer) {
	frameBuffer->Width = 0;
	frameBuffer->Height = 0;
	frameBuffer->TextureSwapChainLength = 0;
	frameBuffer->TextureSwapChainIndex = 0;
	frameBuffer->ColorSwapChain.Handle = XR_NULL_HANDLE;
	frameBuffer->ColorSwapChain.Width = 0;
	frameBuffer->ColorSwapChain.Height = 0;
	frameBuffer->ColorSwapChainImage = NULL;
	frameBuffer->DepthSwapChain.Handle = XR_NULL_HANDLE;
	frameBuffer->DepthSwapChain.Width = 0;
	frameBuffer->DepthSwapChain.Height = 0;
	frameBuffer->DepthSwapChainImage = NULL;
	frameBuffer->HasDepthSwapchain = false;

	frameBuffer->GLDepthBuffers = NULL;
	frameBuffer->GLFrameBuffers = NULL;
	frameBuffer->Acquired = false;
}

#if XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES

static const char* GlErrorString(GLenum error) {
	switch (error) {
	case GL_NO_ERROR:
		return "GL_NO_ERROR";
	case GL_INVALID_ENUM:
		return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE:
		return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION:
		return "GL_INVALID_OPERATION";
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		return "GL_INVALID_FRAMEBUFFER_OPERATION";
	case GL_OUT_OF_MEMORY:
		return "GL_OUT_OF_MEMORY";
	default:
		return "unknown";
	}
}

void GLCheckErrors(const char* file, int line) {
	for (int i = 0; i < 10; i++) {
		const GLenum error = glGetError();
		if (error == GL_NO_ERROR) {
			break;
		}
		ALOGE("GL error on line %s:%d %s", file, line, GlErrorString(error));
	}
}

#endif

#if XR_USE_GRAPHICS_API_OPENGL_ES || XR_USE_GRAPHICS_API_OPENGL

static bool ovrFramebuffer_CreateGL(XrSession session, ovrFramebuffer* frameBuffer, int width, int height) {
	frameBuffer->Width = width;
	frameBuffer->Height = height;

	XrSwapchainCreateInfo swapChainCreateInfo;
	memset(&swapChainCreateInfo, 0, sizeof(swapChainCreateInfo));
	swapChainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	swapChainCreateInfo.sampleCount = 1;
	swapChainCreateInfo.width = width;
	swapChainCreateInfo.height = height;
	swapChainCreateInfo.faceCount = 1;
	swapChainCreateInfo.mipCount = 1;
	swapChainCreateInfo.arraySize = 1;

	frameBuffer->ColorSwapChain.Width = swapChainCreateInfo.width;
	frameBuffer->ColorSwapChain.Height = swapChainCreateInfo.height;

	// Create the color swapchain.
	swapChainCreateInfo.format = GL_SRGB8_ALPHA8;
	swapChainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	OXR(xrCreateSwapchain(session, &swapChainCreateInfo, &frameBuffer->ColorSwapChain.Handle));
	OXR(xrEnumerateSwapchainImages(frameBuffer->ColorSwapChain.Handle, 0, &frameBuffer->TextureSwapChainLength, NULL));
	frameBuffer->ColorSwapChainImage = malloc(frameBuffer->TextureSwapChainLength * sizeof(XR_GL_IMAGE));

	// Populate the swapchain image array.
	for (uint32_t i = 0; i < frameBuffer->TextureSwapChainLength; i++) {
		((XR_GL_IMAGE*)frameBuffer->ColorSwapChainImage)[i].type = XR_GL_SWAPCHAIN;
		((XR_GL_IMAGE*)frameBuffer->ColorSwapChainImage)[i].next = NULL;
	}
	OXR(xrEnumerateSwapchainImages(
			frameBuffer->ColorSwapChain.Handle,
			frameBuffer->TextureSwapChainLength,
			&frameBuffer->TextureSwapChainLength,
			(XrSwapchainImageBaseHeader*)frameBuffer->ColorSwapChainImage));

	frameBuffer->GLDepthBuffers = (GLuint*)malloc(frameBuffer->TextureSwapChainLength * sizeof(GLuint));
	frameBuffer->GLFrameBuffers = (GLuint*)malloc(frameBuffer->TextureSwapChainLength * sizeof(GLuint));
	for (uint32_t i = 0; i < frameBuffer->TextureSwapChainLength; i++) {
		const GLuint colorTexture = ((XR_GL_IMAGE*)frameBuffer->ColorSwapChainImage)[i].image;

		// Create the depth buffer.
		GL(glGenRenderbuffers(1, &frameBuffer->GLDepthBuffers[i]));
		GL(glBindRenderbuffer(GL_RENDERBUFFER, frameBuffer->GLDepthBuffers[i]));
		GL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height));
		GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

		// Create the frame buffer.
		GL(glGenFramebuffers(1, &frameBuffer->GLFrameBuffers[i]));
		GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->GLFrameBuffers[i]));
		GL(glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, frameBuffer->GLDepthBuffers[i]));
		GL(glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, frameBuffer->GLDepthBuffers[i]));
		GL(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0));
		GL(GLenum renderFramebufferStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
		GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
		if (renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
			ALOGE("Incomplete frame buffer object: %d", renderFramebufferStatus);
			return false;
		}
	}

	// Attempt to create depth swapchain for runtime reprojection
	frameBuffer->HasDepthSwapchain = false;
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_DEPTH)) {
		// Enumerate supported swapchain formats to find a depth format
		uint32_t formatCount = 0;
		xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr);
		if (formatCount > 0) {
			std::vector<int64_t> formats(formatCount);
			xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data());

			// Prefer 32F > 24 > 16 depth formats
			const int64_t preferredDepthFormats[] = {
				GL_DEPTH_COMPONENT32F,
				GL_DEPTH_COMPONENT24,
				GL_DEPTH_COMPONENT16,
			};
			int64_t depthFormat = 0;
			for (auto preferred : preferredDepthFormats) {
				for (auto available : formats) {
					if (available == preferred) {
						depthFormat = preferred;
						break;
					}
				}
				if (depthFormat) break;
			}

			if (depthFormat != 0) {
				XrSwapchainCreateInfo depthCI = {};
				depthCI.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
				depthCI.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
				depthCI.format = depthFormat;
				depthCI.sampleCount = 1;
				depthCI.width = width;
				depthCI.height = height;
				depthCI.faceCount = 1;
				depthCI.arraySize = 1;
				depthCI.mipCount = 1;

				XrResult depthResult = xrCreateSwapchain(session, &depthCI, &frameBuffer->DepthSwapChain.Handle);
				if (XR_SUCCEEDED(depthResult)) {
					frameBuffer->DepthSwapChain.Width = width;
					frameBuffer->DepthSwapChain.Height = height;

					// Enumerate depth swapchain images
					uint32_t depthImageCount = 0;
					xrEnumerateSwapchainImages(frameBuffer->DepthSwapChain.Handle, 0, &depthImageCount, NULL);
					frameBuffer->DepthSwapChainImage = malloc(depthImageCount * sizeof(XR_GL_IMAGE));
					for (uint32_t i = 0; i < depthImageCount; i++) {
						((XR_GL_IMAGE*)frameBuffer->DepthSwapChainImage)[i].type = XR_GL_SWAPCHAIN;
						((XR_GL_IMAGE*)frameBuffer->DepthSwapChainImage)[i].next = NULL;
					}
					xrEnumerateSwapchainImages(
						frameBuffer->DepthSwapChain.Handle,
						depthImageCount, &depthImageCount,
						(XrSwapchainImageBaseHeader*)frameBuffer->DepthSwapChainImage);

					frameBuffer->HasDepthSwapchain = true;

					{
						char buf[256];
						snprintf(buf, sizeof(buf), "[VR] Depth swapchain created: format=0x%X %dx%d images=%u",
							(unsigned int)depthFormat, width, height, depthImageCount);
						VRLog(buf);
					}
				} else {
					char buf[256];
					snprintf(buf, sizeof(buf), "[VR] Depth swapchain creation failed: result=%d (SteamVR OpenGL depth bug?)", (int)depthResult);
					VRLog(buf);
				}
			} else {
				VRLog("[VR] No depth swapchain format available from runtime");
			}
		}
	}

	return true;
}

#endif

void ovrFramebuffer_Destroy(ovrFramebuffer* frameBuffer) {
#if XR_USE_GRAPHICS_API_OPENGL_ES || XR_USE_GRAPHICS_API_OPENGL
	GL(glDeleteRenderbuffers(frameBuffer->TextureSwapChainLength, frameBuffer->GLDepthBuffers));
	GL(glDeleteFramebuffers(frameBuffer->TextureSwapChainLength, frameBuffer->GLFrameBuffers));
	free(frameBuffer->GLDepthBuffers);
	free(frameBuffer->GLFrameBuffers);
#endif
	if (frameBuffer->HasDepthSwapchain) {
		xrDestroySwapchain(frameBuffer->DepthSwapChain.Handle);
		free(frameBuffer->DepthSwapChainImage);
	}
	OXR(xrDestroySwapchain(frameBuffer->ColorSwapChain.Handle));
	free(frameBuffer->ColorSwapChainImage);

	ovrFramebuffer_Clear(frameBuffer);
}

void* ovrFramebuffer_SetCurrent(ovrFramebuffer* frameBuffer) {
#if XR_USE_GRAPHICS_API_OPENGL_ES || XR_USE_GRAPHICS_API_OPENGL
	GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->GLFrameBuffers[frameBuffer->TextureSwapChainIndex]));
#endif
	return nullptr;
}

void ovrFramebuffer_Acquire(ovrFramebuffer* frameBuffer) {
	XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, NULL};
	OXR(xrAcquireSwapchainImage(frameBuffer->ColorSwapChain.Handle, &acquireInfo, &frameBuffer->TextureSwapChainIndex));

	XrSwapchainImageWaitInfo waitInfo;
	waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
	waitInfo.next = NULL;
	waitInfo.timeout = XR_INFINITE_DURATION;
	XrResult res = xrWaitSwapchainImage(frameBuffer->ColorSwapChain.Handle, &waitInfo);
	frameBuffer->Acquired = res == XR_SUCCESS;

	if (frameBuffer->HasDepthSwapchain) {
		uint32_t depthIndex;
		XrSwapchainImageAcquireInfo depthAcquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, NULL};
		xrAcquireSwapchainImage(frameBuffer->DepthSwapChain.Handle, &depthAcquireInfo, &depthIndex);
		XrSwapchainImageWaitInfo depthWaitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, NULL, XR_INFINITE_DURATION};
		xrWaitSwapchainImage(frameBuffer->DepthSwapChain.Handle, &depthWaitInfo);
		// NOTE: Do NOT attach depth swapchain texture as GL_DEPTH_ATTACHMENT here.
		// The FBO uses a packed GL_DEPTH24_STENCIL8 renderbuffer for both depth and stencil.
		// Replacing GL_DEPTH_ATTACHMENT with a depth-only texture makes the FBO incomplete
		// (stencil still references the packed renderbuffer, violating GL spec).
		// Depth data is acquired/released for the OpenXR lifecycle but not used for composition.
	}

	ovrFramebuffer_SetCurrent(frameBuffer);

#if XR_USE_GRAPHICS_API_OPENGL_ES || XR_USE_GRAPHICS_API_OPENGL
	GL(glEnable( GL_SCISSOR_TEST ));
	GL(glViewport( 0, 0, frameBuffer->Width, frameBuffer->Height ));
	GL(glClearColor( 0.0f, 0.0f, 0.0f, 1.0f ));
	GL(glScissor( 0, 0, frameBuffer->Width, frameBuffer->Height ));
	GL(glClear( GL_COLOR_BUFFER_BIT ));
	GL(glScissor( 0, 0, 0, 0 ));
	GL(glDisable( GL_SCISSOR_TEST ));
#endif
}

void ovrFramebuffer_Release(ovrFramebuffer* frameBuffer) {
	if (frameBuffer->Acquired) {
#if XR_USE_GRAPHICS_API_OPENGL_ES || XR_USE_GRAPHICS_API_OPENGL
		// Ensure we're bound to the correct VR FBO before alpha clear
		ovrFramebuffer_SetCurrent(frameBuffer);

		// Clear the alpha channel to 1.0 BEFORE releasing to OpenXR.
		// MUST disable scissor — PPSSPP 3D rendering may leave scissor enabled,
		// and glClear is affected by scissor test, causing partial alpha clear.
		GL(glDisable(GL_SCISSOR_TEST));
		GL(glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE));
		GL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
		GL(glClear(GL_COLOR_BUFFER_BIT));
		GL(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));

#endif

		if (frameBuffer->HasDepthSwapchain) {
			XrSwapchainImageReleaseInfo depthReleaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, NULL};
			xrReleaseSwapchainImage(frameBuffer->DepthSwapChain.Handle, &depthReleaseInfo);
		}

		XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, NULL};
		OXR(xrReleaseSwapchainImage(frameBuffer->ColorSwapChain.Handle, &releaseInfo));
		frameBuffer->Acquired = false;
	}
}

/*
================================================================================

ovrRenderer

================================================================================
*/

void ovrRenderer_Clear(ovrRenderer* renderer) {
	for (int i = 0; i < ovrMaxNumEyes; i++) {
		ovrFramebuffer_Clear(&renderer->FrameBuffer[i]);
		renderer->StagingTexture[i] = 0;
		renderer->StagingFBO[i] = 0;
	}
	renderer->StagingWidth = 0;
	renderer->StagingHeight = 0;
}

void ovrRenderer_CreateStagingFBO(ovrRenderer* renderer, int width, int height) {
#if XR_USE_GRAPHICS_API_OPENGL_ES || XR_USE_GRAPHICS_API_OPENGL
	renderer->StagingWidth = width;
	renderer->StagingHeight = height;
	for (int i = 0; i < ovrMaxNumEyes; i++) {
		GL(glGenTextures(1, &renderer->StagingTexture[i]));
		GL(glBindTexture(GL_TEXTURE_2D, renderer->StagingTexture[i]));
		GL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
		GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
		GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
		GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
		GL(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16.0f));

		GL(glGenFramebuffers(1, &renderer->StagingFBO[i]));
		GL(glBindFramebuffer(GL_FRAMEBUFFER, renderer->StagingFBO[i]));
		GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderer->StagingTexture[i], 0));
		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			char buf[128];
			snprintf(buf, sizeof(buf), "[StagingFBO] Framebuffer %d incomplete: 0x%x", i, status);
			VRLog(buf);
		}
	}
	GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	GL(glBindTexture(GL_TEXTURE_2D, 0));
	VRLog("[StagingFBO] Created staging FBOs for cylinder correction");
#endif
}

void ovrRenderer_DestroyStagingFBO(ovrRenderer* renderer) {
#if XR_USE_GRAPHICS_API_OPENGL_ES || XR_USE_GRAPHICS_API_OPENGL
	for (int i = 0; i < ovrMaxNumEyes; i++) {
		if (renderer->StagingFBO[i]) {
			GL(glDeleteFramebuffers(1, &renderer->StagingFBO[i]));
			renderer->StagingFBO[i] = 0;
		}
		if (renderer->StagingTexture[i]) {
			GL(glDeleteTextures(1, &renderer->StagingTexture[i]));
			renderer->StagingTexture[i] = 0;
		}
	}
	renderer->StagingWidth = 0;
	renderer->StagingHeight = 0;
#endif
}

void ovrRenderer_Create(XrSession session, ovrRenderer* renderer, int width, int height) {
	for (int i = 0; i < ovrMaxNumEyes; i++) {
#if XR_USE_GRAPHICS_API_OPENGL_ES || XR_USE_GRAPHICS_API_OPENGL
		ovrFramebuffer_CreateGL(session, &renderer->FrameBuffer[i], width, height);
#endif
	}
	ovrRenderer_CreateStagingFBO(renderer, width, height);
}

void ovrRenderer_Destroy(ovrRenderer* renderer) {
	ovrRenderer_DestroyStagingFBO(renderer);
	for (int i = 0; i < ovrMaxNumEyes; i++) {
		ovrFramebuffer_Destroy(&renderer->FrameBuffer[i]);
	}
}

void ovrRenderer_MouseCursor(ovrRenderer* renderer, int x, int y, int sx, int sy) {
#if XR_USE_GRAPHICS_API_OPENGL_ES || XR_USE_GRAPHICS_API_OPENGL
	GL(glEnable(GL_SCISSOR_TEST));
	GL(glScissor(x, y, sx, sy));
	GL(glViewport(x, y, sx, sy));
	GL(glClearColor(1.0f, 1.0f, 1.0f, 1.0f));
	GL(glClear(GL_COLOR_BUFFER_BIT));
	GL(glDisable(GL_SCISSOR_TEST));
#endif
}

void ovrRenderer_StereoDebugWatermark(int fboIndex) {
#if XR_USE_GRAPHICS_API_OPENGL_ES || XR_USE_GRAPHICS_API_OPENGL
	// Diagnostic watermark: FBO 0 = RED, FBO 1 = BLUE
	// Large square in bottom-left corner (1/4 of FBO height)
	// If LEFT eye sees RED → FBO mapping correct → issue is IPD sign (Stage 1/2)
	// If LEFT eye sees BLUE → FBO mapping swapped → issue is FBO/submission (Stage 3/4)
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	int size = viewport[3] / 4;  // 1/4 of FBO height
	if (size < 50) size = 200;   // fallback if viewport not set

	GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
	GL(glEnable(GL_SCISSOR_TEST));
	GL(glScissor(0, 0, size, size));
	if (fboIndex == 0) {
		GL(glClearColor(1.0f, 0.0f, 0.0f, 1.0f));  // RED = FBO 0
	} else {
		GL(glClearColor(0.0f, 0.0f, 1.0f, 1.0f));  // BLUE = FBO 1
	}
	GL(glClear(GL_COLOR_BUFFER_BIT));
	if (!scissorWasEnabled) {
		GL(glDisable(GL_SCISSOR_TEST));
	}
#endif
}

/*
================================================================================

ovrApp

================================================================================
*/

void ovrApp_Clear(ovrApp* app) {
	app->Focused = false;
	app->Instance = XR_NULL_HANDLE;
	app->Session = XR_NULL_HANDLE;
	memset(&app->ViewportConfig, 0, sizeof(XrViewConfigurationProperties));
	memset(&app->ViewConfigurationView, 0, ovrMaxNumEyes * sizeof(XrViewConfigurationView));
	app->SystemId = XR_NULL_SYSTEM_ID;
	app->HeadSpace = XR_NULL_HANDLE;
	app->StageSpace = XR_NULL_HANDLE;
	app->FakeStageSpace = XR_NULL_HANDLE;
	app->CurrentSpace = XR_NULL_HANDLE;
	app->SessionActive = false;
	app->SwapInterval = 1;
	memset(app->Layers, 0, sizeof(ovrCompositorLayer_Union) * ovrMaxLayerCount);
	app->LayerCount = 0;
	app->MainThreadTid = 0;
	app->RenderThreadTid = 0;

	ovrRenderer_Clear(&app->Renderer);
}

void ovrApp_Destroy(ovrApp* app) {
	ovrApp_Clear(app);
}


void ovrApp_HandleSessionStateChanges(ovrApp* app, XrSessionState state) {
	if (state == XR_SESSION_STATE_READY) {
		assert(app->SessionActive == false);

		XrSessionBeginInfo sessionBeginInfo;
		memset(&sessionBeginInfo, 0, sizeof(sessionBeginInfo));
		sessionBeginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
		sessionBeginInfo.next = NULL;
		sessionBeginInfo.primaryViewConfigurationType = app->ViewportConfig.viewConfigurationType;

		XrResult result;
		OXR(result = xrBeginSession(app->Session, &sessionBeginInfo));
		app->SessionActive = (result == XR_SUCCESS);

#ifdef ANDROID
		if (app->SessionActive && VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PERFORMANCE)) {
			XrPerfSettingsLevelEXT cpuPerfLevel = XR_PERF_SETTINGS_LEVEL_BOOST_EXT;
			XrPerfSettingsLevelEXT gpuPerfLevel = XR_PERF_SETTINGS_LEVEL_BOOST_EXT;

			PFN_xrPerfSettingsSetPerformanceLevelEXT pfnPerfSettingsSetPerformanceLevelEXT = NULL;
			OXR(xrGetInstanceProcAddr(
					app->Instance,
					"xrPerfSettingsSetPerformanceLevelEXT",
					(PFN_xrVoidFunction*)(&pfnPerfSettingsSetPerformanceLevelEXT)));

			OXR(pfnPerfSettingsSetPerformanceLevelEXT(app->Session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, cpuPerfLevel));
			OXR(pfnPerfSettingsSetPerformanceLevelEXT(app->Session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, gpuPerfLevel));

			PFN_xrSetAndroidApplicationThreadKHR pfnSetAndroidApplicationThreadKHR = NULL;
			OXR(xrGetInstanceProcAddr(
					app->Instance,
					"xrSetAndroidApplicationThreadKHR",
					(PFN_xrVoidFunction*)(&pfnSetAndroidApplicationThreadKHR)));

			OXR(pfnSetAndroidApplicationThreadKHR(app->Session, XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, app->MainThreadTid));
			OXR(pfnSetAndroidApplicationThreadKHR(app->Session, XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR, app->RenderThreadTid));
		}
#endif
	} else if (state == XR_SESSION_STATE_STOPPING) {
		assert(app->SessionActive);

		OXR(xrEndSession(app->Session));
		app->SessionActive = false;
	}
}

int ovrApp_HandleXrEvents(ovrApp* app) {
	XrEventDataBuffer eventDataBuffer = {};
	int recenter = 0;

	// Poll for events
	for (;;) {
		XrEventDataBaseHeader* baseEventHeader = (XrEventDataBaseHeader*)(&eventDataBuffer);
		baseEventHeader->type = XR_TYPE_EVENT_DATA_BUFFER;
		baseEventHeader->next = NULL;
		XrResult r;
		OXR(r = xrPollEvent(app->Instance, &eventDataBuffer));
		if (r != XR_SUCCESS) {
			break;
		}

		switch (baseEventHeader->type) {
			case XR_TYPE_EVENT_DATA_EVENTS_LOST:
				ALOGV("xrPollEvent: received XR_TYPE_EVENT_DATA_EVENTS_LOST event");
				break;
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				const XrEventDataInstanceLossPending* instance_loss_pending_event =
						(XrEventDataInstanceLossPending*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING event: time %f",
						FromXrTime(instance_loss_pending_event->lossTime));
			} break;
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
				ALOGV("xrPollEvent: received XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED event");
				break;
			case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
				const XrEventDataPerfSettingsEXT* perf_settings_event =
						(XrEventDataPerfSettingsEXT*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT event: type %d subdomain %d : level %d -> level %d",
						perf_settings_event->type,
						perf_settings_event->subDomain,
						perf_settings_event->fromLevel,
						perf_settings_event->toLevel);
			} break;
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				XrEventDataReferenceSpaceChangePending* ref_space_change_event =
						(XrEventDataReferenceSpaceChangePending*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING event: changed space: %d for session %p at time %f",
						ref_space_change_event->referenceSpaceType,
						(void*)ref_space_change_event->session,
						FromXrTime(ref_space_change_event->changeTime));
				recenter = 1;
			} break;
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				const XrEventDataSessionStateChanged* session_state_changed_event =
						(XrEventDataSessionStateChanged*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: %d for session %p at time %f",
						session_state_changed_event->state,
						(void*)session_state_changed_event->session,
						FromXrTime(session_state_changed_event->time));

				switch (session_state_changed_event->state) {
					case XR_SESSION_STATE_IDLE:
						// Do nothing, wait for READY
						app->Focused = false;
						app->SessionActive = false;
						break;
					case XR_SESSION_STATE_READY:
					case XR_SESSION_STATE_STOPPING:
						ovrApp_HandleSessionStateChanges(app, session_state_changed_event->state);
						break;
					case XR_SESSION_STATE_SYNCHRONIZED:
						// Session is synchronized but not yet visible -- submit empty frames
						app->Focused = false;
						break;
					case XR_SESSION_STATE_VISIBLE:
						// Session is visible but not focused -- render VR but no input
						app->Focused = false;
						break;
					case XR_SESSION_STATE_FOCUSED:
						// Full operation -- render and input
						app->Focused = true;
						break;
					case XR_SESSION_STATE_EXITING:
						// Runtime wants us to exit -- clean shutdown
						app->SessionActive = false;
						break;
					case XR_SESSION_STATE_LOSS_PENDING:
						// Instance is about to be lost -- clean shutdown
						app->SessionActive = false;
						break;
					default:
						break;
				}
				recenter = true;
			} break;
			default:
				ALOGV("xrPollEvent: Unknown event");
				break;
		}
	}
	return recenter;
}
