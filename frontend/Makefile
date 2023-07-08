CXXFLAGS+=-O0 -g
#CXXFLAGS+=-O2

CXXFLAGS+=-pthread
LDLIBS+=-lm

CXXFLAGS+=$(shell sdl2-config --cflags)
LDLIBS+=$(shell sdl2-config --libs)

PKGS=gl
CXXFLAGS+=$(shell pkg-config --cflags $(PKGS))
LDLIBS+=$(shell pkg-config --libs $(PKGS))

spcfront: spcfront.o imgui.o imgui_widgets.o imgui_tables.o imgui_draw.o imgui_impl_sdl2.o imgui_impl_opengl2.o
	$(CXX) $^ $(LDLIBS) -o $@

all: spcfront

clean:
	rm -f *.o spcfront
