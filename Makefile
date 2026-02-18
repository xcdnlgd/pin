.PHONY: build_gpu release_gpu build_cpu release_cpu clean install loc
build_gpu: impl.o
	clang -Wall -Wextra -Wno-unused-parameter -Wno-unused -o pin \
    	-g \
    	-lX11 -lXrandr -lm -lGL \
    	./src/gpu.c ./impl.o

impl.o: ./3rd/glad/gl.h ./3rd/RGFW.h ./3rd/stb_image.h ./src/impl.c
	clang -Wall -Wextra -Wno-unused-parameter -Wno-unused -g -c ./src/impl.c -fPIC

release_gpu:
	clang -Wall -Wextra -Wno-unused-parameter -Wno-unused -o pin \
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
