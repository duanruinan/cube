include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJS := cube_manager test_client

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

cube_manager: cube_manager.o
	$(CC) $^ -L$(RPATH)/utils -lcube_utils -o $@

cube_manager.o: cube_manager.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

test_client: test_client.o
	$(CC) $^ -L$(RPATH)/utils -lcube_utils -o $@

test_client.o: test_client.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

clean:
	-@rm -f $(OBJS) *.o

