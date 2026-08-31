# SPDX-License-Identifier: GPL-3.0-or-later
CC ?= cc
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
BUILD_DIR ?= build
VERSION := 0.2.0-alpha.1
CPPFLAGS ?= -D_FORTIFY_SOURCE=3
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -fstack-protector-strong -fPIE -march=x86-64 -mtune=generic
LDFLAGS ?= -Wl,-z,relro,-z,now -pie
PKG_CONFIG ?= pkg-config
CORE_ROOT ?=
ifeq ($(strip $(CORE_ROOT)),)
CORE_CFLAGS := $(shell $(PKG_CONFIG) --cflags synapse-core)
CORE_LIBS := $(shell $(PKG_CONFIG) --libs synapse-core)
TEST_ENV :=
else
CORE_CFLAGS := -I$(CORE_ROOT)$(PREFIX)/include
CORE_LIBS := -L$(CORE_ROOT)$(LIBDIR) -lsynapse-core
TEST_ENV := LD_LIBRARY_PATH=$(CORE_ROOT)$(LIBDIR)
endif
JSON_C_CFLAGS := $(shell $(PKG_CONFIG) --cflags json-c)
JSON_C_LIBS := $(shell $(PKG_CONFIG) --libs json-c)
SOURCE := src/synapse_settings.c src/audio.c src/graphics.c
BINARY := $(BUILD_DIR)/synapse-settings
TEST_BINARY := $(BUILD_DIR)/synapse-settings-test
QMLLINT ?= /usr/lib/qt6/bin/qmllint
QMLTESTRUNNER ?= /usr/lib/qt6/bin/qmltestrunner

.PHONY: all clean test test-gui test-all install
all: $(BINARY)

$(BUILD_DIR):
	install -d -m 0755 "$@"

$(BINARY): $(SOURCE) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -DSYNAPSE_SETTINGS_VERSION='"$(VERSION)"' $(CFLAGS) \
		$(CORE_CFLAGS) $(JSON_C_CFLAGS) -o "$@" $(SOURCE) $(LDFLAGS) $(CORE_LIBS) $(JSON_C_LIBS)

$(TEST_BINARY): $(SOURCE) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -DSYNAPSE_SETTINGS_TEST_HOOKS=1 \
		-DSYNAPSE_SETTINGS_VERSION='"$(VERSION)"' $(CFLAGS) \
		$(CORE_CFLAGS) $(JSON_C_CFLAGS) -o "$@" $(SOURCE) $(LDFLAGS) $(CORE_LIBS) $(JSON_C_LIBS)

test: $(TEST_BINARY)
	$(TEST_ENV) ./tests/run.sh "$(abspath $(TEST_BINARY))"

test-gui:
	test -x "$(QMLLINT)"
	test -x "$(QMLTESTRUNNER)"
	"$(QMLLINT)" gui/qml/*.qml tests/qml/*.qml
	QT_QPA_PLATFORM=offscreen "$(QMLTESTRUNNER)" -input tests/qml

test-all: test test-gui

install: $(BINARY)
	install -D -m 0755 "$(BINARY)" "$(DESTDIR)$(BINDIR)/synapse-settings"

clean:
	rm -rf "$(BUILD_DIR)"
