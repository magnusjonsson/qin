BUNDLE = mj-qin.lv2
INSTALL_DIR = /usr/local/lib/lv2


$(BUNDLE): manifest.ttl qin.so
	rm -rf $(BUNDLE)
	mkdir $(BUNDLE)
	cp manifest.ttl qin.so $(BUNDLE)

qin.so: qin.c
	gcc --std=gnu99 -shared -fPIC -DPIC qin.c -o qin.so

install: $(BUNDLE)
	mkdir -p $(INSTALL_DIR)
	rm -rf $(INSTALL_DIR)/$(BUNDLE)
	cp -R $(BUNDLE) $(INSTALL_DIR)

clean:
	rm -rf $(BUNDLE) qin.so
