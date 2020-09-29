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
#include <drm_fourcc.h>
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_array.h>
#include <cube_region.h>
#include <cube_shm.h>
#include <cube_signal.h>
#include <cube_compositor.h>
#include <cube_renderer.h>

static enum cb_log_level gles_dbg = CB_LOG_NOTICE;
static enum cb_log_level egl_dbg = CB_LOG_NOTICE;

#define LAYOUT_CHG_CNT 4

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

	bool support_unpack_subimage;
	bool support_context_priority;
	bool support_surfaceless_context;
	bool support_texture_rg;
	bool support_dmabuf_import;

	struct list_head dmabuf_images;

	struct gl_shader texture_shader_rgba;
	struct gl_shader texture_shader_egl_external;
	struct gl_shader texture_shader_rgbx;
	struct gl_shader texture_shader_y_u_v;
	struct gl_shader texture_shader_y_uv;
	struct gl_shader texture_shader_y_xuxv;
	struct gl_shader *current_shader;

	struct cb_signal destroy_signal;
};

struct gl_dma_buffer {
	struct cb_buffer base;
	EGLImageKHR image;
	struct list_head link;
	struct gl_renderer *r;
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
	"  gl_FragColor.r = y + 1.59602678 * v;\n"			\
	"  gl_FragColor.g = y - 0.39176229 * u - 0.81296764 * v;\n"	\
	"  gl_FragColor.b = y + 2.01723214 * u;\n"			\
	"  gl_FragColor.a = alpha;\n"

static const char texture_fragment_shader_y_u_v[] =
	"precision mediump float;\n"
	"uniform sampler2D tex;\n"
	"uniform sampler2D tex1;\n"
	"uniform sampler2D tex2;\n"
	"varying vec2 v_texcoord;\n"
	"uniform float alpha;\n"
	"void main() {\n"
	"  float y = 1.16438356 * (texture2D(tex, v_texcoord).x - 0.0625);\n"
	"  float u = texture2D(tex1, v_texcoord).x - 0.5;\n"
	"  float v = texture2D(tex2, v_texcoord).x - 0.5;\n"
	FRAGMENT_CONVERT_YUV
	;

static const char texture_fragment_shader_y_uv[] =
	"precision mediump float;\n"
	"uniform sampler2D tex;\n"
	"uniform sampler2D tex1;\n"
	"varying vec2 v_texcoord;\n"
	"uniform float alpha;\n"
	"void main() {\n"
	"  float y = 1.16438356 * (texture2D(tex, v_texcoord).x - 0.0625);\n"
	"  float u = texture2D(tex1, v_texcoord).r - 0.5;\n"
	"  float v = texture2D(tex1, v_texcoord).g - 0.5;\n"
	FRAGMENT_CONVERT_YUV
	;

static const char texture_fragment_shader_y_xuxv[] =
	"precision mediump float;\n"
	"uniform sampler2D tex;\n"
	"uniform sampler2D tex1;\n"
	"varying vec2 v_texcoord;\n"
	"uniform float alpha;\n"
	"void main() {\n"
	"  float y = 1.16438356 * (texture2D(tex, v_texcoord).x - 0.0625);\n"
	"  float u = texture2D(tex1, v_texcoord).g - 0.5;\n"
	"  float v = texture2D(tex1, v_texcoord).a - 0.5;\n"
	FRAGMENT_CONVERT_YUV
	;

static const char fragment_brace[] =
	"}\n";

static const EGLint gl_opaque_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_ALPHA_SIZE, 0,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_NONE,
};

struct gl_surface_state {
	GLfloat color[4];
	struct gl_shader *shader;

	GLuint textures[3];
	s32 count_textures;
	bool needs_full_upload;
	struct cb_region texture_damage;

	GLenum gl_format[3];
	GLenum gl_pixel_type;

	GLenum target;

	s32 pitch;
	s32 h;
	bool y_inverted;

	s32 offset[3];
	s32 hsub[3];
	s32 vsub[3];

	struct cb_surface *surface;

	enum cb_buffer_type buf_type;

	struct cb_buffer *buffer;

	struct cb_listener renderer_destroy_listener;
	struct cb_listener surface_destroy_listener;
};

struct gl_output_state {
	struct r_output base;
	s32 pipe;
	EGLSurface egl_surface;
	struct gl_renderer *r;
	struct cb_rect render_area;
	u32 disp_w, disp_h;
	/* a count to set all surface buffer with new view port */
	s32 layout_changed;
};

static inline struct gl_renderer *to_glr(struct renderer *renderer)
{
	return container_of(renderer, struct gl_renderer, base);
}

static inline struct gl_output_state *to_glo(struct r_output *o)
{
	return container_of(o, struct gl_output_state, base);
}

static void dmabuf_destroy(struct gl_dma_buffer *buffer)
{
	struct gl_renderer *r = buffer->r;

	if (!buffer)
		return;

	if (buffer->image != EGL_NO_IMAGE_KHR) {
		r->destroy_image(r->egl_display, buffer->image);
		buffer->image = EGL_NO_IMAGE_KHR;
	}
	list_del(&buffer->link);
	free(buffer);
}

static void gl_renderer_destroy(struct renderer *renderer)
{
	struct gl_renderer *r = to_glr(renderer);
	struct gl_dma_buffer *buffer, *next;

	if (!renderer)
		return;

	eglMakeCurrent(r->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
	list_for_each_entry_safe(buffer, next, &r->dmabuf_images, link)
		dmabuf_destroy(buffer);

	if (r->dummy_surface != EGL_NO_SURFACE)
		eglDestroySurface(r->egl_display, r->dummy_surface);
	
	eglTerminate(r->egl_display);
	eglReleaseThread();
	cb_array_release(&r->vertices);
	cb_array_release(&r->vtxcnt);
	free(r);
}

static u32 cb_pix_fmt_to_fourcc(enum cb_pix_fmt fmt)
{
	u32 fourcc;

	switch (fmt) {
	case CB_PIX_FMT_ARGB8888:
		fourcc = DRM_FORMAT_ARGB8888;
		break;
	case CB_PIX_FMT_XRGB8888:
		fourcc = DRM_FORMAT_XRGB8888;
		break;
	case CB_PIX_FMT_RGB888:
		fourcc = DRM_FORMAT_RGB888;
		break;
	case CB_PIX_FMT_RGB565:
		fourcc = DRM_FORMAT_RGB565;
		break;
	case CB_PIX_FMT_NV12:
		fourcc = DRM_FORMAT_NV12;
		break;
	case CB_PIX_FMT_NV16:
		fourcc = DRM_FORMAT_NV16;
		break;
	case CB_PIX_FMT_NV24:
		fourcc = DRM_FORMAT_NV24;
		break;
	case CB_PIX_FMT_YUYV:
		fourcc = DRM_FORMAT_YUYV;
		break;
	case CB_PIX_FMT_YUV420:
		fourcc = DRM_FORMAT_YUV420;
		break;
	case CB_PIX_FMT_YUV422:
		fourcc = DRM_FORMAT_YUV422;
		break;
	case CB_PIX_FMT_YUV444:
		fourcc = DRM_FORMAT_YUV444;
		break;
	default:
		fourcc = 0;
	}

	return fourcc;
}

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

static void egl_error_state(void)
{
	EGLint err;

	err = eglGetError();
	egl_err("EGL err: %s (0x%04lX)", egl_strerror(err), (u64)err);
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

static s32 egl_choose_config(struct gl_renderer *r, const EGLint *attribs,
			     const EGLint *visual_ids, const s32 count_ids,
			     EGLConfig *config_matched, EGLint *vid)
{
	EGLint count_configs = 0;
	EGLint count_matched = 0;
	EGLConfig *configs;
	s32 i, config_index = -1;

	if (!eglGetConfigs(r->egl_display, NULL, 0, &count_configs)) {
		egl_err("Cannot get EGL configs.");
		return -1;
	}
	egl_debug("count_configs = %d", count_configs);

	configs = calloc(count_configs, sizeof(*configs));
	if (!configs)
		return -ENOMEM;

	if (!eglChooseConfig(r->egl_display, attribs, configs,
			     count_configs, &count_matched)
	    || !count_matched) {
		egl_err("cannot select appropriate configs.");
		goto out;
	}
	egl_debug("count_matched = %d", count_matched);

	if (!visual_ids || count_ids == 0)
		config_index = 0;

	for (i = 0; config_index == -1 && i < count_ids; i++) {
		config_index = match_config_to_visual(r->egl_display,
						      visual_ids[i],
						      configs,
						      count_matched);
		egl_debug("config_index = %d i = %d count_ids = %d",
			  config_index, i, count_ids);
	}

	if (config_index != -1)
		*config_matched = configs[config_index];

out:
	if (visual_ids) {
		*vid = visual_ids[i - 1];
	} else {
		for (i = 0; i < count_matched; i++) {
			if (!eglGetConfigAttrib(r->egl_display, configs[0],
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

static void set_egl_client_extensions(struct gl_renderer *r)
{
	const char *extensions;
	
	extensions = (const char *)eglQueryString(EGL_NO_DISPLAY,
						  EGL_EXTENSIONS);
	if (!extensions) {
		egl_info("cannot query client EGL_EXTENSIONS");
		return;
	}

	if (check_egl_extension(extensions, "EGL_EXT_platform_base")) {
		r->create_platform_window = (void *)eglGetProcAddress(
				"eglCreatePlatformWindowSurfaceEXT");
		if (!r->create_platform_window)
			egl_warn("failed to call "
				 "eglCreatePlatformWindowSurfaceEXT");
	} else {
		egl_warn("EGL_EXT_platform_base not supported.");
	}
}

static s32 set_egl_extensions(struct gl_renderer *r)
{
	const char *extensions;

	r->create_image = (void *)eglGetProcAddress("eglCreateImageKHR");
	r->destroy_image = (void *)eglGetProcAddress("eglDestroyImageKHR");
	extensions = (const char *)eglQueryString(r->egl_display,
						  EGL_EXTENSIONS);
	if (!extensions) {
		egl_err("cannot query EGL_EXTENSIONS");
		return -1;
	}

	if (check_egl_extension(extensions, "EGL_IMG_context_priority"))
		r->support_context_priority = true;

	if (check_egl_extension(extensions, "EGL_KHR_surfaceless_context"))
		r->support_surfaceless_context = true;

	if (check_egl_extension(extensions, "EGL_EXT_image_dma_buf_import"))
		r->support_dmabuf_import = true;

	set_egl_client_extensions(r);
	egl_info("EGL_IMG_context_priority: %s",
		 r->support_context_priority ? "Y" : "N");
	egl_info("EGL_KHR_surfaceless_context: %s",
		 r->support_surfaceless_context ? "Y" : "N");
	egl_info("EGL_EXT_image_dma_buf_import: %s",
		 r->support_dmabuf_import ? "Y" : "N");
	return 0;
}

static s32 create_pbuffer_surface(struct gl_renderer *r)
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

	if (egl_choose_config(r, pbuffer_config_attribs, NULL, 0,
			      &pbuffer_config, &vid) < 0) {
		egl_err("failed to choose EGL config for PbufferSurface");
		return -1;
	}

	r->dummy_surface = eglCreatePbufferSurface(r->egl_display,
						   pbuffer_config,
						   pbuffer_attribs);

	if (r->dummy_surface == EGL_NO_SURFACE) {
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

static void set_shaders(struct gl_renderer *r)
{
	r->texture_shader_rgba.vertex_source = vertex_shader;
	r->texture_shader_rgba.fragment_source = texture_fragment_shader_rgba;

	r->texture_shader_egl_external.vertex_source = vertex_shader;
	r->texture_shader_egl_external.fragment_source =
		texture_fragment_shader_egl_external;

	r->texture_shader_rgbx.vertex_source = vertex_shader;
	r->texture_shader_rgbx.fragment_source = texture_fragment_shader_rgbx;

	r->texture_shader_y_u_v.vertex_source = vertex_shader;
	r->texture_shader_y_u_v.fragment_source =
						texture_fragment_shader_y_u_v;

	r->texture_shader_y_uv.vertex_source = vertex_shader;
	r->texture_shader_y_uv.fragment_source = texture_fragment_shader_y_uv;

	r->texture_shader_y_xuxv.vertex_source = vertex_shader;
	r->texture_shader_y_xuxv.fragment_source =
						texture_fragment_shader_y_xuxv;
}

static s32 gl_setup(struct gl_renderer *r, EGLSurface egl_surface)
{
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

	if (r->support_context_priority) {
		context_attribs[count_attrs++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
		context_attribs[count_attrs++] = EGL_CONTEXT_PRIORITY_HIGH_IMG;
	}

	assert(count_attrs < ARRAY_SIZE(context_attribs));
	context_attribs[count_attrs] = EGL_NONE;

	context_config = r->egl_config;

	context_attribs[1] = 3;
	r->egl_context = eglCreateContext(r->egl_display, context_config,
					  EGL_NO_CONTEXT, context_attribs);
	if (r->egl_context == NULL) {
		context_attribs[1] = 2;
		r->egl_context = eglCreateContext(r->egl_display,
						  context_config,
						  EGL_NO_CONTEXT,
						  context_attribs);
		if (r->egl_context == EGL_NO_CONTEXT) {
			egl_err("failed to create context");
			egl_error_state();
			return -1;
		}
		egl_info("Create OpenGLES2 context");
	} else {
		egl_info("Create OpenGLES3 context");
	}

	if (r->support_context_priority) {
		eglQueryContext(r->egl_display, r->egl_context,
				EGL_CONTEXT_PRIORITY_LEVEL_IMG, &value);

		if (value != EGL_CONTEXT_PRIORITY_HIGH_IMG) {
			egl_err("Failed to obtain a high priority context.");
		}
	}

	ret = eglMakeCurrent(r->egl_display, egl_surface,
			     egl_surface, r->egl_context);
	if (ret == EGL_FALSE) {
		egl_err("Failed to make EGL context current.");
		egl_error_state();
		return -1;
	}

	r->gl_version = get_gl_version();
	if (r->gl_version == GEN_GL_VERSION_INVALID) {
		gles_warn("failed to detect GLES version, "
			  "defaulting to 2.0.");
		r->gl_version = GEN_GL_VERSION(2, 0);
	}

	gl_info();
	r->image_target_texture_2d =
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

	if (r->gl_version >= GEN_GL_VERSION(3, 0)
	     || check_egl_extension(extensions, "GL_EXT_unpack_subimage"))
		r->support_unpack_subimage = true;
	
	if (r->gl_version >= GEN_GL_VERSION(3, 0)
	     || check_egl_extension(extensions, "GL_EXT_texture_rg"))
		r->support_texture_rg = true;

	glActiveTexture(GL_TEXTURE0);

	set_shaders(r);

	gles_info("GL_EXT_texture_rg: %s", r->support_texture_rg ? "Y" : "N");
	gles_info("GL_EXT_unpack_subimage: %s",
		  r->support_unpack_subimage ? "Y" : "N");
	return 0;
}

static struct cb_buffer *gl_import_dmabuf(struct renderer *renderer,
					  struct cb_buffer_info *info)
{
	struct gl_renderer *r = to_glr(renderer);
	struct gl_dma_buffer *dma_buf = NULL;
	EGLint attribs[50] = {0};
	s32 attrib = 0;

	if (!r->support_dmabuf_import) {
		egl_err("cannot support dmabuf import feature.");
		return NULL;
	}

	if (info->pix_fmt != CB_PIX_FMT_ARGB8888
	    && info->pix_fmt != CB_PIX_FMT_ARGB8888
	    && info->pix_fmt != CB_PIX_FMT_NV12
	    && info->pix_fmt != CB_PIX_FMT_NV16) {
		egl_err("cannot support pixel fmt %u", info->pix_fmt);
		return NULL;
	}

	dma_buf = calloc(1, sizeof(*dma_buf));
	if (!dma_buf)
		return NULL;

	dma_buf->base.info = *info;

	if (info->pix_fmt == CB_PIX_FMT_ARGB8888
	    || info->pix_fmt == CB_PIX_FMT_XRGB8888) {
		attribs[attrib++] = EGL_WIDTH;
		attribs[attrib++] = info->width;
		attribs[attrib++] = EGL_HEIGHT;
		attribs[attrib++] = info->height;
		attribs[attrib++] = EGL_LINUX_DRM_FOURCC_EXT;
		attribs[attrib++] = cb_pix_fmt_to_fourcc(info->pix_fmt);
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_FD_EXT;
		attribs[attrib++] = info->fd[0];
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
		attribs[attrib++] = info->offsets[0];
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
		attribs[attrib++] = info->strides[0] / 4;
		attribs[attrib++] = EGL_NONE;
		egl_info("w = %u h = %u fd = %d pixel_fmt = %u stride = %u\n",
			 info->width, info->height, info->fd[0],
			 info->pix_fmt, info->strides[0]);
	} else if (info->pix_fmt == CB_PIX_FMT_NV12 ||
		   info->pix_fmt == CB_PIX_FMT_NV16) {
		attribs[attrib++] = EGL_WIDTH;
		attribs[attrib++] = info->width;
		attribs[attrib++] = EGL_HEIGHT;
		attribs[attrib++] = info->height;
		attribs[attrib++] = EGL_LINUX_DRM_FOURCC_EXT;
		attribs[attrib++] = cb_pix_fmt_to_fourcc(info->pix_fmt);
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_FD_EXT;
		attribs[attrib++] = info->fd[0];
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
		attribs[attrib++] = info->offsets[0];
		attribs[attrib++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
		attribs[attrib++] = info->strides[0];
		attribs[attrib++] = EGL_DMA_BUF_PLANE1_FD_EXT;
		attribs[attrib++] = info->fd[0];
		attribs[attrib++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
		attribs[attrib++] = info->offsets[1];
		attribs[attrib++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
		attribs[attrib++] = info->strides[1];
		attribs[attrib++] = EGL_YUV_COLOR_SPACE_HINT_EXT;
		attribs[attrib++] = EGL_ITU_REC709_EXT;
		attribs[attrib++] = EGL_SAMPLE_RANGE_HINT_EXT;
		attribs[attrib++] = EGL_YUV_FULL_RANGE_EXT;
		attribs[attrib++] = EGL_NONE;
		egl_info("w = %u h = %u fd = %d pixel_fmt = %u stride = %u\n",
			 info->width, info->height, info->fd[0],
			 info->pix_fmt, info->strides[0]);
	}

	dma_buf->image = r->create_image(r->egl_display,
					 EGL_NO_CONTEXT,
					 EGL_LINUX_DMA_BUF_EXT,
					 NULL, attribs);
	if (dma_buf->image == EGL_NO_IMAGE_KHR) {
		gles_err("cannot create EGL image by DMABUF");
		egl_error_state();
		free(dma_buf);
		return NULL;
	}

	dma_buf->r = r;

	list_add_tail(&dma_buf->link, &r->dmabuf_images);

	dma_buf->base.info.type = CB_BUF_TYPE_DMA;

	return &dma_buf->base;
}

static void gl_release_dmabuf(struct renderer *renderer,
			      struct cb_buffer *buffer)
{
	struct gl_renderer *r = to_glr(renderer);
	struct gl_dma_buffer *dma_buf = container_of(buffer,
						     struct gl_dma_buffer,
						     base);

	if (!dma_buf)
		return;

	if (dma_buf->image != EGL_NO_IMAGE_KHR) {
		egl_info("destroy egl image");
		r->destroy_image(r->egl_display, dma_buf->image);
		dma_buf->image = EGL_NO_IMAGE_KHR;
	}

	egl_info("close fd %d", buffer->info.fd[0]);
	// TODO free GEM
	close(buffer->info.fd[0]);
	list_del(&dma_buf->link);
	free(dma_buf);
}

static void gl_output_layout_changed(struct r_output *output,
				     struct cb_rect *render_area,
				     u32 disp_w, u32 disp_h)
{
	struct gl_output_state *go = to_glo(output);

	go->layout_changed = LAYOUT_CHG_CNT;
	go->disp_w = disp_w;
	go->disp_h = disp_h;
	memcpy(&go->render_area, render_area, sizeof(*render_area));
}

/* switch surface */
static s32 gl_switch_output(struct gl_output_state *go)
{
	struct gl_renderer *r = go->r;
	static s32 errored = 0;

	if (eglMakeCurrent(r->egl_display, go->egl_surface, go->egl_surface,
			   r->egl_context) == EGL_FALSE) {
		if (errored) {
			egl_err("Switch egl surface (eglMakeCurrent) failed.");
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

static s32 load_shader(struct gl_shader *shader, struct gl_renderer *r,
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

static void use_shader(struct gl_renderer *r, struct gl_shader *shader)
{
	s32 ret;

	if (!shader->program) {
		ret = load_shader(shader, r,
				  shader->vertex_source,
				  shader->fragment_source);
		if (ret < 0)
			gles_err("failed to compile shader");
	}

	if (r->current_shader == shader)
		return;

	glUseProgram(shader->program);
	r->current_shader = shader;
}

static void gl_surface_state_destroy(struct gl_surface_state *gs)
{
	if (gs) {
		if (gs->surface)
			gs->surface->renderer_state = NULL;
		glDeleteTextures(gs->count_textures, gs->textures);
		cb_region_fini(&gs->texture_damage);
		list_del(&gs->renderer_destroy_listener.link);
		INIT_LIST_HEAD(&gs->renderer_destroy_listener.link);
		list_del(&gs->surface_destroy_listener.link);
		INIT_LIST_HEAD(&gs->surface_destroy_listener.link);
		free(gs);
	}
}

static void surface_state_handle_surface_destroy(struct cb_listener *listener,
						 void *data)
{
	struct gl_surface_state *gs = container_of(listener,
						   struct gl_surface_state,
						   surface_destroy_listener);
	gl_surface_state_destroy(gs);
}

static void surface_state_handle_renderer_destroy(struct cb_listener *listener,
						  void *data)
{
	struct gl_surface_state *gs = container_of(listener,
						   struct gl_surface_state,
						   renderer_destroy_listener);
	gl_surface_state_destroy(gs);
}

static s32 gl_surface_state_create(struct gl_renderer *r,
				   struct cb_surface *surface)
{
	struct gl_surface_state *gs;

	gs = calloc(1, sizeof(*gs));
	if (!gs)
		return -ENOMEM;

	gs->pitch = 1;
	gs->y_inverted = true;
	gs->surface = surface;
	cb_region_init(&gs->texture_damage);

	gs->surface_destroy_listener.notify =
		surface_state_handle_surface_destroy;
	cb_signal_add(&surface->destroy_signal, &gs->surface_destroy_listener);

	gs->renderer_destroy_listener.notify =
		surface_state_handle_renderer_destroy;
	cb_signal_add(&r->destroy_signal, &gs->renderer_destroy_listener);

	surface->renderer_state = gs;
	return 0;
}

static struct gl_surface_state *get_surface_state(struct gl_renderer *r,
						  struct cb_surface *surface)
{
	if (!surface->renderer_state)
		gl_surface_state_create(r, surface);

	return (struct gl_surface_state *)surface->renderer_state;
}

static void shader_uniforms(struct gl_shader *shader, struct cb_view *v,
			    struct gl_output_state *go)
{
	s32 i;
	struct gl_renderer *r = go->r;
	struct gl_surface_state *gs = get_surface_state(r, v->surface);
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
	
	projmat_yinvert[0] /= go->render_area.w;
	projmat_yinvert[5] /= go->render_area.h;

	projmat_normal[0] /= go->render_area.w;
	projmat_normal[5] /= go->render_area.h;

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

static s32 merge_down(struct cb_box *a, struct cb_box *b, struct cb_box *merge)
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

static s32 compress_bands(struct cb_box *inboxes, s32 count_in,
			  struct cb_box **outboxes)
{
	s32 merged = 0;
	struct cb_box *out, merge_rect;
	s32 i, j, count_out;

	if (!count_in) {
		*outboxes = NULL;
		return 0;
	}

	out = calloc(count_in, sizeof(struct cb_box));
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

static s32 calculate_edges(struct cb_view *v, struct cb_box *box,
			   struct cb_box *surf_box, GLfloat *ex, GLfloat *ey,
			   struct cb_pos *output_base)
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

static s32 texture_region(struct gl_renderer *r,
			  struct cb_view *view,
			  struct cb_region *region,
			  struct cb_region *surf_region,
			  struct cb_pos *output_base)
{
	struct gl_surface_state *gs = get_surface_state(r, view->surface);
	s32 count_boxes, count_surf_boxes, count_raw_boxes, i, j, k;
	s32 n;
	bool use_band_compression;
	struct cb_box *raw_boxes, *boxes, *surf_boxes, *box, *surf_box;
	u32 count_vtx = 0, *vtxcnt;
	GLfloat *v, inv_w, inv_h;
	GLfloat ex[8], ey[8];
	GLfloat bx, by;

	raw_boxes = cb_region_boxes(region, &count_raw_boxes);
	surf_boxes = cb_region_boxes(surf_region, &count_surf_boxes);

	if (count_raw_boxes < 4) {
		use_band_compression = false;
		count_boxes = count_raw_boxes;
		boxes = raw_boxes;
	} else {
		use_band_compression = true;
		count_boxes = compress_bands(raw_boxes, count_raw_boxes,&boxes);
	}

	v = cb_array_add(&r->vertices,
			 count_boxes * count_surf_boxes * 8 * 4 * sizeof(*v));
	vtxcnt = cb_array_add(&r->vtxcnt,
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

static void repaint_region(struct gl_renderer *r,
			   struct cb_view *view,
			   struct cb_region *region,
			   struct cb_region *surf_region,
			   struct cb_pos *pos)
{
	GLfloat *v;
	u32 *vtxcnt;
	s32 i, first, nfans;

	nfans = texture_region(r, view, region, surf_region, pos);

	v = r->vertices.data;
	vtxcnt = r->vtxcnt.data;
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

	r->vertices.size = 0;
	r->vtxcnt.size = 0;
}

static bool draw_view(struct cb_view *v, struct gl_output_state *go,
		      struct cb_region *damage)
{
	struct gl_renderer *r = go->r;
	struct gl_surface_state *gs = get_surface_state(r, v->surface);
	struct cb_region surface_opaque, surface_blend;
	struct cb_region view_area, output_area;
	/*
	struct cb_box *boxes;
	*/
	GLint filter;
	s32 i;
	/*
	s32 n;
	*/
	bool repainted = false;

	if (!gs->shader)
		return false;

	/*
	gles_debug("damage area left:");
	boxes = cb_region_boxes(damage, &n);
	for (i = 0; i < n; i++) {
		gles_debug("(%u, %u) (%u, %u)", boxes[i].p1.x, boxes[i].p1.y,
			   boxes[i].p2.x, boxes[i].p2.y);
	}
	*/
	
	gles_debug("view_area %d,%d %ux%u", v->area.pos.x, v->area.pos.y,
		   v->area.w, v->area.h);
	cb_region_init_rect(&view_area, v->area.pos.x, v->area.pos.y,
			    v->area.w, v->area.h);
	cb_region_init_rect(&output_area, go->render_area.pos.x,
			    go->render_area.pos.y,
			    go->render_area.w,
			    go->render_area.h);
	gles_debug("output area: %d,%d %ux%u",
		   go->render_area.pos.x,
		   go->render_area.pos.y,
		   go->render_area.w,
		   go->render_area.h);
	cb_region_intersect(&view_area, &view_area, &output_area);

	if (!cb_region_is_not_empty(&view_area))
		goto out;

	cb_region_translate(&view_area, -go->render_area.pos.x,
			    -go->render_area.pos.y);
	cb_region_intersect(&view_area, &view_area, damage);
	/*
	gles_debug("view_area to repaint:");
	boxes = cb_region_boxes(&view_area, &n);
	for (i = 0; i < n; i++) {
		gles_debug("(%u, %u) (%u, %u)", boxes[i].p1.x, boxes[i].p1.y,
			   boxes[i].p2.x, boxes[i].p2.y);
	}
	*/
	cb_region_subtract(damage, damage, &view_area);

	/*
	gles_debug("damage area left:");
	boxes = cb_region_boxes(damage, &n);
	for (i = 0; i < n; i++) {
		gles_debug("(%u, %u) (%u, %u)", boxes[i].p1.x, boxes[i].p1.y,
			   boxes[i].p2.x, boxes[i].p2.y);
	}
	*/

	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	use_shader(r, gs->shader);
	shader_uniforms(gs->shader, v, go);

	filter = GL_LINEAR; /* GL_NEAREST */
	for (i = 0; i < gs->count_textures; i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(gs->target, gs->textures[i]);
		glTexParameteri(gs->target, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(gs->target, GL_TEXTURE_MAG_FILTER, filter);
	}

	cb_region_init_rect(&surface_blend, 0, 0, v->surface->width,
			    v->surface->height);
	cb_region_subtract(&surface_blend, &surface_blend, &v->surface->opaque);

	cb_region_init(&surface_opaque);
	cb_region_copy(&surface_opaque, &v->surface->opaque);

	if (cb_region_is_not_empty(&surface_opaque)) {
		if (gs->shader == &r->texture_shader_rgba) {
			use_shader(r, &r->texture_shader_rgbx);
			shader_uniforms(&r->texture_shader_rgbx, v, go);
		}
		if (v->alpha < 1.0f)
			glEnable(GL_BLEND);
		else
			glDisable(GL_BLEND);
		repaint_region(r, v, &view_area, &surface_opaque,
			       &go->render_area.pos);
		repainted = true;
	}

	if (cb_region_is_not_empty(&surface_blend)) {
		use_shader(r, gs->shader);
		glEnable(GL_BLEND);
		repaint_region(r, v, &view_area, &surface_blend,
			       &go->render_area.pos);
	}

	cb_region_fini(&surface_blend);
	cb_region_fini(&surface_opaque);

out:
	cb_region_fini(&view_area);
	cb_region_fini(&output_area);
	return repainted;
}

static bool repaint_views(struct gl_output_state *go, struct cb_region *damage,
			  struct list_head *views)
{
	struct cb_view *view;
	bool repainted = false;

	list_for_each_entry_reverse(view, views, link) {
		if (!(view->output_mask & (1U << go->pipe)))
			continue;
		if (view->direct_show) /* not renderable surface */
			continue;
		gles_debug("view %p", view);
		if (draw_view(view, go, damage)) {
			repainted = true;
			view->painted = true;
		}
	}

	return repainted;
}

static bool gl_output_repaint(struct r_output *output, struct list_head *views)
{
	struct gl_output_state *go = to_glo(output);
	struct gl_renderer *r = go->r;
	struct cb_rect *area = &go->render_area;
	struct cb_region total_damage;
	EGLBoolean ret;
	static s32 errored = 0;
	s32 left, top, calc;
	u32 width, height;
	bool repainted;

	calc = go->disp_w * area->h / area->w;
	if (calc <= go->disp_h) {
		left = 0;
		top = (go->disp_h - calc) / 2;
		width = go->disp_w;
		height = calc;
	} else {
		calc = area->w * go->disp_h / area->h;
		left = (go->disp_w - calc) / 2;
		top = 0;
		width = calc;
		height = go->disp_h;
	}

	if (gl_switch_output(go) < 0)
		return false;

	if (go->layout_changed) {
		if (left) {
			/* draw left and right black bar */
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			glViewport(0, 0, left - 1, height);
			glViewport(left + width, 0, left - 1, height);
		}
		if (top) {
			/* draw top and bottom black bar */
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			glViewport(left, 0, width, top - 1);
			glViewport(left, top + height, width, top - 1);
		}
		go->layout_changed--;
	}

	glViewport(left, top, width, height);

	gles_debug("Output[%d] View %d,%d %ux%u %ux%u", go->pipe,
		   left, top, width, height,
		   go->disp_w, go->disp_h);

	cb_region_init_rect(&total_damage, 0, 0, area->w, area->h);
	repainted = repaint_views(go, &total_damage, views);
	cb_region_fini(&total_damage);
	if (!repainted)
		return false;
	/* TODO send frame signal */
	egl_debug("EGL Swap buffer.");
	ret = eglSwapBuffers(r->egl_display, go->egl_surface);
	if (ret == EGL_FALSE && !errored) {
		errored = 1;
		egl_err("Failed to call eglSwapBuffers.");
		egl_error_state();
	}
	return true;
}

static void gl_output_destroy(struct r_output *o)
{
	struct gl_output_state *go = to_glo(o);
	struct gl_renderer *r = go->r;

	eglMakeCurrent(r->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
	eglDestroySurface(r->egl_display, go->egl_surface);
	free(go);
}

static EGLSurface gl_create_output_surface(struct gl_renderer *r,
					   void *legacy_win,
					   void *window,
					   s32 *formats,
					   s32 count_fmts,
					   s32 *vid)
{
	EGLSurface egl_surface;
	EGLConfig egl_config;

	if (egl_choose_config(r, gl_opaque_attribs, formats, count_fmts,
			      &egl_config, vid) < 0) {
		egl_err("failed to choose EGL config for output");
		return EGL_NO_SURFACE;
	}

	if (egl_config != r->egl_config) {
		egl_err("attempt to use different config for output.");
		return EGL_NO_SURFACE;
	}

	if (r->create_platform_window && window) {
		egl_surface = r->create_platform_window(r->egl_display,
							egl_config,
							window,
							NULL);
	} else if (legacy_win) {
		egl_surface = eglCreateWindowSurface(
					r->egl_display,
					egl_config,
					(EGLNativeWindowType)legacy_win,
					NULL);
	} else {
		egl_err("create_platform_window = %p, window = %p, "
			"legacy_win = %lu", r->create_platform_window,
			window, (u64)legacy_win);
		return EGL_NO_SURFACE;
	}

	return egl_surface;
}

static struct r_output *gl_output_create(struct renderer *renderer,
					 void *window_for_legacy,
					 void *window,
					 s32 *formats,
					 s32 count_fmts,
					 s32 *vid,
					 struct cb_rect *render_area,
					 u32 disp_w, u32 disp_h,
					 s32 pipe)
{
	struct gl_output_state *go;
	struct gl_renderer *r = to_glr(renderer);
	EGLSurface egl_surface;

	egl_surface = gl_create_output_surface(r, window_for_legacy,
					       window, formats, count_fmts,
					       vid);
	if (egl_surface == EGL_NO_SURFACE) {
		egl_err("failed to create output surface.");
		return NULL;
	}

	go = calloc(1, sizeof(*go));
	if (!go) {
		eglDestroySurface(r->egl_display, egl_surface);
		return NULL;
	}

	go->pipe = pipe;

	go->egl_surface = egl_surface;
	go->r = r;
	go->layout_changed = LAYOUT_CHG_CNT;
	go->disp_w = disp_w;
	go->disp_h = disp_h;
	memcpy(&go->render_area, render_area, sizeof(*render_area));

	go->base.destroy = gl_output_destroy;
	go->base.repaint = gl_output_repaint;
	go->base.layout_changed = gl_output_layout_changed;

	return &go->base;
}

static void alloc_textures(struct gl_surface_state *gs, s32 count_textures)
{
	s32 i;

	if (count_textures <= gs->count_textures)
		return;

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

static void gl_attach_dma_buffer(struct gl_renderer *r,
				 struct cb_surface *surface,
				 struct cb_buffer *buffer)
{
	struct gl_surface_state *gs = get_surface_state(r, surface);
	struct gl_dma_buffer *dmabuf = container_of(buffer,
						    struct gl_dma_buffer, base);

	if (buffer->info.pix_fmt == CB_PIX_FMT_XRGB8888) {
		gs->target = GL_TEXTURE_2D;
		surface->is_opaque = true;
		gs->shader = &r->texture_shader_rgba;
		gs->pitch = buffer->info.strides[0] / 4;
	} else if (buffer->info.pix_fmt == CB_PIX_FMT_ARGB8888) {
		gs->target = GL_TEXTURE_2D;
		surface->is_opaque = false;
		gs->shader = &r->texture_shader_rgba;
		gs->pitch = buffer->info.strides[0] / 4;
	} else if (buffer->info.pix_fmt == CB_PIX_FMT_NV12
	        || buffer->info.pix_fmt == CB_PIX_FMT_NV16) {
		gs->target = GL_TEXTURE_EXTERNAL_OES;
		surface->is_opaque = true;
		gs->shader = &r->texture_shader_egl_external;
		gs->pitch = buffer->info.width;
	} else {
		gles_err("illegal pixel fmt %u", buffer->info.pix_fmt);
		return;
	}
	alloc_textures(gs, 1);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(gs->target, gs->textures[0]);
	r->image_target_texture_2d(gs->target, dmabuf->image);
	gs->h = buffer->info.height;
	gs->buf_type = CB_BUF_TYPE_DMA;
	gs->y_inverted = true;
	gs->surface = surface;
}

static void gl_attach_shm_buffer(struct gl_renderer *r,
				 struct cb_surface *surface,
				 struct cb_buffer *buffer)
{
	struct gl_surface_state *gs = get_surface_state(r, surface);
	GLenum gl_format[3] = {0, 0, 0};
	GLenum gl_pixel_type;
	s32 pitch;
	s32 count_planes;

	count_planes = 1;
	gs->offset[0] = 0;
	gs->hsub[0] = 1;
	gs->vsub[0] = 1;

	switch (buffer->info.pix_fmt) {
	case CB_PIX_FMT_XRGB8888:
		gs->shader = &r->texture_shader_rgbx;
		pitch = buffer->info.strides[0] / 4;
		gl_format[0] = GL_BGRA_EXT;
		gl_pixel_type = GL_UNSIGNED_BYTE;
		surface->is_opaque = true;
		break;
	case CB_PIX_FMT_ARGB8888:
		gs->shader = &r->texture_shader_rgba;
		pitch = buffer->info.strides[0] / 4;
		gl_format[0] = GL_BGRA_EXT;
		gl_pixel_type = GL_UNSIGNED_BYTE;
		surface->is_opaque = false;
		break;
	case CB_PIX_FMT_NV12:
		pitch = buffer->info.strides[0];
		gl_pixel_type = GL_UNSIGNED_BYTE;
		count_planes = 2;
		gs->offset[0] = buffer->info.offsets[0];
		gs->offset[1] = buffer->info.offsets[1];
/*
		gs->offset[1] = gs->offset[0] + (pitch / gs->hsub[0]) *
				(buffer->info.height / gs->vsub[0]);
*/
		gs->hsub[1] = 2;
		gs->vsub[1] = 2;
		if (r->support_texture_rg) {
			gs->shader = &r->texture_shader_y_uv;
			gl_format[0] = GL_R8_EXT;
			gl_format[1] = GL_RG8_EXT;
		} else {
			gs->shader = &r->texture_shader_y_xuxv;
			gl_format[0] = GL_LUMINANCE;
			gl_format[1] = GL_LUMINANCE_ALPHA;
		}
		surface->is_opaque = true;
		break;
	case CB_PIX_FMT_NV16:
		pitch = buffer->info.strides[0];
		gl_pixel_type = GL_UNSIGNED_BYTE;
		count_planes = 2;
		gs->offset[0] = buffer->info.offsets[0];
		gs->offset[1] = buffer->info.offsets[1];
/*
		gs->offset[1] = gs->offset[0] + (pitch / gs->hsub[0]) *
				(buffer->info.height / gs->vsub[0]);
*/
		gs->hsub[1] = 2;
		gs->vsub[1] = 1;
		if (r->support_texture_rg) {
			gs->shader = &r->texture_shader_y_uv;
			gl_format[0] = GL_R8_EXT;
			gl_format[1] = GL_RG8_EXT;
		} else {
			gs->shader = &r->texture_shader_y_xuxv;
			gl_format[0] = GL_LUMINANCE;
			gl_format[1] = GL_LUMINANCE_ALPHA;
		}
		surface->is_opaque = true;
		break;
	case CB_PIX_FMT_NV24:
		pitch = buffer->info.strides[0];
		gl_pixel_type = GL_UNSIGNED_BYTE;
		count_planes = 2;
		gs->offset[0] = buffer->info.offsets[0];
		gs->offset[1] = buffer->info.offsets[1];
/*
		gs->offset[1] = gs->offset[0] + (pitch / gs->hsub[0]) *
				(buffer->info.height / gs->vsub[0]);
*/
		gs->hsub[1] = 1;
		gs->vsub[1] = 1;
		if (r->support_texture_rg) {
			gs->shader = &r->texture_shader_y_uv;
			gl_format[0] = GL_R8_EXT;
			gl_format[1] = GL_RG8_EXT;
		} else {
			gs->shader = &r->texture_shader_y_xuxv;
			gl_format[0] = GL_LUMINANCE;
			gl_format[1] = GL_LUMINANCE_ALPHA;
		}
		surface->is_opaque = true;
		break;
	case CB_PIX_FMT_YUV420:
		gs->shader = &r->texture_shader_y_u_v;
		pitch = buffer->info.strides[0];
		gl_pixel_type = GL_UNSIGNED_BYTE;
		count_planes = 3;
		gs->offset[0] = buffer->info.offsets[0];
		gs->offset[1] = buffer->info.offsets[1];
/*
		gs->offset[1] = gs->offset[0] + (pitch / gs->hsub[0]) *
				(buffer->info.height / gs->vsub[0]);
*/
		gs->hsub[1] = 2;
		gs->vsub[1] = 2;
		gs->offset[2] = buffer->info.offsets[2];
/*
		gs->offset[2] = gs->offset[1] + (pitch / gs->hsub[1]) *
				(buffer->info.height / gs->vsub[1]);
*/
		gs->hsub[2] = 2;
		gs->vsub[2] = 2;
		if (r->support_texture_rg) {
			gl_format[0] = GL_R8_EXT;
			gl_format[1] = GL_R8_EXT;
			gl_format[2] = GL_R8_EXT;
		} else {
			gl_format[0] = GL_LUMINANCE;
			gl_format[1] = GL_LUMINANCE;
			gl_format[2] = GL_LUMINANCE;
		}
		surface->is_opaque = true;
		break;
	case CB_PIX_FMT_YUV444:
		gs->shader = &r->texture_shader_y_u_v;
		pitch = buffer->info.strides[0];
		gl_pixel_type = GL_UNSIGNED_BYTE;
		count_planes = 3;
		gs->offset[0] = buffer->info.offsets[0];
		gs->offset[1] = buffer->info.offsets[1];
/*
		gs->offset[1] = gs->offset[0] + (pitch / gs->hsub[0]) *
				(buffer->info.height / gs->vsub[0]);
*/
		gs->hsub[1] = 1;
		gs->vsub[1] = 1;
		gs->offset[2] = buffer->info.offsets[2];
/*
		gs->offset[2] = gs->offset[1] + (pitch / gs->hsub[1]) *
				(buffer->info.height / gs->vsub[1]);
*/
		gs->hsub[2] = 1;
		gs->vsub[2] = 1;
		if (r->support_texture_rg) {
			gl_format[0] = GL_R8_EXT;
			gl_format[1] = GL_R8_EXT;
			gl_format[2] = GL_R8_EXT;
		} else {
			gl_format[0] = GL_LUMINANCE;
			gl_format[1] = GL_LUMINANCE;
			gl_format[2] = GL_LUMINANCE;
		}
		surface->is_opaque = true;
		break;
	default:
		gles_err("unknown pixel format %u", buffer->info.pix_fmt);
		return;
	}

	if (pitch != gs->pitch
	    || buffer->info.height != gs->h
	    || gl_format[0] != gs->gl_format[0]
	    || gl_format[1] != gs->gl_format[1]
	    || gl_format[2] != gs->gl_format[2]
	    || gl_pixel_type != gs->gl_pixel_type
	    || gs->buf_type != CB_BUF_TYPE_SHM) {
		gs->pitch = pitch;
		gs->target = GL_TEXTURE_2D;
		gs->h = buffer->info.height;
		gs->gl_format[0] = gl_format[0];
		gs->gl_format[1] = gl_format[1];
		gs->gl_format[2] = gl_format[2];
		gs->gl_pixel_type = gl_pixel_type;
		gs->needs_full_upload = true;
		gs->y_inverted = true;
		gs->surface = surface;
		gs->buf_type = CB_BUF_TYPE_SHM;
		alloc_textures(gs, count_planes);
	}
}

static void gl_attach_buffer(struct renderer *renderer,
			     struct cb_surface *surface,
			     struct cb_buffer *buffer)
{
	struct gl_renderer *r = to_glr(renderer);
	struct gl_surface_state *gs;

	gs = get_surface_state(r, surface);

	if (!buffer) {
		glDeleteTextures(gs->count_textures, gs->textures);
		gs->count_textures = 0;
		gs->y_inverted = true;
		gs->buffer = NULL;
		surface->is_opaque = false;
		return;
	}

	if (buffer->info.type == CB_BUF_TYPE_SHM) {
		gl_attach_shm_buffer(r, surface, buffer);
		gs->buffer = buffer;
	} else if (buffer->info.type == CB_BUF_TYPE_DMA) {
		gl_attach_dma_buffer(r, surface, buffer);
		gs->buffer = buffer;
	} else {
		gles_err("unknown buffer type %p %u", buffer,
			 buffer->info.type);
		gs->count_textures = 0;
		gs->y_inverted = true;
		surface->is_opaque = false;
	}
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

static void gl_flush_damage(struct renderer *renderer,
			    struct cb_surface *surface)
{
	struct gl_renderer *r = to_glr(renderer);
	struct gl_surface_state *gs = get_surface_state(r, surface);
	struct cb_buffer *buffer = gs->buffer;
	struct cb_box *boxes, *box;
	struct shm_buffer *shm_buffer;
	u8 *data;
	s32 i, j, count_boxes;

	cb_region_union(&gs->texture_damage, &gs->texture_damage,
			&surface->damage);

	if (!buffer)
		return;

	if (!cb_region_is_not_empty(&gs->texture_damage)
		&& !gs->needs_full_upload)
		goto done;

	shm_buffer = container_of(buffer, struct shm_buffer, base);
	data = shm_buffer->shm.map;
	assert(data);

	if (!r->support_unpack_subimage) {
		/* begin access buffer */
		for (j = 0; j < gs->count_textures; j++) {
			glBindTexture(GL_TEXTURE_2D, gs->textures[j]);
			glTexImage2D(GL_TEXTURE_2D, 0,
				     gs->gl_format[j],
				     gs->pitch / gs->hsub[j],
				     buffer->info.height / gs->vsub[j],
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
				   buffer->info.height / gs->vsub[j],
				   0,
				   data, gs->offset[j]);

			glTexImage2D(GL_TEXTURE_2D, 0,
				     gs->gl_format[j],
				     gs->pitch / gs->hsub[j],
				     buffer->info.height / gs->vsub[j],
				     0,
				     gl_format_from_internal(gs->gl_format[j]),
				     gs->gl_pixel_type,
				     data + gs->offset[j]);
		}
		/* end access buffer */
		goto done;
	}

	boxes = cb_region_boxes(&gs->texture_damage, &count_boxes);
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
	cb_region_fini(&gs->texture_damage);
	cb_region_init(&gs->texture_damage);
	gs->needs_full_upload = false;
}

struct renderer *renderer_create(struct compositor *c,
				 u32 *formats, s32 count_fmts,
				 bool no_winsys, void *native_window, s32 *vid)
{
	struct gl_renderer *r = NULL;
	EGLint major, minor;

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

	if (!eglInitialize(r->egl_display, &major, &minor)) {
		egl_err("failed to initialize EGL.");
		goto err_egl_init;
	}

	print_egl_info(r->egl_display);

	if (egl_choose_config(r, gl_opaque_attribs, (s32 *)formats, count_fmts,
			      &r->egl_config, vid) < 0) {
		egl_err("failed to choose EGL config");
		goto err;
	}

	if (set_egl_extensions(r) < 0) {
		egl_err("failed to set EGL extensions.");
		goto err_egl_init;
	}

	if (r->support_surfaceless_context) {
		r->dummy_surface = EGL_NO_SURFACE;
	} else {
		egl_info("EGL_KHR_surfaceless_context unavailable. "
			 "Tring PbufferSurface");
		if (create_pbuffer_surface(r) < 0)
			goto err_egl_init;
	}

	if (gl_setup(r, r->dummy_surface) < 0)
		goto err_egl_init;

	cb_array_init(&r->vertices);
	cb_array_init(&r->vtxcnt);

	cb_signal_init(&r->destroy_signal);

	INIT_LIST_HEAD(&r->dmabuf_images);

	r->base.output_create = gl_output_create;
	r->base.import_dmabuf = gl_import_dmabuf;
	r->base.release_dmabuf = gl_release_dmabuf;
	r->base.flush_damage = gl_flush_damage;
	r->base.attach_buffer = gl_attach_buffer;

	return &r->base;

err_egl_init:
	egl_error_state();
err:
	if (r && r->egl_display != EGL_NO_DISPLAY)
		eglTerminate(r->egl_display);
	if (r)
		free(r);
	return NULL;
}

