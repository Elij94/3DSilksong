#---------------------------------------------------------------------------------
# 3DSilksong - fan port of Hollow Knight: Silksong for the New 3DS family (HD only: 800x240, 124 MB)
# Build with devkitPro (3ds-dev: libctru, citro2d, citro3d):  make        make cia        make data
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET      := 3dsilksong
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := source
ROMFS       := romfs
DATAFILE    := 3dsilksong.romfs

APP_TITLE       := 3DSilksong
APP_DESCRIPTION := Fan port (New 3DS, personal use)
APP_AUTHOR      := 3DSilksong
ICON            := cia/icon.png
RSF             := cia/3dsilksong.rsf
BANNER          := cia/banner.png

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections -fno-strict-aliasing \
           $(ARCH) $(INCLUDE) -D__3DS__ -DSS_HD
CFLAGS  += -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS := $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD   := $(CC)
export OFILES_SOURCES := $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN     := $(addsuffix .o,$(BINFILES))
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES         := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# the icon is optional until there is one (cia/icon.png, 48x48): devkitPro's default otherwise
ifneq ($(wildcard $(TOPDIR)/$(ICON)),)
export APP_ICON := $(TOPDIR)/$(ICON)
endif
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
# FULL=1 puts the game data inside the .3dsx (handy on an emulator); by default it is the SD card file
ifeq ($(FULL),1)
export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean cia data

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia

VERSION = $$(sed -n 's/.*SS_VERSION "\(.*\)"/\1/p' source/version.h)
LITE := $(BUILD)/romfs_lite
# the small CIA: the code, and a RomFS holding only a README (the game data is the file on the SD card).
# Needs bannertool + makerom on PATH, cia/icon.png (48x48), cia/banner.png (256x128) and cia/banner.wav.
cia: all
	@bannertool makebanner -i $(BANNER) -a cia/banner.wav -o $(BUILD)/banner.bnr
	@bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $(ICON) -o $(BUILD)/icon.icn
	@mkdir -p $(LITE); echo "The game data is SD:/3ds/3dsilksong/$(DATAFILE)." > $(LITE)/README.txt
	@makerom -f cia -o $(TARGET).cia -elf $(TARGET).elf -rsf $(RSF) -DROMFS_DIR=$(LITE) -icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr -exefslogo -target t
	@echo built ... $(TARGET).cia

# the game data file for the SD card (SD:/3ds/3dsilksong/$(DATAFILE)): $(ROMFS)/ as one RomFS image.
# MKROMFS defaults to mkromfs3ds; on Windows without it: make data MKROMFS="python C:/HK3DS/tools/mkromfs.py"
MKROMFS ?= mkromfs3ds
data:
	@mkdir -p $(ROMFS); echo $(VERSION) > $(ROMFS)/version.txt
	@$(MKROMFS) $(ROMFS) $(DATAFILE)
	@echo built ... $(DATAFILE)

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx : $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf  : $(OFILES)
$(OFILES_SOURCES) : $(HFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
