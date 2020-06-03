include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

OBJS := libcube_utils.so cube_log

CFLAGS += -I$(RPATH)/utils
CFLAGS += -fPIC

CUBE_UTILS_H += cube_utils.h
CUBE_UTILS_H += cube_log.h
CUBE_UTILS_H += cube_ipc.h
CUBE_UTILS_H += cube_signal.h
CUBE_UTILS_H += cube_event.h
CUBE_UTILS_H += cube_region.h
CUBE_UTILS_H += cube_array.h
CUBE_UTILS_H += cube_shm.h

CUBE_UTILS_OBJS += cube_log.o
CUBE_UTILS_OBJS += cube_ipc.o
CUBE_UTILS_OBJS += cube_signal.o
CUBE_UTILS_OBJS += cube_event.o
CUBE_UTILS_OBJS += cube_region.o
CUBE_UTILS_OBJS += cube_array.o
CUBE_UTILS_OBJS += cube_shm.o

all: $(OBJS)

libcube_utils.so: $(CUBE_UTILS_OBJS)
	$(CC) -shared -rdynamic $^ $(LDFLAGS) -lrt -o $@

cube_log.o: cube_log.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

cube_ipc.o: cube_ipc.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

cube_signal.o: cube_signal.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

cube_event.o: cube_event.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

cube_region.o: cube_region.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

cube_array.o: cube_array.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

cube_log: log_server.o
	$(CC) $^ $(LDFLAGS) -L$(RPATH)/utils -lcube_utils -o $@

log_server.o: log_server.c $(CUBE_UTILS_H)
	$(CC) -c $< $(CFLAGS) -o $@

clean:
	-@rm -f $(OBJS) *.o

