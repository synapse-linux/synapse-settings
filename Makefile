# SPDX-License-Identifier: GPL-3.0-or-later
CC ?= cc
CXX ?= c++
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
LIBDIR ?= $(PREFIX)/lib
QMLDIR ?= $(LIBDIR)/qt6/qml
BUILD_DIR ?= build
VERSION := 1.0.0-alpha.1

BASE_CPPFLAGS = -D_FORTIFY_SOURCE=3 -DSYNAPSE_SETTINGS_VERSION='"$(VERSION)"'
BASE_CFLAGS = -O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
	-fstack-protector-strong -fPIE -march=x86-64 -mtune=generic
BASE_CXXFLAGS = -O2 -g -std=c++17 -Wall -Wextra -Wpedantic -Werror \
	-fstack-protector-strong -fPIC -march=x86-64 -mtune=generic
BASE_LDFLAGS = -Wl,-z,relro,-z,now -pie
REPRO_FLAGS = -ffile-prefix-map=$(BUILD_DIR)=build \
	-fdebug-prefix-map=$(BUILD_DIR)=build -fmacro-prefix-map=$(BUILD_DIR)=build \
	-ffile-prefix-map=$(CURDIR)=. -fdebug-prefix-map=$(CURDIR)=. \
	-fmacro-prefix-map=$(CURDIR)=. -ffile-prefix-map=$(abspath $(BUILD_DIR))=build \
	-fdebug-prefix-map=$(abspath $(BUILD_DIR))=build \
	-fmacro-prefix-map=$(abspath $(BUILD_DIR))=build

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
LUPDATE6 ?= lupdate6
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

AUDIO_SOURCES := src/audio.c src/audio_policy.c src/audio_broker_status.c
SETTINGS_AUDIO_SOURCES := src/audio_goxlr_status.c
SETTINGS_AUDIO_CPPFLAGS := -DSYNAPSE_SETTINGS_WITH_GOXLR_STATUS=1 \
	-DSYNAPSE_SETTINGS_WITH_PROFILE_PORT=1
AUDIO_INTERNALS := src/audio_control.inc src/audio_profile_port.inc
ISA_NOTE_SOURCE := src/x86_64_baseline_note.c
ISA_NOTE_SCRIPT := src/x86_64_baseline_note.ld
ISA_NOTE_LDFLAGS := -Wl,-T,$(abspath $(ISA_NOTE_SCRIPT))
SOURCES := src/synapse_settings.c $(AUDIO_SOURCES) \
	$(SETTINGS_AUDIO_SOURCES) $(ISA_NOTE_SOURCE)
BROKER_SOURCES := src/audio_broker.c $(AUDIO_SOURCES) $(ISA_NOTE_SOURCE)
BINARY := $(BUILD_DIR)/synapse-settings
TEST_BINARY := $(BUILD_DIR)/synapse-settings-test
BROKER_BINARY := $(BUILD_DIR)/synapse-audio-route-broker
BROKER_TEST_BINARY := $(BUILD_DIR)/synapse-audio-route-broker-test
GUI_BINARY := $(BUILD_DIR)/synapse-settings-gui
GUI_SOURCES := gui/main.cpp gui/audio_adapter.cpp gui/localization.cpp
GUI_HEADERS := gui/audio_adapter.h gui/localization.h
GUI_ISA_NOTE_OBJECT := $(BUILD_DIR)/x86_64_baseline_note.o
GUI_MOC := $(BUILD_DIR)/moc_audio_adapter.cpp $(BUILD_DIR)/moc_localization.cpp
GUI_QML := gui/qml/Main.qml gui/qml/AudioSettings.qml \
	gui/qml/AudioSettingsSection.qml gui/qml/AudioShellHost.qml
GUI_LOCALES := ar as ast az az_AZ be bg bn ca ca@valencia cs_CZ da de el \
	en_US en_GB eo es es_AR es_MX et eu fa fi_FI fr fur gl he hi hr hu \
	ia id is it_IT ja ka ko lt ml mr nb nl oc pl pt_BR pt_PT ro ru si sk \
	sl sq sr sr@latin sv tg th tr_TR uk uz vi zh_CN zh_TW
GUI_COMPLETE_LOCALES := en_US it_IT
GUI_FALLBACK_LOCALES := $(filter-out $(GUI_COMPLETE_LOCALES),$(GUI_LOCALES))
GUI_COMPLETE_QM := $(addprefix $(BUILD_DIR)/i18n/synapse-settings_,$(addsuffix .qm,$(GUI_COMPLETE_LOCALES)))
GUI_FALLBACK_QM := $(addprefix $(BUILD_DIR)/i18n/synapse-settings_,$(addsuffix .qm,$(GUI_FALLBACK_LOCALES)))
GUI_QM := $(GUI_COMPLETE_QM) $(GUI_FALLBACK_QM)
GUI_QRC_FILE := $(BUILD_DIR)/resources.qrc
GUI_RCC := $(BUILD_DIR)/qrc_resources.cpp
GUI_PLUGIN := $(BUILD_DIR)/libsynapse_settings_audio_qml.so
GUI_PLUGIN_TEST := $(BUILD_DIR)/libsynapse_settings_audio_qml_test.so
GUI_PLUGIN_SOURCES := gui/audio_qml_plugin.cpp gui/audio_adapter.cpp \
	gui/localization.cpp gui/x86_64_baseline_note.cpp
GUI_PLUGIN_HEADERS := gui/audio_qml_plugin.h gui/audio_adapter.h \
	gui/localization.h
GUI_PLUGIN_MOC := $(BUILD_DIR)/moc_audio_qml_plugin.cpp
GUI_PLUGIN_QRC_FILE := $(BUILD_DIR)/audio_plugin_resources.qrc
GUI_PLUGIN_RCC := $(BUILD_DIR)/qrc_audio_plugin_resources.cpp
GUI_PLUGIN_LDFLAGS := -shared -Wl,-z,relro,-z,now $(ISA_NOTE_LDFLAGS)
GUI_MODULE_REL := Synapse/Settings/Audio
GUI_MODULE_ROOT := $(BUILD_DIR)/qml
GUI_MODULE_DIR := $(GUI_MODULE_ROOT)/$(GUI_MODULE_REL)
GUI_MODULE_STAMP := $(GUI_MODULE_DIR)/.staged
GUI_TEST_MODULE_ROOT := $(BUILD_DIR)/test-qml
GUI_TEST_MODULE_DIR := $(GUI_TEST_MODULE_ROOT)/$(GUI_MODULE_REL)
GUI_TEST_MODULE_STAMP := $(GUI_TEST_MODULE_DIR)/.staged
GUI_MODULE_QMLTYPES := gui/qml-module/synapse-settings-audio.qmltypes
GUI_TEST_MOC := $(BUILD_DIR)/test_audio_adapter.moc
GUI_TEST_BINARY := $(BUILD_DIR)/test-audio-adapter
ALL_TARGETS := $(BINARY) $(BROKER_BINARY)
ifeq ($(GUI_ENABLED),1)
ALL_TARGETS += $(GUI_BINARY) $(GUI_PLUGIN)
endif

.PHONY: all cli broker gui qml-plugin qml-module clean test test-gui test-all install
all: $(ALL_TARGETS)
cli: $(BINARY)
broker: $(BROKER_BINARY)

gui:
ifeq ($(GUI_ENABLED),1)
	$(MAKE) --no-print-directory "$(GUI_BINARY)" "$(GUI_PLUGIN)" \
		"$(GUI_MODULE_STAMP)" BUILD_GUI=1
else
	@echo "synapse-settings: Qt 6 GUI SDK unavailable" >&2; exit 1
endif

qml-plugin:
ifeq ($(GUI_ENABLED),1)
	$(MAKE) --no-print-directory "$(GUI_PLUGIN)" BUILD_GUI=1
else
	@echo "synapse-settings: Qt 6 GUI SDK unavailable" >&2; exit 1
endif

qml-module:
ifeq ($(GUI_ENABLED),1)
	$(MAKE) --no-print-directory "$(GUI_MODULE_STAMP)" BUILD_GUI=1
else
	@echo "synapse-settings: Qt 6 GUI SDK unavailable" >&2; exit 1
endif

$(BUILD_DIR):
	install -d -m 0755 "$@"

$(GUI_ISA_NOTE_OBJECT): $(ISA_NOTE_SOURCE) | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) \
		$(REPRO_FLAGS) -c -o "$@" "$<"

$(BINARY): $(SOURCES) $(AUDIO_INTERNALS) src/settings_internal.h \
		$(ISA_NOTE_SCRIPT) | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(SETTINGS_AUDIO_CPPFLAGS) $(CPPFLAGS) \
		$(BASE_CFLAGS) $(CFLAGS) $(REPRO_FLAGS) $(CORE_CFLAGS) \
		$(JSON_C_CFLAGS) -o "$@" $(SOURCES) $(BASE_LDFLAGS) \
		$(ISA_NOTE_LDFLAGS) $(LDFLAGS) $(CORE_LIBS) $(JSON_C_LIBS) \
		$(LDLIBS)

$(TEST_BINARY): $(SOURCES) $(AUDIO_INTERNALS) src/settings_internal.h \
		$(ISA_NOTE_SCRIPT) | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(SETTINGS_AUDIO_CPPFLAGS) $(CPPFLAGS) \
		-DSYNAPSE_SETTINGS_TEST_HOOKS=1 $(BASE_CFLAGS) $(CFLAGS) \
		$(REPRO_FLAGS) $(CORE_CFLAGS) $(JSON_C_CFLAGS) -o "$@" \
		$(SOURCES) $(BASE_LDFLAGS) $(ISA_NOTE_LDFLAGS) $(LDFLAGS) \
		$(CORE_LIBS) $(JSON_C_LIBS) $(LDLIBS)

$(BROKER_BINARY): $(BROKER_SOURCES) $(AUDIO_INTERNALS) \
		src/settings_internal.h $(ISA_NOTE_SCRIPT) | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) \
		$(REPRO_FLAGS) $(CORE_CFLAGS) $(JSON_C_CFLAGS) -o "$@" \
		$(BROKER_SOURCES) $(BASE_LDFLAGS) $(ISA_NOTE_LDFLAGS) \
		$(LDFLAGS) $(CORE_LIBS) $(JSON_C_LIBS) $(LDLIBS)

$(BROKER_TEST_BINARY): $(BROKER_SOURCES) $(AUDIO_INTERNALS) \
		src/settings_internal.h $(ISA_NOTE_SCRIPT) | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) -DSYNAPSE_SETTINGS_TEST_HOOKS=1 \
		$(BASE_CFLAGS) $(CFLAGS) $(REPRO_FLAGS) $(CORE_CFLAGS) \
		$(JSON_C_CFLAGS) -o "$@" $(BROKER_SOURCES) $(BASE_LDFLAGS) \
		$(ISA_NOTE_LDFLAGS) $(LDFLAGS) $(CORE_LIBS) $(JSON_C_LIBS) \
		$(LDLIBS)

$(BUILD_DIR)/moc_audio_adapter.cpp: gui/audio_adapter.h | $(BUILD_DIR)
	"$(MOC6)" -f audio_adapter.h -o "$@" "$<"

$(BUILD_DIR)/moc_localization.cpp: gui/localization.h | $(BUILD_DIR)
	"$(MOC6)" -f localization.h -o "$@" "$<"

$(GUI_PLUGIN_MOC): gui/audio_qml_plugin.h | $(BUILD_DIR)
	"$(MOC6)" -f audio_qml_plugin.h -o "$@" "$<"

$(GUI_COMPLETE_QM): $(BUILD_DIR)/i18n/%.qm: gui/i18n/%.ts | $(BUILD_DIR)
	install -d -m 0755 "$(BUILD_DIR)/i18n"
	"$(LRELEASE6)" -silent -fail-on-unfinished -fail-on-invalid "$<" -qm "$@"

$(GUI_FALLBACK_QM): $(BUILD_DIR)/i18n/%.qm: gui/i18n/%.ts | $(BUILD_DIR)
	install -d -m 0755 "$(BUILD_DIR)/i18n"
	"$(LRELEASE6)" -silent -fail-on-invalid "$<" -qm "$@"

$(GUI_QRC_FILE): $(GUI_QML) $(GUI_QM) Makefile | $(BUILD_DIR)
	printf '%s\n' '<!DOCTYPE RCC><RCC version="1.0"><qresource prefix="/">' \
		'<file alias="qml/Main.qml">$(abspath gui/qml/Main.qml)</file>' \
		'<file alias="qml/AudioSettings.qml">$(abspath gui/qml/AudioSettings.qml)</file>' \
		'<file alias="qml/AudioSettingsSection.qml">$(abspath gui/qml/AudioSettingsSection.qml)</file>' \
		'<file alias="qml/AudioShellHost.qml">$(abspath gui/qml/AudioShellHost.qml)</file>' \
		>"$@"
	for locale in $(GUI_LOCALES); do \
		printf '%s\n' "<file alias=\"i18n/synapse-settings_$${locale}.qm\">$(abspath $(BUILD_DIR))/i18n/synapse-settings_$${locale}.qm</file>"; \
	done >>"$@"
	printf '%s\n' '</qresource></RCC>' >>"$@"

$(GUI_RCC): $(GUI_QRC_FILE) | $(BUILD_DIR)
	"$(RCC6)" -o "$@" "$<"

$(GUI_PLUGIN_QRC_FILE): $(GUI_QM) Makefile | $(BUILD_DIR)
	printf '%s\n' '<!DOCTYPE RCC><RCC version="1.0"><qresource prefix="/">' >"$@"
	for locale in $(GUI_LOCALES); do \
		printf '%s\n' "<file alias=\"i18n/synapse-settings_$${locale}.qm\">$(abspath $(BUILD_DIR))/i18n/synapse-settings_$${locale}.qm</file>"; \
	done >>"$@"
	printf '%s\n' '</qresource></RCC>' >>"$@"

$(GUI_PLUGIN_RCC): $(GUI_PLUGIN_QRC_FILE) | $(BUILD_DIR)
	"$(RCC6)" -name synapse_settings_audio_qml -o "$@" "$<"

$(GUI_BINARY): $(GUI_SOURCES) $(GUI_HEADERS) $(GUI_MOC) $(GUI_RCC) \
		$(GUI_ISA_NOTE_OBJECT) $(BINARY) $(ISA_NOTE_SCRIPT) | $(BUILD_DIR)
	$(CXX) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CXXFLAGS) $(CXXFLAGS) \
		$(REPRO_FLAGS) $(GUI_CXX_COMPAT) -Igui \
		$$( $(PKG_CONFIG) --cflags $(GUI_PACKAGES) ) -o "$@" \
		$(GUI_SOURCES) $(GUI_MOC) $(GUI_RCC) $(GUI_ISA_NOTE_OBJECT) \
		$(BASE_LDFLAGS) $(ISA_NOTE_LDFLAGS) $(LDFLAGS) \
		$$( $(PKG_CONFIG) --libs $(GUI_PACKAGES) ) $(LDLIBS)

$(GUI_PLUGIN): $(GUI_PLUGIN_SOURCES) $(GUI_PLUGIN_HEADERS) \
		$(GUI_MOC) $(GUI_PLUGIN_MOC) $(GUI_PLUGIN_RCC) \
		$(ISA_NOTE_SCRIPT) | $(BUILD_DIR)
	$(CXX) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CXXFLAGS) $(CXXFLAGS) \
		$(REPRO_FLAGS) $(GUI_CXX_COMPAT) -Igui \
		$$( $(PKG_CONFIG) --cflags $(GUI_PACKAGES) ) -o "$@" \
		$(GUI_PLUGIN_SOURCES) $(GUI_MOC) $(GUI_PLUGIN_MOC) \
		$(GUI_PLUGIN_RCC) $(GUI_PLUGIN_LDFLAGS) $(LDFLAGS) \
		$$( $(PKG_CONFIG) --libs $(GUI_PACKAGES) ) $(LDLIBS)

$(GUI_PLUGIN_TEST): $(GUI_PLUGIN_SOURCES) $(GUI_PLUGIN_HEADERS) \
		$(GUI_MOC) $(GUI_PLUGIN_MOC) $(GUI_PLUGIN_RCC) \
		$(ISA_NOTE_SCRIPT) | $(BUILD_DIR)
	$(CXX) $(BASE_CPPFLAGS) $(CPPFLAGS) \
		-DSYNAPSE_SETTINGS_GUI_TEST_HOOKS=1 \
		$(BASE_CXXFLAGS) $(CXXFLAGS) $(REPRO_FLAGS) $(GUI_CXX_COMPAT) \
		-Igui $$( $(PKG_CONFIG) --cflags $(GUI_PACKAGES) ) -o "$@" \
		$(GUI_PLUGIN_SOURCES) $(GUI_MOC) $(GUI_PLUGIN_MOC) \
		$(GUI_PLUGIN_RCC) $(GUI_PLUGIN_LDFLAGS) $(LDFLAGS) \
		$$( $(PKG_CONFIG) --libs $(GUI_PACKAGES) ) $(LDLIBS)

$(GUI_MODULE_STAMP): $(GUI_PLUGIN) $(GUI_QML) \
		gui/qml-module/qmldir $(GUI_MODULE_QMLTYPES)
	rm -rf "$(GUI_MODULE_DIR)"
	install -d -m 0755 "$(GUI_MODULE_DIR)"
	install -m 0755 "$(GUI_PLUGIN)" \
		"$(GUI_MODULE_DIR)/libsynapse_settings_audio_qml.so"
	install -m 0644 gui/qml-module/qmldir $(GUI_MODULE_QMLTYPES) \
		gui/qml/AudioSettings.qml gui/qml/AudioSettingsSection.qml \
		gui/qml/AudioShellHost.qml "$(GUI_MODULE_DIR)/"
	touch "$@"

$(GUI_TEST_MODULE_STAMP): $(GUI_PLUGIN_TEST) $(GUI_QML) \
		gui/qml-module/qmldir $(GUI_MODULE_QMLTYPES)
	rm -rf "$(GUI_TEST_MODULE_DIR)"
	install -d -m 0755 "$(GUI_TEST_MODULE_DIR)"
	install -m 0755 "$(GUI_PLUGIN_TEST)" \
		"$(GUI_TEST_MODULE_DIR)/libsynapse_settings_audio_qml.so"
	install -m 0644 gui/qml-module/qmldir $(GUI_MODULE_QMLTYPES) \
		gui/qml/AudioSettings.qml gui/qml/AudioSettingsSection.qml \
		gui/qml/AudioShellHost.qml "$(GUI_TEST_MODULE_DIR)/"
	touch "$@"

$(GUI_TEST_MOC): tests/test_audio_adapter.cpp | $(BUILD_DIR)
	"$(MOC6)" -o "$@" "$<"

$(GUI_TEST_BINARY): tests/test_audio_adapter.cpp gui/audio_adapter.cpp \
		gui/audio_adapter.h gui/localization.cpp gui/localization.h \
		$(BUILD_DIR)/moc_audio_adapter.cpp $(BUILD_DIR)/moc_localization.cpp \
		$(GUI_PLUGIN_RCC) $(GUI_TEST_MOC) $(TEST_BINARY) | $(BUILD_DIR)
	$(CXX) $(BASE_CPPFLAGS) $(CPPFLAGS) -DSYNAPSE_SETTINGS_GUI_TEST_HOOKS=1 \
		$(BASE_CXXFLAGS) $(CXXFLAGS) $(REPRO_FLAGS) $(GUI_CXX_COMPAT) \
		$(GUI_TEST_COMPAT) -Igui -I$(BUILD_DIR) \
		$$( $(PKG_CONFIG) --cflags $(GUI_TEST_PACKAGES) ) -o "$@" \
		tests/test_audio_adapter.cpp gui/audio_adapter.cpp gui/localization.cpp \
		$(BUILD_DIR)/moc_audio_adapter.cpp $(BUILD_DIR)/moc_localization.cpp \
		$(GUI_PLUGIN_RCC) $(BASE_LDFLAGS) $(LDFLAGS) \
		$$( $(PKG_CONFIG) --libs $(GUI_TEST_PACKAGES) ) $(LDLIBS)

test: $(TEST_BINARY) $(BROKER_TEST_BINARY)
	$(TEST_ENV) ./tests/run.sh "$(abspath $(TEST_BINARY))"
	$(TEST_ENV) ./tests/broker-run.sh "$(abspath $(TEST_BINARY))" \
		"$(abspath $(BROKER_TEST_BINARY))"

test-gui:
ifeq ($(GUI_ENABLED),1)
	LUPDATE6="$(LUPDATE6)" python3 ./tests/localization-run.py
	$(MAKE) --no-print-directory "$(GUI_BINARY)" "$(GUI_TEST_BINARY)" \
		"$(GUI_MODULE_STAMP)" "$(GUI_TEST_MODULE_STAMP)" BUILD_GUI=1
	"$(QMLLINT)" -I "$(GUI_MODULE_ROOT)" gui/qml/Main.qml \
		gui/qml/AudioSettings.qml gui/qml/AudioSettingsSection.qml \
		tests/qml/tst_settings_sections.qml
	"$(QMLLINT)" -I "$(GUI_MODULE_ROOT)" "$(GUI_MODULE_DIR)"/*.qml \
		tests/qml/tst_audio_module.qml
	QT_QPA_PLATFORM=offscreen "$(QMLTESTRUNNER)" \
		-input tests/qml/tst_settings_sections.qml
	QT_QPA_PLATFORM=offscreen SYNAPSE_SETTINGS_TEST_BACKEND="$(abspath $(TEST_BINARY))" \
		$(TEST_ENV) "$(GUI_TEST_BINARY)"
	$(TEST_ENV) ./tests/qml-module-run.sh \
		"$(abspath $(QMLTESTRUNNER))" "$(abspath $(GUI_TEST_MODULE_ROOT))" \
		"$(abspath $(TEST_BINARY))"
	./tests/qml-module-boundary.sh "$(GUI_MODULE_DIR)"
	$(TEST_ENV) ./tests/gui-run.sh "$(abspath $(GUI_BINARY))" \
		"$(abspath $(TEST_BINARY))"
else
	@echo "synapse-settings: Qt 6 GUI SDK unavailable" >&2; exit 1
endif

test-all: all test test-gui
	./tests/elf-isa-boundary.sh "$(BINARY)" "$(BROKER_BINARY)" \
		"$(GUI_BINARY)" "$(GUI_PLUGIN)"
	./tests/audio-goxlr-production-boundary.sh "$(BINARY)" "$(TEST_BINARY)" \
		"$(BROKER_BINARY)"
	./tests/audio-profile-port-production-boundary.sh "$(BINARY)" \
		"$(TEST_BINARY)" "$(BROKER_BINARY)"

install: $(ALL_TARGETS)
	install -D -m 0755 "$(BINARY)" "$(DESTDIR)$(BINDIR)/synapse-settings"
	install -D -m 0755 "$(BROKER_BINARY)" \
		"$(DESTDIR)$(BINDIR)/synapse-audio-route-broker"
	install -D -m 0644 data/synapse-audio-route-broker.service \
		"$(DESTDIR)$(LIBDIR)/systemd/user/synapse-audio-route-broker.service"
ifeq ($(GUI_ENABLED),1)
	install -D -m 0755 "$(GUI_BINARY)" \
		"$(DESTDIR)$(BINDIR)/synapse-settings-gui"
	install -D -m 0755 "$(GUI_PLUGIN)" \
		"$(DESTDIR)$(QMLDIR)/$(GUI_MODULE_REL)/libsynapse_settings_audio_qml.so"
	install -D -m 0644 gui/qml-module/qmldir \
		"$(DESTDIR)$(QMLDIR)/$(GUI_MODULE_REL)/qmldir"
	install -D -m 0644 "$(GUI_MODULE_QMLTYPES)" \
		"$(DESTDIR)$(QMLDIR)/$(GUI_MODULE_REL)/synapse-settings-audio.qmltypes"
	install -D -m 0644 gui/qml/AudioSettings.qml \
		"$(DESTDIR)$(QMLDIR)/$(GUI_MODULE_REL)/AudioSettings.qml"
	install -D -m 0644 gui/qml/AudioSettingsSection.qml \
		"$(DESTDIR)$(QMLDIR)/$(GUI_MODULE_REL)/AudioSettingsSection.qml"
	install -D -m 0644 gui/qml/AudioShellHost.qml \
		"$(DESTDIR)$(QMLDIR)/$(GUI_MODULE_REL)/AudioShellHost.qml"
	install -D -m 0644 data/org.synapse.Settings.desktop \
		"$(DESTDIR)$(DATADIR)/applications/org.synapse.Settings.desktop"
endif
	install -D -m 0644 LICENSE \
		"$(DESTDIR)$(DATADIR)/licenses/synapse-settings/LICENSE"

clean:
	rm -rf "$(BUILD_DIR)"
