/*
 * Copyright © 2020 Ruinan Duan, duanruinan@zoho.com 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_array.h>
#include <cube_region.h>
#include <cube_shm.h>
#include <cube_signal.h>
#include <cube_compositor.h>
#include <cube_renderer.h>

static enum cb_log_level gles_dbg = CB_LOG_DEBUG;
static enum cb_log_level egl_dbg = CB_LOG_DEBUG;

#define gles_debug(fmt, ...) do { \
	if (gles_dbg >= CB_LOG_DEBUG) { \
		cb_tlog("[GLES][DEBUG ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define gles_info(fmt, ...) do { \
	if (gles_dbg >= CB_LOG_INFO) { \
		cb_tlog("[GLES][INFO  ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define gles_notice(fmt, ...) do { \
	if (gles_dbg >= CB_LOG_NOTICE) { \
		cb_tlog("[GLES][NOTICE] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define gles_warn(fmt, ...) do { \
	cb_tlog("[GLES][WARN  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define gles_err(fmt, ...) do { \
	cb_tlog("[GLES][ERROR ] " fmt, ##__VA_ARGS__); \
} while (0);

#define egl_debug(fmt, ...) do { \
	if (egl_dbg >= CB_LOG_DEBUG) { \
		cb_tlog("[EGL ][DEBUG ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define egl_info(fmt, ...) do { \
	if (egl_dbg >= CB_LOG_INFO) { \
		cb_tlog("[EGL ][INFO  ] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define egl_notice(fmt, ...) do { \
	if (egl_dbg >= CB_LOG_NOTICE) { \
		cb_tlog("[EGL ][NOTICE] " fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define egl_warn(fmt, ...) do { \
	cb_tlog("[EGL ][WARN  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define egl_err(fmt, ...) do { \
	cb_tlog("[EGL ][ERROR] " fmt, ##__VA_ARGS__); \
} while (0);

static PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display = NULL;

struct gl_shader {
	GLuint program;
	GLuint vertex_shader, fragment_shader;
	GLint proj_uniform;
	GLint tex_uniforms[3];
	GLint alpha_uniform;
	GLint color_uniform;
	const char *vertex_source, *fragment_source;
};

struct gl_renderer {
	struct renderer base;
	struct compositor *c;
	EGLDisplay egl_display;
	EGLContext egl_context;
	EGLConfig egl_config;
	EGLSurface dummy_surface;
	u32 gl_version;

	struct cb_array vertices;
	struct cb_array vtxcnt;

	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
	PFNEGLCREATEIMAGEKHRPROC create_image;
	PFNEGLDESTROYIMAGEKHRPROC destroy_image;
	PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC create_platform_window;

	s32 support_unpack_subimage;
	s32 support_context_priority;
	s32 support_surfaceless_context;
	s32 support_texture_rg;
	s32 support_dmabuf_import;

	struct list_head dmabuf_images;

	struct gl_shader texture_shader_rgba;
	struct gl_shader texture_shader_egl_external;
	struct gl_shader texture_shader_rgbx;
	struct gl_shader texture_shader_y_u_v;
	struct gl_shader *current_shader;

	struct cb_signal destroy_signal;
};

static inline struct gl_renderer *to_glr(struct renderer *renderer)
{
	return container_of(renderer, struct gl_renderer, base);
}

static void gl_renderer_destroy(struct renderer *renderer)
{
	struct gl_renderer *r = to_glr(renderer);

	if (!renderer)
		return;

	eglMakeCurrent(r->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
	
	eglTerminate(r->egl_display);
	eglReleaseThread();
	free(r);
}

struct renderer *renderer_create(struct compositor *c,
				 u32 *formats, s32 count_fmts,
				 bool no_winsys, void *native_window, s32 *vid)
{
	struct gl_renderer *r = NULL;

	if (!c)
		return NULL;

	r = calloc(1, sizeof(*r));
	r->base.destroy = gl_renderer_destroy;
	r->c = c;

	r->egl_display = EGL_NO_DISPLAY;

	if (no_winsys) {
		if (!get_platform_display) {
			get_platform_display = (void *)eglGetProcAddress(
				"eglGetPlatformDisplayEXT");
		}
		if (!get_platform_display)
			goto err;
		r->egl_display = get_platform_display(EGL_PLATFORM_GBM_KHR,
						      native_window,
						      NULL);
	} else {
		r->egl_display = eglGetDisplay(native_window);
	}

	if (r->egl_display == EGL_NO_DISPLAY) {
		egl_err("failed to create EGL display.");
		goto err;
	}

	return &r->base;

err:
	if (r && r->egl_display != EGL_NO_DISPLAY)
		eglTerminate(r->egl_display);
	if (r)
		free(r);
	return NULL;
}

#if 0
static const char vertex_shader[] =
	"uniform mat4 proj;\n"
	"attribute vec2 position;\n"
	"attribute vec2 texcoord;\n"
	"varying vec2 v_texcoord;\n"
	"void main()\n"
	"{\n"
	"   gl_Position = proj * vec4(position, 0.0, 1.0);\n"
	"   v_texcoord = texcoord;\n"
	"}\n";

static const char texture_fragment_shader_rgba[] =
	"precision mediump float;\n"
	"varying vec2 v_texcoord;\n"
	"uniform sampler2D tex;\n"
	"uniform float alpha;\n"
	"void main()\n"
	"{\n"
	"   gl_FragColor = alpha * texture2D(tex, v_texcoord)\n;"
	;

static const char texture_fragment_shader_rgbx[] =
	"precision mediump float;\n"
	"varying vec2 v_texcoord;\n"
	"uniform sampler2D tex;\n"
	"uniform float alpha;\n"
	"void main()\n"
	"{\n"
	"   gl_FragColor.rgb = alpha * texture2D(tex, v_texcoord).rgb\n;"
	"   gl_FragColor.a = alpha;\n"
	;

static const char texture_fragment_shader_egl_external[] =
	"#extension GL_OES_EGL_image_external : require\n"
	"precision mediump float;\n"
	"varying vec2 v_texcoord;\n"
	"uniform samplerExternalOES tex;\n"
	"uniform float alpha;\n"
	"void main()\n"
	"{\n"
	"   gl_FragColor = alpha * texture2D(tex, v_texcoord)\n;"
	;

#define FRAGMENT_CONVERT_YUV						\
	"  y *= alpha;\n"						\
	"  u *= alpha;\n"						\
	"  v *= alpha;\n"						\
	"  gl_FragColor.r = y + 1.792 * v;\n"			\
	"  gl_FragColor.g = y - 0.213 * u - 0.534 * v;\n"	\
	"  gl_FragColor.b = y + 2.114 * u;\n"			\
	"  gl_FragColor.a = alpha;\n"

static const char texture_fragment_shader_y_u_v[] =
	"precision mediump float;\n"
	"uniform sampler2D tex;\n"
	"uniform sampler2D tex1;\n"
	"uniform sampler2D tex2;\n"
	"varying vec2 v_texcoord;\n"
	"uniform float alpha;\n"
	"void main() {\n"
	"  float y = 1.16438356 * (texture2D(tex, v_texcoord).x - 0.0627);\n"
	"  float u = texture2D(tex1, v_texcoord).x - 0.502;\n"
	"  float v = texture2D(tex2, v_texcoord).x - 0.502;\n"
	FRAGMENT_CONVERT_YUV
	;

static const char fragment_brace[] =
	"}\n";

struct dma_buffer {
	struct cb_buffer base;
	EGLImageKHR image;
	struct list_head link;
	struct gl_display *disp;
};

struct polygon8 {
	float x[8];
	float y[8];
	s32 n;
};

struct clip_context {
	struct {
		float x;
		float y;
	} prev;

	struct {
		float x1, y1;
		float x2, y2;
	} clip;

	struct {
		float *x;
		float *y;
	} vertices;
};

static const EGLint gl_opaque_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_ALPHA_SIZE, 0,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_NONE,
};

struct gl_output_state {
	EGLSurface egl_surface;
};

struct gl_shader {
	GLuint program;
	GLuint vertex_shader, fragment_shader;
	GLint proj_uniform;
	GLint tex_uniforms[3];
	GLint alpha_uniform;
	GLint color_uniform;
	const char *vertex_source, *fragment_source;
};

struct gl_renderer {
	struct renderer base;
	EGLDisplay egl_display;
	EGLContext egl_context;
	EGLConfig egl_config;
	EGLSurface dummy_surface;
	u32 gl_version;

	struct cb_array vertices;
	struct cb_array vtxcnt;

	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
	PFNEGLCREATEIMAGEKHRPROC create_image;
	PFNEGLDESTROYIMAGEKHRPROC destroy_image;
	PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC create_platform_window;

	s32 support_unpack_subimage;
	s32 support_context_priority;
	s32 support_surfaceless_context;
	s32 support_texture_rg;
	s32 support_dmabuf_import;

	struct list_head dmabuf_images;

	struct gl_shader texture_shader_rgba;
	struct gl_shader texture_shader_egl_external;
	struct gl_shader texture_shader_rgbx;
	struct gl_shader texture_shader_y_u_v;
	struct gl_shader *current_shader;

	struct cb_signal destroy_signal;
};

struct gl_surface_state {
	GLfloat color[4];
	struct gl_shader *shader;

	GLuint textures[3];
	s32 count_textures;
	s32 needs_full_upload;
	struct cb_region texture_damage;

	GLenum gl_format[3];
	GLenum gl_pixel_type;

	GLenum target;

	s32 pitch;
	s32 h;
	s32 y_inverted;

	s32 offset[3];
	s32 hsub[3];
	s32 vsub[3];

	struct cb_surface *surface;

	enum cb_buffer_type buf_type;

	struct cb_buffer *buffer;

	struct cb_listener display_destroy_listener;
	struct cb_listener surface_destroy_listener;
};

static PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display = NULL;

static const char *egl_strerror(EGLint err)
{
#define EGLERROR(x) case x: return #x;
	switch (err) {
	EGLERROR(EGL_SUCCESS)
	EGLERROR(EGL_NOT_INITIALIZED)
	EGLERROR(EGL_BAD_ACCESS)
	EGLERROR(EGL_BAD_ALLOC)
	EGLERROR(EGL_BAD_ATTRIBUTE)
	EGLERROR(EGL_BAD_CONTEXT)
	EGLERROR(EGL_BAD_CONFIG)
	EGLERROR(EGL_BAD_CURRENT_SURFACE)
	EGLERROR(EGL_BAD_DISPLAY)
	EGLERROR(EGL_BAD_SURFACE)
	EGLERROR(EGL_BAD_MATCH)
	EGLERROR(EGL_BAD_PARAMETER)
	EGLERROR(EGL_BAD_NATIVE_PIXMAP)
	EGLERROR(EGL_BAD_NATIVE_WINDOW)
	EGLERROR(EGL_CONTEXT_LOST)
	default:
		return "unknown";
	}
#undef EGLERROR
}

static void egl_error_state(void)
{
	EGLint err;

	err = eglGetError();
	egl_err("EGL err: %s (0x%04lX)", egl_strerror(err), (u64)err);
}

static inline struct gl_display *get_display(struct clv_compositor *c)
{
	return container_of(c->renderer, struct gl_display, base);
}

static s32 match_config_to_visual(EGLDisplay egl_display, EGLint visual_id,
				  EGLConfig *configs, s32 count)
{
	s32 i;
	EGLint id;

	for (i = 0; i < count; i++) {
		if (!eglGetConfigAttrib(egl_display, configs[i],
					EGL_NATIVE_VISUAL_ID, &id)) {
			egl_warn("get VISUAL_ID failed.");
			continue;
		} else {
			egl_debug("get VISUAL_ID ok. %u %u", id, visual_id);
		}
		if (id == visual_id)
			return i;
	}

	return -1;
}

static s32 egl_choose_config(struct gl_display *disp, const EGLint *attribs,
			     const EGLint *visual_ids, const s32 count_ids,
			     EGLConfig *config_matched, EGLint *vid)
{
	EGLint count_configs = 0;
	EGLint count_matched = 0;
	EGLConfig *configs;
	s32 i, config_index = -1;

	if (!eglGetConfigs(disp->egl_display, NULL, 0, &count_configs)) {
		egl_err("Cannot get EGL configs.");
		return -1;
	}
	egl_debug("count_configs = %d", count_configs);

	configs = calloc(count_configs, sizeof(*configs));
	if (!configs)
		return -ENOMEM;

	if (!eglChooseConfig(disp->egl_display, attribs, configs,
			     count_configs, &count_matched)
	    || !count_matched) {
		egl_err("cannot select appropriate configs.");
		goto out1;
	}
	egl_debug("count_matched = %d", count_matched);

	if (!visual_ids || count_ids == 0)
		config_index = 0;

	for (i = 0; config_index == -1 && i < count_ids; i++) {
		config_index = match_config_to_visual(disp->egl_display,
						      visual_ids[i],
						      configs,
						      count_matched);
		egl_debug("config_index = %d i = %d count_ids = %d",
			  config_index, i, count_ids);
	}

	if (config_index != -1)
		*config_matched = configs[config_index];

out1:
	if (visual_ids) {
		*vid = visual_ids[i - 1];
	} else {
		for (i = 0; i < count_matched; i++) {
			if (!eglGetConfigAttrib(disp->egl_display, configs[0],
						EGL_NATIVE_VISUAL_ID, vid)) {
				egl_err("Get visual id failed.");
				continue;
			}
			break;
		}
	}
	free(configs);
	if (config_index == -1)
		return -1;

	if (i > 1)
		egl_warn("Unable to use first choice EGL config with ID "
			 "0x%x, succeeded with alternate ID 0x%x",
			 visual_ids[0], visual_ids[i - 1]);

	return 0;
}

static s32 check_egl_extension(const char *extensions, const char *extension)
{
	u32 extlen = strlen(extension);
	const char *end = extensions + strlen(extensions);

	while (extensions < end) {
		size_t n = 0;

		if (*extensions == ' ') {
			extensions++;
			continue;
		}

		n = strcspn(extensions, " ");

		if (n == extlen && strncmp(extension, extensions, n) == 0)
			return 1;

		extensions += n;
	}

	return 0;
}

static void set_egl_client_extensions(struct gl_display *disp)
{
	const char *extensions;
	
	extensions = (const char *)eglQueryString(EGL_NO_DISPLAY,
						  EGL_EXTENSIONS);
	if (!extensions) {
		egl_info("cannot query client EGL_EXTENSIONS");
		return;
	}

	if (check_egl_extension(extensions, "EGL_EXT_platform_base")) {
		disp->create_platform_window = (void *)eglGetProcAddress(
				"eglCreatePlatformWindowSurfaceEXT");
		if (!disp->create_platform_window)
			egl_warn("failed to call "
				 "eglCreatePlatformWindowSurfaceEXT");
	} else {
		egl_warn("EGL_EXT_platform_base not supported.");
	}
}

static s32 set_egl_extensions(struct gl_display *disp)
{
	const char *extensions;

	disp->create_image = (void *)eglGetProcAddress("eglCreateImageKHR");
	disp->destroy_image = (void *)eglGetProcAddress("eglDestroyImageKHR");
	extensions = (const char *)eglQueryString(disp->egl_display,
						  EGL_EXTENSIONS);
	if (!extensions) {
		egl_err("cannot query EGL_EXTENSIONS");
		return -1;
	}

	if (check_egl_extension(extensions, "EGL_IMG_context_priority"))
		disp->support_context_priority = 1;

	if (check_egl_extension(extensions, "EGL_KHR_surfaceless_context"))
		disp->support_surfaceless_context = 1;

	if (check_egl_extension(extensions, "EGL_EXT_image_dma_buf_import"))
		disp->support_dmabuf_import = 1;

	set_egl_client_extensions(disp);
	egl_info("EGL_IMG_context_priority: %s",
		 disp->support_context_priority ? "Y" : "N");
	egl_info("EGL_KHR_surfaceless_context: %s",
		 disp->support_surfaceless_context ? "Y" : "N");
	egl_info("EGL_EXT_image_dma_buf_import: %s",
		 disp->support_dmabuf_import ? "Y" : "N");
	return 0;
}

static void print_egl_info(EGLDisplay disp)
{
	const char *str;

	str = eglQueryString(disp, EGL_VERSION);
	egl_info("EGL version: %s", str ? str : "(null)");

	str = eglQueryString(disp, EGL_VENDOR);
	egl_info("EGL vendor: %s", str ? str : "(null)");

	str = eglQueryString(disp, EGL_CLIENT_APIS);
	egl_info("EGL client APIs: %s", str ? str : "(null)");

	str = eglQueryString(disp, EGL_EXTENSIONS);
	egl_info("EGL extensions: %s", str ? str : "(null)");
}

static void gl_info(void)
{
	const char *str;

	str = (char *)glGetString(GL_VERSION);
	gles_info("GL version: %s", str ? str : "(null)");

	str = (char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
	gles_info("GLSL version: %s", str ? str : "(null)");

	str = (char *)glGetString(GL_VENDOR);
	gles_info("GL vendor: %s", str ? str : "(null)");

	str = (char *)glGetString(GL_RENDERER);
	gles_info("GL renderer: %s", str ? str : "(null)");

	str = (char *)glGetString(GL_EXTENSIONS);
	gles_info("GL extensions: %s", str ? str : "(null)");
}

static s32 create_pbuffer_surface(struct gl_display *disp)
{
	EGLConfig pbuffer_config;
	EGLint vid;

	static const EGLint pbuffer_config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE,
	};

	static const EGLint pbuffer_attribs[] = {
		EGL_WIDTH, 10,
		EGL_HEIGHT, 10,
		EGL_NONE,
	};

	if (egl_choose_config(disp, pbuffer_config_attribs, NULL, 0,
			      &pbuffer_config, &vid) < 0) {
		egl_err("failed to choose EGL config for PbufferSurface");
		return -1;
	}

	disp->dummy_surface = eglCreatePbufferSurface(disp->egl_display,
						      pbuffer_config,
						      pbuffer_attribs);

	if (disp->dummy_surface == EGL_NO_SURFACE) {
		egl_err("failed to create PbufferSurface");
		return -1;
	}

	return 0;
}

#define GEN_GL_VERSION(major, minor) (((u32)(major) << 16) | (u32)(minor))
#define GEN_GL_VERSION_INVALID GEN_GL_VERSION(0, 0)

static u32 get_gl_version(void)
{
	const char *version;
	s32 major, minor;

	version = (const char *)glGetString(GL_VERSION);
	if (version && (sscanf(version, "%d.%d", &major, &minor) == 2
	     || sscanf(version, "OpenGL ES %d.%d", &major, &minor) == 2)) {
		return GEN_GL_VERSION(major, minor);
	}

	return GEN_GL_VERSION_INVALID;
}

static void set_shaders(struct clv_compositor *c)
{
	struct gl_display *disp = get_display(c);

	disp->texture_shader_rgba.vertex_source = vertex_shader;
	disp->texture_shader_rgba.fragment_source =texture_fragment_shader_rgba;

	disp->texture_shader_egl_external.vertex_source = vertex_shader;
	disp->texture_shader_egl_external.fragment_source =
		texture_fragment_shader_egl_external;

	disp->texture_shader_rgbx.vertex_source = vertex_shader;
	disp->texture_shader_rgbx.fragment_source =texture_fragment_shader_rgbx;

	disp->texture_shader_y_u_v.vertex_source = vertex_shader;
	disp->texture_shader_y_u_v.fragment_source =
						texture_fragment_shader_y_u_v;
}

static s32 gl_setup(struct clv_compositor *c, EGLSurface egl_surface)
{
	struct gl_display *disp = get_display(c);
	const char *extensions;
	EGLConfig context_config;
	EGLBoolean ret;
	EGLint context_attribs[16] = {
		EGL_CONTEXT_CLIENT_VERSION, 0,
	};
	u32 count_attrs = 2;
	EGLint value = EGL_CONTEXT_PRIORITY_MEDIUM_IMG;

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		egl_err("failed to bind EGL_OPENGL_ES_API");
		egl_error_state();
		return -1;
	}

	if (disp->support_context_priority) {
		context_attribs[count_attrs++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
		context_attribs[count_attrs++] = EGL_CONTEXT_PRIORITY_HIGH_IMG;
	}

	assert(count_attrs < ARRAY_SIZE(context_attribs));
	context_attribs[count_attrs] = EGL_NONE;

	context_config = disp->egl_config;

	context_attribs[1] = 3;
	disp->egl_context = eglCreateContext(disp->egl_display, context_config,
					     EGL_NO_CONTEXT, context_attribs);
	if (disp->egl_context == NULL) {
		context_attribs[1] = 2;
		disp->egl_context = eglCreateContext(disp->egl_display,
						     context_config,
						     EGL_NO_CONTEXT,
						     context_attribs);
		if (disp->egl_context == EGL_NO_CONTEXT) {
			egl_err("failed to create context");
			egl_error_state();
			return -1;
		}
		egl_info("Create OpenGLES2 context");
	} else {
		egl_info("Create OpenGLES3 context");
	}

	if (disp->support_context_priority) {
		eglQueryContext(disp->egl_display, disp->egl_context,
				EGL_CONTEXT_PRIORITY_LEVEL_IMG, &value);

		if (value != EGL_CONTEXT_PRIORITY_HIGH_IMG) {
			egl_err("Failed to obtain a high priority context.");
		}
	}

	ret = eglMakeCurrent(disp->egl_display, egl_surface,
			     egl_surface, disp->egl_context);
	if (ret == EGL_FALSE) {
		egl_err("Failed to make EGL context current.");
		egl_error_state();
		return -1;
	}

	disp->gl_version = get_gl_version();
	if (disp->gl_version == GEN_GL_VERSION_INVALID) {
		gles_warn("failed to detect GLES version, "
			  "defaulting to 2.0.");
		disp->gl_version = GEN_GL_VERSION(2, 0);
	}

	gl_info();
	disp->image_target_texture_2d =
		(void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");

	extensions = (const char *)glGetString(GL_EXTENSIONS);
	if (!extensions) {
		gles_err("Cannot get GL extension string");
		return -1;
	}
	if (!check_egl_extension(extensions, "GL_EXT_texture_format_BGRA8888")){
		gles_err("GL_EXT_texture_format_BGRA8888 not available");
		return -1;
	}

	if (disp->gl_version >= GEN_GL_VERSION(3, 0)
	     || check_egl_extension(extensions, "GL_EXT_unpack_subimage"))
		disp->support_unpack_subimage = 1;
	
	if (disp->gl_version >= GEN_GL_VERSION(3, 0)
	     || check_egl_extension(extensions, "GL_EXT_texture_rg"))
		disp->support_texture_rg = 1;

	glActiveTexture(GL_TEXTURE0);

	set_shaders(c);

	gles_info("GL_EXT_texture_rg: %s",
		  disp->support_texture_rg ? "Y" : "N");
	gles_info("GL_EXT_unpack_subimage: %s",
		  disp->support_unpack_subimage ? "Y" : "N");
	return 0;
}

static void gl_surface_state_destroy(struct gl_surface_state *gs)
{
	/* struct clv_buffer *buffer;
	struct dma_buffer *dmabuf;
	struct gl_display *disp; */

	if (gs) {
		if (gs->surface)
			gs->surface->renderer_state = NULL;
		glDeleteTextures(gs->count_textures, gs->textures);
		/*
		buffer = gs->buffer;
		if (buffer && buffer->type == CLV_BUF_TYPE_DMA) {
			dmabuf = container_of(buffer, struct dma_buffer, base);
			disp = dmabuf->disp;
			if (dmabuf->image != EGL_NO_IMAGE_KHR) {
				disp->destroy_image(disp->egl_display,
						    dmabuf->image);
				dmabuf->image = EGL_NO_IMAGE_KHR;
			}
		}
		*/
		clv_region_fini(&gs->texture_damage);
		list_del(&gs->display_destroy_listener.link);
		INIT_LIST_HEAD(&gs->display_destroy_listener.link);
		list_del(&gs->surface_destroy_listener.link);
		INIT_LIST_HEAD(&gs->surface_destroy_listener.link);
		free(gs);
	}
}

static void surface_state_handle_surface_destroy(struct clv_listener *listener,
						 void *data)
{
	struct gl_surface_state *gs = container_of(listener,
						   struct gl_surface_state,
						   surface_destroy_listener);
	gl_surface_state_destroy(gs);
}

static void surface_state_handle_display_destroy(struct clv_listener *listener,
						 void *data)
{
	struct gl_surface_state *gs = container_of(listener,
						   struct gl_surface_state,
						   display_destroy_listener);
	gl_surface_state_destroy(gs);
}

static s32 gl_surface_state_create(struct clv_surface *surface)
{
	struct clv_compositor *c = surface->c;
	struct gl_display *disp = get_display(c);
	struct gl_surface_state *gs;

	gs = calloc(1, sizeof(*gs));
	if (!gs)
		return -ENOMEM;

	gs->pitch = 1;
	gs->y_inverted = 1;
	gs->surface = surface;
	clv_region_init(&gs->texture_damage);

	gs->surface_destroy_listener.notify =
		surface_state_handle_surface_destroy;
	clv_signal_add(&surface->destroy_signal, &gs->surface_destroy_listener);

	gs->display_destroy_listener.notify =
		surface_state_handle_display_destroy;
	clv_signal_add(&disp->destroy_signal, &gs->display_destroy_listener);

	surface->renderer_state = gs;
	return 0;
}

static struct gl_surface_state *get_surface_state(struct clv_surface *surface)
{
	if (!surface->renderer_state)
		gl_surface_state_create(surface);

	return (struct gl_surface_state *)surface->renderer_state;
}

static void alloc_textures(struct gl_surface_state *gs, s32 count_textures)
{
	s32 i;

	if (count_textures <= gs->count_textures) {
		return;
	}

	for (i = gs->count_textures; i < count_textures; i++) {
		glGenTextures(1, &gs->textures[i]);
		glBindTexture(gs->target, gs->textures[i]);
		glTexParameteri(gs->target,
				GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(gs->target,
				GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	gs->count_textures = count_textures;
	glBindTexture(gs->target, 0);
}

static void gl_attach_dma_buffer(struct clv_surface *surface,
				 struct clv_buffer *buffer)
{
	struct clv_compositor *c = surface->c;
	struct gl_display *disp = get_display(c);
	struct gl_surface_state *gs = get_surface_state(surface);
	struct dma_buffer *dmabuf = container_of(buffer, struct dma_buffer,
						 base);
	//struct timespec t1, t2;

	//clock_gettime(c->clk_id, &t1);
	assert(dmabuf);
	if (buffer->pixel_fmt == CLV_PIXEL_FMT_XRGB8888) {
		gs->target = GL_TEXTURE_2D;
		surface->is_opaque = 1;
		gs->shader = &disp->texture_shader_rgba;
		gs->pitch = buffer->stride / 4;
	} else if (buffer->pixel_fmt == CLV_PIXEL_FMT_ARGB8888) {
		gs->target = GL_TEXTURE_2D;
		surface->is_opaque = 0;
		gs->shader = &disp->texture_shader_rgba;
		gs->pitch = buffer->stride / 4;
	} else if (buffer->pixel_fmt == CLV_PIXEL_FMT_NV12
	        || buffer->pixel_fmt == CLV_PIXEL_FMT_NV16) {
		gs->target = GL_TEXTURE_EXTERNAL_OES;
		surface->is_opaque = 1;
		gs->shader = &disp->texture_shader_egl_external;
		gs->pitch = buffer->w;
	} else {
		clv_err("illegal pixel fmt %u", buffer->pixel_fmt);
		return;
	}
	alloc_textures(gs, 1);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(gs->target, gs->textures[0]);
	disp->image_target_texture_2d(gs->target, dmabuf->image);
	gs->h = buffer->h;
	gs->buf_type = CLV_BUF_TYPE_DMA;
	gs->y_inverted = 1;
	gs->surface = surface;
	//clock_gettime(c->clk_id, &t2);
	//printf("attach dma buffer spent %lu\n", timespec_sub_to_msec(&t2, &t1));
}

static void gl_attach_shm_buffer(struct clv_surface *surface,
				 struct clv_buffer *buffer)
{
	struct clv_compositor *c = surface->c;
	struct gl_display *disp = get_display(c);
	struct gl_surface_state *gs = get_surface_state(surface);
	GLenum gl_format[3] = {0, 0, 0};
	GLenum gl_pixel_type;
	s32 pitch;
	s32 count_planes;
	struct timespec t1, t2;

	count_planes = 1;
	gs->offset[0] = 0;
	gs->hsub[0] = 1;
	gs->vsub[0] = 1;

	clock_gettime(c->clk_id, &t1);
	switch (buffer->pixel_fmt) {
	case CLV_PIXEL_FMT_XRGB8888:
		gs->shader = &disp->texture_shader_rgbx;
		pitch = buffer->stride / 4;
		gl_format[0] = GL_BGRA_EXT;
		gl_pixel_type = GL_UNSIGNED_BYTE;
		surface->is_opaque = 1;
		break;
	case CLV_PIXEL_FMT_ARGB8888:
		gs->shader = &disp->texture_shader_rgba;
		pitch = buffer->stride / 4;
		gl_format[0] = GL_BGRA_EXT;
		gl_pixel_type = GL_UNSIGNED_BYTE;
		surface->is_opaque = 0;
		break;
	case CLV_PIXEL_FMT_YUV420P:
		gs->shader = &disp->texture_shader_y_u_v;
		pitch = buffer->stride;
		gl_pixel_type = GL_UNSIGNED_BYTE;
		count_planes = 3;
		gs->offset[1] = gs->offset[0] + (pitch / gs->hsub[0]) *
				(buffer->h / gs->vsub[0]);
		gs->hsub[1] = 2;
		gs->vsub[1] = 2;
		gs->offset[2] = gs->offset[1] + (pitch / gs->hsub[1]) *
				(buffer->h / gs->vsub[1]);
		gs->hsub[2] = 2;
		gs->vsub[2] = 2;
		if (disp->support_texture_rg) {
			gl_format[0] = GL_R8_EXT;
			gl_format[1] = GL_R8_EXT;
			gl_format[2] = GL_R8_EXT;
		} else {
			gl_format[0] = GL_LUMINANCE;
			gl_format[1] = GL_LUMINANCE;
			gl_format[2] = GL_LUMINANCE;
		}
		surface->is_opaque = 1;
		break;
	case CLV_PIXEL_FMT_YUV444P:
		gs->shader = &disp->texture_shader_y_u_v;
		pitch = buffer->stride;
		gl_pixel_type = GL_UNSIGNED_BYTE;
		count_planes = 3;
		gs->offset[1] = gs->offset[0] + (pitch / gs->hsub[0]) *
				(buffer->h / gs->vsub[0]);
		gs->hsub[1] = 1;
		gs->vsub[1] = 1;
		gs->offset[2] = gs->offset[1] + (pitch / gs->hsub[1]) *
				(buffer->h / gs->vsub[1]);
		gs->hsub[2] = 1;
		gs->vsub[2] = 1;
		if (disp->support_texture_rg) {
			gl_format[0] = GL_R8_EXT;
			gl_format[1] = GL_R8_EXT;
			gl_format[2] = GL_R8_EXT;
		} else {
			gl_format[0] = GL_LUMINANCE;
			gl_format[1] = GL_LUMINANCE;
			gl_format[2] = GL_LUMINANCE;
		}
		surface->is_opaque = 1;
		break;
	default:
		gles_err("unknown pixel format %u", buffer->pixel_fmt);
		return;
	}

	if (pitch != gs->pitch
	    || buffer->h != gs->h
	    || gl_format[0] != gs->gl_format[0]
	    || gl_format[1] != gs->gl_format[1]
	    || gl_format[2] != gs->gl_format[2]
	    || gl_pixel_type != gs->gl_pixel_type
	    || gs->buf_type != CLV_BUF_TYPE_SHM) {
		gs->pitch = pitch;
		gs->target = GL_TEXTURE_2D;
		gs->h = buffer->h;
		gs->gl_format[0] = gl_format[0];
		gs->gl_format[1] = gl_format[1];
		gs->gl_format[2] = gl_format[2];
		gs->gl_pixel_type = gl_pixel_type;
		gs->needs_full_upload = 1;
		gs->y_inverted = 1;
		gs->surface = surface;
		gs->buf_type = CLV_BUF_TYPE_SHM;
		alloc_textures(gs, count_planes);
	}
	clock_gettime(c->clk_id, &t2);
}

static void gl_attach_buffer(struct clv_surface *surface,
			     struct clv_buffer *buffer)
{
	struct gl_surface_state *gs;

	gs = get_surface_state(surface);

	if (!buffer) {
		glDeleteTextures(gs->count_textures, gs->textures);
		gs->count_textures = 0;
		gs->y_inverted = 1;
		gs->buffer = NULL;
		surface->is_opaque = 0;
		return;
	}

	if (buffer->type == CLV_BUF_TYPE_SHM) {
		gl_attach_shm_buffer(surface, buffer);
		gs->buffer = buffer;
	} else if (buffer->type == CLV_BUF_TYPE_DMA) {
		gl_attach_dma_buffer(surface, buffer);
		gs->buffer = buffer;
	} else {
		gles_err("unknown buffer type %p %u", buffer, buffer->type);
		gs->count_textures = 0;
		gs->y_inverted = 1;
		surface->is_opaque = 0;
	}
}

static s32 gl_switch_output(struct clv_output *output)
{
	struct gl_output_state *go = output->renderer_state;
	struct gl_display *disp = get_display(output->c);
	static s32 errored = 0;

	if (eglMakeCurrent(disp->egl_display, go->egl_surface, go->egl_surface,
			   disp->egl_context) == EGL_FALSE) {
		if (errored) {
			egl_err("eglMakeCurrent failed.");
			return -1;
		}
		errored = 1;
		egl_err("failed to switch EGL context.");
		egl_error_state();
		return -1;
	}

	return 0;
}

static s32 compile_shader(GLenum type, s32 count, const char **sources)
{
	GLuint s;
	char msg[512];
	GLint status;

	s = glCreateShader(type);
	glShaderSource(s, count, sources, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &status);
	if (!status) {
		glGetShaderInfoLog(s, sizeof msg, NULL, msg);
		gles_err("shader info: %s", msg);
		return GL_NONE;
	}

	return s;
}

static s32 load_shader(struct gl_shader *shader, struct gl_display *disp,
		       const char *vertex_source, const char *fragment_source)
{
	char msg[512];
	GLint status;
	const char *sources[2];

	shader->vertex_shader =
		compile_shader(GL_VERTEX_SHADER, 1, &vertex_source);

	sources[0] = fragment_source;
	sources[1] = fragment_brace;

	shader->fragment_shader = compile_shader(GL_FRAGMENT_SHADER, 2,
						 sources);

	shader->program = glCreateProgram();
	glAttachShader(shader->program, shader->vertex_shader);
	glAttachShader(shader->program, shader->fragment_shader);
	glBindAttribLocation(shader->program, 0, "position");
	glBindAttribLocation(shader->program, 1, "texcoord");

	glLinkProgram(shader->program);
	glGetProgramiv(shader->program, GL_LINK_STATUS, &status);
	if (!status) {
		glGetProgramInfoLog(shader->program, sizeof(msg), NULL, msg);
		gles_err("link info: %s", msg);
		return -1;
	}

	shader->proj_uniform = glGetUniformLocation(shader->program, "proj");
	shader->tex_uniforms[0] = glGetUniformLocation(shader->program, "tex");
	shader->tex_uniforms[1] = glGetUniformLocation(shader->program, "tex1");
	shader->tex_uniforms[2] = glGetUniformLocation(shader->program, "tex2");
	shader->alpha_uniform = glGetUniformLocation(shader->program, "alpha");
	shader->color_uniform = glGetUniformLocation(shader->program, "color");

	return 0;
}

static void use_shader(struct gl_display *disp, struct gl_shader *shader)
{
	s32 ret;

	if (!shader->program) {
		ret = load_shader(shader, disp,
				  shader->vertex_source,
				  shader->fragment_source);
		if (ret < 0)
			gles_err("failed to compile shader");
	}

	if (disp->current_shader == shader)
		return;

	glUseProgram(shader->program);
	disp->current_shader = shader;
}

static void shader_uniforms(struct gl_shader *shader, struct clv_view *v,
			    struct clv_output *output)
{
	s32 i;
	struct gl_surface_state *gs = get_surface_state(v->surface);
/* TODO
	struct gl_output_state *go = output->renderer_state;
*/
	static GLfloat projmat_normal_temp[16] = { /* transpose */
		 2.0f,  0.0f, 0.0f, 0.0f,
		 0.0f,  2.0f, 0.0f, 0.0f,
		 0.0f,  0.0f, 1.0f, 0.0f,
		-1.0f, -1.0f, 0.0f, 1.0f
	};
	static GLfloat projmat_yinvert_temp[16] = { /* transpose */
		 2.0f,  0.0f, 0.0f, 0.0f,
		 0.0f, -2.0f, 0.0f, 0.0f,
		 0.0f,  0.0f, 1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f, 1.0f
	};
	static GLfloat projmat_yinvert[16];
	static GLfloat projmat_normal[16];

	memcpy(projmat_yinvert, projmat_yinvert_temp,
	       sizeof(projmat_yinvert));
	memcpy(projmat_normal, projmat_normal_temp,
	       sizeof(projmat_normal));
	
	projmat_yinvert[0] /= output->render_area.w;
	projmat_yinvert[5] /= output->render_area.h;

	projmat_normal[0] /= output->render_area.w;
	projmat_normal[5] /= output->render_area.h;

	if (gs->y_inverted) {
		glUniformMatrix4fv(shader->proj_uniform,
				   1, GL_FALSE, projmat_yinvert);
	} else {
		glUniformMatrix4fv(shader->proj_uniform,
				   1, GL_FALSE, projmat_normal);
	}
	glUniform4fv(shader->color_uniform, 1, gs->color);
	glUniform1f(shader->alpha_uniform, v->alpha);

	for (i = 0; i < gs->count_textures; i++)
		glUniform1i(shader->tex_uniforms[i], i);
}

static s32 merge_down(struct clv_box *a, struct clv_box *b,
		      struct clv_box *merge)
{
	if (a->p1.x == b->p1.x && a->p2.x == b->p2.x && a->p1.y == b->p2.y) {
		merge->p1.x = a->p1.x;
		merge->p2.x = a->p2.x;
		merge->p1.y = b->p1.y;
		merge->p2.y = a->p2.y;
		return 1;
	}
	return 0;
}

static s32 compress_bands(struct clv_box *inboxes, s32 count_in,
			  struct clv_box **outboxes)
{
	s32 merged = 0;
	struct clv_box *out, merge_rect;
	s32 i, j, count_out;

	if (!count_in) {
		*outboxes = NULL;
		return 0;
	}

	out = calloc(count_in, sizeof(struct clv_box));
	out[0] = inboxes[0];
	count_out = 1;
	for (i = 1; i < count_in; i++) {
		for (j = 0; j < count_out; j++) {
			merged = merge_down(&inboxes[i], &out[j], &merge_rect);
			if (merged) {
				out[j] = merge_rect;
				break;
			}
		}
		if (!merged) {
			out[count_out] = inboxes[i];
			count_out++;
		}
	}
	*outboxes = out;
	return count_out;
}

#define clip(x, a, b)  MIN(MAX(x, a), b)
static s32 clip_simple(struct clip_context *ctx, struct polygon8 *surf,
		       float *ex, float *ey)
{
	s32 i;

	for (i = 0; i < surf->n; i++) {
		ex[i] = clip(surf->x[i], ctx->clip.x1, ctx->clip.x2);
		ey[i] = clip(surf->y[i], ctx->clip.y1, ctx->clip.y2);
	}
	return surf->n;
}

static s32 calculate_edges(struct clv_view *v, struct clv_box *box,
			   struct clv_box *surf_box, GLfloat *ex, GLfloat *ey,
			   struct clv_pos *output_base)
{

	struct clip_context ctx;
	s32 i;
	GLfloat min_x, max_x, min_y, max_y;
	struct polygon8 surf = {
		{ surf_box->p1.x, surf_box->p2.x,
		  surf_box->p2.x, surf_box->p1.x },
		{ surf_box->p1.y, surf_box->p1.y,
		  surf_box->p2.y, surf_box->p2.y },
		4
	};

	ctx.clip.x1 = box->p1.x;
	ctx.clip.y1 = box->p1.y;
	ctx.clip.x2 = box->p2.x;
	ctx.clip.y2 = box->p2.y;

	/* transform surface to screen space: */
	for (i = 0; i < surf.n; i++) {
		surf.x[i] = surf.x[i] + v->area.pos.x;
		surf.x[i] -= output_base->x;
		surf.y[i] = surf.y[i] + v->area.pos.y;
		surf.y[i] -= output_base->y;
	}

	/* find bounding box: */
	min_x = max_x = surf.x[0];
	min_y = max_y = surf.y[0];

	for (i = 1; i < surf.n; i++) {
		min_x = MIN(min_x, surf.x[i]);
		max_x = MAX(max_x, surf.x[i]);
		min_y = MIN(min_y, surf.y[i]);
		max_y = MAX(max_y, surf.y[i]);
	}

	/* First, simple bounding box check to discard early transformed
	 * surface rects that do not intersect with the clip region:
	 */
	if ((min_x >= ctx.clip.x2) || (max_x <= ctx.clip.x1) ||
	    (min_y >= ctx.clip.y2) || (max_y <= ctx.clip.y1))
		return 0;

	/* Simple case, bounding box edges are parallel to surface edges,
	 * there will be only four edges.  We just need to clip the surface
	 * vertices to the clip rect bounds:
	 */
	return clip_simple(&ctx, &surf, ex, ey);
}

static s32 texture_region(struct clv_view *view, struct clv_region *region,
			  struct clv_region *surf_region,
			  struct clv_pos *output_base)
{
	struct gl_surface_state *gs = get_surface_state(view->surface);
	struct clv_compositor *c = view->surface->c;
	struct gl_display *disp = get_display(c);
	s32 count_boxes, count_surf_boxes, count_raw_boxes, i, j, k;
	s32 use_band_compression, n;
	struct clv_box *raw_boxes, *boxes, *surf_boxes, *box, *surf_box;
	u32 count_vtx = 0, *vtxcnt;
	GLfloat *v, inv_w, inv_h;
	GLfloat ex[8], ey[8];
	GLfloat bx, by;

	raw_boxes = clv_region_boxes(region, &count_raw_boxes);
	surf_boxes = clv_region_boxes(surf_region, &count_surf_boxes);

	if (count_raw_boxes < 4) {
		use_band_compression = 0;
		count_boxes = count_raw_boxes;
		boxes = raw_boxes;
	} else {
		use_band_compression = 1;
		count_boxes = compress_bands(raw_boxes, count_raw_boxes,&boxes);
	}

	v = clv_array_add(&disp->vertices,
			  count_boxes * count_surf_boxes * 8 * 4 * sizeof(*v));
	vtxcnt = clv_array_add(&disp->vtxcnt,
			       count_boxes * count_surf_boxes *sizeof(*vtxcnt));
	inv_w = 1.0f / gs->pitch;
	inv_h = 1.0f / gs->h;

	for (i = 0; i < count_boxes; i++) {
		box = &boxes[i];
		for (j = 0; j < count_surf_boxes; j++) {
			surf_box = &surf_boxes[j];
			n = calculate_edges(view, box, surf_box, ex, ey,
					    output_base);
			if (n < 3)
				continue;
			/* emit edge points: */
			for (k = 0; k < n; k++) {
				/* position: */
				*(v++) = ex[k];
				*(v++) = ey[k];
				gles_debug("position (%f, %f)", *(v-2), *(v-1));

				/* make buffer point */
				bx = ex[k] - view->area.pos.x + output_base->x;
				by = ey[k] - view->area.pos.y + output_base->y;

				/* texcoord: */
				*(v++) = bx * inv_w;
				gles_debug("%f %f %u", bx, inv_w, gs->pitch);
				if (gs->y_inverted) {
					*(v++) = by * inv_h;
				} else {
					*(v++) = (gs->h - by) * inv_h;
				}
				gles_debug("texcoord (%f, %f)", *(v-2), *(v-1));
			}
			vtxcnt[count_vtx++] = n;
		}
	}

	if (use_band_compression)
		free(boxes);

	return count_vtx;
}

static GLenum gl_format_from_internal(GLenum internal_format)
{
	switch (internal_format) {
	case GL_R8_EXT:
		return GL_RED_EXT;
	case GL_RG8_EXT:
		return GL_RG_EXT;
	default:
		return internal_format;
	}
}

static void repaint_region(struct clv_view *view, struct clv_region *region,
			   struct clv_region *surf_region,
			   struct clv_pos *pos)
{
	struct clv_compositor *c = view->surface->c;
	struct gl_display *disp = get_display(c);
	GLfloat *v;
	u32 *vtxcnt;
	s32 i, first, nfans;

	nfans = texture_region(view, region, surf_region, pos);

	v = disp->vertices.data;
	vtxcnt = disp->vtxcnt.data;
	/* position: */
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(*v), &v[0]);
	glEnableVertexAttribArray(0);

	/* texcoord: */
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(*v), &v[2]);
	glEnableVertexAttribArray(1);

	for (i = 0, first = 0; i < nfans; i++) {
		gles_debug("draw TRIANGLE_FAN: %d, %d", first, vtxcnt[i]);
		glDrawArrays(GL_TRIANGLE_FAN, first, vtxcnt[i]);
		first += vtxcnt[i];
	}

	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(0);

	disp->vertices.size = 0;
	disp->vtxcnt.size = 0;
}

static void draw_view(struct clv_view *v, struct clv_output *output,
		      struct clv_region *damage)
{
	struct clv_compositor *c = v->surface->c;
	struct gl_display *disp = get_display(c);
	struct gl_surface_state *gs = get_surface_state(v->surface);
	struct clv_region surface_opaque, surface_blend;
	struct clv_region view_area, output_area;
	struct clv_box *boxes;
	GLint filter;
	s32 i, n;

	if (!gs->shader)
		return;

	gles_debug("damage area left:");
	boxes = clv_region_boxes(damage, &n);
	for (i = 0; i < n; i++) {
		gles_debug("(%u, %u) (%u, %u)", boxes[i].p1.x, boxes[i].p1.y,
			   boxes[i].p2.x, boxes[i].p2.y);
	}
	
	gles_debug("view_area %d,%d %ux%u", v->area.pos.x, v->area.pos.y,
			     v->area.w, v->area.h);
	clv_region_init_rect(&view_area, v->area.pos.x, v->area.pos.y,
			     v->area.w, v->area.h);
	clv_region_init_rect(&output_area, output->render_area.pos.x,
			     output->render_area.pos.y,
			     output->render_area.w,
			     output->render_area.h);
	gles_debug("output area: %d,%d %ux%u",
		   output->render_area.pos.x,
		   output->render_area.pos.y,
		   output->render_area.w,
		   output->render_area.h);
	clv_region_intersect(&view_area, &view_area, &output_area);

	if (!clv_region_is_not_empty(&view_area))
		goto out;

	clv_region_translate(&view_area, -output->render_area.pos.x,
			     -output->render_area.pos.y);
	clv_region_intersect(&view_area, &view_area, damage);
	gles_debug("view_area to repaint:");
	boxes = clv_region_boxes(&view_area, &n);
	for (i = 0; i < n; i++) {
		gles_debug("(%u, %u) (%u, %u)", boxes[i].p1.x, boxes[i].p1.y,
			   boxes[i].p2.x, boxes[i].p2.y);
	}
	clv_region_subtract(damage, damage, &view_area);

	gles_debug("damage area left:");
	boxes = clv_region_boxes(damage, &n);
	for (i = 0; i < n; i++) {
		gles_debug("(%u, %u) (%u, %u)", boxes[i].p1.x, boxes[i].p1.y,
			   boxes[i].p2.x, boxes[i].p2.y);
	}

	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	use_shader(disp, gs->shader);
	shader_uniforms(gs->shader, v, output);

	filter = GL_LINEAR; /* GL_NEAREST */
	for (i = 0; i < gs->count_textures; i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(gs->target, gs->textures[i]);
		glTexParameteri(gs->target, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(gs->target, GL_TEXTURE_MAG_FILTER, filter);
	}

	clv_region_init_rect(&surface_blend, 0, 0, v->surface->w,v->surface->h);
	clv_region_subtract(&surface_blend, &surface_blend,&v->surface->opaque);

	clv_region_init(&surface_opaque);
	clv_region_copy(&surface_opaque, &v->surface->opaque);

	if (clv_region_is_not_empty(&surface_opaque)) {
		if (gs->shader == &disp->texture_shader_rgba) {
			use_shader(disp, &disp->texture_shader_rgbx);
			shader_uniforms(&disp->texture_shader_rgbx, v, output);
		}
		if (v->alpha < 1.0f)
			glEnable(GL_BLEND);
		else
			glDisable(GL_BLEND);
		repaint_region(v, &view_area, &surface_opaque,
			       &output->render_area.pos);
	}

	if (clv_region_is_not_empty(&surface_blend)) {
		use_shader(disp, gs->shader);
		glEnable(GL_BLEND);
		repaint_region(v, &view_area, &surface_blend,
			       &output->render_area.pos);
	}

	clv_region_fini(&surface_blend);
	clv_region_fini(&surface_opaque);

out:
	clv_region_fini(&view_area);
	clv_region_fini(&output_area);
	v->painted = 1;
}

static void repaint_views(struct clv_output *output, struct clv_region *damage)
{
	struct clv_compositor *c = output->c;
	struct clv_view *view;
	//struct timespec t1, t2;

	//clock_gettime(c->clk_id, &t1);
	list_for_each_entry_reverse(view, &c->views, link) {
		gles_debug("view plane %p, primary_plane %p",
			   view->plane, &output->c->primary_plane);
		//if (view->type == CLV_VIEW_TYPE_PRIMARY) {
		if (view->plane == &output->c->primary_plane
		    && view->output_mask & (1 << output->index)) {
			draw_view(view, output, damage);
			view->need_to_draw = 0;
		}
	}
	//clock_gettime(c->clk_id, &t2);
	//printf("repaint views spent %lu\n", timespec_sub_to_msec(&t2, &t1));
}

static void gl_repaint_output(struct clv_output *output)
{
	struct gl_output_state *go = output->renderer_state;
	struct clv_compositor *c = output->c;
	struct gl_display *disp = get_display(c);
	struct clv_rect *area = &output->render_area;
	struct clv_region total_damage;
	EGLBoolean ret;
	static s32 errored = 0;
	s32 left, top, calc;
	u32 width, height;
	//struct timespec t1, t2;

	calc = output->current_mode->w * output->render_area.h
		/ output->render_area.w;
	if (calc <= output->current_mode->h) {
		left = 0;
		top = (output->current_mode->h - calc) / 2;
		width = output->current_mode->w;
		height = calc;
	} else {
		calc = output->render_area.w * output->current_mode->h
			/ output->render_area.h;
		left = (output->current_mode->w - calc) / 2;
		top = 0;
		width = calc;
		height = output->current_mode->h;
	}

	if (gl_switch_output(output) < 0)
		return;

	//glViewport(0, 0, area->w, area->h);
	//gles_debug("%d,%d %ux%u", 0, 0, area->w, area->h);
	if (output->changed) {
		if (left) {
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			glViewport(0, 0, left - 1, height);
			glViewport(left + width, 0, left - 1, height);
		}
		if (top) {
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			glViewport(left, 0, width, top - 1);
			glViewport(left, top + height, width, top - 1);
		}
		output->changed--;
	}
	glViewport(left, top, width, height);
	gles_debug("%d,%d %ux%u %ux%u", left, top, width, height,
		 output->current_mode->w, output->current_mode->h);

	clv_region_init_rect(&total_damage, 0, 0, area->w, area->h);
	repaint_views(output, &total_damage);
	clv_region_fini(&total_damage);
	/* TODO send frame signal */
	egl_debug("EGL Swap buffer.");
	//clock_gettime(c->clk_id, &t1);
	ret = eglSwapBuffers(disp->egl_display, go->egl_surface);
	//clock_gettime(c->clk_id, &t2);
	//printf("Swap spent %ld ms\n", timespec_sub_to_msec(&t2, &t1));
	if (ret == EGL_FALSE && !errored) {
		errored = 1;
		egl_err("Failed to call eglSwapBuffers.");
		egl_error_state();
	}
}

static void gl_flush_damage(struct clv_surface *surface)
{
	struct gl_display *disp = get_display(surface->c);
	struct gl_surface_state *gs = get_surface_state(surface);
	struct clv_buffer *buffer = gs->buffer;
	struct clv_box *boxes, *box;
	struct shm_buffer *shm_buffer;
	u8 *data;
	s32 i, j, count_boxes;

	clv_region_union(&gs->texture_damage, &gs->texture_damage,
			 &surface->damage);

	if (!buffer)
		return;

	if (!clv_region_is_not_empty(&gs->texture_damage)
		&& !gs->needs_full_upload)
		goto done;

	shm_buffer = container_of(buffer, struct shm_buffer, base);
	data = shm_buffer->shm.map;
	assert(data);

	if (!disp->support_unpack_subimage) {
		/* begin access buffer */
		for (j = 0; j < gs->count_textures; j++) {
			glBindTexture(GL_TEXTURE_2D, gs->textures[j]);
			glTexImage2D(GL_TEXTURE_2D, 0,
				     gs->gl_format[j],
				     gs->pitch / gs->hsub[j],
				     buffer->h / gs->vsub[j],
				     0,
				     gl_format_from_internal(gs->gl_format[j]),
				     gs->gl_pixel_type,
				     data + gs->offset[j]);
		}
		/* end access buffer */
		goto done;
	}

	if (gs->needs_full_upload) {
		glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
		glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);
		/* begin access buffer */
		for (j = 0; j < gs->count_textures; j++) {
			glBindTexture(GL_TEXTURE_2D, gs->textures[j]);
			gles_debug("ROW LENGTH %u", gs->pitch / gs->hsub[j]);
			glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT,
				      gs->pitch / gs->hsub[j]);
			gles_debug("glTexImage2D %u %u %u %p %u",
				   gs->pitch / gs->hsub[j],
				   buffer->h / gs->vsub[j],
				   0,
				   data, gs->offset[j]);

			glTexImage2D(GL_TEXTURE_2D, 0,
				     gs->gl_format[j],
				     gs->pitch / gs->hsub[j],
				     buffer->h / gs->vsub[j],
				     0,
				     gl_format_from_internal(gs->gl_format[j]),
				     gs->gl_pixel_type,
				     data + gs->offset[j]);
		}
		/* end access buffer */
		goto done;
	}

	boxes = clv_region_boxes(&gs->texture_damage, &count_boxes);
	/* begin access buffer */
	for (i = 0; i < count_boxes; i++) {
		box = &boxes[i];
		gles_debug("data[0]: 0x%02X data[1]: 0x%02X "
		       "data[2]: 0x%02X data[3]: 0x%02X\n",
		       data[0], data[1],
		       data[2], data[3]);
		gles_debug("count_textures = %d", gs->count_textures);
		for (j = 0; j < gs->count_textures; j++) {
			glBindTexture(GL_TEXTURE_2D, gs->textures[j]);
			gles_debug("ROW LENGTH %u", gs->pitch / gs->hsub[j]);
			glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT,
				      gs->pitch / gs->hsub[j]);
			gles_debug("Skip pixels %u",box->p1.x / gs->hsub[j]);
			glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT,
				      box->p1.x / gs->hsub[j]);
			gles_debug("Skip rows %u",box->p1.y / gs->vsub[j]);
			glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT,
				      box->p1.y / gs->vsub[j]);
			gles_debug("glTexSubImage2D %u %u %u %u %p %u",
				   box->p1.x / gs->hsub[j],
				   box->p1.y / gs->vsub[j],
				   (box->p2.x - box->p1.x) / gs->hsub[j],
				   (box->p2.y - box->p1.y) / gs->vsub[j],
				   data, gs->offset[j]);

			glTexSubImage2D(GL_TEXTURE_2D, 0,
					box->p1.x / gs->hsub[j],
					box->p1.y / gs->vsub[j],
					(box->p2.x - box->p1.x) / gs->hsub[j],
					(box->p2.y - box->p1.y) / gs->vsub[j],
					gl_format_from_internal(
						gs->gl_format[j]),
					gs->gl_pixel_type,
					data + gs->offset[j]);
		}
	}
	/* end access buffer */

done:
	clv_region_fini(&gs->texture_damage);
	clv_region_init(&gs->texture_damage);
	gs->needs_full_upload = 0;
}

static void dmabuf_destroy(struct dma_buffer *buffer)
{
	struct gl_display *disp = buffer->disp;

	if (!buffer)
		return;

	if (buffer->image != EGL_NO_IMAGE_KHR) {
		disp->destroy_image(disp->egl_display, buffer->image);
		buffer->image = EGL_NO_IMAGE_KHR;
	}
	list_del(&buffer->link);
	free(buffer);
}

static void gl_display_destroy(struct clv_compositor *c)
{
	struct gl_display *disp = get_display(c);
	struct dma_buffer *buffer, *t;

	eglMakeCurrent(disp->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
	list_for_each_entry_safe(buffer, t, &disp->dmabuf_images, link)
		dmabuf_destroy(buffer);
	if (disp->dummy_surface != EGL_NO_SURFACE)
		eglDestroySurface(disp->egl_display, disp->dummy_surface);
	eglTerminate(disp->egl_display);
	eglReleaseThread();
	clv_array_release(&disp->vertices);
	clv_array_release(&disp->vtxcnt);
	free(disp);
}

static s32 gl_output_state_create(struct clv_output *output,
				  EGLSurface surface)
{
	struct gl_output_state *go;

	go = calloc(1, sizeof(*go));
	if (!go)
		return -ENOMEM;

	go->egl_surface = surface;
	output->renderer_state = go;
	return 0;
}

static EGLSurface gl_create_output_surface(struct gl_display *disp,
					   void *legacy_win,
					   void *window,
					   s32 *formats,
					   s32 count_fmts,
					   s32 *vid)
{
	EGLSurface egl_surface;
	EGLConfig egl_config;

	if (egl_choose_config(disp, gl_opaque_attribs, formats, count_fmts,
			      &egl_config, vid) < 0) {
		egl_err("failed to choose EGL config for output");
		return EGL_NO_SURFACE;
	}

	if (egl_config != disp->egl_config) {
		egl_err("attempt to use different config for output.");
		return EGL_NO_SURFACE;
	}

	if (disp->create_platform_window && window) {
		egl_surface = disp->create_platform_window(disp->egl_display,
							   egl_config,
							   window,
							   NULL);
	} else if (legacy_win) {
		egl_surface = eglCreateWindowSurface(
					disp->egl_display,
					egl_config,
					(EGLNativeWindowType)legacy_win,
					NULL);
	} else {
		egl_err("create_platform_window = %p, window = %p, "
			"legacy_win = %lu", disp->create_platform_window,
			window, (u64)legacy_win);
		return EGL_NO_SURFACE;
	}

	return egl_surface;
}

static s32 gl_output_create(struct clv_output *output,
			    void *window_for_legacy,
			    void *window,
			    s32 *formats,
			    s32 count_fmts,
			    s32 *vid)
{
	struct clv_compositor *c = output->c;
	struct gl_display *disp = get_display(c);
	EGLSurface egl_surface;
	s32 ret;

	egl_surface = gl_create_output_surface(disp, window_for_legacy,
					       window, formats, count_fmts,
					       vid);
	if (egl_surface == EGL_NO_SURFACE) {
		egl_err("failed to create output surface.");
		return -1;
	}

	ret = gl_output_state_create(output, egl_surface);
	if (ret < 0)
		eglDestroySurface(disp->egl_display, egl_surface);

	return ret;
}

static void gl_output_destroy(struct clv_output *output)
{
	struct gl_display *disp = get_display(output->c);
	struct gl_output_state *go = output->renderer_state;

	eglMakeCurrent(disp->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
	eglDestroySurface(disp->egl_display, go->egl_surface);
	free(go);
}

static void gl_dmabuf_release(struct clv_compositor *c,
			      struct clv_buffer *buffer)
{
	struct gl_display *disp = get_display(c);
	struct dma_buffer *dma_buf = container_of(buffer, struct dma_buffer,
						  base);

	if (!dma_buf)
		return;

	if (dma_buf->image != EGL_NO_IMAGE_KHR) {
		printf("destroy egl image\n");
		disp->destroy_image(disp->egl_display, dma_buf->image);
		dma_buf->image = EGL_NO_IMAGE_KHR;
	}

	printf("close fd %d\n", buffer->fd);
	// TODO free GEM
	close(buffer->fd);

	list_del(&dma_buf->link);
	printf("free dma buffer\n");
	free(dma_buf);
}

static struct clv_buffer *gl_import_dmabuf(struct clv_compositor *c,
					   s32 fd, u32 w, u32 h,
					   u32 stride,
					   u32 vstride,
					   enum clv_pixel_fmt pixel_fmt,
					   u32 internal_fmt)
{
	struct gl_display *disp = get_display(c);
	struct dma_buffer *dma_buf = NULL;
	EGLint attribs[50] = {0};
	s32 attrib = 0;
	u32 w_align, h_align;

	printf("fd = %d stride = %u width = %u\n", fd, stride, w);
	if (!disp->support_dmabuf_import) {
		clv_err("cannot support dmabuf import feature.");
		return NULL;
	}

	if (pixel_fmt != CLV_PIXEL_FMT_ARGB8888
	    && pixel_fmt != CLV_PIXEL_FMT_ARGB8888
	    && pixel_fmt != CLV_PIXEL_FMT_NV12
	    && pixel_fmt != CLV_PIXEL_FMT_NV16) {
		clv_err("cannot support pixel fmt %u", pixel_fmt);
		return NULL;
	}

	dma_buf = calloc(1, sizeof(*dma_buf));
	if (!dma_buf)
		return NULL;

	dma_buf->base.type = CLV_BUF_TYPE_DMA;
	dma_buf->base.w = w;
	dma_buf->base.h = h;
	dma_buf->base.stride = stride;
	dma_buf->base.pixel_fmt = pixel_fmt;
	dma_buf->base.count_planes = 1;
	dma_buf->base.fd = fd;

	if (dma_buf->base.pixel_fmt == CLV_PIXEL_FMT_ARGB8888
	    || dma_buf->base.pixel_fmt == CLV_PIXEL_FMT_XRGB8888) {
		attribs[attrib++] = EGL_WIDTH;
		attribs[attrib++] = w;
		attribs[attrib++] = EGL_HEIGHT;
		attribs[attrib++] = h;
		attribs[attrib++] = EGL_LINUX_DRM_FOURCC_EXT;
		attribs[attrib++] = internal_fmt;
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_FD_EXT;
		attribs[attrib++] = fd;
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
		attribs[attrib++] = 0;
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
		attribs[attrib++] = stride;
		attribs[attrib++] = EGL_NONE;
		printf("w = %u h = %u fd = %d pixel_fmt = %u stride = %u\n",
			w, h, fd, pixel_fmt, stride);
	} else if (dma_buf->base.pixel_fmt == CLV_PIXEL_FMT_NV12) {
		if (vstride) {
			w_align = stride;
			h_align = vstride;
		} else {
			w_align = (w + 16 - 1) & ~(16 - 1);
			h_align = (h + 16 - 1) & ~(16 - 1);
		}
		attribs[attrib++] = EGL_WIDTH;
		attribs[attrib++] = w;
		attribs[attrib++] = EGL_HEIGHT;
		attribs[attrib++] = h;
		attribs[attrib++] = EGL_LINUX_DRM_FOURCC_EXT;
		attribs[attrib++] = internal_fmt;
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_FD_EXT;
		attribs[attrib++] = fd;
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
		attribs[attrib++] = 0;
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
		attribs[attrib++] = w_align;
		attribs[attrib++] = EGL_DMA_BUF_PLANE1_FD_EXT;
		attribs[attrib++] = fd;
		attribs[attrib++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
		attribs[attrib++] = w_align * h_align;
		attribs[attrib++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
		attribs[attrib++] = w_align;
		attribs[attrib++] = EGL_YUV_COLOR_SPACE_HINT_EXT;
		attribs[attrib++] = EGL_ITU_REC709_EXT;
		attribs[attrib++] = EGL_SAMPLE_RANGE_HINT_EXT;
		attribs[attrib++] = EGL_YUV_FULL_RANGE_EXT;
		attribs[attrib++] = EGL_NONE;
		printf("fourcc = %u !!!!!!!!!!\n", internal_fmt);
		printf("w = %u h = %u fd = %d pixel_fmt = %u stride = %u\n",
			w, h, fd, pixel_fmt, w_align);
	} else if (dma_buf->base.pixel_fmt == CLV_PIXEL_FMT_NV16) {
		if (vstride) {
			w_align = stride;
			h_align = vstride;
		} else {
			w_align = (w + 16 - 1) & ~(16 - 1);
			h_align = (h + 16 - 1) & ~(16 - 1);
		}
		attribs[attrib++] = EGL_WIDTH;
		attribs[attrib++] = w;
		attribs[attrib++] = EGL_HEIGHT;
		attribs[attrib++] = h;
		attribs[attrib++] = EGL_LINUX_DRM_FOURCC_EXT;
		attribs[attrib++] = internal_fmt;
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_FD_EXT;
		attribs[attrib++] = fd;
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
		attribs[attrib++] = 0;
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
		attribs[attrib++] = w_align;
		attribs[attrib++] = EGL_DMA_BUF_PLANE1_FD_EXT;
		attribs[attrib++] = fd;
		attribs[attrib++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
		attribs[attrib++] = w_align * h_align;
		attribs[attrib++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
		attribs[attrib++] = w_align;
		attribs[attrib++] = EGL_YUV_COLOR_SPACE_HINT_EXT;
		attribs[attrib++] = EGL_ITU_REC709_EXT;
		attribs[attrib++] = EGL_SAMPLE_RANGE_HINT_EXT;
		attribs[attrib++] = EGL_YUV_FULL_RANGE_EXT;
		attribs[attrib++] = EGL_NONE;
		printf("fourcc = %u !!!!!!!!!!\n", internal_fmt);
		printf("w = %u h = %u fd = %d pixel_fmt = %u stride = %u\n",
			w, h, fd, pixel_fmt, w_align);
	}

	dma_buf->image = disp->create_image(disp->egl_display,
					    EGL_NO_CONTEXT,
					    EGL_LINUX_DMA_BUF_EXT,
					    NULL, attribs);
	if (dma_buf->image == EGL_NO_IMAGE_KHR) {
		gles_err("cannot create EGL image by DMABUF");
		egl_error_state();
		free(dma_buf);
		return NULL;
	}

	dma_buf->disp = disp;

	list_add_tail(&dma_buf->link, &disp->dmabuf_images);

	return &dma_buf->base;
}

s32 gl_renderer_create(struct clv_compositor *c, s32 *formats, s32 count_fmts,
		       s32 no_winsys, void *native_window, s32 *vid)
{
	struct gl_display *disp;
	EGLint major, minor;

	gles_dbg = 15;
	egl_dbg = 15;
	disp = calloc(1, sizeof(*disp));
	if (!disp)
		return -ENOMEM;

	disp->egl_display = EGL_NO_DISPLAY;

	if (no_winsys) {
		if (!get_platform_display) {
			get_platform_display = (void *)eglGetProcAddress(
				"eglGetPlatformDisplayEXT");
		}
		if (!get_platform_display)
			goto err1;
		disp->egl_display = get_platform_display(EGL_PLATFORM_GBM_KHR,
							 native_window,
							 NULL);
	} else {
		disp->egl_display = eglGetDisplay(native_window);
	}

	if (disp->egl_display == EGL_NO_DISPLAY) {
		egl_err("failed to create EGL display.");
		goto err1;
	}

	if (!eglInitialize(disp->egl_display, &major, &minor)) {
		egl_err("failed to initialize EGL.");
		goto err3;
	}

	print_egl_info(disp->egl_display);

	if (egl_choose_config(disp, gl_opaque_attribs, formats, count_fmts,
			      &disp->egl_config, vid) < 0) {
		egl_err("failed to choose EGL config");
		goto err2;
	}

	if (set_egl_extensions(disp) < 0) {
		egl_err("failed to set EGL extensions.");
		goto err3;
	}

	if (disp->support_surfaceless_context) {
		disp->dummy_surface = EGL_NO_SURFACE;
	} else {
		egl_info("EGL_KHR_surfaceless_context unavailable. "
			 "Tring PbufferSurface");
		if (create_pbuffer_surface(disp) < 0)
			goto err3;
	}

	c->renderer = &disp->base;
	if (gl_setup(c, disp->dummy_surface) < 0)
		goto err3;

	clv_array_init(&disp->vertices);
	clv_array_init(&disp->vtxcnt);

	clv_signal_init(&disp->destroy_signal);

	INIT_LIST_HEAD(&disp->dmabuf_images);

	disp->base.repaint_output = gl_repaint_output;
	disp->base.flush_damage = gl_flush_damage;
	disp->base.attach_buffer = gl_attach_buffer;
	disp->base.output_create = gl_output_create;
	disp->base.output_destroy = gl_output_destroy;
	disp->base.destroy = gl_display_destroy;
	disp->base.import_dmabuf = gl_import_dmabuf;
	disp->base.release_dmabuf = gl_dmabuf_release;

	gles_dbg = 0;
	egl_dbg = 0;

	return 0;

err3:
	egl_error_state();
err2:
	eglTerminate(disp->egl_display);
err1:
	free(disp);
	return -1;
}


#endif
