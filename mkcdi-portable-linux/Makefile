CXX = g++
CC = gcc
CXXFLAGS = -std=c++17 -O2 -Wall -Wno-narrowing -Wno-unused-result -I. -Ithird_party/libisofs -Ithird_party/libisofs/libisofs
CFLAGS = -O2 -Wall -DHAVE_CONFIG_H -I. -Ithird_party/libisofs -Ithird_party/libisofs/libisofs
LDFLAGS =
LDLIBS = -lpthread

WIN_CXX = x86_64-w64-mingw32-g++
WIN_CC = x86_64-w64-mingw32-gcc
WIN_EXTRA = third_party/libisofs/winpatch/iconv.c

WIN_CXXFLAGS = -std=c++17 -O2 -Wall -Wno-narrowing -static -I. -Ithird_party/libisofs -Ithird_party/libisofs/libisofs -Ithird_party/libisofs/winpatch
WIN_CFLAGS = -O2 -Wall -Wno-overflow -DHAVE_CONFIG_H -I. -Ithird_party/libisofs -Ithird_party/libisofs/libisofs -Ithird_party/libisofs/winpatch
WIN_LDLIBS = -lpthread

ARM64_CXX = aarch64-linux-gnu-g++
ARM64_CC = aarch64-linux-gnu-gcc
ARM64_CXXFLAGS = -std=c++17 -O2 -Wall -Wno-narrowing -static -I. -Ithird_party/libisofs -Ithird_party/libisofs/libisofs
ARM64_CFLAGS = -O2 -Wall -DHAVE_CONFIG_H -I. -Ithird_party/libisofs -Ithird_party/libisofs/libisofs
ARM64_LDLIBS = -lpthread

SRCS = mkcdi_portable.cpp \
       libisofs_wrapper.cpp \
       hack4_lib.cpp \
       bincon.cpp \
       binhack32_lib.cpp \
       logoinsert_lib.cpp \
       cdibuilder_lib.cpp \
       scramble.cpp \
       elf_parser.cpp \
       binhack.cpp \
       embedded_assets.cpp

# libisofs C sources
LIBISOFS_DIR = third_party/libisofs/libisofs
LIBISOFS_SRCS = $(LIBISOFS_DIR)/aaip_0_2.c \
                $(LIBISOFS_DIR)/buffer.c \
                $(LIBISOFS_DIR)/builder.c \
                $(LIBISOFS_DIR)/data_source.c \
                $(LIBISOFS_DIR)/ecma119.c \
                $(LIBISOFS_DIR)/ecma119_tree.c \
                $(LIBISOFS_DIR)/eltorito.c \
                $(LIBISOFS_DIR)/filesrc.c \
                $(LIBISOFS_DIR)/filter.c \
                $(LIBISOFS_DIR)/find.c \
                $(LIBISOFS_DIR)/fs_image.c \
                $(LIBISOFS_DIR)/fs_local.c \
                $(LIBISOFS_DIR)/fsource.c \
                $(LIBISOFS_DIR)/hfsplus.c \
                $(LIBISOFS_DIR)/hfsplus_case.c \
                $(LIBISOFS_DIR)/hfsplus_classes.c \
                $(LIBISOFS_DIR)/hfsplus_decompose.c \
                $(LIBISOFS_DIR)/image.c \
                $(LIBISOFS_DIR)/iso1999.c \
                $(LIBISOFS_DIR)/joliet.c \
                $(LIBISOFS_DIR)/libiso_msgs.c \
                $(LIBISOFS_DIR)/make_isohybrid_mbr.c \
                $(LIBISOFS_DIR)/md5.c \
                $(LIBISOFS_DIR)/messages.c \
                $(LIBISOFS_DIR)/node.c \
                $(LIBISOFS_DIR)/rockridge.c \
                $(LIBISOFS_DIR)/rockridge_read.c \
                $(LIBISOFS_DIR)/stream.c \
                $(LIBISOFS_DIR)/system_area.c \
                $(LIBISOFS_DIR)/tree.c \
                $(LIBISOFS_DIR)/util.c \
                $(LIBISOFS_DIR)/util_htable.c \
                $(LIBISOFS_DIR)/util_rbtree.c \
                $(LIBISOFS_DIR)/zisofs_stubs.c

OBJS = $(SRCS:.cpp=.o)
LIBISOFS_OBJS = $(LIBISOFS_SRCS:.c=.o)

WIN_OBJS = $(SRCS:.cpp=_win.o)
WIN_LIBISOFS_OBJS = $(LIBISOFS_SRCS:.c=_win.o)

ARM64_OBJS = $(SRCS:.cpp=_arm64.o)
ARM64_LIBISOFS_OBJS = $(LIBISOFS_SRCS:.c=_arm64.o)

TARGET = mkcdi
WIN_TARGET = mkcdi.exe
ARM64_TARGET = mkcdi-arm64

.PHONY: all clean assets win arm64 static

all: assets $(TARGET)

static: CXXFLAGS += -static
static: assets $(TARGET)

win: assets $(WIN_TARGET)

arm64: assets $(ARM64_TARGET)

assets:
	python3 tools/embed_assets.py

$(TARGET): $(OBJS) $(LIBISOFS_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(ARM64_TARGET): $(ARM64_OBJS) $(ARM64_LIBISOFS_OBJS)
	$(ARM64_CXX) $(ARM64_CXXFLAGS) -o $@ $^ $(ARM64_LDLIBS)

$(WIN_TARGET): $(WIN_OBJS) $(WIN_LIBISOFS_OBJS) mkcdi_res.o $(WIN_EXTRA:.c=_win.o)
	$(WIN_CXX) $(WIN_CXXFLAGS) -o $@ $^ $(WIN_LDLIBS)

mkcdi_res.o: mkcdi.rc icon.ico
	x86_64-w64-mingw32-windres --use-temp-file mkcdi.rc mkcdi_res.o

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%_win.o: %.cpp
	$(WIN_CXX) $(WIN_CXXFLAGS) -c -o $@ $<

%_win.o: %.c
	$(WIN_CC) $(WIN_CFLAGS) -c -o $@ $<

%_arm64.o: %.cpp
	$(ARM64_CXX) $(ARM64_CXXFLAGS) -c -o $@ $<

%_arm64.o: %.c
	$(ARM64_CC) $(ARM64_CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(LIBISOFS_OBJS) $(WIN_OBJS) $(WIN_LIBISOFS_OBJS) $(ARM64_OBJS) $(ARM64_LIBISOFS_OBJS) mkcdi_res.o $(TARGET) $(WIN_TARGET) $(ARM64_TARGET)
	find third_party -name "*.o" -delete

distclean: clean
	rm -f embedded_assets.h embedded_assets.cpp

# Dependencies
mkcdi_portable.o: mkcdi_portable.cpp embedded_assets.h hack4_lib.hpp bincon.hpp binhack32_lib.hpp logoinsert_lib.hpp cdibuilder_lib.hpp libisofs_wrapper.hpp scramble.h elf_parser.hpp elf.h
libisofs_wrapper.o: libisofs_wrapper.cpp libisofs_wrapper.hpp
hack4_lib.o: hack4_lib.cpp hack4_lib.hpp
bincon.o: bincon.cpp bincon.hpp
binhack32_lib.o: binhack32_lib.cpp binhack32_lib.hpp binhack.hpp
logoinsert_lib.o: logoinsert_lib.cpp logoinsert_lib.hpp
cdibuilder_lib.o: cdibuilder_lib.cpp cdibuilder_lib.hpp
scramble.o: scramble.cpp scramble.h
elf_parser.o: elf_parser.cpp elf_parser.hpp elf.h
binhack.o: binhack.cpp binhack.hpp
embedded_assets.o: embedded_assets.cpp embedded_assets.h
