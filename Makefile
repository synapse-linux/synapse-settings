# SPDX-License-Identifier: GPL-3.0-or-later
CC ?= cc
CXX ?= c++
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
LIBDIR ?= $(PREFIX)/lib
BUILD_DIR ?= build
VERSION := 0.4.0-alpha.1

BASE_CPPFLAGS = -D_FORTIFY_SOURCE=3 -DSYNAPSE_SETTINGS_VERSION='"$(VERSION)"'
BASE_CFLAGS = -O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
	-fstack-protector-strong -fPIE -march=x86-64 -mtune=generic
BASE_CXXFLAGS = -O2 -g -std=c++17 -Wall -Wextra -Wpedantic -Werror \
	-fstack-protector-strong -fPIC -march=x86-64 -mtune=generic
BASE_LDFLAGS = -Wl,-z,relro,-z,now -pie
REPRO_FLAGS = -ffile-prefix-map=$(abspath $(BUILD_DIR))=build \
	-fdebug-prefix-map=$(abspath $(BUILD_DIR))=build \
	-fmacro-prefix-map=$(abspath $(BUILD_DIR))=build \
	-ffile-prefix-map=$(CURDIR)=. -fdebug-prefix-map=$(CURDIR)=. \
	-fmacro-prefix-map=$(CURDIR)=.

CPPFLAGS ?=
CFLAGS ?=
CXXFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
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

BUILD_GUI ?= auto
QMAKE6 ?= qmake6
QT6_LIBEXECS := $(shell $(QMAKE6) -query QT_HOST_LIBEXECS 2>/dev/null)
MOC6 ?= $(QT6_LIBEXECS)/moc
RCC6 ?= $(QT6_LIBEXECS)/rcc
LRELEASE6 ?= lrelease6
QMLLINT ?= /usr/lib/qt6/bin/qmllint
QMLTESTRUNNER ?= /usr/lib/qt6/bin/qmltestrunner
GUI_PACKAGES := Qt6Core Qt6Gui Qt6Qml Qt6Quick Qt6QuickControls2 Qt6Widgets
GUI_TEST_PACKAGES := Qt6Core Qt6Gui Qt6Test Qt6Widgets
GUI_CXX_COMPAT := $(if $(findstring clang,$(shell $(CXX) --version 2>/dev/null)),-Wno-c++26-extensions,) \
	$(if $(findstring GCC,$(shell $(CXX) --version 2>/dev/null)),-Wno-sfinae-incomplete,)
GUI_TEST_COMPAT := $(if $(findstring GCC,$(shell $(CXX) --version 2>/dev/null)),-Wno-sfinae-incomplete,)
GUI_DEPS_AVAILABLE := $(shell $(PKG_CONFIG) --exists $(GUI_PACKAGES) $(GUI_TEST_PACKAGES) \
	>/dev/null 2>&1 && test -x "$(MOC6)" && test -x "$(RCC6)" \
	&& command -v "$(LRELEASE6)" >/dev/null 2>&1 && echo 1 || echo 0)
ifeq ($(BUILD_GUI),1)
ifeq ($(GUI_DEPS_AVAILABLE),0)
$(error BUILD_GUI=1 requires Qt 6 Core/Gui/Qml/Quick/QuickControls2/Widgets/Test and moc/rcc/lrelease)
endif
GUI_ENABLED := 1
else ifeq ($(BUILD_GUI),0)
GUI_ENABLED := 0
else
GUI_ENABLED := $(GUI_DEPS_AVAILABLE)
endif

SOURCES := src/synapse_settings.c src/audio.c src/audio_policy.c
BROKER_SOURCES := src/audio_broker.c src/audio.c src/audio_policy.c
BINARY := $(BUILD_DIR)/synapse-settings
TEST_BINARY := $(BUILD_DIR)/synapse-settings-test
BROKER_BINARY := $(BUILD_DIR)/synapse-audio-route-broker
BROKER_TEST_BINARY := $(BUILD_DIR)/synapse-audio-route-broker-test
GUI_BINARY := $(BUILD_DIR)/synapse-settings-gui
GUI_SOURCES := gui/main.cpp gui/audio_adapter.cpp gui/localization.cpp
GUI_HEADERS := gui/audio_adapter.h gui/localization.h
GUI_MOC := $(BUILD_DIR)/moc_audio_adapter.cpp $(BUILD_DIR)/moc_localization.cpp
GUI_QML := gui/qml/Main.qml gui/qml/AudioSettings.qml \
	gui/qml/AudioSettingsSection.qml
GUI_QM := $(BUILD_DIR)/i18n/synapse-settings_en_US.qm \
	$(BUILD_DIR)/i18n/synapse-settings_it_IT.qm
GUI_QRC_FILE := $(BUILD_DIR)/resources.qrc
GUI_RCC := $(BUILD_DIR)/qrc_resources.cpp
GUI_TEST_MOC := $(BUILD_DIR)/test_audio_adapter.moc
GUI_TEST_BINARY := $(BUILD_DIR)/test-audio-adapter
ALL_TARGETS := $(BINARY) $(BROKER_BINARY)
ifeq ($(GUI_ENABLED),1)
ALL_TARGETS += $(GUI_BINARY)
endif

.PHONY: all cli broker gui clean test test-gui test-all install
all: $(ALL_TARGETS)
cli: $(BINARY)
broker: $(BROKER_BINARY)

gui:
ifeq ($(GUI_ENABLED),1)
	$(MAKE) --no-print-directory "$(GUI_BINARY)" BUILD_GUI=1
else
	@echo "synapse-settings: Qt 6 GUI SDK unavailable" >&2; exit 1
endif

$(BUILD_DIR):
	install -d -m 0755 "$@"

$(BINARY): $(SOURCES) src/settings_internal.h | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) \
		$(REPRO_FLAGS) $(CORE_CFLAGS) $(JSON_C_CFLAGS) -o "$@" \
		$(SOURCES) $(BASE_LDFLAGS) $(LDFLAGS) $(CORE_LIBS) \
		$(JSON_C_LIBS) $(LDLIBS)

$(TEST_BINARY): $(SOURCES) src/settings_internal.h | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) -DSYNAPSE_SETTINGS_TEST_HOOKS=1 \
		$(BASE_CFLAGS) $(CFLAGS) $(REPRO_FLAGS) $(CORE_CFLAGS) \
		$(JSON_C_CFLAGS) -o "$@" $(SOURCES) $(BASE_LDFLAGS) \
		$(LDFLAGS) $(CORE_LIBS) $(JSON_C_LIBS) $(LDLIBS)

$(BROKER_BINARY): $(BROKER_SOURCES) src/settings_internal.h | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) \
		$(REPRO_FLAGS) $(CORE_CFLAGS) $(JSON_C_CFLAGS) -o "$@" \
		$(BROKER_SOURCES) $(BASE_LDFLAGS) $(LDFLAGS) $(CORE_LIBS) \
		$(JSON_C_LIBS) $(LDLIBS)

$(BROKER_TEST_BINARY): $(BROKER_SOURCES) src/settings_internal.h | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) -DSYNAPSE_SETTINGS_TEST_HOOKS=1 \
		$(BASE_CFLAGS) $(CFLAGS) $(REPRO_FLAGS) $(CORE_CFLAGS) \
		$(JSON_C_CFLAGS) -o "$@" $(BROKER_SOURCES) $(BASE_LDFLAGS) \
		$(LDFLAGS) $(CORE_LIBS) $(JSON_C_LIBS) $(LDLIBS)

$(BUILD_DIR)/moc_audio_adapter.cpp: gui/audio_adapter.h | $(BUILD_DIR)
	"$(MOC6)" -f audio_adapter.h -o "$@" "$<"

$(BUILD_DIR)/moc_localization.cpp: gui/localization.h | $(BUILD_DIR)
	"$(MOC6)" -f localization.h -o "$@" "$<"

$(BUILD_DIR)/i18n/%.qm: gui/i18n/%.ts | $(BUILD_DIR)
	install -d -m 0755 "$(BUILD_DIR)/i18n"
	"$(LRELEASE6)" -silent -fail-on-unfinished -fail-on-invalid "$<" -qm "$@"

$(GUI_QRC_FILE): $(GUI_QML) $(GUI_QM) | $(BUILD_DIR)
	printf '%s\n' '<!DOCTYPE RCC><RCC version="1.0"><qresource prefix="/">' \
		'<file alias="qml/Main.qml">$(abspath gui/qml/Main.qml)</file>' \
		'<file alias="qml/AudioSettings.qml">$(abspath gui/qml/AudioSettings.qml)</file>' \
		'<file alias="qml/AudioSettingsSection.qml">$(abspath gui/qml/AudioSettingsSection.qml)</file>' \
		'<file alias="i18n/synapse-settings_en_US.qm">$(abspath $(BUILD_DIR)/i18n/synapse-settings_en_US.qm)</file>' \
		'<file alias="i18n/synapse-settings_it_IT.qm">$(abspath $(BUILD_DIR)/i18n/synapse-settings_it_IT.qm)</file>' \
		'</qresource></RCC>' >"$@"

$(GUI_RCC): $(GUI_QRC_FILE) | $(BUILD_DIR)
	"$(RCC6)" -o "$@" "$<"

$(GUI_BINARY): $(GUI_SOURCES) $(GUI_HEADERS) $(GUI_MOC) $(GUI_RCC) $(BINARY) | $(BUILD_DIR)
	$(CXX) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CXXFLAGS) $(CXXFLAGS) \
		$(REPRO_FLAGS) $(GUI_CXX_COMPAT) -Igui \
		$$( $(PKG_CONFIG) --cflags $(GUI_PACKAGES) ) -o "$@" \
		$(GUI_SOURCES) $(GUI_MOC) $(GUI_RCC) $(BASE_LDFLAGS) \
		$(LDFLAGS) $$( $(PKG_CONFIG) --libs $(GUI_PACKAGES) ) $(LDLIBS)

$(GUI_TEST_MOC): tests/test_audio_adapter.cpp | $(BUILD_DIR)
	"$(MOC6)" -o "$@" "$<"

$(GUI_TEST_BINARY): tests/test_audio_adapter.cpp gui/audio_adapter.cpp \
		gui/audio_adapter.h $(BUILD_DIR)/moc_audio_adapter.cpp \
		$(GUI_TEST_MOC) $(TEST_BINARY) | $(BUILD_DIR)
	$(CXX) $(BASE_CPPFLAGS) $(CPPFLAGS) -DSYNAPSE_SETTINGS_GUI_TEST_HOOKS=1 \
		$(BASE_CXXFLAGS) $(CXXFLAGS) $(REPRO_FLAGS) $(GUI_CXX_COMPAT) \
		$(GUI_TEST_COMPAT) -Igui -I$(BUILD_DIR) \
		$$( $(PKG_CONFIG) --cflags $(GUI_TEST_PACKAGES) ) -o "$@" \
		tests/test_audio_adapter.cpp gui/audio_adapter.cpp \
		$(BUILD_DIR)/moc_audio_adapter.cpp $(BASE_LDFLAGS) $(LDFLAGS) \
		$$( $(PKG_CONFIG) --libs $(GUI_TEST_PACKAGES) ) $(LDLIBS)

test: $(TEST_BINARY) $(BROKER_TEST_BINARY)
	$(TEST_ENV) ./tests/run.sh "$(abspath $(TEST_BINARY))"
	$(TEST_ENV) ./tests/broker-run.sh "$(abspath $(TEST_BINARY))" \
		"$(abspath $(BROKER_TEST_BINARY))"

test-gui:
ifeq ($(GUI_ENABLED),1)
	$(MAKE) --no-print-directory "$(GUI_BINARY)" "$(GUI_TEST_BINARY)" BUILD_GUI=1
	"$(QMLLINT)" gui/qml/*.qml tests/qml/*.qml
	QT_QPA_PLATFORM=offscreen "$(QMLTESTRUNNER)" -input tests/qml
	QT_QPA_PLATFORM=offscreen SYNAPSE_SETTINGS_TEST_BACKEND="$(abspath $(TEST_BINARY))" \
		$(TEST_ENV) "$(GUI_TEST_BINARY)"
	$(TEST_ENV) ./tests/gui-run.sh "$(abspath $(GUI_BINARY))" \
		"$(abspath $(TEST_BINARY))"
else
	@echo "synapse-settings: Qt 6 GUI SDK unavailable" >&2; exit 1
endif

test-all: test test-gui

install: $(ALL_TARGETS)
	install -D -m 0755 "$(BINARY)" "$(DESTDIR)$(BINDIR)/synapse-settings"
	install -D -m 0755 "$(BROKER_BINARY)" \
		"$(DESTDIR)$(BINDIR)/synapse-audio-route-broker"
	install -D -m 0644 data/synapse-audio-route-broker.service \
		"$(DESTDIR)$(LIBDIR)/systemd/user/synapse-audio-route-broker.service"
ifeq ($(GUI_ENABLED),1)
	install -D -m 0755 "$(GUI_BINARY)" \
		"$(DESTDIR)$(BINDIR)/synapse-settings-gui"
	install -D -m 0644 data/org.synapse.Settings.desktop \
		"$(DESTDIR)$(DATADIR)/applications/org.synapse.Settings.desktop"
endif
	install -D -m 0644 LICENSE \
		"$(DESTDIR)$(DATADIR)/licenses/synapse-settings/LICENSE"

clean:
	rm -rf "$(BUILD_DIR)"
