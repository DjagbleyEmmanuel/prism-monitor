CC ?= gcc
PKG_CONFIG ?= pkg-config

APP := prism-monitor
BUILD := build
BIN := bin

PKGS := gtk4
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PKGS))

CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Isrc $(PKG_CFLAGS)
LDFLAGS +=
LDLIBS += $(PKG_LIBS) -lm -pthread

CORE_SRC := $(wildcard src/core/*.c)
SYS_SRC := $(wildcard src/sys/*.c)
UI_SRC := $(wildcard src/ui/*.c)
MAIN_SRC := $(wildcard src/*.c)
APP_SRC := $(MAIN_SRC) $(CORE_SRC) $(SYS_SRC) $(UI_SRC)
APP_OBJ := $(APP_SRC:%.c=$(BUILD)/%.o)
APP_DEP := $(APP_OBJ:.o=.d)

VERSION := $(shell sed -n 's/^\#define PRISM_VERSION "\(.*\)"/\1/p' src/core/common.h)
ICON_NAME := io.prism.Monitor
DEB_ARCH := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)
DEB_NAME := $(APP)_$(VERSION)_$(DEB_ARCH)
DEB_ROOT := $(BUILD)/$(DEB_NAME)
DEB_MAINTAINER := Priderock-Elohimmanuel <priderock-elohimmanuel@users.noreply.github.com>
DEB_DEPENDS := libgtk-4-1 (>= 4.10.0), libpango-1.0-0, libcairo2, libc6 (>= 2.34)
DIST := dist

.PHONY: all run debug clean test install uninstall deb i18n

all: $(BIN)/$(APP)

i18n:
	xgettext --keyword=_ --from-code=UTF-8 -f po/POTFILES.in -o po/prism-monitor.pot
	@for f in po/*.po; do msgmerge --update $$f po/prism-monitor.pot; done
	@echo "Updated po/prism-monitor.pot"

$(BIN)/$(APP): $(APP_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(APP_OBJ) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

run: $(BIN)/$(APP)
	./$(BIN)/$(APP)

debug:
	$(MAKE) CFLAGS="-O0 -g3 -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Isrc $(PKG_CFLAGS)" all

test: $(BUILD)/tests/test_core
	./$(BUILD)/tests/test_core

$(BUILD)/tests/test_core: tests/test_core.c src/core/parse.c src/core/format.c src/core/history.c src/core/alerts.c src/core/export.c src/core/plugins.c src/sys/proc.c src/sys/disk.c src/sys/net.c src/core/common.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

install: $(BIN)/$(APP)
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m755 $(BIN)/$(APP) "$(DESTDIR)$(PREFIX)/bin/$(APP)"
	install -d "$(DESTDIR)$(PREFIX)/share/applications"
	install -m644 data/$(APP).desktop "$(DESTDIR)$(PREFIX)/share/applications/"
	install -d "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps"
	install -m644 assets/$(ICON_NAME).svg "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/$(ICON_NAME).svg"
	install -d "$(DESTDIR)$(PREFIX)/share/metainfo"
	install -m644 data/$(ICON_NAME).metainfo.xml "$(DESTDIR)$(PREFIX)/share/metainfo/"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/$(APP)"
	rm -f "$(DESTDIR)$(PREFIX)/share/applications/$(APP).desktop"
	rm -f "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/$(ICON_NAME).svg"
	rm -f "$(DESTDIR)$(PREFIX)/share/metainfo/$(ICON_NAME).metainfo.xml"

deb: $(BIN)/$(APP)
	rm -rf "$(DEB_ROOT)" "$(DIST)"
	$(MAKE) install DESTDIR="$(CURDIR)/$(DEB_ROOT)" PREFIX=/usr
	install -d "$(DEB_ROOT)/DEBIAN"
	install -d "$(DEB_ROOT)/usr/share/doc/$(APP)"
	install -m644 LICENSE "$(DEB_ROOT)/usr/share/doc/$(APP)/copyright"
	size=$$(du -sk "$(DEB_ROOT)/usr" | cut -f1); \
	printf '%s\n' \
	  'Package: $(APP)' \
	  'Version: $(VERSION)' \
	  'Section: utils' \
	  'Priority: optional' \
	  'Architecture: $(DEB_ARCH)' \
	  'Maintainer: $(DEB_MAINTAINER)' \
	  "Installed-Size: $$size" \
	  'Depends: $(DEB_DEPENDS)' \
	  'Description: fast native system monitor for Linux' \
	  ' Prism Monitor renders live CPU, memory, GPU, storage, network, process,' \
	  ' sensor and power telemetry with custom Cairo charts on GTK 4.' \
	  > "$(DEB_ROOT)/DEBIAN/control"
	install -d "$(DIST)"
	dpkg-deb --root-owner-group --build "$(DEB_ROOT)" "$(DIST)/$(DEB_NAME).deb"
	@echo "Built: $(DIST)/$(DEB_NAME).deb"

clean:
	rm -rf $(BUILD) $(BIN) $(DIST)

PREFIX ?= /usr/local

-include $(APP_DEP)
