#------------------------------------------------------------------------------#
# This makefile was generated by 'cbp2make' tool rev.147                       #
#------------------------------------------------------------------------------#


WORKDIR = `pwd`

CC = gcc
CXX = g++
AR = ar
LD = gcc
WINDRES = windres

INC =
CFLAGS = -Wall
RESINC =
LIBDIR =
LIB =
LDFLAGS =

INC_DEBUG = $(INC)
CFLAGS_DEBUG = $(CFLAGS) -Wall -ggdb
RESINC_DEBUG = $(RESINC)
RCFLAGS_DEBUG = $(RCFLAGS)
LIBDIR_DEBUG = $(LIBDIR)
LIB_DEBUG = $(LIB) /usr/lib/arm-linux-gnueabihf/libmosquitto.so.1
LDFLAGS_DEBUG = $(LDFLAGS)
OBJDIR_DEBUG = obj/Debug
DEP_DEBUG =
OUT_DEBUG = bin/Debug/ehzlogger_v3

INC_RELEASE = $(INC)
CFLAGS_RELEASE = $(CFLAGS) -O2 -Wall
RESINC_RELEASE = $(RESINC)
RCFLAGS_RELEASE = $(RCFLAGS)
LIBDIR_RELEASE = $(LIBDIR)
LIB_RELEASE = $(LIB)  /usr/lib/arm-linux-gnueabihf/libmosquitto.so.1
LDFLAGS_RELEASE = $(LDFLAGS) -s
OBJDIR_RELEASE = obj/Release
DEP_RELEASE =
OUT_RELEASE = bin/Release/ehzlogger_v3

OBJ_DEBUG = $(OBJDIR_DEBUG)/main.o $(OBJDIR_DEBUG)/send_data.o $(OBJDIR_DEBUG)/util.o

OBJ_RELEASE = $(OBJDIR_RELEASE)/main.o $(OBJDIR_RELEASE)/send_data.o $(OBJDIR_RELEASE)/util.o

all: debug release

clean: clean_debug clean_release

before_debug:
	test -d bin/Debug || mkdir -p bin/Debug
	test -d $(OBJDIR_DEBUG) || mkdir -p $(OBJDIR_DEBUG)

after_debug:

debug: before_debug out_debug after_debug

out_debug: before_debug $(OBJ_DEBUG) $(DEP_DEBUG)
	$(LD) $(LIBDIR_DEBUG) -o $(OUT_DEBUG) $(OBJ_DEBUG)  $(LDFLAGS_DEBUG) $(LIB_DEBUG)

$(OBJDIR_DEBUG)/main.o: main.c
	$(CC) $(CFLAGS_DEBUG) $(INC_DEBUG) -c main.c -o $(OBJDIR_DEBUG)/main.o

$(OBJDIR_DEBUG)/send_data.o: send_data.c
	$(CC) $(CFLAGS_DEBUG) $(INC_DEBUG) -c send_data.c -o $(OBJDIR_DEBUG)/send_data.o

$(OBJDIR_DEBUG)/util.o: util.c
	$(CC) $(CFLAGS_DEBUG) $(INC_DEBUG) -c util.c -o $(OBJDIR_DEBUG)/util.o

clean_debug:
	rm -f $(OBJ_DEBUG) $(OUT_DEBUG)
	rm -rf bin/Debug
	rm -rf $(OBJDIR_DEBUG)

before_release:
	test -d bin/Release || mkdir -p bin/Release
	test -d $(OBJDIR_RELEASE) || mkdir -p $(OBJDIR_RELEASE)

after_release:

release: before_release out_release after_release

out_release: before_release $(OBJ_RELEASE) $(DEP_RELEASE)
	$(LD) $(LIBDIR_RELEASE) -o $(OUT_RELEASE) $(OBJ_RELEASE)  $(LDFLAGS_RELEASE) $(LIB_RELEASE)

$(OBJDIR_RELEASE)/main.o: main.c
	$(CC) $(CFLAGS_RELEASE) $(INC_RELEASE) -c main.c -o $(OBJDIR_RELEASE)/main.o

$(OBJDIR_RELEASE)/send_data.o: send_data.c
	$(CC) $(CFLAGS_RELEASE) $(INC_RELEASE) -c send_data.c -o $(OBJDIR_RELEASE)/send_data.o

$(OBJDIR_RELEASE)/util.o: util.c
	$(CC) $(CFLAGS_RELEASE) $(INC_RELEASE) -c util.c -o $(OBJDIR_RELEASE)/util.o

clean_release:
	rm -f $(OBJ_RELEASE) $(OUT_RELEASE)
	rm -rf bin/Release
	rm -rf $(OBJDIR_RELEASE)

.PHONY: before_debug after_debug clean_debug before_release after_release clean_release

