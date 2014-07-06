CC := gcc

PLUGIN_NAME = nickchange

all: build install-user

build: $(PLUGIN_NAME).so

install-user: ~/.purple/plugins/$(PLUGIN_NAME).so

$(HOME)/.purple/plugins/$(PLUGIN_NAME).so: $(PLUGIN_NAME).so
	cp -v $< $@

(PLUGIN_NAME).so: $(PLUGIN_NAME).c
	$(CC) $(CFLAGS) -Wall -fPIC $< -o $@ -shared `pkg-config --cflags --libs glib-2.0 purple pidgin`

clean:
	rm -rf *.o *.c~ *.h~ *.so *.la .libs
