all:clean
ifeq ($(OS), Windows_NT)
	gcc main.c `pkg-config --cflags --libs gtk+-3.0`  -c -o main.o
	gcc main.o `pkg-config --cflags --libs gtk+-3.0` -mwindows  -lhidapi  -lsetupapi  -lconfig  -o  main
else ifeq ($(shell uname -s), Linux)
	gcc -Wall -Os -c -o main.o main.c `pkg-config --cflags --libs gtk+-3.0` 
	gcc  `pkg-config --cflags gtk+-3.0` -c main.c -o main.o 
	gcc -Wall -g `pkg-config --cflags gtk+-3.0` -o main main.o `pkg-config --libs gtk+-3.0` -lhidapi-libusb	-rdynamic
	strip main
else ifeq ($(shell uname -s), Darwin)
	gcc -Wall -Os -c -o main.o main.c `pkg-config --cflags --libs gtk+-3.0` 
	gcc -o main main.o  -lusb `pkg-config --cflags --libs gtk+-3.0` -Wl,--export-dynamic -lhidapi  -lconfig  -lsqlite3
	strip main
endif
	rm -rf *.o
	
	
clean:
	@echo + OS = $(OS) 
	@echo - shell uname= $(shell uname -s)
	rm -rf *.o *.exe main
	
program:all
	./main
	
run:all
	./main
#in msys2
# pacman -S mingw-w64-x86_64-libconfig
#in ubuntu
#sudo apt install libhidapi-dev 
