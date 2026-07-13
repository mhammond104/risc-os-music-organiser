CC := arm-unknown-riscos-gcc
CPPFLAGS := -Iinclude -I$(GCCSDK_INSTALL_ENV)/include
CFLAGS ?= -O2 -Wall -Wextra -pedantic -std=c11
LDFLAGS ?= -L$(GCCSDK_INSTALL_ENV)/lib
LDLIBS ?= -lpng16 -lz -lOSLib32

BUILD_DIR := build/riscos
TARGET := app/!ImgOrg/!RunImage

SOURCES := \
	src/main.c \
	src/application.c \
	src/browser_window.c \
	src/image_entry.c \
	src/image_list.c

OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

.PHONY: all clean package

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

package: all
	@echo "Built RISC OS application directory: app/!ImgOrg"

clean:
	rm -rf build
	rm -f $(TARGET)

-include $(DEPS)
