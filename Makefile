BUILD_DIR  := build
PREFIX     ?= /usr/local
BINDIR     := $(DESTDIR)$(PREFIX)/bin
MANDIR     := $(DESTDIR)$(PREFIX)/share/man/man1
UDEVDIR    := /etc/udev/rules.d

.PHONY: all clean install uninstall package test

all: $(BUILD_DIR)/ajazz

$(BUILD_DIR)/ajazz: $(BUILD_DIR)/Makefile FORCE
	cmake --build $(BUILD_DIR) --parallel

$(BUILD_DIR)/Makefile: CMakeLists.txt
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release

install: $(BUILD_DIR)/ajazz
	install -Dm755 $(BUILD_DIR)/ajazz $(BINDIR)/ajazz
	install -Dm644 ajazz.1 $(MANDIR)/ajazz.1
	sudo install -Dm644 udev/99-ajazz-ak35i.rules $(UDEVDIR)/99-ajazz-ak35i.rules
	sudo udevadm control --reload-rules
	sudo udevadm trigger

uninstall:
	rm -f $(BINDIR)/ajazz
	rm -f $(MANDIR)/ajazz.1
	sudo rm -f $(UDEVDIR)/99-ajazz-ak35i.rules
	sudo udevadm control --reload-rules

package: $(BUILD_DIR)/ajazz
	cmake --build $(BUILD_DIR) --target package

test: $(BUILD_DIR)/ajazz
	bash tests/test_args.sh $(BUILD_DIR)/ajazz

clean:
	rm -rf $(BUILD_DIR)

FORCE:
