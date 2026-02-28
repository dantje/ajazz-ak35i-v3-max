BUILD_DIR  := build
PREFIX     ?= /usr/local
BINDIR     := $(DESTDIR)$(PREFIX)/bin
UDEVDIR    := /etc/udev/rules.d

.PHONY: all clean install uninstall package test gtk

all: $(BUILD_DIR)/ajazz

$(BUILD_DIR)/ajazz: $(BUILD_DIR)/Makefile FORCE
	cmake --build $(BUILD_DIR) --parallel

$(BUILD_DIR)/Makefile: CMakeLists.txt
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release

install: $(BUILD_DIR)/ajazz
	install -Dm755 $(BUILD_DIR)/ajazz $(BINDIR)/ajazz
	sudo install -Dm644 udev/99-ajazz-ak35i.rules $(UDEVDIR)/99-ajazz-ak35i.rules
	sudo udevadm control --reload-rules
	sudo udevadm trigger

uninstall:
	rm -f $(BINDIR)/ajazz
	sudo rm -f $(UDEVDIR)/99-ajazz-ak35i.rules
	sudo udevadm control --reload-rules

package: $(BUILD_DIR)/ajazz
	cmake --build $(BUILD_DIR) --target package

gtk: $(BUILD_DIR)/Makefile FORCE
	cmake --build $(BUILD_DIR) --target ajazz-gtk --parallel

test: $(BUILD_DIR)/ajazz
	bash tests/test_args.sh $(BUILD_DIR)/ajazz

clean:
	rm -rf $(BUILD_DIR)

FORCE:
