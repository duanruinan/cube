include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJS := test_atomic libcube_scanout_drm.so test_drm

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

all: $(OBJS)

test_atomic: test_atomic.o
	$(CC) $^ -L$(RPATH)/utils -ldrm -o $@

test_atomic.o: test_atomic.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm -o $@

libcube_scanout_drm.so: drm.o
	$(CC) -shared -rdynamic $^ -L$(RPATH)/utils -lcube_utils \
		-ldrm -ludev -o $@

drm.o: drm.c cube_scanout.h cube_compositor.h
	$(CC) -c $< $(CFLAGS) -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm -o $@

test_drm: test_drm.o
	$(CC) $^ -L$(RPATH)/utils -L$(RPATH)/server -lcube_scanout_drm -o $@

test_drm.o: test_drm.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm -o $@

clean:
	-@rm -f $(OBJS) *.o *.so

