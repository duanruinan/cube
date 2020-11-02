include $(RPATH)/build/build_env

.PHONY: all
.PHONY: clean

all:
	make -C $(RPATH)/utils RPATH=$(RPATH) PLATFORM=$(PLATFORM)
	make -C $(RPATH)/server RPATH=$(RPATH) PLATFORM=$(PLATFORM)
	make -C $(RPATH)/client RPATH=$(RPATH) PLATFORM=$(PLATFORM)
	make -C $(RPATH)/tools RPATH=$(RPATH) PLATFORM=$(PLATFORM)
	-@mkdir -p $(RPATH)/out/usr/bin
	-@mkdir -p $(RPATH)/out/usr/lib
	-@cp $(RPATH)/utils/*.so $(RPATH)/out/usr/lib
	-@cp $(RPATH)/server/*.so $(RPATH)/out/usr/lib
	-@cp $(RPATH)/server/cube_server $(RPATH)/out/usr/bin
	-@cp $(RPATH)/server/test_drm $(RPATH)/out/usr/bin
	-@cp $(RPATH)/server/test_cache $(RPATH)/out/usr/bin
	-@cp $(RPATH)/utils/cube_log $(RPATH)/out/usr/bin
	# -@cp $(RPATH)/server/cube $(RPATH)/out/usr/bin
	-@cp $(RPATH)/client/cube_manager $(RPATH)/out/usr/bin
	-@cp $(RPATH)/client/libcube_client.so $(RPATH)/out/usr/lib
	-@cp $(RPATH)/client/test_client $(RPATH)/out/usr/bin
	-@cp $(RPATH)/client/test_mc $(RPATH)/out/usr/bin
	-@cp $(RPATH)/client/stat_tips $(RPATH)/out/usr/bin
	-@cp $(RPATH)/client/test_stat_tips $(RPATH)/out/usr/bin
	-@cp $(RPATH)/client/libstat_tips.so $(RPATH)/out/usr/lib
	-@cp $(RPATH)/tools/detect_drm $(RPATH)/out/usr/bin
	-@cp $(RPATH)/build/env.sh $(RPATH)/out

clean:
	make -C $(RPATH)/tools RPATH=$(RPATH) clean
	make -C $(RPATH)/client RPATH=$(RPATH) clean
	make -C $(RPATH)/server RPATH=$(RPATH) clean
	make -C $(RPATH)/utils RPATH=$(RPATH) clean
	-@rm -rf $(RPATH)/out

