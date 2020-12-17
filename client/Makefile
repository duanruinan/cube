include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJS := libcube_client.so cube_manager test_mc test_client stat_tips \
		libstat_tips.so test_stat_tips

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

libcube_client.so: cube_client.o
	$(CC) -shared -rdynamic $^ -L$(RPATH)/utils -lcube_utils -ldrm -lgbm \
			-o $@

cube_client.o: cube_client.c cube_client.h $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm -o $@

cube_manager: cube_manager.o
	$(CC) $^ -L$(RPATH)/utils -lcube_utils \
		-L$(RPATH)/client -lcube_client \
		-Wl,-rpath,$(RPATH)/client \
		-Wl,-rpath,$(RPATH)/utils \
		-o $@

cube_manager.o: cube_manager.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

test_mc: test_mc.o
	$(CC) $^ -L$(RPATH)/utils -lcube_utils \
		-L$(RPATH)/client -lcube_client \
		-Wl,-rpath,$(RPATH)/client \
		-Wl,-rpath,$(RPATH)/utils \
		-o $@

test_mc.o: test_mc.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

test_client: test_client.o
	$(CC) $^ -L$(RPATH)/utils -lcube_utils \
		-L$(RPATH)/client -lcube_client \
		-Wl,-rpath,$(RPATH)/client \
		-Wl,-rpath,$(RPATH)/utils \
		-o $@

test_client.o: test_client.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

stat_tips: stat_tips.o
	$(CC) $^ -L$(RPATH)/utils -lcube_utils \
		-L$(RPATH)/client -lcube_client \
		-Wl,-rpath,$(RPATH)/client \
		-Wl,-rpath,$(RPATH)/utils \
		-lgbm -lGLESv2 -lEGL \
		-lfreetype \
		-o $@

stat_tips.o: stat_tips.c $(CUBE_UTILS_H) stat_tips.h
	$(CC) -c $< $(CFLAGS) -I${TOOLCHAIN_SYSROOT}/usr/include/libdrm \
		-I${TOOLCHAIN_SYSROOT}/usr/include/freetype2 \
		-I${TOOLCHAIN_SYSROOT}/usr/include/libpng16 \
		-o $@

libstat_tips.so: stat_tips_cli.o
	$(CC) -shared -rdynamic $^ -L$(RPATH)/utils -lcube_utils -o $@

stat_tips_cli.o: stat_tips_cli.c stat_tips.h
	$(CC) -c $< $(CFLAGS) -o $@

test_stat_tips: test_stat_tips.o
	$(CC) $^ -L$(RPATH)/utils -lcube_utils \
		-L$(RPATH)/client -lcube_client \
		-Wl,-rpath,$(RPATH)/client \
		-Wl,-rpath,$(RPATH)/utils \
		-L$(RPATH)/client -lstat_tips -o $@

test_stat_tips.o: test_stat_tips.c
	$(CC) -c $< $(CFLAGS) -o $@

clean:
	-@rm -f $(OBJS) *.o

