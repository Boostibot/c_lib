BUILD_DIR := build
HOST_COMP  := gcc
HOST_FLAGS := -g -ggdb -Wall -fmax-errors=10 -DTEST_RUNNER -Wlogical-op -Wno-conversion -Wno-sign-conversion -Wno-format-zero-length -Wno-format -Wno-sign-compare -Wno-format-zero-length -Wno-unknown-pragmas -Wno-unused-function -Wno-unused-local-typedefs -Wno-missing-braces
HOST_LINK  := -lm -rdynamic

ALL_SOURCES = $(shell find -L -regex '.*/.*\.\(c\|h\|cpp\|hpp\|cu\|cuh\)$ ')
DEPENDENCIES = $(ALL_SOURCES) Makefile
D := $(BUILD_DIR)

$(D)/main.out: $(DEPENDENCIES) 
	$(HOST_COMP) $(HOST_FLAGS) -x c tests/test_all.h -o $@ $(HOST_LINK)

clean:
	rm -f $(D)/*.o

$(info $(shell mkdir -p $(D)))