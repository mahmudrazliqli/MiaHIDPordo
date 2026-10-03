TARGET     	:= miahidpordo
VERSION  	:= 17.1
TITLE	 	:= MiaHIDPordo
AUTOR		:=Mahmudrazliqli <mahmudrazliqli@yahoo.com>
SECTION		:=electronics
DESCRIPTION :=$(TITLE) -HID device terminal , supports output, input and feature.
####################################################################################################################
CFLAGS   += -Wall -Wextra `pkg-config --cflags gtk+-3.0 libconfig` 
CFLAGS   += -DWINTITLE='"$(TITLE) $(VERSION)"' -DTARGET='"$(TARGET)"'
LDLIBS   := `pkg-config --libs gtk+-3.0 libconfig`

ifeq ($(OS), Windows_NT)
LDLIBS   +=  -mwindows -lhidapi -lshlwapi -lsetupapi
else ifeq ($(shell uname -s), Linux)
LDLIBS   += -lhidapi-libusb
endif
####################################################################################################################
all:clean info gresource
	gcc	-c main.c		-o main.o		$(CFLAGS)
	gcc	-c resources.c	-o resources.o	$(CFLAGS)
	gcc	*.o -o $(TARGET) $(LDLIBS)
	@rm -rf main *.o resources.c
	
run:all
	./$(TARGET)
####################################################################################################################
info: $(RES_SRC)
	@echo + OS = $(OS) 
	@echo - shell uname= $(shell uname -s)	
ifeq ($(OS), Windows_NT)
	rsvg-convert -w 256 -h 256 resources/icon.svg -o 	resources/icon.png
	icotool -c -o windows/Application/Application.ico 	resources/icon.png
	sed -e 's|@APP_VERSION@|$(VERSION)|g' -e 's|@APP_NAME@|$(TARGET)|g' windows/application/Application.manifest.in > windows/application/Application.manifest
	sed -e 's|@APP_VERSION@|$(VERSION)|g' -e 's|@APP_NAME@|$(TARGET)|g' windows/application/resource.rc.in > windows/application/resource.rc
	windres -I. -Iwindows/application -i windows/application/resource.rc -o winresource.o
endif
####################################################################################################################
gresource:
	rm -f resources/resources.gresource.xml
	@mkdir -p resources
	@printf '%s\n' \
	  '<?xml version="1.0" encoding="UTF-8"?>' \
	  '<gresources>' \
	  '  <gresource prefix="/org/$(TARGET)">' \
	  '    <file>window1.glade</file>' \
	  '    <file>style.css</file>' \
	  '  </gresource>' \
	  '</gresources>' \
	  > resources/resources.gresource.xml
	@echo "Generated resources/resources.gresource.xml"
	@echo -  GResource :
	glib-compile-resources resources/resources.gresource.xml --sourcedir=resources --generate-source --target=resources.c --c-name=$(TARGET)
####################################################################################################################
clean:
	rm -rf main *.o  *.exe $(TARGET)
	rm -rf resources.c resources/resources.gresource.xml  resources/icon.png
	rm -rf windows/nsis/installer.nsi windows/nsis/*.ico windows/nsis/*.exe windows/nsis/*.dll  windows/nsis/*.glade 
	rm -rf windows/Application/Application.manifest windows/Application/resource.rc windows/Application/Application.ico
	rm -rf  .deb *.deb debian/$(TARGET).desktop debian/copyright debian/$(TARGET).svg
####################################################################################################################
ifeq ($(shell uname -s),Linux)
  ifneq ($(wildcard /etc/debian_version),)
program: deb

PREFIX   := /usr
DEBARCH  := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)
STAGE    := .deb/$(TARGET)-$(VERSION)
DEBFILE  := $(TARGET)_$(VERSION)_$(DEBARCH).deb
deb:all	
	cp resources/icon.svg debian/$(TARGET).svg
	@mkdir -p $(STAGE)/DEBIAN $(STAGE)$(PREFIX)/bin $(STAGE)$(PREFIX)/share/$(TARGET) $(STAGE)$(PREFIX)/share/icons/hicolor/scalable/apps $(STAGE)$(PREFIX)/share/doc/$(TARGET) $(STAGE)$(PREFIX)/share/applications
	@echo "--Creating debian/$(TARGET).desktop"
	sed -e 's/@PACKAGENAME@/$(TARGET)/g' -e 's/@TITLE@/$(TITLE)/g' -e 's/@DESCRIPTION@/$(DESCRIPTION)/g' debian/app.desktop.in > debian/$(TARGET).desktop
	@echo "--Creating debian/copyright"
	sed -e 's/@PACKAGENAME@/$(TARGET)/g' -e 's/@TITLE@/$(TITLE)/g' 	debian/copyright.in > debian/copyright
	@echo "######################   DEB BASED LINUX   ##########################"
	
	@install -m 0755 $(TARGET) $(STAGE)$(PREFIX)/bin/$(TARGET)
	@install -m 0644 debian/$(TARGET).desktop $(STAGE)$(PREFIX)/share/applications/
	@install -m 0644 debian/$(TARGET).svg $(STAGE)$(PREFIX)/share/icons/hicolor/scalable/apps/$(TARGET).svg
	@install -m 0644 debian/copyright $(STAGE)$(PREFIX)/share/doc/$(TARGET)/copyright
	@DEPS=$$(dpkg-shlibdeps -O $(TARGET) 2>/dev/null | sed -n 's/^shlibs:Depends=//p'); \
	sed -e 's/@PACKAGENAME@/$(TARGET)/g' -e 's/@VERSION@/$(VERSION)/g' -e 's/@DESCRIPTION@/$(DESCRIPTION)/g' -e 's/@AUTOR@/$(AUTOR)/g' \
	-e 's/@SECTION@/$(SECTION)/g' -e 's/@ARCH@/$(DEBARCH)/g' -e "s|@DEPS@|$$DEPS|g" \
	debian/control.in > $(STAGE)/DEBIAN/control
	@dpkg-deb --root-owner-group --build $(STAGE) $(DEBFILE)
	@rm -rf .deb
	@echo "############### $(DEBFILE) IS READY ###################"
  else
	@echo "deb: not a Debian-based system" >&2
	@exit 1
  endif
endif
#sudo apt install libhidapi-dev librsvg2-bin icoutils
####################################################################################################################
ifeq ($(OS), Windows_NT)	
program:all
	@echo "######################   WINDOWS SETUP   ##########################"
	sed -e 's|@APP_VERSION@|$(VERSION)|g' -e 's|@APP_NAME@|$(TITLE)|g' windows/nsis/installer.nsi.in > windows/nsis/installer.nsi
	cp windows/Application/application.ico  windows/nsis/$(TARGET).ico
	cp $(TARGET) windows/nsis/$(TARGET)
	7z x windows/nsis/dlls.7z -owindows/nsis -y;
	rm -f windows/nsis/uninst.exe
	cd windows/nsis && makensis installer.nsi
	mv windows/nsis/$(TARGET)_Setup.exe ./$(TITLE)_$(VERSION)_Setup.exe
	@rm -f windows/nsis/*.ico windows/nsis/*.exe windows/nsis/*.dll .deb windows/nsis/installer.nsi
	@echo "#############   $(TITLE)_$(VERSION)_Setup.exe  IS READY  ####################"
endif
#pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-pkg-config mingw-w64-x86_64-gtk3 mingw-w64-x86_64-libconfig mingw-w64-x86_64-nsis p7zip
#pacman -S mingw-w64x86_64-librsvg mingw-w64x86_64-icoutils
####################################################################################################################
