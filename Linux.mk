# MiaHIDPordo - GTK3 HIDl port monitor with libconfig settings

NAME     := miaHIDpordo
VERSION  := 11.1

CC       ?= gcc

CFLAGS   += -Wall -Wextra $(shell pkg-config --cflags gtk+-3.0 libconfig) -DMIAHIDPORDO_DATA_DIR='"/usr/share/$(NAME)"'
LDLIBS   := $(shell pkg-config --libs gtk+-3.0 libconfig)  -lhidapi-libusb

TARGET   := $(NAME)
SRCS     := main.c

PREFIX   := /usr
DEBARCH  := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)
STAGE    := .deb/$(NAME)-$(VERSION)
DEBFILE  := $(NAME)_$(VERSION)_$(DEBARCH).deb



all: clean $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) *.o main .deb *.deb


# ---------------------------------------------------------------------
# Build a .deb package: make deb -> miaHIDpordo_<version>_<arch>.deb
# Runtime library packages are detected from this build host.

deb: all
	mkdir -p $(STAGE)/DEBIAN $(STAGE)$(PREFIX)/bin
	mkdir -p $(STAGE)$(PREFIX)/share/$(NAME) 
	mkdir -p $(STAGE)$(PREFIX)/share/icons/hicolor/scalable/apps
	mkdir -p $(STAGE)$(PREFIX)/share/doc/$(NAME)
	mkdir -p $(STAGE)$(PREFIX)/share/applications
	install -m 0755 $(TARGET) $(STAGE)$(PREFIX)/bin/$(NAME)
	install -m 0644 window1.glade $(STAGE)$(PREFIX)/share/$(NAME)
	install -m 0644 debian/$(NAME).desktop $(STAGE)$(PREFIX)/share/applications/
	install -m 0644 debian/$(NAME).svg $(STAGE)$(PREFIX)/share/icons/hicolor/scalable/apps/$(NAME).svg
	install -m 0644 debian/copyright $(STAGE)$(PREFIX)/share/doc/$(NAME)/copyright
	
	DEPS=$$(dpkg-shlibdeps -O $(TARGET) 2>/dev/null | sed 's/^shlibs=//'); \
	sed -e 's/@VERSION@/$(VERSION)/g' -e 's/@ARCH@/$(DEBARCH)/g' -e "s|@DEPS@|$${DEPS}|g" debian/control.in > $(STAGE)/DEBIAN/control
	
	dpkg-deb --root-owner-group --build $(STAGE) $(DEBFILE)
	rm -rf $(STAGE) .deb
	@echo "############### Built $(DEBFILE) OK ###################"
