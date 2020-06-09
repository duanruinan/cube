#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <drm_fourcc.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <cube_utils.h>

struct drm_dev {
	s32 fd;
	u32 connector_id;
	u32 crtc_id;
	u32 plane_id;
	drmModeObjectProperties *plane_props;
	drmModeObjectProperties *crtc_props;
	drmModeObjectProperties *connector_props;
	drmModeConnectorPtr conn;

	u32 prop_active;
	u32 prop_mode_id;

	u32 prop_conn_crtc_id;
	u32 prop_plane_crtc_id;

	u32 prop_fb_id;

	u32 prop_crtc_x;
	u32 prop_crtc_y;
	u32 prop_crtc_w;
	u32 prop_crtc_h;

	u32 prop_src_x;
	u32 prop_src_y;
	u32 prop_src_w;
	u32 prop_src_h;

	drmModeResPtr res;
	drmModePlaneResPtr pres;

	u32 mode_blob_id;
	u32 width, height;
};

struct dma_buf {
	u32 width, height;
	u32 hstride;
	u32 handle;
	size_t size;
	u32 fourcc;
	void *map;
	u32 fb_id;
};

static s32 get_prop_id(s32 fd, drmModeObjectProperties *props, const char *name)
{
	drmModePropertyPtr property;
	u32 i, id = 0;

	for (i = 0; i < props->count_props; i++) {
		property = drmModeGetProperty(fd, props->props[i]);
		if (!property)
			continue;
		if (!strcasecmp(property->name, name))
			id = property->prop_id;
		drmModeFreeProperty(property);
		if (id)
			return id;
	}

	return -1;
}

/*
static s32 get_prop_value(s32 fd, drmModeObjectProperties *props,
			  const char *name, u32 *value)
{
	drmModePropertyPtr property;
	u32 i;

	for (i = 0; i < props->count_props; i++) {
		property = drmModeGetProperty(fd, props->props[i]);
		if (!property)
			continue;
		if (!strcasecmp(property->name, name)) {
			*value = props->prop_values[i];
			drmModeFreeProperty(property);
			return 0;
		}
		drmModeFreeProperty(property);
	}
	return -ENOENT;
}
*/

void drm_dev_destroy(struct drm_dev *dev)
{
	if (dev->mode_blob_id)
		drmModeDestroyPropertyBlob(dev->fd, dev->mode_blob_id);
	drmModeFreeConnector(dev->conn);
	drmModeFreeObjectProperties(dev->plane_props);
	drmModeFreeObjectProperties(dev->crtc_props);
	drmModeFreeObjectProperties(dev->connector_props);
	drmModeFreeResources(dev->res);
	drmModeFreePlaneResources(dev->pres);
	close(dev->fd);
	free(dev);
}

struct drm_dev *drm_dev_create(void)
{
	struct drm_dev *dev = calloc(1, sizeof(*dev));
	s32 ret;

	dev->fd = open("/dev/dri/card0", O_RDWR, 0644);
	drmSetClientCap(dev->fd, DRM_CLIENT_CAP_ATOMIC, 1);
	drmSetClientCap(dev->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	dev->res = drmModeGetResources(dev->fd);
	dev->pres = drmModeGetPlaneResources(dev->fd);
	dev->connector_id = dev->res->connectors[0];
	printf("connector_id = %u\n", dev->connector_id);
	dev->conn = drmModeGetConnector(dev->fd, dev->connector_id);
	if (dev->conn && dev->conn->connection != DRM_MODE_CONNECTED) {
		printf("connector disconnected\n");
		exit(1);
	}
	dev->crtc_id = dev->res->crtcs[0];
	printf("crtc_id = %u\n", dev->crtc_id);
	dev->plane_id = dev->pres->planes[0];
	printf("plane_id = %u\n", dev->plane_id);
	dev->connector_props = drmModeObjectGetProperties(dev->fd,
							  dev->connector_id,
						 DRM_MODE_OBJECT_CONNECTOR);
	dev->prop_conn_crtc_id = get_prop_id(dev->fd, dev->connector_props,
			"crtc_id");
	dev->crtc_props = drmModeObjectGetProperties(dev->fd,
						     dev->crtc_id,
						 DRM_MODE_OBJECT_CRTC);
	dev->prop_active = get_prop_id(dev->fd, dev->crtc_props, "active");
	dev->prop_mode_id = get_prop_id(dev->fd, dev->crtc_props, "mode_id");
	dev->plane_props = drmModeObjectGetProperties(dev->fd,
						      dev->plane_id,
						 DRM_MODE_OBJECT_PLANE);
	dev->prop_plane_crtc_id = get_prop_id(dev->fd, dev->plane_props,
			"crtc_id");
	dev->prop_crtc_x = get_prop_id(dev->fd, dev->plane_props, "crtc_x");
	dev->prop_crtc_y = get_prop_id(dev->fd, dev->plane_props, "crtc_y");
	dev->prop_crtc_w = get_prop_id(dev->fd, dev->plane_props, "crtc_w");
	dev->prop_crtc_h = get_prop_id(dev->fd, dev->plane_props, "crtc_h");
	dev->prop_src_x = get_prop_id(dev->fd, dev->plane_props, "src_x");
	dev->prop_src_y = get_prop_id(dev->fd, dev->plane_props, "src_y");
	dev->prop_src_w = get_prop_id(dev->fd, dev->plane_props, "src_w");
	dev->prop_src_h = get_prop_id(dev->fd, dev->plane_props, "src_h");
	dev->prop_fb_id = get_prop_id(dev->fd, dev->plane_props, "fb_id");
	ret = drmModeCreatePropertyBlob(dev->fd, &dev->conn->modes[0],
			sizeof(dev->conn->modes[0]), &dev->mode_blob_id);
	printf("ret = %d\n", ret);
	printf("mode_blob_id = %u\n", dev->mode_blob_id);
	printf("mode: %ux%u\n", dev->conn->modes[0].hdisplay,
			dev->conn->modes[0].vdisplay);
	dev->width = dev->conn->modes[0].hdisplay;
	dev->height = dev->conn->modes[0].vdisplay;
	return dev;
}

void dma_buf_destroy(s32 fd, struct dma_buf *buffer)
{
	struct drm_mode_destroy_dumb destroy_arg;

	if (!buffer)
		return;

	if (buffer->fb_id)
		drmModeRmFB(fd, buffer->fb_id);

	if (buffer->map)
		munmap(buffer->map, buffer->size);

	if (buffer->handle) {
		memset(&destroy_arg, 0, sizeof(destroy_arg));
		destroy_arg.handle = buffer->handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
	}
	free(buffer);
}

struct dma_buf *dma_buf_create(s32 fd, u32 width, u32 height, u32 color)
{
	struct dma_buf *buffer;
	struct drm_mode_create_dumb create_arg;
	struct drm_mode_map_dumb map_arg;
	u32 size_dword;
	u32 *p;
	s32 i;
	u32 handles[4] = {0,0,0,0};
	u32 offsets[4] = {0,0,0,0};
	u32 strides[4] = {0,0,0,0};

	buffer = calloc(1, sizeof(*buffer));

	buffer->width = width;
	buffer->height = height;
	buffer->fourcc = DRM_FORMAT_XRGB8888;

	memset(&create_arg, 0, sizeof(create_arg));
	create_arg.bpp = 32;
	create_arg.width = width;
	create_arg.height = height;
	drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg);
	buffer->handle = create_arg.handle;
	buffer->hstride = create_arg.pitch;
	printf("stride = %u\n", create_arg.pitch);
	strides[0] = create_arg.pitch;
	handles[0] = create_arg.handle;

	memset(&map_arg, 0, sizeof(map_arg));
	map_arg.handle = create_arg.handle;
	drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_arg);
	buffer->size = create_arg.size;
	buffer->map = mmap(NULL, create_arg.size, PROT_WRITE, MAP_SHARED,
			fd, map_arg.offset);

	size_dword = buffer->size / sizeof(u32);
	p = (u32 *)(buffer->map);
	for (i = 0; i < size_dword; i++)
		p[i] = color;

	drmModeAddFB2(fd, buffer->width, buffer->height, buffer->fourcc,
			handles, strides, offsets, &buffer->fb_id, 0);
	printf("fb_id = %u\n", buffer->fb_id);

	return buffer;
}

s32 main(s32 argc, char **argv)
{
	s32 i, ret;
	u32 color;
	struct dma_buf *bufs[3] = {NULL, NULL, NULL};
	struct drm_dev *dev = drm_dev_create();
	drmModeAtomicReq *req;
	u32 flags;
	s32 mode = 0;

	if (argc > 1)
		mode = 1;

	flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;
	flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;

	for (i = 0; i < 3; i++) {
		if ((i % 3) == 0) {
			color = 0xFFFF0000;
		} else if ((i % 3) == 1) {
			color = 0xFF00FF00;
		} else if ((i % 3) == 2) {
			color = 0xFF0000FF;
		}
		bufs[i] = dma_buf_create(dev->fd, dev->width, dev->height,
				color);
	}

	if (mode) {
		drmModeSetCrtc(dev->fd, dev->crtc_id, bufs[0]->fb_id,
			0, 0, &dev->connector_id, 1, &dev->conn->modes[0]);
		sleep(1);
		drmModeSetCrtc(dev->fd, dev->crtc_id, bufs[1]->fb_id,
			0, 0, &dev->connector_id, 1, &dev->conn->modes[0]);
		sleep(1);
		drmModeSetCrtc(dev->fd, dev->crtc_id, bufs[2]->fb_id,
			0, 0, &dev->connector_id, 1, &dev->conn->modes[0]);
		sleep(1);
		goto out;
	}

	printf("red\n");
	req = drmModeAtomicAlloc();
	
	ret = drmModeAtomicAddProperty(req, dev->connector_id,
				dev->prop_conn_crtc_id, dev->crtc_id);
	
	ret = drmModeAtomicAddProperty(req, dev->crtc_id,
				dev->prop_active, 1);
	
	ret = drmModeAtomicAddProperty(req, dev->crtc_id,
				dev->prop_mode_id, dev->mode_blob_id);
	
	ret = drmModeAtomicAddProperty(req, dev->plane_id,
				dev->prop_plane_crtc_id, dev->crtc_id);
	
	ret = drmModeAtomicAddProperty(req, dev->plane_id, dev->prop_crtc_x, 0);
	
	ret = drmModeAtomicAddProperty(req, dev->plane_id, dev->prop_crtc_y, 0);
	
	ret = drmModeAtomicAddProperty(req, dev->plane_id, dev->prop_crtc_w, dev->width);
	ret = drmModeAtomicAddProperty(req, dev->plane_id, dev->prop_crtc_h, dev->height);
	ret = drmModeAtomicAddProperty(req, dev->plane_id, dev->prop_src_x, 0);
	
	ret = drmModeAtomicAddProperty(req, dev->plane_id, dev->prop_src_y, 0);
	
	ret = drmModeAtomicAddProperty(req, dev->plane_id, dev->prop_src_w, dev->width << 16);
	
	ret = drmModeAtomicAddProperty(req, dev->plane_id, dev->prop_src_h, dev->height << 16);

	ret = drmModeAtomicAddProperty(req, dev->plane_id, dev->prop_fb_id, bufs[0]->fb_id);
	
	ret = drmModeAtomicCommit(dev->fd, req, flags, dev);
	printf(".");
	fflush(stdout);
	if (ret) {
		printf("failed to commit (%s)", strerror(errno));
		printf("fd = %d, req = %p, flags = %08X, dev = %p\n",
			dev->fd, req, flags, dev);
	}
	sleep(1);
	drmModeAtomicFree(req);


	printf("green\n");
	req = drmModeAtomicAlloc();
	ret = drmModeAtomicAddProperty(req, dev->plane_id, dev->prop_fb_id, bufs[1]->fb_id);
	
	flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;
	//flags = 0;
	ret = drmModeAtomicCommit(dev->fd, req, flags, dev);
	if (ret) {
		printf("failed to commit (%s)", strerror(errno));
		printf("fd = %d, req = %p, flags = %08X, dev = %p\n",
			dev->fd, req, flags, dev);
	}
	drmModeAtomicFree(req);
	sleep(1);
	printf("x");
	fflush(stdout);


	printf("blue\n");
	req = drmModeAtomicAlloc();
	ret = drmModeAtomicAddProperty(req, dev->plane_id, dev->prop_fb_id, bufs[2]->fb_id);
	
	flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;
	//flags = 0;
	ret = drmModeAtomicCommit(dev->fd, req, flags, dev);
	if (ret) {
		printf("failed to commit (%s)", strerror(errno));
		printf("fd = %d, req = %p, flags = %08X, dev = %p\n",
			dev->fd, req, flags, dev);
	}
	drmModeAtomicFree(req);
	sleep(1);
	printf("#");
	fflush(stdout);
	
out:
	for (i = 0; i < 3; i++) {
		dma_buf_destroy(dev->fd, bufs[i]);
	}

	drm_dev_destroy(dev);
	return 0;
}

