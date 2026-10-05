TARGET     	:= miahidpordo
VERSION  	:= 17.4
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
	@echo
	@echo "+++++++++++++++++++++++++++++++++++"
	@echo "+        Compiling files          +"
	@echo "+++++++++++++++++++++++++++++++++++"
	@echo "- main.c --> main.o"
	gcc	-c main.c -o main.o $(CFLAGS)
	@echo "- resources.c --> resources.o"
	gcc	-c resources.c	-o resources.o	$(CFLAGS)
	@echo "+++++++++++++++++++++++++++++++++++"
	@echo "+     Linking *.o and  LDLIBS     +"
	@echo "+++++++++++++++++++++++++++++++++++"
	@echo "- *.o + LDLIBS --> $(TARGET)"
	gcc	*.o -o $(TARGET) $(LDLIBS)
	@echo "+++++++++++++++++++++++++++++++++++"
	@echo "+        $(TARGET) is ready     +"
	@echo "+++++++++++++++++++++++++++++++++++"
	@rm -rf main *.o resources.c
	
run:all
	./$(TARGET)
####################################################################################################################
info: $(RES_SRC)
	@echo "+++++++++++++++++++++++++++++++++++"
	@echo "+ shell uname= $(shell uname -s) +"
	@echo "+++++++++++++++++++++++++++++++++++"
ifeq ($(OS), Windows_NT)
	@echo "+ ******* OS = $(OS) ******* +"
	@echo "- resources/icon.svg --> resources/icon.png  "
	rsvg-convert -w 256 -h 256 resources/icon.svg -o 	resources/icon.png
	@echo "-  resources/icon.png --> windows/Application.ico  "
	icotool -c -o windows/Application.ico 	resources/icon.png
	@echo "- windows/Application.manifest.in --> windows/Application.manifest  "
	sed -e 's|@APP_VERSION@|$(VERSION)|g' -e 's|@APP_NAME@|$(TARGET)|g' windows/Application.manifest.in > windows/Application.manifest
	@echo "- windows/resource.rc.in --> windows/resource.rc  "
	sed -e 's|@APP_VERSION@|$(VERSION)|g' -e 's|@APP_NAME@|$(TARGET)|g' windows/resource.rc.in > windows/resource.rc
	@echo "- windows/resource.rc --> winresource.o  "
	windres -I. -Iwindows -i windows/resource.rc -o winresource.o
	@rm -rf windows/resource.rc windows/Application.manifest
endif
####################################################################################################################
gresource:
	@echo "+++++++++++++++++++++++++++++++++++"
	@echo "+        gresource files          +"
	@echo "+++++++++++++++++++++++++++++++++++"
	@rm -rf resources/resources.gresource.xml
	@echo "- writing resources/resources.gresource.xml  "
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
	@echo "echo --> resources/resources.gresource.xml"
	@echo "- resources.gresource.xml --> resources.c"
	glib-compile-resources resources/resources.gresource.xml --sourcedir=resources --generate-source --target=resources.c --c-name=$(TARGET)
####################################################################################################################
clean:
	@echo "+++++++++++++++++++++++++++++++++++"
	@echo "+        Cleaning ...             +"
	@rm -rf main *.o  *.exe $(TARGET)
	@rm -rf resources.c resources/resources.gresource.xml  resources/icon.png
	@rm -rf windows/installer.nsi windows/*.ico windows/*.exe windows/*.dll  windows/*.glade 
	@rm -rf windows/Application.manifest windows/resource.rc windows/Application.ico
	@rm -rf  debian/.deb *.deb debian/$(TARGET).desktop debian/copyright debian/$(TARGET).svg
	@echo "+++++++++++++++++++++++++++++++++++"
####################################################################################################################
ifeq ($(shell uname -s),Linux)
  ifneq ($(wildcard /etc/debian_version),)
program: deb

PREFIX   := /usr
DEBARCH  := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)
STAGE    := debian/.deb/$(TARGET)-$(VERSION)
DEBFILE  := $(TARGET)_$(VERSION)_$(DEBARCH).deb

DATE := $(shell date -R)

deb:all
	@echo "######################   DEB BASED LINUX   ##########################"
	@echo "--Creating debian/changelog"
	@printf '%s\n' \
	  '$(TARGET) ($(VERSION)-1) unstable; urgency=medium' \
	  '' \
	  '  * Initial release.' \
	  '' \
	  ' -- $(AUTOR)  $(DATE)' \
	  > debian/changelog
	# ---------- 1. Create staging directories ----------
	@mkdir -p $(STAGE)/DEBIAN \
	          $(STAGE)$(PREFIX)/bin \
	          $(STAGE)$(PREFIX)/share/$(TARGET) \
	          $(STAGE)$(PREFIX)/share/icons/hicolor/scalable/apps \
	          $(STAGE)$(PREFIX)/share/doc/$(TARGET) \
	          $(STAGE)$(PREFIX)/share/applications

	# ---------- 2. Generate auxiliary files ----------
	@echo "--Creating debian/$(TARGET).desktop"
	@sed -e 's/@PACKAGENAME@/$(TARGET)/g' -e 's/@TITLE@/$(TITLE)/g' \
	     -e 's/@DESCRIPTION@/$(DESCRIPTION)/g' \
	     debian/app.desktop.in > debian/$(TARGET).desktop

	@echo "--Creating debian/copyright"
	@sed -e 's/@PACKAGENAME@/$(TARGET)/g' -e 's/@TITLE@/$(TITLE)/g' \
	     debian/copyright.in > debian/copyright

	@cp resources/icon.svg debian/$(TARGET).svg

	# ---------- 3. Install files into STAGE ----------
	@install -m 0755 $(TARGET) $(STAGE)$(PREFIX)/bin/$(TARGET)
	@install -m 0644 debian/$(TARGET).desktop $(STAGE)$(PREFIX)/share/applications/
	@install -m 0644 debian/$(TARGET).svg \
	    $(STAGE)$(PREFIX)/share/icons/hicolor/scalable/apps/$(TARGET).svg
	@install -m 0644 debian/copyright $(STAGE)$(PREFIX)/share/doc/$(TARGET)/copyright

	# ---------- 4. Generate debian/control from control.in ----------
	#     (dpkg-shlibdeps and dpkg-gencontrol read this file)
	@echo "--Creating debian/control from control.in"
	@sed -e 's/@PACKAGENAME@/$(TARGET)/g' \
	     -e 's/@VERSION@/$(VERSION)/g' \
	     -e 's/@DESCRIPTION@/$(DESCRIPTION)/g' \
	     -e 's/@AUTOR@/$(AUTOR)/g' \
	     -e 's/@SECTION@/$(SECTION)/g' \
	     -e 's/@ARCH@/$(DEBARCH)/g' \
	     debian/control.in > debian/control

	# ---------- 5. Extract shlibs dependencies ----------
	#     dpkg-shlibdeps writes shlibs:Depends into debian/substvars
	@echo "--Running dpkg-shlibdeps"
	@dpkg-shlibdeps -O -e$(STAGE)$(PREFIX)/bin/$(TARGET) > debian/substvars
	@echo "--debian/substvars:"
	@cat debian/substvars

	# ---------- 6. Generate final control file ----------
	#     dpkg-gencontrol substitutes ${shlibs:Depends} and ${misc:Depends}
	#     using debian/control + debian/substvars
	@echo "--Running dpkg-gencontrol"
	@dpkg-gencontrol -p$(TARGET) -P$(STAGE) -v$(VERSION) -DArchitecture=$(DEBARCH) -cdebian/control -Tdebian/substvars -O$(STAGE)/DEBIAN/control

	# ---------- 7. Build the .deb package ----------
	@dpkg-deb --root-owner-group --build $(STAGE) $(DEBFILE)

	# ---------- 8. Clean up temporary files ----------
	@rm -rf debian/.deb debian/copyright debian/$(TARGET).desktop debian/$(TARGET).svg debian/control debian/substvars debian/changelog debian/files

	@echo "############### $(DEBFILE) IS READY ###################"

  else
	@echo "deb: not a Debian-based system" >&2
	@exit 1
  endif
endif
#sudo apt install libhidapi-dev librsvg2-bin icoutils
#sudo apt install dpkg-dev debhelper libgtk-3-dev libconfig-dev libhidapi-dev
#sudo apt install devscripts
####################################################################################################################
ifeq ($(OS), Windows_NT)
#pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-pkg-config mingw-w64-x86_64-gtk3 mingw-w64-x86_64-libconfig mingw-w64-x86_64-nsis p7zip mingw-w64-x86_64-librsvg mingw-w64-x86_64-icoutils  mingw-w64-x86_64-ntldd
program:all
	@echo "- ------------------------------- -"
	@echo "+++++++++++++++++++++++++++++++++++"
	@echo "+      WINDOWS SETUP FILE         +"
	@echo "+++++++++++++++++++++++++++++++++++"
	@rm -f windows/*.dll
	@echo "- mingw64 /*.dlls --> windows/*.dll "
	@cp $(TARGET).exe windows/
	@ntldd -R $(TARGET).exe | awk '/mingw64.bin/ {print $$3}' | sort -u | \
		while read -r dll; do cp "$$dll" windows/ ; done
	@echo "+ windows/*.dll ready "
	@echo "-  windows/installer.nsi.in --> windows/installer.nsi"
	sed -e 's|@APP_VERSION@|$(VERSION)|g' -e 's|@APP_NAME@|$(TITLE)|g' windows/installer.nsi.in > windows/installer.nsi
	@echo "- nsis/$(TARGET).ico "
	cp windows/application.ico  windows/$(TARGET).ico
	@echo "- windows/$(TARGET) "
	cp $(TARGET) windows/$(TARGET)
	@echo "-   Nsis   "
	cd windows && makensis installer.nsi
	mv windows/$(TARGET)_Setup.exe ./$(TITLE)_$(VERSION)_Setup.exe
	@rm -f windows/*.ico windows/*.exe windows/*.dll windows/*.nsi 
	@echo "+++++++++++++++++++++++++++++++++++"
	@echo "+   $(TITLE)_$(VERSION)_Setup.exe    +"
	@echo "+++++++++++++++++++++++++++++++++++"
endif
####################################################################################################################
