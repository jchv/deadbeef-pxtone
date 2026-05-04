CXX ?= g++
DEADBEEF_ROOT ?= /opt/deadbeef
PXTONE_ROOT ?= ./pxtone
PXTONE_SOURCES := $(wildcard $(PXTONE_ROOT)/*.cpp)
PXTONE_OBJECTS := $(PXTONE_SOURCES:.cpp=.o)
PKGCONFIG_DEPS := vorbisfile vorbis ogg
CXXFLAGS ?= \
	-g -O2 \
	-fvisibility=hidden \
	-std=c++11 \
	$(shell pkg-config $(PKGCONFIG_DEPS) --cflags)
INCLUDES ?= \
	-I$(DEADBEEF_ROOT)/include \
	-I.
DEFINES ?= \
	-DpxINCLUDE_OGGVORBIS
LDFLAGS ?= \
	$(shell pkg-config $(PKGCONFIG_DEPS) --libs)

all: compile_flags.txt pxtone.so

clean:
	find . -name "*.o" -delete
	rm -f pxtone.so compile_flags.txt

compile_flags.txt: Makefile
	(echo $(CXXFLAGS) $(INCLUDES) $(DEFINES) | xargs -n1 echo) > $@

%.o: %.cpp
	$(CXX) -c -o $@ $^ $(INCLUDES) $(DEFINES) $(CXXFLAGS) $(EXTRA_CXXFLAGS)

pxtone.o: pxtone.cpp
	$(CXX) -c -o $@ pxtone.cpp $(INCLUDES) $(DEFINES) $(CXXFLAGS) $(EXTRA_CXXFLAGS)

pxtone.so: $(PXTONE_OBJECTS) pxtone.o
	$(CXX) -shared -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(EXTRA_LDFLAGS)

install: pxtone.so
	install -D pxtone.so $(DEADBEEF_ROOT)/lib/deadbeef/pxtone.so

install-local: pxtone.so
	install -D pxtone.so $(HOME)/.local/lib/deadbeef/pxtone.so

.PHONY: all install install-local clean
