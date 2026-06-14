CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wno-narrowing -Wno-unused-result

WIN_CXX = x86_64-w64-mingw32-g++
WIN_CXXFLAGS = -std=c++17 -O2 -Wall -Wno-narrowing -static

SRCS = mkcdi_portable.cpp \
       hack4_lib.cpp \
       bincon.cpp \
       binhack32_lib.cpp \
       logoinsert_lib.cpp \
       cdibuilder_lib.cpp \
       scramble.cpp \
       elf_parser.cpp \
       binhack.cpp \
       embedded_assets.cpp

OBJS = $(SRCS:.cpp=.o)
WIN_OBJS = $(SRCS:.cpp=_win.o)
TARGET = mkcdi
WIN_TARGET = mkcdi.exe

.PHONY: all clean assets win

all: assets $(TARGET)

win: assets $(WIN_TARGET)

assets:
	python3 tools/embed_assets.py

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(WIN_TARGET): $(WIN_OBJS) mkcdi_res.o
	$(WIN_CXX) $(WIN_CXXFLAGS) -o $@ $^

mkcdi_res.o: mkcdi.rc icon.ico
	x86_64-w64-mingw32-windres --use-temp-file mkcdi.rc mkcdi_res.o

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%_win.o: %.cpp
	$(WIN_CXX) $(WIN_CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(WIN_OBJS) $(TARGET) $(WIN_TARGET)

distclean: clean
	rm -f embedded_assets.h embedded_assets.cpp

# Dependencies
mkcdi_portable.o: mkcdi_portable.cpp embedded_assets.h hack4_lib.hpp bincon.hpp binhack32_lib.hpp logoinsert_lib.hpp cdibuilder_lib.hpp scramble.h elf_parser.hpp elf.h
hack4_lib.o: hack4_lib.cpp hack4_lib.hpp
bincon.o: bincon.cpp bincon.hpp
binhack32_lib.o: binhack32_lib.cpp binhack32_lib.hpp binhack.hpp
logoinsert_lib.o: logoinsert_lib.cpp logoinsert_lib.hpp
cdibuilder_lib.o: cdibuilder_lib.cpp cdibuilder_lib.hpp
scramble.o: scramble.cpp scramble.h
elf_parser.o: elf_parser.cpp elf_parser.hpp elf.h
binhack.o: binhack.cpp binhack.hpp
embedded_assets.o: embedded_assets.cpp embedded_assets.h
