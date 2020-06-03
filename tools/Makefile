include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJS := detect_drm

CFLAGS += -I$(RPATH)/utils
CFLAGS += -I./
CFLAGS += -fPIC

CLOVER_UTILS_H += $(RPATH)/utils/cube_utils.h

all: $(OBJS)

detect_drm: detect_drm.o
	$(CC) $^ -ldrm -o $@

detect_drm.o: detect_drm.c $(CLOVER_UTILS_H)
	$(CC) -c $< $(CFLAGS) -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm -o $@

clean:
	-@rm -f $(OBJS) *.o

