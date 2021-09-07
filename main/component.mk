#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

# Try to avoid warnings about deprecated declarations
CFLAGS += -Wno-deprecated-declarations
CXXFLAGS += -Wno-deprecated-declarations

#
# Derive numeric version macros to pass to both the C and C++ preprocessor
#
MY_IDF_VER := $(shell cd ${IDF_PATH} && git describe --always --tags --dirty)
MY_CFLAGS = $(shell echo ${MY_IDF_VER} | awk -f ${COMPONENT_PATH}/idf-version.awk)
CFLAGS += ${MY_CFLAGS}
CXXFLAGS += ${MY_CFLAGS}

COMPONENT_SRCDIRS += ../libraries \
	../libraries/arduinojson \
	../libraries/acmeclient \
	../components/littlefs \
	../components/esp_littlefs \
	../libraries/ftpclient/src

COMPONENT_ADD_INCLUDEDIRS := ../libraries \
	../libraries/arduinojson \
	../libraries/acmeclient \
	../libraries/littlefs \
	../components/esp_littlefs \
	../components/ftpclient/include \
	.

COMPONENT_EXTRA_CLEAN := build.c

$(COMPONENT_BUILD_DIR)/build.o:	build.c
	$(CC) -c -o $(COMPONENT_BUILD_DIR)/build.o build.c
	echo "ADDED CFLAGS" ${CFLAGS}

$(COMPONENT_LIBRARY):	$(COMPONENT_BUILD_DIR)/build.o

COMPONENT_EMBED_TXTFILES :=
