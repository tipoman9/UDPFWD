CFLAGS=-O1 -g -fsanitize=address -fno-omit-frame-pointer -Wall -Wextra
LDFLAGS=-g -fsanitize=address
LDLIBS=-levent_core

#Compile for SSC338q
#CC=/home/home/src/ipcssc338/openipc-firmware/output/per-package/mavfwd/host/bin/arm-openipc-linux-gnueabihf-gcc

udpfwd:
