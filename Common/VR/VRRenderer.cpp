#define _USE_MATH_DEFINES
#include <cmath>

#include "VRBase.h"
#include "VRInput.h"
#include "VRRenderer.h"
#include "OpenXRLoader.h"

#include <cstdlib>
#include <cstring>

extern void VRLog(const char* msg);

enum VRDisplaySurface { VR_SURFACE_FLAT = 0, VR_SURFACE_CURVED = 1, VR_SURFACE_IMMERSIVE = 2 };

XrFovf fov;
XrView* projections;
XrPosef invViewTransform[2];
XrFrameState frameState = {};
bool initialized = false;
bool stageSupported = false;
int vrConfig[VR_CONFIG_MAX] = {};
float vrConfigFloat[VR_CONFIG_FLOAT_MAX] = {};

XrVector3f hmdorientation;

XrPassthroughFB passthrough = XR_NULL_HANDLE;
XrPassthroughLayerFB passthroughLayer = XR_NULL_HANDLE;
bool passthroughRunning = false;
DECL_PFN(xrCreatePassthroughFB);
DECL_PFN(xrDestroyPassthroughFB);
DECL_PFN(xrPassthroughStartFB);
DECL_PFN(xrPassthroughPauseFB);
DECL_PFN(xrCreatePassthroughLayerFB);
DECL_PFN(xrDestroyPassthroughLayerFB);
DECL_PFN(xrPassthroughLayerPauseFB);
DECL_PFN(xrPassthroughLayerResumeFB);


void VR_UpdateStageBounds(ovrApp* pappState) {
	XrExtent2Df stageBounds = {};

	XrResult result;
	OXR(result = xrGetReferenceSpaceBoundsRect(pappState->Session, XR_REFERENCE_SPACE_TYPE_STAGE, &stageBounds));
	if (result != XR_SUCCESS) {
		stageBounds.width = 1.0f;
		stageBounds.height = 1.0f;
		pappState->CurrentSpace = pappState->FakeStageSpace;
	}
}

void VR_GetResolution(engine_t* engine, int *pWidth, int *pHeight) {
	static int width = 0;
	static int height = 0;

	VRLog("[VR_GetResolution] entered");
	if (engine) {
		VRLog("[VR_GetResolution] engine valid, enumerating view configs...");
		// Enumerate the viewport configurations.
		uint32_t viewportConfigTypeCount = 0;
		xrEnumerateViewConfigurations(
				engine->appState.Instance, engine->appState.SystemId, 0, &viewportConfigTypeCount, NULL);
		VRLog("[VR_GetResolution] xrEnumerateViewConfigurations count OK");

		XrViewConfigurationType* viewportConfigurationTypes =
				(XrViewConfigurationType*)malloc(viewportConfigTypeCount * sizeof(XrViewConfigurationType));

		xrEnumerateViewConfigurations(
				engine->appState.Instance,
				engine->appState.SystemId,
				viewportConfigTypeCount,
				&viewportConfigTypeCount,
				viewportConfigurationTypes);
		VRLog("[VR_GetResolution] xrEnumerateViewConfigurations list OK");

		ALOGV("Available Viewport Configuration Types: %d", viewportConfigTypeCount);

		VRLog("[VR_GetResolution] entering loop");
		for (uint32_t i = 0; i < viewportConfigTypeCount; i++) {
			const XrViewConfigurationType viewportConfigType = viewportConfigurationTypes[i];
			VRLog("[VR_GetResolution] loop iteration");

			ALOGV(
					"Viewport configuration type %d : %s",
					viewportConfigType,
					viewportConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ? "Selected" : "");

			XrViewConfigurationProperties viewportConfig;
			viewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
			viewportConfig.next = NULL;
			xrGetViewConfigurationProperties(
					engine->appState.Instance, engine->appState.SystemId, viewportConfigType, &viewportConfig);
			VRLog("[VR_GetResolution] xrGetViewConfigurationProperties OK");
			ALOGV(
					"FovMutable=%s ConfigurationType %d",
					viewportConfig.fovMutable ? "true" : "false",
					viewportConfig.viewConfigurationType);

			uint32_t viewCount = 0;
			VRLog("[VR_GetResolution] enumerating views count...");
			xrEnumerateViewConfigurationViews(
					engine->appState.Instance, engine->appState.SystemId, viewportConfigType, 0, &viewCount, NULL);
			VRLog("[VR_GetResolution] views count OK");

			if (viewCount > 0) {
				XrViewConfigurationView* elements =
						(XrViewConfigurationView*)malloc(viewCount * sizeof(XrViewConfigurationView));

				for (uint32_t e = 0; e < viewCount; e++) {
					elements[e].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
					elements[e].next = NULL;
				}

				VRLog("[VR_GetResolution] enumerating view details");
				xrEnumerateViewConfigurationViews(
						engine->appState.Instance,
						engine->appState.SystemId,
						viewportConfigType,
						viewCount,
						&viewCount,
						elements);
				VRLog("[VR_GetResolution] view details OK");

				// Cache the view config properties for the selected config type.
				if (viewportConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
					VRLog("[VR_GetResolution] PRIMARY_STEREO found");
					for (uint32_t e = 0; e < viewCount; e++) {
						engine->appState.ViewConfigurationView[e] = elements[e];
					}
				}

				free(elements);
			} else {
				ALOGE("Empty viewport configuration type: %d", viewCount);
			}
		}

		free(viewportConfigurationTypes);
		VRLog("[VR_GetResolution] view config loop done");

		*pWidth = width = engine->appState.ViewConfigurationView[0].recommendedImageRectWidth;
		*pHeight = height = engine->appState.ViewConfigurationView[0].recommendedImageRectHeight;
		VRLog("[VR_GetResolution] resolution cached");
	} else {
		//use cached values
		*pWidth = width;
		*pHeight = height;
	}

	*pWidth = (int)(*pWidth * VR_GetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING));
	*pHeight = (int)(*pHeight * VR_GetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING));
}

void VR_Recenter(engine_t* engine) {

	// Calculate recenter reference
	XrReferenceSpaceCreateInfo spaceCreateInfo = {};
	spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
	if (engine->appState.CurrentSpace != XR_NULL_HANDLE) {
		XrSpaceLocation loc = {};
		loc.type = XR_TYPE_SPACE_LOCATION;
		OXR(xrLocateSpace(engine->appState.HeadSpace, engine->appState.CurrentSpace, engine->predictedDisplayTime, &loc));
		hmdorientation = XrQuaternionf_ToEulerAngles(loc.pose.orientation);

		VR_SetConfigFloat(VR_CONFIG_RECENTER_YAW, VR_GetConfigFloat(VR_CONFIG_RECENTER_YAW) + hmdorientation.y);
		float recenterYaw = ToRadians(VR_GetConfigFloat(VR_CONFIG_RECENTER_YAW));
		spaceCreateInfo.poseInReferenceSpace.orientation.x = 0;
		spaceCreateInfo.poseInReferenceSpace.orientation.y = sinf(recenterYaw / 2);
		spaceCreateInfo.poseInReferenceSpace.orientation.z = 0;
		spaceCreateInfo.poseInReferenceSpace.orientation.w = cosf(recenterYaw / 2);
	}

	// Delete previous space instances
	if (engine->appState.StageSpace != XR_NULL_HANDLE) {
		OXR(xrDestroySpace(engine->appState.StageSpace));
	}
	if (engine->appState.FakeStageSpace != XR_NULL_HANDLE) {
		OXR(xrDestroySpace(engine->appState.FakeStageSpace));
	}

	// Create a default stage space to use if SPACE_TYPE_STAGE is not
	// supported, or calls to xrGetReferenceSpaceBoundsRect fail.
	spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceCreateInfo.poseInReferenceSpace = {};
	spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0;
	if (VR_GetPlatformFlag(VR_PLATFORM_TRACKING_FLOOR)) {
		spaceCreateInfo.poseInReferenceSpace.position.y = -1.6750f;
	}
	OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.FakeStageSpace));
	ALOGV("Created fake stage space from local space with offset");
	engine->appState.CurrentSpace = engine->appState.FakeStageSpace;

	if (stageSupported) {
		spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
		spaceCreateInfo.poseInReferenceSpace = {};
		spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0;
		OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.StageSpace));
		ALOGV("Created stage space");
		if (VR_GetPlatformFlag(VR_PLATFORM_TRACKING_FLOOR)) {
			engine->appState.CurrentSpace = engine->appState.StageSpace;
		}
	}

	// Update menu orientation
	VR_SetConfigFloat(VR_CONFIG_MENU_PITCH, hmdorientation.x);
	VR_SetConfigFloat(VR_CONFIG_MENU_YAW, 0.0f);
}

void VR_InitRenderer( engine_t* engine ) {
	VRLog("[VR_InitRenderer] entered");
	if (initialized) {
		VR_DestroyRenderer(engine);
	}

	VRLog("[VR_InitRenderer] checking passthrough...");
	if (VR_GetPlatformFlag(VRPlatformFlag::VR_PLATFORM_EXTENSION_PASSTHROUGH)) {
		INIT_PFN(xrCreatePassthroughFB);
		INIT_PFN(xrDestroyPassthroughFB);
		INIT_PFN(xrPassthroughStartFB);
		INIT_PFN(xrPassthroughPauseFB);
		INIT_PFN(xrCreatePassthroughLayerFB);
		INIT_PFN(xrDestroyPassthroughLayerFB);
		INIT_PFN(xrPassthroughLayerPauseFB);
		INIT_PFN(xrPassthroughLayerResumeFB);
	}

	VRLog("[VR_InitRenderer] calling VR_GetResolution...");
	int eyeW, eyeH;
	VR_GetResolution(engine, &eyeW, &eyeH);
	VR_SetConfig(VR_CONFIG_VIEWPORT_WIDTH, eyeW);
	VR_SetConfig(VR_CONFIG_VIEWPORT_HEIGHT, eyeH);
	VRLog("[VR_InitRenderer] VR_GetResolution OK");

	// Get the viewport configuration info for the chosen viewport configuration type.
	engine->appState.ViewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
	OXR(xrGetViewConfigurationProperties(engine->appState.Instance, engine->appState.SystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &engine->appState.ViewportConfig));
	VRLog("[VR_InitRenderer] xrGetViewConfigurationProperties OK");

	uint32_t numOutputSpaces = 0;
	OXR(xrEnumerateReferenceSpaces(engine->appState.Session, 0, &numOutputSpaces, NULL));
	XrReferenceSpaceType* referenceSpaces = (XrReferenceSpaceType*)malloc(numOutputSpaces * sizeof(XrReferenceSpaceType));
	OXR(xrEnumerateReferenceSpaces(engine->appState.Session, numOutputSpaces, &numOutputSpaces, referenceSpaces));
	VRLog("[VR_InitRenderer] xrEnumerateReferenceSpaces OK");

	for (uint32_t i = 0; i < numOutputSpaces; i++) {
		if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
			stageSupported = true;
			break;
		}
	}

	free(referenceSpaces);

	if (engine->appState.CurrentSpace == XR_NULL_HANDLE) {
		VRLog("[VR_InitRenderer] calling VR_Recenter...");
		VR_Recenter(engine);
		VRLog("[VR_InitRenderer] VR_Recenter OK");
	}

	projections = (XrView*)(malloc(ovrMaxNumEyes * sizeof(XrView)));
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		memset(&projections[eye], 0, sizeof(XrView));
		projections[eye].type = XR_TYPE_VIEW;
	}

	{
		char buf[256];
		snprintf(buf, sizeof(buf), "[VR_InitRenderer] eyeW=%d eyeH=%d supersampling=%.2f",
			eyeW, eyeH, VR_GetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING));
		VRLog(buf);
	}
	VRLog("[VR_InitRenderer] calling ovrRenderer_Create (includes staging FBO)...");
	ovrRenderer_Create(engine->appState.Session, &engine->appState.Renderer, eyeW, eyeH);
	VRLog("[VR_InitRenderer] ovrRenderer_Create OK");

	if (VR_GetPlatformFlag(VRPlatformFlag::VR_PLATFORM_EXTENSION_PASSTHROUGH)) {
		XrPassthroughCreateInfoFB ptci = {XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
		XrResult result;
		OXR(result = xrCreatePassthroughFB(engine->appState.Session, &ptci, &passthrough));

		if (XR_SUCCEEDED(result)) {
			XrPassthroughLayerCreateInfoFB plci = {XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
			plci.passthrough = passthrough;
			plci.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
			OXR(xrCreatePassthroughLayerFB(engine->appState.Session, &plci, &passthroughLayer));
		}

		OXR(xrPassthroughStartFB(passthrough));
	}
	initialized = true;
}

void VR_DestroyRenderer( engine_t* engine ) {
	if (VR_GetPlatformFlag(VRPlatformFlag::VR_PLATFORM_EXTENSION_PASSTHROUGH)) {
		if (passthroughRunning) {
			OXR(xrPassthroughLayerPauseFB(passthroughLayer));
		}
		OXR(xrPassthroughPauseFB(passthrough));
		OXR(xrDestroyPassthroughFB(passthrough));
		passthrough = XR_NULL_HANDLE;
	}
	ovrRenderer_Destroy(&engine->appState.Renderer);
	free(projections);
	initialized = false;
}

bool VR_InitFrame( engine_t* engine ) {
	bool stageBoundsDirty = true;
	if (ovrApp_HandleXrEvents(&engine->appState)) {
		VR_Recenter(engine);
	}
	if (engine->appState.SessionActive == false) {
		return false;
	}

	if (stageBoundsDirty) {
		VR_UpdateStageBounds(&engine->appState);
		stageBoundsDirty = false;
	}

	// Update passthrough
	if (passthroughRunning != (VR_GetConfig(VR_CONFIG_PASSTHROUGH) != 0)) {
		if (VR_GetConfig(VR_CONFIG_PASSTHROUGH)) {
			OXR(xrPassthroughLayerResumeFB(passthroughLayer));
		} else {
			OXR(xrPassthroughLayerPauseFB(passthroughLayer));
		}
		passthroughRunning = (VR_GetConfig(VR_CONFIG_PASSTHROUGH) != 0);
	}

	frameState.type = XR_TYPE_FRAME_STATE;
	frameState.next = NULL;

	OXR(xrWaitFrame(engine->appState.Session, 0, &frameState));
	engine->predictedDisplayTime = frameState.predictedDisplayTime;

	XrViewLocateInfo projectionInfo = {};
	projectionInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
	projectionInfo.next = NULL;
	projectionInfo.viewConfigurationType = engine->appState.ViewportConfig.viewConfigurationType;
	projectionInfo.displayTime = frameState.predictedDisplayTime;
	projectionInfo.space = engine->appState.CurrentSpace;

	XrViewState viewState = {XR_TYPE_VIEW_STATE, NULL};

	uint32_t projectionCapacityInput = ovrMaxNumEyes;
	uint32_t projectionCountOutput = projectionCapacityInput;

	OXR(xrLocateViews(
			engine->appState.Session,
			&projectionInfo,
			&viewState,
			projectionCapacityInput,
			&projectionCountOutput,
			projections));

	// Get the HMD pose, predicted for the middle of the time period during which
	// the new eye images will be displayed. The number of frames predicted ahead
	// depends on the pipeline depth of the engine and the synthesis rate.
	// The better the prediction, the less black will be pulled in at the edges.
	XrFrameBeginInfo beginFrameDesc = {};
	beginFrameDesc.type = XR_TYPE_FRAME_BEGIN_INFO;
	beginFrameDesc.next = NULL;
	OXR(xrBeginFrame(engine->appState.Session, &beginFrameDesc));

	fov = {};
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		fov.angleLeft += projections[eye].fov.angleLeft / 2.0f;
		fov.angleRight += projections[eye].fov.angleRight / 2.0f;
		fov.angleUp += projections[eye].fov.angleUp / 2.0f;
		fov.angleDown += projections[eye].fov.angleDown / 2.0f;
		invViewTransform[eye] = projections[eye].pose;
	}

	// Update HMD and controllers
	hmdorientation = XrQuaternionf_ToEulerAngles(invViewTransform[0].orientation);
	IN_VRInputFrame(engine);

	for (int i = 0; i < ovrMaxNumEyes; i++) {
		ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer[i];
		frameBuffer->TextureSwapChainIndex++;
		frameBuffer->TextureSwapChainIndex %= frameBuffer->TextureSwapChainLength;
	}

	engine->appState.LayerCount = 0;
	memset(engine->appState.Layers, 0, sizeof(ovrCompositorLayer_Union) * ovrMaxLayerCount);
	return true;
}

static int vrFrameLogCount = 0;

void VR_BeginFrame( engine_t* engine, int fboIndex ) {
	if (vrFrameLogCount < 5) {
		char buf[128];
		snprintf(buf, sizeof(buf), "[VR_BeginFrame] fboIndex=%d frame=%d", fboIndex, vrFrameLogCount);
		VRLog(buf);
	}
	vrConfig[VR_CONFIG_CURRENT_FBO] = fboIndex;
	ovrFramebuffer_Acquire(&engine->appState.Renderer.FrameBuffer[fboIndex]);
}

void VR_EndFrame( engine_t* engine ) {
	int fboIndex = vrConfig[VR_CONFIG_CURRENT_FBO];
	if (vrFrameLogCount < 5) {
		char buf[128];
		snprintf(buf, sizeof(buf), "[VR_EndFrame] fboIndex=%d displaySurface=%d", fboIndex, vrConfig[VR_CONFIG_DISPLAY_SURFACE]);
		VRLog(buf);
	}
	VR_BindFramebuffer(engine);

	// Show mouse cursor
	int vrMode = vrConfig[VR_CONFIG_MODE];
	bool screenMode = (vrMode == VR_MODE_MONO_SCREEN) || (vrMode == VR_MODE_STEREO_SCREEN);
	if (screenMode && (vrConfig[VR_CONFIG_MOUSE_SIZE] > 0)) {
		int x = vrConfig[VR_CONFIG_MOUSE_X];
		int y = vrConfig[VR_CONFIG_MOUSE_Y];
		int sx = vrConfig[VR_CONFIG_MOUSE_SIZE];
		int sy = (int)((float)sx * VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT));
		ovrRenderer_MouseCursor(&engine->appState.Renderer, x, y, sx, sy);
	}

	// Stereo debug watermark: RED=FBO0, BLUE=FBO1
	// If LEFT eye sees RED → FBO mapping OK, issue is IPD sign
	// If LEFT eye sees BLUE → FBO mapping swapped
	{
		static int wmLogCount = 0;
		if (wmLogCount < 10) {
			char buf[256];
			snprintf(buf, sizeof(buf),
				"[VR-WATERMARK] vrMode=%d fboIndex=%d STEREO_6DOF=%d passes=%d",
				vrMode, fboIndex, VR_MODE_STEREO_6DOF, vrConfig[VR_CONFIG_REPROJECTION]);
			VRLog(buf);
			wmLogCount++;
		}
		ovrRenderer_StereoDebugWatermark(fboIndex);
	}

	ovrFramebuffer_Release(&engine->appState.Renderer.FrameBuffer[fboIndex]);
}

void VR_FinishFrame( engine_t* engine ) {
	if (vrFrameLogCount < 5) {
		char buf[256];
		snprintf(buf, sizeof(buf), "[VR_FinishFrame] mode=%d displaySurface=%d reproj=%d layerCount=%d",
			vrConfig[VR_CONFIG_MODE], vrConfig[VR_CONFIG_DISPLAY_SURFACE],
			vrConfig[VR_CONFIG_REPROJECTION], engine->appState.LayerCount);
		VRLog(buf);
	}
	if (VR_GetPlatformFlag(VRPlatformFlag::VR_PLATFORM_EXTENSION_PASSTHROUGH) && VR_GetConfig(VR_CONFIG_PASSTHROUGH)) {
		if (passthroughLayer != XR_NULL_HANDLE) {
			XrCompositionLayerPassthroughFB passthrough_layer = {XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
			passthrough_layer.layerHandle = passthroughLayer;
			passthrough_layer.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			passthrough_layer.space = XR_NULL_HANDLE;
			engine->appState.Layers[engine->appState.LayerCount++].Passthrough = passthrough_layer;
		}
	}

	int vrMode = vrConfig[VR_CONFIG_MODE];
	XrCompositionLayerProjectionView projection_layer_elements[2] = {};
	// Depth info chaining disabled: depth swapchain is not written during rendering
	// (attaching it as FBO depth breaks packed depth-stencil, see VRFramebuffer.cpp)
	bool headTracking = (vrMode == VR_MODE_MONO_6DOF) || (vrMode == VR_MODE_SBS_6DOF) || (vrMode == VR_MODE_STEREO_6DOF);
	bool reprojection = vrConfig[VR_CONFIG_REPROJECTION];
	if (headTracking && reprojection) {
		VR_SetConfigFloat(VR_CONFIG_MENU_YAW, hmdorientation.y);

		for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
			ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer[0];
			XrPosef pose = invViewTransform[0];
			if (vrMode == VR_MODE_STEREO_6DOF) {
				frameBuffer = &engine->appState.Renderer.FrameBuffer[eye];
				// True Stereo submission: must match the view matrix exactly.
				// Position is interpolated by stereo intensity (same as UpdateVRViewMatrices).
				// Orientation is per-eye (may include toe-in on some runtimes).
				float intensity = VR_GetConfigFloat(VR_CONFIG_STEREO_INTENSITY);
				pose.orientation = invViewTransform[eye].orientation;
				pose.position.x = invViewTransform[0].position.x +
					(invViewTransform[eye].position.x - invViewTransform[0].position.x) * intensity;
				pose.position.y = invViewTransform[0].position.y +
					(invViewTransform[eye].position.y - invViewTransform[0].position.y) * intensity;
				pose.position.z = invViewTransform[0].position.z +
					(invViewTransform[eye].position.z - invViewTransform[0].position.z) * intensity;
			} else if (vrMode != VR_MODE_MONO_6DOF) {
				pose = invViewTransform[eye];
			}

			memset(&projection_layer_elements[eye], 0, sizeof(XrCompositionLayerProjectionView));
			projection_layer_elements[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projection_layer_elements[eye].pose = pose;
			projection_layer_elements[eye].fov = projections[eye].fov;

			memset(&projection_layer_elements[eye].subImage, 0, sizeof(XrSwapchainSubImage));
			projection_layer_elements[eye].subImage.swapchain = frameBuffer->ColorSwapChain.Handle;
			projection_layer_elements[eye].subImage.imageRect.offset.x = 0;
			projection_layer_elements[eye].subImage.imageRect.offset.y = 0;
			projection_layer_elements[eye].subImage.imageRect.extent.width = frameBuffer->ColorSwapChain.Width;
			projection_layer_elements[eye].subImage.imageRect.extent.height = frameBuffer->ColorSwapChain.Height;
			projection_layer_elements[eye].subImage.imageArrayIndex = 0;

			if (vrMode == VR_MODE_SBS_6DOF) {
				projection_layer_elements[eye].subImage.imageRect.extent.width /= 2;
				if (eye == 1) {
					projection_layer_elements[eye].subImage.imageRect.offset.x += frameBuffer->ColorSwapChain.Width / 2;
				}
			}

			// Depth info chaining disabled: depth swapchain not populated during rendering
		}

		XrCompositionLayerProjection projection_layer = {};
		projection_layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
		projection_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		projection_layer.layerFlags |= XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
		projection_layer.space = engine->appState.CurrentSpace;
		projection_layer.viewCount = ovrMaxNumEyes;
		projection_layer.views = projection_layer_elements;

		engine->appState.Layers[engine->appState.LayerCount++].Projection = projection_layer;
	} else {

		// Flat screen pose
		float distance = VR_GetConfigFloat(VR_CONFIG_CANVAS_DISTANCE) / 4.0f - 1.0f;
		float menuPitch = 0.0f; // Cinema screens always vertical (gravity-aligned)
		float menuYaw = ToRadians(VR_GetConfigFloat(VR_CONFIG_MENU_YAW));
		XrVector3f pos = {-sinf(menuYaw) * distance, 0, -cosf(menuYaw) * distance};
		if (!VR_GetConfig(VR_CONFIG_CANVAS_6DOF)) {
			pos.x += invViewTransform[0].position.x;
			pos.y += invViewTransform[0].position.y;
			pos.z += invViewTransform[0].position.z;
		}
		XrQuaternionf pitch = XrQuaternionf_CreateFromVectorAngle({1, 0, 0}, -menuPitch);
		XrQuaternionf yaw = XrQuaternionf_CreateFromVectorAngle({0, 1, 0}, menuYaw);

		// Determine display surface with graceful fallback chain
		int requestedSurface = vrConfig[VR_CONFIG_DISPLAY_SURFACE];
		int actualSurface = requestedSurface;
		// Fallback: Curved -> Flat
		if (actualSurface == VR_SURFACE_CURVED && !VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_CYLINDER)) {
			actualSurface = VR_SURFACE_FLAT;
		}

		if (actualSurface == VR_SURFACE_CURVED) {
			// Cylinder layer for curved cinema screen (Quest Link, Pico, any runtime with cylinder support)
			// Player is at the center of the cylinder, looking at the inner surface.
			XrCompositionLayerCylinderKHR cylinder_layer = {};
			cylinder_layer.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
			cylinder_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			cylinder_layer.space = engine->appState.CurrentSpace;
			memset(&cylinder_layer.subImage, 0, sizeof(XrSwapchainSubImage));
			cylinder_layer.subImage.imageRect.offset.x = 0;
			cylinder_layer.subImage.imageRect.offset.y = 0;
			cylinder_layer.subImage.imageRect.extent.width = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Width;
			cylinder_layer.subImage.imageRect.extent.height = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Height;
			cylinder_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Handle;
			cylinder_layer.subImage.imageArrayIndex = 0;
			cylinder_layer.pose.orientation = XrQuaternionf_Multiply(pitch, yaw);
			cylinder_layer.pose.position = invViewTransform[0].position;
			cylinder_layer.radius = 2.0f;
			float fovScale = VR_GetConfigFloat(VR_CONFIG_FOV_SCALE);
			float baseCentralAngle = (float)(M_PI * 8.0 / 9.0);  // 160deg base
			float centralAngle = baseCentralAngle * fovScale;
			if (centralAngle > (float)(M_PI * 3.0 / 2.0)) centralAngle = (float)(M_PI * 3.0 / 2.0);
			cylinder_layer.centralAngle = centralAngle;
			cylinder_layer.aspectRatio = VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT);
			if (headTracking && !reprojection) {
				float width = (float)engine->appState.ViewConfigurationView[0].recommendedImageRectWidth;
				float height = (float)engine->appState.ViewConfigurationView[0].recommendedImageRectHeight;
				cylinder_layer.aspectRatio = 2.0f * width / height;
				cylinder_layer.centralAngle = (float)(M_PI);
			}

			// Build the cylinder layer
			if ((vrMode == VR_MODE_MONO_SCREEN) || (vrMode == VR_MODE_MONO_6DOF)) {
				cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				engine->appState.Layers[engine->appState.LayerCount++].Cylinder = cylinder_layer;
			} else if ((vrMode == VR_MODE_SBS_SCREEN) || (vrMode == VR_MODE_SBS_6DOF)) {
				cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
				cylinder_layer.subImage.imageRect.extent.width /= 2;
				engine->appState.Layers[engine->appState.LayerCount++].Cylinder = cylinder_layer;
				cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
				cylinder_layer.subImage.imageRect.offset.x += cylinder_layer.subImage.imageRect.extent.width;
				engine->appState.Layers[engine->appState.LayerCount++].Cylinder = cylinder_layer;
			} else {
				cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
				engine->appState.Layers[engine->appState.LayerCount++].Cylinder = cylinder_layer;
				cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
				cylinder_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer[1].ColorSwapChain.Handle;
				engine->appState.Layers[engine->appState.LayerCount++].Cylinder = cylinder_layer;
			}
		} else {
			// Quad layer for flat cinema screen (default, SteamVR and runtimes without cylinder/equirect)
			VR_SetConfig(VR_CONFIG_DEPTH_SUBMIT, 0);
			float aspect = VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT);
			float screenWidth, screenHeight;

			if (headTracking && !reprojection) {
				float fboW = (float)engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Width;
				float fboH = (float)engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Height;
				aspect = 2.0f * fboW / fboH;
				float immersiveDist = 2.0f;
				pos = {-sinf(menuYaw) * immersiveDist, 0, -cosf(menuYaw) * immersiveDist};
				if (!VR_GetConfig(VR_CONFIG_CANVAS_6DOF)) {
					pos.x += invViewTransform[0].position.x;
					pos.y += invViewTransform[0].position.y;
					pos.z += invViewTransform[0].position.z;
				}
				screenWidth = 2.0f * immersiveDist * tanf(60.0f * (float)M_PI / 180.0f);
				screenHeight = screenWidth / aspect;
			} else {
				float fovScale = VR_GetConfigFloat(VR_CONFIG_FOV_SCALE);
				if (fovScale < 1.0f) fovScale = 1.0f;
				float absDistance = fabs(distance);
				if (absDistance < 0.5f) absDistance = 0.5f;
				float baseWidth = absDistance * 0.8f;
				screenWidth = baseWidth * fovScale;
				screenHeight = screenWidth / aspect;
			}
			if (screenHeight < 0.1f) screenHeight = 0.1f;

			XrCompositionLayerQuad quad_layer = {};
			quad_layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
			quad_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			quad_layer.space = engine->appState.CurrentSpace;
			memset(&quad_layer.subImage, 0, sizeof(XrSwapchainSubImage));
			quad_layer.subImage.imageRect.offset.x = 0;
			quad_layer.subImage.imageRect.offset.y = 0;
			quad_layer.subImage.imageRect.extent.width = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Width;
			quad_layer.subImage.imageRect.extent.height = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Height;
			quad_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Handle;
			quad_layer.subImage.imageArrayIndex = 0;
			quad_layer.pose.orientation = XrQuaternionf_Multiply(pitch, yaw);
			quad_layer.pose.position = pos;
			quad_layer.size.width = screenWidth;
			quad_layer.size.height = screenHeight;

			// Build the quad layer
			if ((vrMode == VR_MODE_MONO_SCREEN) || (vrMode == VR_MODE_MONO_6DOF)) {
				quad_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				engine->appState.Layers[engine->appState.LayerCount++].Quad = quad_layer;
			} else if ((vrMode == VR_MODE_SBS_SCREEN) || (vrMode == VR_MODE_SBS_6DOF)) {
				quad_layer.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
				quad_layer.subImage.imageRect.extent.width /= 2;
				engine->appState.Layers[engine->appState.LayerCount++].Quad = quad_layer;
				quad_layer.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
				quad_layer.subImage.imageRect.offset.x += quad_layer.subImage.imageRect.extent.width;
				engine->appState.Layers[engine->appState.LayerCount++].Quad = quad_layer;
			} else {
				quad_layer.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
				engine->appState.Layers[engine->appState.LayerCount++].Quad = quad_layer;
				quad_layer.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
				quad_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer[1].ColorSwapChain.Handle;
				engine->appState.Layers[engine->appState.LayerCount++].Quad = quad_layer;
			}
		}
	}

	// Compose the layers for this frame.
	if (vrFrameLogCount < 5) {
		char buf[128];
		snprintf(buf, sizeof(buf), "[VR_FinishFrame] layers built, layerCount=%d", engine->appState.LayerCount);
		VRLog(buf);
	}
	const XrCompositionLayerBaseHeader* layers[ovrMaxLayerCount] = {};
	for (int i = 0; i < engine->appState.LayerCount; i++) {
		layers[i] = (const XrCompositionLayerBaseHeader*)&engine->appState.Layers[i];
	}

	XrFrameEndInfo endFrameInfo = {};
	endFrameInfo.type = XR_TYPE_FRAME_END_INFO;
	endFrameInfo.displayTime = frameState.predictedDisplayTime;
	endFrameInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	// When shouldRender is false (e.g. SYNCHRONIZED state), submit zero layers
	if (frameState.shouldRender) {
		endFrameInfo.layerCount = engine->appState.LayerCount;
		endFrameInfo.layers = layers;
	} else {
		endFrameInfo.layerCount = 0;
		endFrameInfo.layers = nullptr;
	}
	XrResult endResult = xrEndFrame(engine->appState.Session, &endFrameInfo);
	if (XR_FAILED(endResult)) {
		char buf[256];
		snprintf(buf, sizeof(buf), "[VR_FinishFrame] xrEndFrame FAILED: %d", (int)endResult);
		VRLog(buf);
	}
	if (vrFrameLogCount < 5) {
		char buf[128];
		snprintf(buf, sizeof(buf), "[VR_FinishFrame] xrEndFrame result=%d, frame %d complete", (int)endResult, vrFrameLogCount);
		VRLog(buf);
		vrFrameLogCount++;
	}
}

int VR_GetConfig( VRConfig config ) {
	return vrConfig[config];
}

void VR_SetConfig( VRConfig config, int value) {
	vrConfig[config] = value;
}

float VR_GetConfigFloat(VRConfigFloat config) {
	return vrConfigFloat[config];
}

void VR_SetConfigFloat(VRConfigFloat config, float value) {
	vrConfigFloat[config] = value;
}

void* VR_BindFramebuffer(engine_t *engine) {
	if (!initialized) return nullptr;
	int fboIndex = VR_GetConfig(VR_CONFIG_CURRENT_FBO);
	return ovrFramebuffer_SetCurrent(&engine->appState.Renderer.FrameBuffer[fboIndex]);
}

XrView VR_GetView(int eye) {
	return projections[eye];
}

XrVector3f VR_GetHMDAngles() {
	return hmdorientation;
}
