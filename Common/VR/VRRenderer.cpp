#define _USE_MATH_DEFINES
#include <cmath>

#include "VRBase.h"
#include "VRInput.h"
#include "VRRenderer.h"
#include "OpenXRLoader.h"

#include <cstdlib>
#include <cstring>

#if XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES
#include "ext/OpenXR-SDK/src/common/xr_linear.h"
#if XR_USE_GRAPHICS_API_OPENGL_ES
#define VR_GL_SWAPCHAIN_IMAGE XrSwapchainImageOpenGLESKHR
#else
#define VR_GL_SWAPCHAIN_IMAGE XrSwapchainImageOpenGLKHR
#endif
#endif

extern void VRLog(const char* msg);

#if XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES
static GLuint cylinderVAO = 0, cylinderVBO = 0, cylinderEBO = 0;
static int cylinderIndexCount = 0;
static GLuint cylinderProgram = 0;
static GLint cylUniformModel = -1, cylUniformView = -1, cylUniformProj = -1, cylUniformTex = -1;
static bool cylinderMeshReady = false;
static float cylinderHalfHeight = 0.0f;

// Cylinder 120° mode: rectilinear-corrected 120° cylinder
static GLuint cyl120VAO = 0, cyl120VBO = 0, cyl120EBO = 0;
static int cyl120IndexCount = 0;
static GLuint cyl120Program = 0;
static GLint cyl120UniformModel = -1, cyl120UniformView = -1, cyl120UniformProj = -1;
static GLint cyl120UniformTex = -1, cyl120UniformHalfFOV = -1;
static bool cyl120MeshReady = false;
#endif

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


#if XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES

static void GenerateCylinderMesh(float radius, float height, float arcAngle, int segments,
	GLuint* outVAO, GLuint* outVBO, GLuint* outEBO, int* outIndexCount) {
	int vertsPerRow = segments + 1;
	int totalVerts = vertsPerRow * 2;
	int floatsPerVert = 5; // xyz + uv

	float* vertices = (float*)malloc(totalVerts * floatsPerVert * sizeof(float));
	int totalIndices = segments * 6;
	unsigned int* indices = (unsigned int*)malloc(totalIndices * sizeof(unsigned int));

	float* vptr = vertices;
	unsigned int* iptr = indices;

	float startAngle = -arcAngle * 0.5f;
	float dTheta = arcAngle / (float)segments;

	for (int i = 0; i <= segments; i++) {
		float theta = startAngle + i * dTheta;
		float x = radius * sinf(theta);
		float z = -radius * cosf(theta);
		float u = (float)i / (float)segments;

		// Top vertex
		*vptr++ = x; *vptr++ = height * 0.5f; *vptr++ = z;
		*vptr++ = u; *vptr++ = 1.0f;

		// Bottom vertex
		*vptr++ = x; *vptr++ = -height * 0.5f; *vptr++ = z;
		*vptr++ = u; *vptr++ = 0.0f;
	}

	for (int i = 0; i < segments; i++) {
		int top0 = i * 2;
		int bot0 = top0 + 1;
		int top1 = top0 + 2;
		int bot1 = bot0 + 2;

		*iptr++ = top0; *iptr++ = top1; *iptr++ = bot0;
		*iptr++ = bot0; *iptr++ = top1; *iptr++ = bot1;
	}

	*outIndexCount = totalIndices;

	GL(glGenVertexArrays(1, outVAO));
	GL(glBindVertexArray(*outVAO));

	GL(glGenBuffers(1, outVBO));
	GL(glBindBuffer(GL_ARRAY_BUFFER, *outVBO));
	GL(glBufferData(GL_ARRAY_BUFFER, totalVerts * floatsPerVert * sizeof(float), vertices, GL_STATIC_DRAW));

	GL(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0));
	GL(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))));
	GL(glEnableVertexAttribArray(0));
	GL(glEnableVertexAttribArray(1));

	GL(glGenBuffers(1, outEBO));
	GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *outEBO));
	GL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, totalIndices * sizeof(unsigned int), indices, GL_STATIC_DRAW));

	GL(glBindVertexArray(0));

	free(vertices);
	free(indices);
}

static GLuint CompileCylinderShader(GLenum type, const char* source) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		char infoLog[512];
		glGetShaderInfoLog(shader, 512, NULL, infoLog);
		VRLog("[CylinderShader] Compile error:");
		VRLog(infoLog);
	}
	return shader;
}

static void InitCylinderShader() {
	const char* vertSrc =
		"#version 330 core\n"
		"layout(location=0) in vec3 aPos;\n"
		"layout(location=1) in vec2 aUV;\n"
		"out vec2 vUV;\n"
		"uniform mat4 uModel;\n"
		"uniform mat4 uView;\n"
		"uniform mat4 uProj;\n"
		"void main() {\n"
		"    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);\n"
		"    vUV = aUV;\n"
		"}\n";

	const char* fragSrc =
		"#version 330 core\n"
		"uniform sampler2D uTexture;\n"
		"in vec2 vUV;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"    fragColor = texture(uTexture, vUV);\n"
		"}\n";

	GLuint vert = CompileCylinderShader(GL_VERTEX_SHADER, vertSrc);
	GLuint frag = CompileCylinderShader(GL_FRAGMENT_SHADER, fragSrc);

	cylinderProgram = glCreateProgram();
	glAttachShader(cylinderProgram, vert);
	glAttachShader(cylinderProgram, frag);
	glLinkProgram(cylinderProgram);

	GLint success;
	glGetProgramiv(cylinderProgram, GL_LINK_STATUS, &success);
	if (!success) {
		char infoLog[512];
		glGetProgramInfoLog(cylinderProgram, 512, NULL, infoLog);
		VRLog("[CylinderShader] Link error:");
		VRLog(infoLog);
	}

	cylUniformModel = glGetUniformLocation(cylinderProgram, "uModel");
	cylUniformView = glGetUniformLocation(cylinderProgram, "uView");
	cylUniformProj = glGetUniformLocation(cylinderProgram, "uProj");
	cylUniformTex = glGetUniformLocation(cylinderProgram, "uTexture");

	glDeleteShader(vert);
	glDeleteShader(frag);
}

static void InitCylinderGeometry() {
	if (cylinderMeshReady) return;

	float radius = 2.0f;
	float arcAngle = (float)(M_PI * 8.0 / 9.0); // 160 degrees
	float arcLength = radius * arcAngle;
	float aspect = 480.0f / 272.0f;
	float height = arcLength / aspect;

	cylinderHalfHeight = height * 0.5f;
	GenerateCylinderMesh(radius, height, arcAngle, 64,
		&cylinderVAO, &cylinderVBO, &cylinderEBO, &cylinderIndexCount);
	InitCylinderShader();

	cylinderMeshReady = true;
	VRLog("[CylinderGeometry] Mesh and shader initialized");
}

// Cylinder 120° mode: rectilinear-corrected shader
static void InitCylinder120Shader() {
	// Vertex shader: plain transform, pass UV and theta to fragment
	const char* vertSrc =
		"#version 330 core\n"
		"layout(location=0) in vec3 aPos;\n"
		"layout(location=1) in vec2 aUV;\n"
		"out vec2 vUV;\n"
		"uniform mat4 uModel;\n"
		"uniform mat4 uView;\n"
		"uniform mat4 uProj;\n"
		"void main() {\n"
		"    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);\n"
		"    vUV = aUV;\n"
		"}\n";

	// Fragment shader: rectilinear correction (horizontal tan + vertical 1/cos stretch)
	const char* fragSrc =
		"#version 330 core\n"
		"uniform sampler2D uTexture;\n"
		"uniform float uHalfFOV;\n"
		"in vec2 vUV;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"    float theta = (vUV.x - 0.5) * 2.0 * uHalfFOV;\n"
		"    float u = 0.5 + tan(theta) / (2.0 * tan(uHalfFOV));\n"
		"    float v = 0.5 + (vUV.y - 0.5) / cos(theta);\n"
		"    fragColor = texture(uTexture, vec2(u, clamp(v, 0.0, 1.0)));\n"
		"}\n";

	GLuint vert = CompileCylinderShader(GL_VERTEX_SHADER, vertSrc);
	GLuint frag = CompileCylinderShader(GL_FRAGMENT_SHADER, fragSrc);

	cyl120Program = glCreateProgram();
	glAttachShader(cyl120Program, vert);
	glAttachShader(cyl120Program, frag);
	glLinkProgram(cyl120Program);

	GLint success;
	glGetProgramiv(cyl120Program, GL_LINK_STATUS, &success);
	if (!success) {
		char infoLog[512];
		glGetProgramInfoLog(cyl120Program, 512, NULL, infoLog);
		VRLog("[Cyl120Shader] Link error:");
		VRLog(infoLog);
	}

	cyl120UniformModel = glGetUniformLocation(cyl120Program, "uModel");
	cyl120UniformView = glGetUniformLocation(cyl120Program, "uView");
	cyl120UniformProj = glGetUniformLocation(cyl120Program, "uProj");
	cyl120UniformTex = glGetUniformLocation(cyl120Program, "uTexture");
	cyl120UniformHalfFOV = glGetUniformLocation(cyl120Program, "uHalfFOV");

	glDeleteShader(vert);
	glDeleteShader(frag);
}

static void InitCylinder120Geometry() {
	if (cyl120MeshReady) return;

	float radius = 2.0f;
	float arcAngle = (float)(M_PI * 2.0 / 3.0); // 120 degrees
	float aspect = 480.0f / 272.0f;
	// Height based on flat-screen equivalent width for correct rectilinear proportions
	float flatWidth = 2.0f * radius * tanf(arcAngle * 0.5f);
	float height = flatWidth / aspect;

	GenerateCylinderMesh(radius, height, arcAngle, 64,
		&cyl120VAO, &cyl120VBO, &cyl120EBO, &cyl120IndexCount);
	InitCylinder120Shader();

	cyl120MeshReady = true;
	VRLog("[Cyl120Geometry] 120-degree mesh and rectilinear shader initialized");
}

static void DestroyCylinder120Geometry() {
	if (!cyl120MeshReady) return;

	if (cyl120Program) { glDeleteProgram(cyl120Program); cyl120Program = 0; }
	if (cyl120VAO) { glDeleteVertexArrays(1, &cyl120VAO); cyl120VAO = 0; }
	if (cyl120VBO) { glDeleteBuffers(1, &cyl120VBO); cyl120VBO = 0; }
	if (cyl120EBO) { glDeleteBuffers(1, &cyl120EBO); cyl120EBO = 0; }
	cyl120MeshReady = false;
	cyl120IndexCount = 0;
}

static void DestroyCylinderGeometry() {
	if (!cylinderMeshReady) return;

	if (cylinderProgram) {
		glDeleteProgram(cylinderProgram);
		cylinderProgram = 0;
	}
	if (cylinderVAO) {
		glDeleteVertexArrays(1, &cylinderVAO);
		cylinderVAO = 0;
	}
	if (cylinderVBO) {
		glDeleteBuffers(1, &cylinderVBO);
		cylinderVBO = 0;
	}
	if (cylinderEBO) {
		glDeleteBuffers(1, &cylinderEBO);
		cylinderEBO = 0;
	}
	cylinderMeshReady = false;
	cylinderIndexCount = 0;
}

static void RenderCylinderScreen(int fboIndex, engine_t* engine) {
	int surface = vrConfig[VR_CONFIG_DISPLAY_SURFACE];

	// Select resources based on mode
	GLuint prog, vao;
	int idxCount;
	GLint uModel, uView, uProj, uTex;

	if (surface == VR_SURFACE_CYLINDER120) {
		prog = cyl120Program;
		vao = cyl120VAO;
		idxCount = cyl120IndexCount;
		uModel = cyl120UniformModel;
		uView = cyl120UniformView;
		uProj = cyl120UniformProj;
		uTex = cyl120UniformTex;
	} else {
		prog = cylinderProgram;
		vao = cylinderVAO;
		idxCount = cylinderIndexCount;
		uModel = cylUniformModel;
		uView = cylUniformView;
		uProj = cylUniformProj;
		uTex = cylUniformTex;
	}

	ovrFramebuffer* fb = &engine->appState.Renderer.FrameBuffer[fboIndex];
	ovrRenderer* renderer = &engine->appState.Renderer;
	int w = fb->Width, h = fb->Height;
	GLuint swapchainFBO = fb->GLFrameBuffers[fb->TextureSwapChainIndex];

	// Save GL state (q3vr pattern)
	GLuint prevVAO, prevProgram, prevTexture;
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, (GLint*)&prevVAO);
	glGetIntegerv(GL_CURRENT_PROGRAM, (GLint*)&prevProgram);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, (GLint*)&prevTexture);
	GLboolean prevBlend = glIsEnabled(GL_BLEND);
	GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
	GLboolean prevCullFace = glIsEnabled(GL_CULL_FACE);
	GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
	GLboolean prevSRGB = glIsEnabled(GL_FRAMEBUFFER_SRGB);
	GLboolean prevDepthMask;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

	// Disable sRGB conversions — raw byte passthrough, no precision loss
	GL(glDisable(GL_FRAMEBUFFER_SRGB));

	// 1. Copy game content swapchain → staging texture (raw byte copy, no sRGB conversion)
	GL(glBindFramebuffer(GL_READ_FRAMEBUFFER, swapchainFBO));
	GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, renderer->StagingFBO[fboIndex]));
	GL(glBlitFramebuffer(0, 0, w, h, 0, 0, renderer->StagingWidth, renderer->StagingHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR));

	// Generate mipmaps for trilinear + anisotropic filtering
	GL(glBindTexture(GL_TEXTURE_2D, renderer->StagingTexture[fboIndex]));
	GL(glGenerateMipmap(GL_TEXTURE_2D));
	GL(glBindTexture(GL_TEXTURE_2D, 0));

	// 2. Draw directly on swapchain FBO (like q3vr — no staging render)
	GL(glBindFramebuffer(GL_FRAMEBUFFER, swapchainFBO));
	GL(glViewport(0, 0, w, h));
	GL(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
	GL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
	GL(glClear(GL_COLOR_BUFFER_BIT));

	// 3. GL state for cylinder draw
	GL(glDisable(GL_DEPTH_TEST));
	GL(glDepthMask(GL_FALSE));
	GL(glDisable(GL_BLEND));
	GL(glDisable(GL_CULL_FACE));
	GL(glDisable(GL_SCISSOR_TEST));
	GL(glDisable(GL_STENCIL_TEST));

	// 4. Build MVP matrices
	float menuYaw = ToRadians(VR_GetConfigFloat(VR_CONFIG_MENU_YAW));
	XrMatrix4x4f model, translation, rotation;
	XrVector3f cylinderCenter = invViewTransform[0].position;
	if (VR_GetConfig(VR_CONFIG_CANVAS_6DOF)) {
		cylinderCenter = {0, 0, 0};
	}
	XrMatrix4x4f_CreateTranslation(&translation, cylinderCenter.x, cylinderCenter.y, cylinderCenter.z);
	XrMatrix4x4f_CreateRotation(&rotation, 0.0f, menuYaw * 180.0f / (float)M_PI, 0.0f);
	XrMatrix4x4f_Multiply(&model, &translation, &rotation);

	XrMatrix4x4f eyePose, view;
	XrMatrix4x4f_CreateFromRigidTransform(&eyePose, &invViewTransform[fboIndex]);
	XrMatrix4x4f_InvertRigidBody(&view, &eyePose);

#if XR_USE_GRAPHICS_API_OPENGL_ES
	GraphicsAPI gfxApi = GRAPHICS_OPENGL_ES;
#else
	GraphicsAPI gfxApi = GRAPHICS_OPENGL;
#endif
	XrMatrix4x4f proj;
	XrMatrix4x4f_CreateProjectionFov(&proj, gfxApi, projections[fboIndex].fov, 0.1f, 100.0f);

	// 5. Draw cylinder on swapchain, sampling from staging texture
	GL(glUseProgram(prog));
	GL(glUniformMatrix4fv(uModel, 1, GL_FALSE, (float*)model.m));
	GL(glUniformMatrix4fv(uView, 1, GL_FALSE, (float*)view.m));
	GL(glUniformMatrix4fv(uProj, 1, GL_FALSE, (float*)proj.m));
	GL(glActiveTexture(GL_TEXTURE0));
	GL(glBindTexture(GL_TEXTURE_2D, renderer->StagingTexture[fboIndex]));
	GL(glUniform1i(uTex, 0));

	// Set halfFOV uniform for rectilinear correction (cylinder 120 mode)
	if (surface == VR_SURFACE_CYLINDER120) {
		GL(glUniform1f(cyl120UniformHalfFOV, (float)(M_PI / 3.0)));
	}

	GL(glBindVertexArray(vao));
	GL(glDrawElements(GL_TRIANGLES, idxCount, GL_UNSIGNED_INT, 0));
	GL(glBindVertexArray(0));

	// Restore GL state
	if (prevSRGB) { GL(glEnable(GL_FRAMEBUFFER_SRGB)); }
	if (prevBlend) { GL(glEnable(GL_BLEND)); }
	if (prevDepthTest) { GL(glEnable(GL_DEPTH_TEST)); }
	if (prevCullFace) { GL(glEnable(GL_CULL_FACE)); }
	if (prevScissor) { GL(glEnable(GL_SCISSOR_TEST)); }
	if (prevStencil) { GL(glEnable(GL_STENCIL_TEST)); }
	if (prevDepthMask) { GL(glDepthMask(GL_TRUE)); }
	GL(glBindTexture(GL_TEXTURE_2D, prevTexture));
	GL(glBindVertexArray(prevVAO));
	GL(glUseProgram(prevProgram));
}

#endif // XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES

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

#if XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES
	InitCylinderGeometry();
	InitCylinder120Geometry();
#endif

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
#if XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES
	DestroyCylinderGeometry();
	DestroyCylinder120Geometry();
#endif
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

	// Render cylinder mesh for cinema cylinder modes
#if XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES
	if (screenMode) {
		int ds = vrConfig[VR_CONFIG_DISPLAY_SURFACE];
		if ((ds == VR_SURFACE_CURVED && cylinderMeshReady) ||
		    (ds == VR_SURFACE_CYLINDER120 && cyl120MeshReady)) {
			RenderCylinderScreen(fboIndex, engine);
		}
	}
#endif

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
		// Geometry-based cylinders only support MONO_SCREEN and STEREO_SCREEN
		if ((actualSurface == VR_SURFACE_CURVED || actualSurface == VR_SURFACE_CYLINDER120) &&
		    vrMode != VR_MODE_MONO_SCREEN && vrMode != VR_MODE_STEREO_SCREEN) {
			actualSurface = VR_SURFACE_FLAT;
		}

		if (actualSurface == VR_SURFACE_CURVED || actualSurface == VR_SURFACE_CYLINDER120) {
			// Geometry-based cylinder: FBO already contains rendered cylinder mesh
			// (rendered in VR_EndFrame via RenderCylinderScreen).
			// Submit as projection layer — works on all OpenXR runtimes.
			// Reuse function-scope projection_layer_elements to avoid dangling pointer
			// (local arrays go out of scope before xrEndFrame reads the layer).
			for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
				ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer[0];
				if (vrMode == VR_MODE_STEREO_SCREEN) {
					frameBuffer = &engine->appState.Renderer.FrameBuffer[eye];
				}

				memset(&projection_layer_elements[eye], 0, sizeof(XrCompositionLayerProjectionView));
				projection_layer_elements[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
				projection_layer_elements[eye].pose = invViewTransform[eye];
				projection_layer_elements[eye].fov = projections[eye].fov;
				projection_layer_elements[eye].subImage.swapchain = frameBuffer->ColorSwapChain.Handle;
				projection_layer_elements[eye].subImage.imageRect.offset.x = 0;
				projection_layer_elements[eye].subImage.imageRect.offset.y = 0;
				projection_layer_elements[eye].subImage.imageRect.extent.width = frameBuffer->ColorSwapChain.Width;
				projection_layer_elements[eye].subImage.imageRect.extent.height = frameBuffer->ColorSwapChain.Height;
				projection_layer_elements[eye].subImage.imageArrayIndex = 0;
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
