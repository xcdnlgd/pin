.PHONY: build release clean install loc
build: impl.o
	clang++ -Wall -Wextra -Wno-unused-parameter -Wno-unused -o pin \
    	-g \
    	-lX11 -lXrandr -lm -lGL \
    	./src/main.cpp ./impl.o

impl.o: ./3rd/glad/gl.h ./3rd/RGFW.h ./3rd/stb_image.h ./src/impl.c
	clang -Wall -Wextra -Wno-unused-parameter -Wno-unused -g -c ./src/impl.c -fPIC

release:
	clang++ -Wall -Wextra -Wno-unused-parameter -Wno-unused -o pin \
		-D RELEASE \
    	-O2 \
    	-lX11 -lXrandr -lm -lGL \
    	./src/main.cpp
clean:
	rm -f ./impl.o ./main

install:
	mkdir -p /usr/local/bin
	cp -f pin /usr/local/bin
	chmod 755 /usr/local/bin/pin
	mkdir -p /usr/local/share/applications
	cp -f pin.desktop /usr/local/share/applications

uninstall:
	rm -f /usr/local/bin/pin
	rm -f /usr/local/share/applications/pin.desktop

loc:
	tokei --exclude 3rd
