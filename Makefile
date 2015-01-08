all:
	mkdir -p bin
	gcc -std=gnu99 -g -O2 -Wall main.c -o bin/ukeycodes
