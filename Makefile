.PHONY: build_gpu release_gpu build_cpu release_cpu clean install loc
build_gpu: impl.o
	clang -Wall -Wextra -Wno-unused-parameter -Wno-unused -o pin \
    	-g \
    	-lX11 -lXrandr -lm -lGL \
    	./src/gpu.c ./impl.o

impl.o: ./3rd/glad/gl.h ./3rd/RGFW.h ./3rd/stb_image.h ./src/impl.c
	clang -Wall -Wextra -Wno-unused-parameter -Wno-unused -g -c ./src/impl.c -fPIC

release_gpu:
	clang -Wall -Wextra -Wno-unused-parameter -Wno-unused -Wno-c23-extensions -o pin \
		-D RELEASE \
    	-O2 \
    	-lX11 -lXrandr -lm -lGL \
    	./src/gpu.c

build_cpu:
	clang -Wall -Wextra -Wno-unused-parameter -Wno-unused -o pin \
    	-g \
    	-lX11 -lm \
    	./src/cpu.c

release_cpu:
	clang -Wall -Wextra -Wno-unused-parameter -Wno-unused -o pin \
    	-O2 \
    	-lX11 -lm \
    	./src/cpu.c

build_cpu_wayland:
	clang -Wall -Wextra -Wno-unused-parameter -Wno-unused -o pin \
    	-g \
    	-lwayland-client \
    	-lm \
    	./src/cpu_wayland.c

wayland_files:
	rm -rf ./wayland/
	mkdir ./wayland/
	wayland-scanner client-header < /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml > ./wayland/xdg-shell-client-protocol.h
	wayland-scanner private-code < /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml > ./wayland/xdg-shell-protocol.c
	wayland-scanner client-header < /usr/share/wayland-protocols/stable/tablet/tablet-v2.xml > ./wayland/tablet-v2-client-protocol.h
	wayland-scanner private-code < /usr/share/wayland-protocols/stable/tablet/tablet-v2.xml > ./wayland/tablet-v2-protocol.c
	wayland-scanner client-header < /usr/share/wayland-protocols/staging/cursor-shape/cursor-shape-v1.xml > ./wayland/cursor-shape-v1-client-protocol.h
	wayland-scanner private-code < /usr/share/wayland-protocols/staging/cursor-shape/cursor-shape-v1.xml > ./wayland/cursor-shape-v1-protocol.c
	wayland-scanner client-header < /usr/share/wayland-protocols/staging/fractional-scale/fractional-scale-v1.xml > ./wayland/fractional-scale-v1-client-protocol.h
	wayland-scanner private-code < /usr/share/wayland-protocols/staging/fractional-scale/fractional-scale-v1.xml > ./wayland/fractional-scale-v1-protocol.c
	wayland-scanner client-header < /usr/share/wayland-protocols/stable/viewporter/viewporter.xml > ./wayland/viewporter-client-protocol.h
	wayland-scanner private-code < /usr/share/wayland-protocols/stable/viewporter/viewporter.xml > ./wayland/viewporter-protocol.c

clean:
	rm -f ./impl.o ./pin

install:
	mkdir -p /usr/local/bin
	cp -f pin /usr/local/bin
	chmod 755 /usr/local/bin/pin
	mkdir -p /usr/local/share/applications
	cp -f pin.desktop /usr/local/share/applications
	mkdir -p /usr/share/pixmaps
	cp -f pin.svg /usr/share/pixmaps/pin.svg

uninstall:
	rm -f /usr/local/bin/pin
	rm -f /usr/local/share/applications/pin.desktop
	rm -f /usr/share/pixmaps/pin.svg

loc:
	tokei --exclude 3rd
