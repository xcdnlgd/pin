.PHONY: build release clean loc
build: impl.o
	clang++ -Wall -Wextra -Wno-unused-parameter -Wno-unused -o main \
    	-g \
    	-lX11 -lXrandr -lm \
    	./src/main.cpp ./impl.o

impl.o: ./3rd/RGFW.h ./3rd/stb_image.h ./src/impl.c
	clang -Wall -Wextra -Wno-unused-parameter -Wno-unused -c ./src/impl.c -fPIC

release:
	clang++ -Wall -Wextra -Wno-unused-parameter -Wno-unused -o main \
		-D RELEASE \
    	-O2 \
    	-lX11 -lXrandr -lm \
    	./src/main.cpp
clean:
	rm -f ./impl.o ./main

loc:
	tokei --exclude 3rd
