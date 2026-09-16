# SPDX-License-Identifier: MIT
CC ?= cc
CXX ?= c++
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
LIBDIR ?= $(PREFIX)/lib
QMLDIR ?= $(LIBDIR)/qt6/qml
BUILD_DIR ?= build
VERSION := 1.1.0-alpha.1

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
INSTALL_TEST_OUTPUT_DIR ?=
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

AUDIO_SOURCES := src/audio.c src/audio_control.c src/audio_policy.c src/audio_broker_status.c
SETTINGS_AUDIO_SOURCES := src/audio_goxlr_status.c src/audio_profile_port.c
SETTINGS_AUDIO_CPPFLAGS := -DSYNAPSE_SETTINGS_WITH_GOXLR_STATUS=1 \
	-DSYNAPSE_SETTINGS_WITH_PROFILE_PORT=1
AUDIO_INTERNALS := src/audio_private.h
AUDIO_UNITS_PROBE := $(BUILD_DIR)/audio-units-probe
AUDIO_UNITS_OUTPUT_DIR ?=
AUDIO_UNITS_EXPECTED ?=
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
GUI_FIXTURE_BINARY := $(BUILD_DIR)/synapse-settings-gui-test
GUI_AUDIO_BOUNDARY_SOURCES := gui/audio_contracts.cpp gui/audio_command.cpp
GUI_AUDIO_BOUNDARY_HEADERS := gui/audio_contracts_p.h gui/audio_command_p.h
GUI_SOURCES := gui/main.cpp gui/audio_adapter.cpp gui/localization.cpp $(GUI_AUDIO_BOUNDARY_SOURCES) \
	gui/theme.cpp
GUI_HEADERS := gui/audio_adapter.h gui/localization.h gui/theme.h $(GUI_AUDIO_BOUNDARY_HEADERS)
GUI_ISA_NOTE_OBJECT := $(BUILD_DIR)/x86_64_baseline_note.o
GUI_PLUGIN_COMMON_MOC := $(BUILD_DIR)/moc_audio_adapter.cpp \
	$(BUILD_DIR)/moc_localization.cpp
GUI_MOC := $(GUI_PLUGIN_COMMON_MOC) $(BUILD_DIR)/moc_theme.cpp
GUI_QML := gui/qml/Main.qml gui/qml/AudioSettings.qml \
	gui/qml/AudioSettingsSection.qml gui/qml/AudioDeviceDetail.qml \
	gui/qml/AudioDeviceList.qml gui/qml/AudioApplicationMixer.qml \
	gui/qml/AudioAdvancedGoXLR.qml gui/qml/AudioAdvancedRouting.qml \
	gui/qml/AudioPortList.qml gui/qml/AudioShellHost.qml \
	gui/qml/AudioPopupHost.qml gui/qml/AudioButton.qml \
	gui/qml/AudioCard.qml gui/qml/AudioComboBox.qml \
	gui/qml/AudioSectionHeading.qml gui/qml/AudioSlider.qml \
	gui/qml/AudioDialog.qml gui/qml/AudioSpinBox.qml
GUI_MODULE_QML := $(filter-out gui/qml/Main.qml,$(GUI_QML))
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
GUI_PLUGIN_SOURCES := gui/audio_qml_plugin.cpp gui/audio_adapter.cpp $(GUI_AUDIO_BOUNDARY_SOURCES) \
	gui/localization.cpp gui/x86_64_baseline_note.cpp
GUI_PLUGIN_HEADERS := gui/audio_qml_plugin.h gui/audio_adapter.h $(GUI_AUDIO_BOUNDARY_HEADERS) \
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
THEME_TEST_MOC := $(BUILD_DIR)/test_theme.moc
THEME_TEST_BINARY := $(BUILD_DIR)/test-settings-theme
ALL_TARGETS := $(BINARY) $(BROKER_BINARY)
ifeq ($(GUI_ENABLED),1)
ALL_TARGETS += $(GUI_BINARY) $(GUI_PLUGIN)
endif

.PHONY: all cli broker gui qml-plugin qml-module clean test test-audio-units test-gui test-install test-all install license-check
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

$(BUILD_DIR)/moc_theme.cpp: gui/theme.h | $(BUILD_DIR)
	"$(MOC6)" -f theme.h -o "$@" "$<"

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
		'<file alias="qml/AudioDeviceDetail.qml">$(abspath gui/qml/AudioDeviceDetail.qml)</file>' \
		'<file alias="qml/AudioDeviceList.qml">$(abspath gui/qml/AudioDeviceList.qml)</file>' \
		'<file alias="qml/AudioApplicationMixer.qml">$(abspath gui/qml/AudioApplicationMixer.qml)</file>' \
		'<file alias="qml/AudioAdvancedGoXLR.qml">$(abspath gui/qml/AudioAdvancedGoXLR.qml)</file>' \
		'<file alias="qml/AudioAdvancedRouting.qml">$(abspath gui/qml/AudioAdvancedRouting.qml)</file>' \
		'<file alias="qml/AudioPortList.qml">$(abspath gui/qml/AudioPortList.qml)</file>' \
		'<file alias="qml/AudioShellHost.qml">$(abspath gui/qml/AudioShellHost.qml)</file>' \
		'<file alias="qml/AudioPopupHost.qml">$(abspath gui/qml/AudioPopupHost.qml)</file>' \
		'<file alias="qml/AudioButton.qml">$(abspath gui/qml/AudioButton.qml)</file>' \
		'<file alias="qml/AudioCard.qml">$(abspath gui/qml/AudioCard.qml)</file>' \
		'<file alias="qml/AudioComboBox.qml">$(abspath gui/qml/AudioComboBox.qml)</file>' \
		'<file alias="qml/AudioSectionHeading.qml">$(abspath gui/qml/AudioSectionHeading.qml)</file>' \
		'<file alias="qml/AudioSlider.qml">$(abspath gui/qml/AudioSlider.qml)</file>' \
		'<file alias="qml/AudioDialog.qml">$(abspath gui/qml/AudioDialog.qml)</file>' \
		'<file alias="qml/AudioSpinBox.qml">$(abspath gui/qml/AudioSpinBox.qml)</file>' \
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

$(GUI_FIXTURE_BINARY): $(GUI_SOURCES) $(GUI_HEADERS) $(GUI_MOC) $(GUI_RCC) \
		$(GUI_ISA_NOTE_OBJECT) $(TEST_BINARY) $(ISA_NOTE_SCRIPT) | $(BUILD_DIR)
	$(CXX) $(BASE_CPPFLAGS) $(CPPFLAGS) -DSYNAPSE_SETTINGS_GUI_TEST_HOOKS=1 \
		$(BASE_CXXFLAGS) $(CXXFLAGS) $(REPRO_FLAGS) $(GUI_CXX_COMPAT) -Igui \
		$$( $(PKG_CONFIG) --cflags $(GUI_PACKAGES) ) -o "$@" \
		$(GUI_SOURCES) $(GUI_MOC) $(GUI_RCC) $(GUI_ISA_NOTE_OBJECT) \
		$(BASE_LDFLAGS) $(ISA_NOTE_LDFLAGS) $(LDFLAGS) \
		$$( $(PKG_CONFIG) --libs $(GUI_PACKAGES) ) $(LDLIBS)

$(GUI_PLUGIN): $(GUI_PLUGIN_SOURCES) $(GUI_PLUGIN_HEADERS) \
		$(GUI_PLUGIN_COMMON_MOC) $(GUI_PLUGIN_MOC) $(GUI_PLUGIN_RCC) \
		$(ISA_NOTE_SCRIPT) | $(BUILD_DIR)
	$(CXX) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CXXFLAGS) $(CXXFLAGS) \
		$(REPRO_FLAGS) $(GUI_CXX_COMPAT) -Igui \
		$$( $(PKG_CONFIG) --cflags $(GUI_PACKAGES) ) -o "$@" \
		$(GUI_PLUGIN_SOURCES) $(GUI_PLUGIN_COMMON_MOC) $(GUI_PLUGIN_MOC) \
		$(GUI_PLUGIN_RCC) $(GUI_PLUGIN_LDFLAGS) $(LDFLAGS) \
		$$( $(PKG_CONFIG) --libs $(GUI_PACKAGES) ) $(LDLIBS)

$(GUI_PLUGIN_TEST): $(GUI_PLUGIN_SOURCES) $(GUI_PLUGIN_HEADERS) \
		$(GUI_PLUGIN_COMMON_MOC) $(GUI_PLUGIN_MOC) $(GUI_PLUGIN_RCC) \
		$(ISA_NOTE_SCRIPT) | $(BUILD_DIR)
	$(CXX) $(BASE_CPPFLAGS) $(CPPFLAGS) \
		-DSYNAPSE_SETTINGS_GUI_TEST_HOOKS=1 \
		$(BASE_CXXFLAGS) $(CXXFLAGS) $(REPRO_FLAGS) $(GUI_CXX_COMPAT) \
		-Igui $$( $(PKG_CONFIG) --cflags $(GUI_PACKAGES) ) -o "$@" \
		$(GUI_PLUGIN_SOURCES) $(GUI_PLUGIN_COMMON_MOC) $(GUI_PLUGIN_MOC) \
		$(GUI_PLUGIN_RCC) $(GUI_PLUGIN_LDFLAGS) $(LDFLAGS) \
		$$( $(PKG_CONFIG) --libs $(GUI_PACKAGES) ) $(LDLIBS)

$(GUI_MODULE_STAMP): $(GUI_PLUGIN) $(GUI_MODULE_QML) \
		gui/qml-module/qmldir $(GUI_MODULE_QMLTYPES)
	rm -rf "$(GUI_MODULE_DIR)"
	install -d -m 0755 "$(GUI_MODULE_DIR)"
	install -m 0755 "$(GUI_PLUGIN)" \
		"$(GUI_MODULE_DIR)/libsynapse_settings_audio_qml.so"
	install -m 0644 gui/qml-module/qmldir $(GUI_MODULE_QMLTYPES) \
		$(GUI_MODULE_QML) "$(GUI_MODULE_DIR)/"
	touch "$@"

$(GUI_TEST_MODULE_STAMP): $(GUI_PLUGIN_TEST) $(GUI_MODULE_QML) \
		gui/qml-module/qmldir $(GUI_MODULE_QMLTYPES)
	rm -rf "$(GUI_TEST_MODULE_DIR)"
	install -d -m 0755 "$(GUI_TEST_MODULE_DIR)"
	install -m 0755 "$(GUI_PLUGIN_TEST)" \
		"$(GUI_TEST_MODULE_DIR)/libsynapse_settings_audio_qml.so"
	install -m 0644 gui/qml-module/qmldir $(GUI_MODULE_QMLTYPES) \
		$(GUI_MODULE_QML) "$(GUI_TEST_MODULE_DIR)/"
	touch "$@"

$(GUI_TEST_MOC): tests/test_audio_adapter.cpp | $(BUILD_DIR)
	"$(MOC6)" -o "$@" "$<"

$(THEME_TEST_MOC): tests/test_theme.cpp | $(BUILD_DIR)
	"$(MOC6)" -o "$@" "$<"

$(THEME_TEST_BINARY): tests/test_theme.cpp gui/theme.cpp gui/theme.h \
		$(BUILD_DIR)/moc_theme.cpp $(THEME_TEST_MOC) | $(BUILD_DIR)
	$(CXX) $(BASE_CPPFLAGS) $(CPPFLAGS) -DSYNAPSE_SETTINGS_GUI_TEST_HOOKS=1 \
		$(BASE_CXXFLAGS) $(CXXFLAGS) $(REPRO_FLAGS) $(GUI_CXX_COMPAT) \
		$(GUI_TEST_COMPAT) -Igui -I$(BUILD_DIR) \
		$$( $(PKG_CONFIG) --cflags $(GUI_TEST_PACKAGES) ) -o "$@" \
		tests/test_theme.cpp gui/theme.cpp $(BUILD_DIR)/moc_theme.cpp \
		$(BASE_LDFLAGS) $(LDFLAGS) \
		$$( $(PKG_CONFIG) --libs $(GUI_TEST_PACKAGES) ) $(LDLIBS)

$(GUI_TEST_BINARY): tests/test_audio_adapter.cpp gui/audio_adapter.cpp $(GUI_AUDIO_BOUNDARY_SOURCES) $(GUI_AUDIO_BOUNDARY_HEADERS) tests/fixture_child.h \
		gui/audio_adapter.h gui/localization.cpp gui/localization.h \
		$(BUILD_DIR)/moc_audio_adapter.cpp $(BUILD_DIR)/moc_localization.cpp \
		$(GUI_PLUGIN_RCC) $(GUI_TEST_MOC) $(TEST_BINARY) | $(BUILD_DIR)
	$(CXX) $(BASE_CPPFLAGS) $(CPPFLAGS) -DSYNAPSE_SETTINGS_GUI_TEST_HOOKS=1 \
		$(BASE_CXXFLAGS) $(CXXFLAGS) $(REPRO_FLAGS) $(GUI_CXX_COMPAT) \
		$(GUI_TEST_COMPAT) -Igui -I$(BUILD_DIR) \
		$$( $(PKG_CONFIG) --cflags $(GUI_TEST_PACKAGES) ) -o "$@" \
		tests/test_audio_adapter.cpp gui/audio_adapter.cpp gui/localization.cpp $(GUI_AUDIO_BOUNDARY_SOURCES) \
		$(BUILD_DIR)/moc_audio_adapter.cpp $(BUILD_DIR)/moc_localization.cpp \
		$(GUI_PLUGIN_RCC) $(BASE_LDFLAGS) $(LDFLAGS) \
		$$( $(PKG_CONFIG) --libs $(GUI_TEST_PACKAGES) ) $(LDLIBS)

$(AUDIO_UNITS_PROBE): tests/audio_units_probe.c $(AUDIO_SOURCES) \
		src/audio_profile_port.c $(AUDIO_INTERNALS) src/settings_internal.h \
		$(ISA_NOTE_SOURCE) $(ISA_NOTE_SCRIPT) | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) -DSYNAPSE_SETTINGS_TEST_HOOKS=1 \
		-DSYNAPSE_SETTINGS_WITH_PROFILE_PORT=1 -Isrc $(BASE_CFLAGS) $(CFLAGS) \
		$(REPRO_FLAGS) $(CORE_CFLAGS) $(JSON_C_CFLAGS) -o "$@" \
		tests/audio_units_probe.c $(AUDIO_SOURCES) src/audio_profile_port.c \
		$(ISA_NOTE_SOURCE) $(BASE_LDFLAGS) $(ISA_NOTE_LDFLAGS) $(LDFLAGS) \
		$(CORE_LIBS) $(JSON_C_LIBS) $(LDLIBS)

test-audio-units: $(AUDIO_UNITS_PROBE) $(TEST_BINARY)
	$(TEST_ENV) "$(AUDIO_UNITS_PROBE)"
	$(TEST_ENV) python3 -I -B tests/audio-units-run.py \
		"$(abspath $(TEST_BINARY))" "$(AUDIO_UNITS_OUTPUT_DIR)" \
		$(if $(AUDIO_UNITS_EXPECTED),"$(abspath $(AUDIO_UNITS_EXPECTED))")

test: $(TEST_BINARY) $(BROKER_TEST_BINARY) test-audio-units
	$(TEST_ENV) ./tests/run.sh "$(abspath $(TEST_BINARY))"
	$(TEST_ENV) ./tests/broker-run.sh "$(abspath $(TEST_BINARY))" \
		"$(abspath $(BROKER_TEST_BINARY))"

test-gui:
ifeq ($(GUI_ENABLED),1)
	LUPDATE6="$(LUPDATE6)" python3 ./tests/localization-run.py
	$(MAKE) --no-print-directory "$(GUI_BINARY)" "$(GUI_FIXTURE_BINARY)" \
		"$(GUI_TEST_BINARY)" "$(THEME_TEST_BINARY)" "$(GUI_MODULE_STAMP)" \
		"$(GUI_TEST_MODULE_STAMP)" BUILD_GUI=1
	"$(QMLLINT)" --max-warnings 0 -I "$(GUI_MODULE_ROOT)" $(GUI_QML) \
		tests/qml/tst_settings_sections.qml
	"$(QMLLINT)" --max-warnings 0 -I "$(GUI_MODULE_ROOT)" "$(GUI_MODULE_DIR)"/*.qml \
		tests/qml/tst_audio_module.qml tests/qml/tst_audio_popup_host.qml \
		tests/qml/tst_installed_audio.qml
	QT_QPA_PLATFORM=offscreen "$(QMLTESTRUNNER)" \
		-translation "$(BUILD_DIR)/i18n/synapse-settings_en_US.qm" \
		-input tests/qml/tst_settings_sections.qml
	QT_QPA_PLATFORM=offscreen SYNAPSE_SETTINGS_TEST_BACKEND="$(abspath $(TEST_BINARY))" \
		$(TEST_ENV) "$(GUI_TEST_BINARY)"
	$(TEST_ENV) "$(THEME_TEST_BINARY)"
	$(TEST_ENV) ./tests/qml-module-run.sh \
		"$(abspath $(QMLTESTRUNNER))" "$(abspath $(GUI_TEST_MODULE_ROOT))" \
		"$(abspath $(TEST_BINARY))"
	QT_QPA_PLATFORM=offscreen "$(QMLTESTRUNNER)" \
		-import "$(abspath $(GUI_TEST_MODULE_ROOT))" \
		-input tests/qml/tst_audio_popup_host.qml
	./tests/qml-module-boundary.sh "$(GUI_MODULE_DIR)"
	$(TEST_ENV) ./tests/gui-run.sh "$(abspath $(GUI_FIXTURE_BINARY))" \
		"$(abspath $(TEST_BINARY))"
else
	@echo "synapse-settings: Qt 6 GUI SDK unavailable" >&2; exit 1
endif

test-install: $(ALL_TARGETS)
	python3 -I -B tests/install-run.py --build-dir "$(abspath $(BUILD_DIR))" \
		--gui "$(GUI_ENABLED)" --qmltestrunner "$(abspath $(QMLTESTRUNNER))" \
		$(if $(INSTALL_TEST_OUTPUT_DIR),--output-dir "$(abspath $(INSTALL_TEST_OUTPUT_DIR))")

test-all: license-check all test test-gui test-install
	./tests/elf-isa-boundary.sh "$(BINARY)" "$(BROKER_BINARY)" \
		"$(GUI_BINARY)" "$(GUI_PLUGIN)"
	./tests/audio-goxlr-production-boundary.sh "$(BINARY)" "$(TEST_BINARY)" \
		"$(BROKER_BINARY)" "$(GUI_BINARY)" "$(GUI_TEST_BINARY)"
	./tests/audio-profile-port-production-boundary.sh "$(BINARY)" \
		"$(TEST_BINARY)" "$(BROKER_BINARY)"

license-check:
	@set -eu; \
	files="$$(find src gui tests data -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.qml' -o -name '*.py' -o -name '*.sh' -o -name '*.inc' -o -name '*.ld' -o -name '*.service' \) -print; find . -maxdepth 1 -type f -name Makefile -print)"; \
	for file in $$files; do \
		grep -Fq 'SPDX-License-Identifier: MIT' "$$file" || { echo "missing MIT SPDX: $$file" >&2; exit 1; }; \
	done; \
	test "$$(sha256sum LICENSE | awk '{print $$1}')" = f83d3e90e0ee9f98f04a46a0dd0cf4aae4846a61f145d1ffbc4501b8ab2a1dda; \
	gpl=GPL; marker="SPDX-License-Identifier: $${gpl}-"; phrase='GNU GENERAL PUBLIC LICEN''SE'; \
	if grep -R -n -E "$${marker}|^$${phrase}" src gui tests data schemas docs Makefile README.md AGENTS.md CHANGELOG.md LICENSE; then \
		echo 'forbidden first-party license marker' >&2; exit 1; \
	fi; \
	echo "license-gate=PASS checked=$$(printf '%s\n' $$files | wc -l)"

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
	install -m 0644 $(GUI_MODULE_QML) "$(DESTDIR)$(QMLDIR)/$(GUI_MODULE_REL)/"
	install -D -m 0644 data/org.synapse.Settings.desktop \
		"$(DESTDIR)$(DATADIR)/applications/org.synapse.Settings.desktop"
endif
	install -D -m 0644 LICENSE \
		"$(DESTDIR)$(DATADIR)/licenses/synapse-settings/LICENSE"

clean:
	rm -rf "$(BUILD_DIR)"
