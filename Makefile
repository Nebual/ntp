CL_EXECUTABLE=ntp_client
SV_EXECUTABLE=ntp_server
CL_SOURCES=ntp_client.cpp
SV_SOURCES=ntp_server.cpp

QUIET=@

CC=g++
STD=-std=c++11
BOOSTDIR=C:/mingw64/boostgcc

ifeq ($(OS),Windows_NT)
LIBS=-lmingw32 -lm -lmswsock -lws2_32 -lboost_thread-mgw48-mt-1_55 -lboost_system-mgw48-mt-1_55 -lboost_filesystem-mgw48-mt-1_55
CFLAGS=-O0 $(STD) -I$(BOOSTDIR)/include -L$(BOOSTDIR)/lib -march=native -Wuninitialized -Wmissing-field-initializers
CL_EXECUTABLE:=$(CL_EXECUTABLE).exe
SV_EXECUTABLE:=$(SV_EXECUTABLE).exe
else
LIBS=-lm -lpthread -lboost_thread -lboost_system -lboost_filesystem
CFLAGS=-O2 $(STD) -march=native -Wuninitialized -Wmissing-field-initializers
endif


all: compile



CL_OBJECTS=$(patsubst %.cpp,build/%.o, $(CL_SOURCES))
SV_OBJECTS=$(patsubst %.cpp,build/%.o, $(SV_SOURCES))
OBJECTS=$(CL_OBJECTS) $(SV_OBJECTS)
build:
	@mkdir build
$(OBJECTS): build/%.o : src/%.cpp | build
	@echo Compiling $< to $@...
	$(QUIET)$(CC) $(CFLAGS) -o $@ -c $<

compile: $(OBJECTS)
	@echo
	@echo Linking $(CL_EXECUTABLE)...
	$(QUIET)$(CC) $(CFLAGS) -o $(CL_EXECUTABLE) $(CL_OBJECTS) $(LIBS)
	@echo Linking $(SV_EXECUTABLE)...
	$(QUIET)$(CC) $(CFLAGS) -o $(SV_EXECUTABLE) $(SV_OBJECTS) $(LIBS)

clean:
	-rm build/*.o
