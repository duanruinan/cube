include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJS := cube_desktop

CFLAGS += -I$(RPATH)/utils
CFLAGS += -I$(RPATH)/client
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
CUBE_UTILS_H += $(RPATH)/utils/cube_network.h

all: $(OBJS)

cube_desktop: cube_desktop.o
	$(CC) $^ -L$(RPATH)/utils -lcube_utils \
		-L$(RPATH)/client -lcube_client \
		-Wl,-rpath,$(RPATH)/client \
		-Wl,-rpath,$(RPATH)/utils \
		-Wl,-rpath,$(RPATH)/gui \
		-o $@

cube_desktop.o: cube_desktop.c $(CUBE_UTILS_H) $(RPATH)/client/cube_client.h
	$(CC) -c $< $(CFLAGS) -o $@

clean:
	-@rm -f $(OBJS) *.o

