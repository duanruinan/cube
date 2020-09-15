include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJS := libcube_drm_scanout.so \
	libcube_gl_renderer.so \
	libcube_scanout_helper.so \
	libcube_compositor.so cube_server \
	test_drm test_atomic test_cache

CFLAGS += -I$(RPATH)/utils
CFLAGS += -I./
CFLAGS += -fPIC

CUBE_UTILS_H += $(RPATH)/utils/cube_utils.h
CUBE_UTILS_H += $(RPATH)/utils/cube_log.h
CUBE_UTILS_H += $(RPATH)/utils/cube_ipc.h
CUBE_UTILS_H += $(RPATH)/utils/cube_signal.h
CUBE_UTILS_H += $(RPATH)/utils/cube_event.h
CUBE_UTILS_H += $(RPATH)/utils/cube_region.h
CUBE_UTILS_H += $(RPATH)/utils/cube_array.h
CUBE_UTILS_H += $(RPATH)/utils/cube_shm.h
CUBE_UTILS_H += $(RPATH)/utils/cube_cache.h
CUBE_UTILS_H += $(RPATH)/utils/cube_protocal.h

all: $(OBJS)

libcube_gl_renderer.so: gl_renderer.o
	$(CC) -shared -rdynamic $^ -L$(RPATH)/utils -lcube_utils \
		-lgbm -lGLESv2 -lEGL -o $@

gl_renderer.o: gl_renderer.c cube_renderer.h cube_compositor.h $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

libcube_drm_scanout.so: drm_scanout.o
	$(CC) -shared -rdynamic $^ -L$(RPATH)/utils -lcube_utils \
		-ldrm -ludev -lgbm -o $@

drm_scanout.o: drm_scanout.c cube_scanout.h cube_compositor.h $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm -o $@

libcube_scanout_helper.so: scanout_helper.o
	$(CC) -shared -rdynamic $^ -L$(RPATH)/utils -lcube_utils -o $@

scanout_helper.o: scanout_helper.c cube_scanout.h cube_compositor.h \
		$(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

libcube_compositor.so: cube_compositor.o
	$(CC) -shared -rdynamic $^ -L$(RPATH)/utils -lcube_utils \
		-L$(RPATH)/server -lcube_drm_scanout -lcube_scanout_helper \
		-lcube_gl_renderer -o $@

cube_compositor.o: cube_compositor.c cube_scanout.h cube_compositor.h \
		cube_renderer.h \
		$(CUBE_UTILS_H) \
		cube_client_agent.h
	$(CC) -c $< $(CFLAGS) -o $@

cube_server: cube_server.o cube_client_agent.o
	$(CC) $^ -L$(RPATH)/utils -lcube_utils \
		-L$(RPATH)/server -lcube_compositor \
			-Wl,-rpath,$(RPATH)/server -o $@

cube_server.o: cube_server.c $(CUBE_UTILS_H) cube_compositor.h \
		cube_client_agent.h
	$(CC) -c $< $(CFLAGS) -o $@

cube_client_agent.o: cube_client_agent.c cube_client_agent.h cube_compositor.h \
		$(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

test_drm: test_drm.o
	$(CC) $^ -L$(RPATH)/utils -L$(RPATH)/server -lcube_drm_scanout \
		-lcube_scanout_helper -lcube_utils -ldrm -o $@

test_drm.o: test_drm.c $(CUBE_UTILS_H) cube_scanout.h
	$(CC) -c $< $(CFLAGS) -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm -o $@

test_atomic: test_atomic.o
	$(CC) $^ -L$(RPATH)/utils -ldrm -o $@

test_atomic.o: test_atomic.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm -o $@

test_cache: test_cache.o
	$(CC) $^ -L$(RPATH)/utils -lcube_utils -o $@

test_cache.o: test_cache.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

clean:
	-@rm -f $(OBJS) *.o *.so

