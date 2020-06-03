include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJS := test_kms

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

test_kms: test_kms.o
	$(CC) $^ -L$(RPATH)/utils -ldrm -o $@

test_kms.o: test_kms.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm -o $@

clean:
	-@rm -f $(OBJS) *.o *.so

