CXX := g++
CXXFLAGS := -std=c++17 -Wall -I/usr/include/freetype2
LDLIBS := -lX11 -lXft

FUNCTION_BIN := function
TERMINAL_BIN := terminal

ICON_XPM := assets/icon.xpm
ICON_PNG := assets/icon.png

DESKTOP_FILE := assets/xterm.desktop
DESKTOP_DEST := $(HOME)/.local/share/applications/xterm.desktop

.PHONY: all run clean icon install-icon file-icon

all: $(FUNCTION_BIN) $(TERMINAL_BIN) $(ICON_XPM) $(ICON_PNG)

$(FUNCTION_BIN): function.cpp headers/*.h
	$(CXX) $(CXXFLAGS) function.cpp -o $(FUNCTION_BIN)

$(TERMINAL_BIN): terminal.cpp headers/*.h
	$(CXX) $(CXXFLAGS) terminal.cpp -o $(TERMINAL_BIN) $(LDLIBS)

run: $(TERMINAL_BIN)
	./$(TERMINAL_BIN)

clean:
	rm -f $(FUNCTION_BIN) $(TERMINAL_BIN)

icon: $(ICON_XPM) $(ICON_PNG)

$(ICON_XPM): assets/gen_icon.cpp
	$(CXX) -std=c++17 -Wall assets/gen_icon.cpp -o /tmp/xterm_gen_icon
	/tmp/xterm_gen_icon > $(ICON_XPM)
	@echo "Regenerated $(ICON_XPM)"

$(ICON_PNG): $(ICON_XPM)
	@if command -v convert >/dev/null 2>&1; then \
		convert $(ICON_XPM) $(ICON_PNG); \
		echo "Regenerated $(ICON_PNG)"; \
	else \
		echo "ImageMagick 'convert' not found skipping $(ICON_PNG) regeneration"; \
	fi

install-icon: $(DESKTOP_FILE)
	mkdir -p $(HOME)/.local/share/applications
	sed 's|@PROJECT_DIR@|$(CURDIR)|g' $(DESKTOP_FILE) > $(DESKTOP_DEST)
	@command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database $(HOME)/.local/share/applications || true
	@echo "Installed $(DESKTOP_DEST) XTerm should now show its icon in your app launcher/file manager."

file-icon: $(TERMINAL_BIN)
	@if command -v gio >/dev/null 2>&1; then \
		gio set $(TERMINAL_BIN) metadata::custom-icon "file://$(CURDIR)/assets/icon.png"; \
		echo "Tagged $(TERMINAL_BIN) with a custom icon (Nautilus/GNOME Files only)."; \
	else \
		echo "gio not found (needs GNOME's GVFS) skipping; this only works in Nautilus anyway."; \
	fi
