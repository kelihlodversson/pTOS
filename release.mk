#
# release.mk - Makefile fragment for building releases
#
# Copyright (C) 2011-2024 The EmuTOS development team.
#
# Authors:
#  VRI      Vincent Rivière
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#

#
# This file contains the targets used to build the release archives.
# It is included from the main Makefile.
#
# Each archive is produced by starting from one of the configurations in
# configs/, so what is released is exactly what "make <name>_defconfig &&
# make" produces.
#

# This subset of the doc directory will be included in all the binary archives
DOCFILES = doc/announce.txt doc/authors.txt doc/bugs.txt doc/changelog.txt \
  doc/emudesk.txt doc/incompatible.txt doc/license.txt doc/status.txt \
  doc/todo.txt doc/tools.txt doc/xhdi.txt

# This subset of the extras directory will be included in all the binary archives
# that have a desktop
EXTRAFILES = extras/*

# The archives will be placed into this directory
RELEASE_DIR = release-archives

# Build the image described by the configuration $(1), for the default
# country of that configuration.
define build-config
$(MAKE) distclean && $(MAKE) $(1)_defconfig && $(MAKE)
endef

# Same, but once per country.  Only makes sense for single-country images.
define build-config-all-countries
$(MAKE) distclean && $(MAKE) $(1)_defconfig && \
for i in $(COUNTRIES); do echo; $(MAKE) COUNTRY=$$i UNIQUE=$$i || exit 1; done
endef

# Copy the mouse cursor and icon resources an archive may want to customise.
define copy-resources
cp aes/mform.def $(1)/emucurs.def && cp aes/mform.rsc $(1)/emucurs.rsc && \
cp desk/icon.def $(1)/emuicon.def && cp desk/icon.rsc $(1)/emuicon.rsc
endef

# Assemble the documentation of an archive and convert it to DOS line endings.
# The generic part is readme_emutos.txt, which is what upstream EmuTOS calls
# readme.txt; here that name is taken by the archive being assembled.
define copy-docs
cat doc/readme-$(2).txt readme_emutos.txt >$(1)/readme.txt && mkdir $(1)/doc && \
cp $(DOCFILES) $(1)/doc && find $(1) -name '*.txt' -exec unix2dos '{}' ';'
endef

.PHONY: release-clean
release-clean:
	rm -rf $(RELEASE_DIR)

.PHONY: release-mkdir
release-mkdir:
	mkdir $(RELEASE_DIR)

.PHONY: release-src
RELEASE_SRC = ptos-src-$(VERSION)
release-src:
	mkdir $(RELEASE_DIR)/$(RELEASE_SRC)
	cp -R $(filter-out . .. .git $(RELEASE_DIR), $(shell echo * .*)) $(RELEASE_DIR)/$(RELEASE_SRC)
	find $(RELEASE_DIR)/$(RELEASE_SRC) -type d -exec chmod 755 '{}' ';'
	find $(RELEASE_DIR)/$(RELEASE_SRC) -type f -exec chmod 644 '{}' ';'
	find $(RELEASE_DIR)/$(RELEASE_SRC) -type f -name '*.sh' -exec chmod 755 '{}' ';'
	find $(RELEASE_DIR)/$(RELEASE_SRC) -type f -name '*.py' -exec chmod 755 '{}' ';'
	tar -C $(RELEASE_DIR) --owner=0 --group=0 -zcf $(RELEASE_DIR)/$(RELEASE_SRC).tar.gz $(RELEASE_SRC)
	rm -r $(RELEASE_DIR)/$(RELEASE_SRC)

.PHONY: release-512k
RELEASE_512K = $(RELEASE_DIR)/ptos-512k-$(VERSION)
release-512k:
	$(call build-config,atari512)
	mkdir $(RELEASE_512K)
	cp ptos512k.img ptos512k.sym $(RELEASE_512K)
	$(call copy-resources,$(RELEASE_512K))
	$(call copy-docs,$(RELEASE_512K),512k)
	cd $(RELEASE_DIR) && zip -9 -r $(notdir $(RELEASE_512K)).zip $(notdir $(RELEASE_512K))
	rm -r $(RELEASE_512K)

.PHONY: release-256k
RELEASE_256K = $(RELEASE_DIR)/ptos-256k-$(VERSION)
release-256k:
	$(call build-config-all-countries,atari256)
	mkdir $(RELEASE_256K)
	cp ptos256*.img $(RELEASE_256K)
	$(call copy-resources,$(RELEASE_256K))
	$(call copy-docs,$(RELEASE_256K),256k)
	cd $(RELEASE_DIR) && zip -9 -r $(notdir $(RELEASE_256K)).zip $(notdir $(RELEASE_256K))
	rm -r $(RELEASE_256K)

.PHONY: release-192k
RELEASE_192K = $(RELEASE_DIR)/ptos-192k-$(VERSION)
release-192k:
	$(call build-config-all-countries,atari192)
	mkdir $(RELEASE_192K)
	cp ptos192*.img $(RELEASE_192K)
	$(call copy-resources,$(RELEASE_192K))
	$(call copy-docs,$(RELEASE_192K),192k)
	cd $(RELEASE_DIR) && zip -9 -r $(notdir $(RELEASE_192K)).zip $(notdir $(RELEASE_192K))
	rm -r $(RELEASE_192K)

.PHONY: release-cartridge
RELEASE_CARTRIDGE = $(RELEASE_DIR)/ptos-cartridge-$(VERSION)
release-cartridge:
	$(call build-config,cartridge)
	mkdir $(RELEASE_CARTRIDGE)
	cp ptoscart.img ptos.stc $(RELEASE_CARTRIDGE)
	$(call copy-docs,$(RELEASE_CARTRIDGE),cartridge)
	cd $(RELEASE_DIR) && zip -9 -r $(notdir $(RELEASE_CARTRIDGE)).zip $(notdir $(RELEASE_CARTRIDGE))
	rm -r $(RELEASE_CARTRIDGE)

.PHONY: release-aranym
RELEASE_ARANYM = $(RELEASE_DIR)/ptos-aranym-$(VERSION)
release-aranym:
	$(call build-config,aranym)
	mkdir $(RELEASE_ARANYM)
	cp ptos-aranym.img $(RELEASE_ARANYM)
	$(call copy-resources,$(RELEASE_ARANYM))
	$(call copy-docs,$(RELEASE_ARANYM),aranym)
	cd $(RELEASE_DIR) && zip -9 -r $(notdir $(RELEASE_ARANYM)).zip $(notdir $(RELEASE_ARANYM))
	rm -r $(RELEASE_ARANYM)

.PHONY: release-firebee
RELEASE_FIREBEE = $(RELEASE_DIR)/ptos-firebee-$(VERSION)
release-firebee:
	$(call build-config,firebee)
	mkdir $(RELEASE_FIREBEE)
	cp ptosfb.s19 $(RELEASE_FIREBEE)
	$(call copy-resources,$(RELEASE_FIREBEE))
	$(call copy-docs,$(RELEASE_FIREBEE),firebee)
	cd $(RELEASE_DIR) && zip -9 -r $(notdir $(RELEASE_FIREBEE)).zip $(notdir $(RELEASE_FIREBEE))
	rm -r $(RELEASE_FIREBEE)

.PHONY: release-amiga-rom
RELEASE_AMIGA_ROM = $(RELEASE_DIR)/ptos-amiga-rom-$(VERSION)
release-amiga-rom:
	$(call build-config,amiga-kickdisk)
	mkdir $(RELEASE_AMIGA_ROM)
	cp ptos-amiga.rom ptos-kickdisk.adf $(RELEASE_AMIGA_ROM)
	$(call build-config,amiga-vampire)
	cp ptos-vampire.rom $(RELEASE_AMIGA_ROM)
	$(call copy-resources,$(RELEASE_AMIGA_ROM))
	$(call copy-docs,$(RELEASE_AMIGA_ROM),amiga-rom)
	cd $(RELEASE_DIR) && zip -9 -r $(notdir $(RELEASE_AMIGA_ROM)).zip $(notdir $(RELEASE_AMIGA_ROM))
	rm -r $(RELEASE_AMIGA_ROM)

.PHONY: release-amiga-floppy
RELEASE_AMIGA_FLOPPY = $(RELEASE_DIR)/ptos-amiga-floppy-$(VERSION)
release-amiga-floppy:
	$(call build-config,amigaflop)
	mkdir $(RELEASE_AMIGA_FLOPPY)
	cp ptos.adf $(RELEASE_AMIGA_FLOPPY)
	$(call build-config,amigaflop-vampire)
	cp ptos-vampire.adf $(RELEASE_AMIGA_FLOPPY)
	$(call copy-resources,$(RELEASE_AMIGA_FLOPPY))
	$(call copy-docs,$(RELEASE_AMIGA_FLOPPY),amiga-floppy)
	cd $(RELEASE_DIR) && zip -9 -r $(notdir $(RELEASE_AMIGA_FLOPPY)).zip $(notdir $(RELEASE_AMIGA_FLOPPY))
	rm -r $(RELEASE_AMIGA_FLOPPY)

.PHONY: release-m548x-dbug
RELEASE_M548X_DBUG = $(RELEASE_DIR)/ptos-m548x-dbug-$(VERSION)
release-m548x-dbug:
	$(call build-config,m548x-dbug)
	mkdir $(RELEASE_M548X_DBUG)
	cp ptos-m548x-dbug.s19 $(RELEASE_M548X_DBUG)
	$(call copy-docs,$(RELEASE_M548X_DBUG),m548x-dbug)
	cd $(RELEASE_DIR) && zip -9 -r $(notdir $(RELEASE_M548X_DBUG)).zip $(notdir $(RELEASE_M548X_DBUG))
	rm -r $(RELEASE_M548X_DBUG)

.PHONY: release-m548x-bas
RELEASE_M548X_BAS = $(RELEASE_DIR)/ptos-m548x-bas-$(VERSION)
release-m548x-bas:
	$(call build-config,m548x-bas)
	mkdir $(RELEASE_M548X_BAS)
	cp ptos-m548x-bas.s19 $(RELEASE_M548X_BAS)
	$(call copy-docs,$(RELEASE_M548X_BAS),m548x-bas)
	cd $(RELEASE_DIR) && zip -9 -r $(notdir $(RELEASE_M548X_BAS)).zip $(notdir $(RELEASE_M548X_BAS))
	rm -r $(RELEASE_M548X_BAS)

.PHONY: release-prg
RELEASE_PRG = $(RELEASE_DIR)/ptos-prg-$(VERSION)
release-prg:
	$(call build-config,prg)
	mkdir $(RELEASE_PRG)
	cp ptos.prg $(RELEASE_PRG)
	$(call build-config-all-countries,prg)
	cp ptos*.prg $(RELEASE_PRG)
	$(call copy-resources,$(RELEASE_PRG))
	$(call copy-docs,$(RELEASE_PRG),prg)
	cd $(RELEASE_DIR) && zip -9 -r $(notdir $(RELEASE_PRG)).zip $(notdir $(RELEASE_PRG))
	rm -r $(RELEASE_PRG)

.PHONY: release-floppy
RELEASE_FLOPPY = $(RELEASE_DIR)/ptos-floppy-$(VERSION)
release-floppy:
	$(call build-config-all-countries,floppy)
	mkdir $(RELEASE_FLOPPY)
	cp ptos*.st $(RELEASE_FLOPPY)
	$(call copy-resources,$(RELEASE_FLOPPY))
	$(call copy-docs,$(RELEASE_FLOPPY),floppy)
	cd $(RELEASE_DIR) && zip -9 -r $(notdir $(RELEASE_FLOPPY)).zip $(notdir $(RELEASE_FLOPPY))
	rm -r $(RELEASE_FLOPPY)

.PHONY: release-raspi-resources
release-raspi-resources:
	@if [ -z '$(DEST)' ]; then \
	  echo 'DEST is not set; usage: make release-raspi-resources DEST=<archive-dir>' >&2; \
	  exit 1; \
	fi
	mkdir -p $(DEST)
	$(call copy-resources,$(DEST))
	# desk/emudesk-raspi.inf: same #R/#E/#Q/#M/#T/file-type-association
	# content deskapp.c's own built-in default generates (see
	# desk_inf_data1/desk_inf_data2), except the first #W window slot
	# has C:\*.* as its path instead of being empty -- deskmain.c opens
	# every #W slot with a non-empty path at boot, so this alone is what
	# makes the desktop come up with a window already open on the drive.
	cp desk/emudesk-raspi.inf $(DEST)/EMUDESK.INF
	# Opts every shipped Pi 1/2/3 card in to the legacy fake_vsync_isr
	# vblank interrupt CONF_WITH_RASPI_VSYNC_IRQ (bios/raspi_vsync.c) can
	# use for real vsync-driven VBL -- scoped off Pi 4 by the file itself,
	# and harmless where the firmware doesn't support it either way, see
	# bios/raspi-config.txt and doc/readme-raspi.md.
	cp bios/raspi-config.txt $(DEST)/config.txt
	# Like copy-docs, but readme.txt is rendered from Markdown with Atari
	# VT52 escapes (tools/md2atari.py) instead of a plain doc/readme-*.txt,
	# CRLF line endings included -- unix2dos refuses those escape bytes as
	# "binary", so it cannot do that part for us here.
	$(PYTHON) tools/md2atari.py doc/readme-raspi.md readme_emutos.txt >$(DEST)/readme.txt
	mkdir -p $(DEST)/doc
	cp $(DOCFILES) $(DEST)/doc
	find $(DEST)/doc -name '*.txt' -exec unix2dos '{}' ';'

.PHONY: release-emucon
RELEASE_EMUCON = $(RELEASE_DIR)/emucon
release-emucon:
	$(MAKE) distclean
	$(MAKE) -C cli
	mkdir $(RELEASE_EMUCON)
	cp cli/emucon2.tos cli/readme.txt $(RELEASE_EMUCON)
	unix2dos $(RELEASE_EMUCON)/readme.txt
	cd $(RELEASE_DIR) && zip -9 -r ptos-emucon-$(VERSION).zip emucon
	rm -r $(RELEASE_EMUCON)

# Main goal to build a full release distribution
.PHONY: release
release: distclean release-clean release-mkdir \
  release-src release-512k release-256k release-192k release-cartridge \
  release-aranym release-firebee release-amiga-rom release-amiga-floppy \
  release-m548x-dbug release-m548x-bas release-prg release-floppy \
  release-emucon
	$(MAKE) distclean
	@echo '# Packages successfully generated inside $(RELEASE_DIR)'
