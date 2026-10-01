# ============================================================
#  OoT Port — Multi-Platform Makefile  (REVISADO)
#
#  Uso:
#    make PLATFORM=PS2            (por defecto)
#    make PLATFORM=PS2 DEBUG=1    (con -g y logs del intérprete DL)
#    make info                    (diagnóstico de qué se va a compilar)
#    make clean
#
#  Cambios respecto a la versión anterior:
#    * Se compilan de verdad src/overlays/** y src/audio/** (antes se
#      ignoraban por completo: eran ~511 + N archivos fuera del build).
#    * Generación automática de dependencias (-MMD -MP): tocar un .h
#      ahora recompila lo que corresponde.
#    * Los stubs de audio de alto nivel (ps2_seq.c / ps2_sfx.c) quedan
#      FUERA del build por defecto, porque redefinen símbolos que ya
#      existen en src/audio del decomp (Audio_PlaySequence, etc.).
#      Actívalos sólo si aún no has copiado src/audio:
#         make PLATFORM=PS2 PS2_AUDIO_STUBS=1
#    * Enlace con $(LD) vía gcc pero SIN -T propio por defecto: ps2sdk
#      ya trae un linkfile correcto. Usa OOT_CUSTOM_LINKER=1 si quieres
#      forzar linker/linkfile.ps2.
# ============================================================

PLATFORM ?= PS2
DEBUG    ?= 0
V        ?= 0

# ──────────────────────────────────────────────
#  Versión de OoT a portar
#
#  OBLIGATORIO: sin OOT_VERSION, include/line_numbers.h corta el build
#  con #error "Unsupported OOT version". El Makefile anterior no lo
#  definía, así que ni un solo archivo del juego llegaba a compilar.
#
#  Valores válidos (include/versions.h): NTSC_1_0, NTSC_1_1, PAL_1_0,
#  NTSC_1_2, PAL_1_1, GC_JP, GC_US, GC_EU, GC_EU_MQ_DBG, IQUE_CN...
# ──────────────────────────────────────────────
OOT_VERSION ?= NTSC_1_0

# Datos de build que espera src/boot/build.c
BUILD_CREATOR ?= oot-ps2-port
BUILD_DATE    ?= $(shell date '+%b %e %Y')
BUILD_TIME    ?= $(shell date '+%H:%M:%S')

# Silencioso salvo V=1
ifeq ($(V),1)
  Q :=
else
  Q := @
endif

# ──────────────────────────────────────────────
#  Toolchain por plataforma
# ──────────────────────────────────────────────
ifeq ($(PLATFORM), PS2)
  PS2DEV    ?= /usr/local/ps2dev
  PS2SDK    ?= $(PS2DEV)/ps2sdk
  GSKIT     ?= $(PS2DEV)/gsKit

  PREFIX    := $(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-
  CC        := $(PREFIX)gcc
  AS        := $(PREFIX)gcc
  LD        := $(PREFIX)gcc
  AR        := $(PREFIX)ar
  OBJCOPY   := $(PREFIX)objcopy
  STRIP     := $(PREFIX)strip

  TARGET    := oot_ps2.elf

  PLATFORM_CFLAGS := \
    -D_EE \
    -DPLATFORM_PS2=1 \
    -DPLATFORM_N64=0 \
    -DPLATFORM_GC=0 \
    -DPLATFORM_IQUE=0 \
    -DF3DEX_GBI_2 \
    -DOOT_PS2_PORT=1 \
    -D_LANGUAGE_C=1 \
    -DNON_MATCHING=1 \
    -G0 \
    -mno-gpopt \
    -fno-builtin-printf

  PLATFORM_INCLUDES := \
    -Iinclude/ps2 \
    -I$(PS2SDK)/ee/include \
    -I$(PS2SDK)/common/include \
    -I$(PS2SDK)/ports/include \
    -I$(GSKIT)/include

  # NOTA: el orden importa. gsKit depende de dmakit; audsrv de libsd.
  PLATFORM_LIBS := \
    -L$(PS2SDK)/ee/lib \
    -L$(PS2SDK)/ports/lib \
    -L$(GSKIT)/lib \
    -lgskit_toolkit \
    -lgskit \
    -ldmakit \
    -laudsrv \
    -lpad \
    -lmc \
    -lpatches \
    -ljpeg \
    -lm \
    -lc \
    -lkernel

else
  # ── N64 ──────────────────────────────────────
  N64_INST  ?= /opt/n64chain
  PREFIX    := $(N64_INST)/bin/mips64-elf-
  CC        := $(PREFIX)gcc
  AS        := $(PREFIX)gcc
  LD        := $(PREFIX)ld
  OBJCOPY   := $(PREFIX)objcopy

  TARGET    := oot.z64

  PLATFORM_CFLAGS := \
    -DPLATFORM_N64=1 \
    -DPLATFORM_PS2=0 \
    -DF3DEX_GBI_2 \
    -mabi=32 \
    -march=vr4300

  PLATFORM_INCLUDES :=
  PLATFORM_LIBS     :=
endif

# ──────────────────────────────────────────────
#  Flags comunes
# ──────────────────────────────────────────────
BUILD_DIR ?= build/$(PLATFORM)

# Optional roots from a local zeldaret extraction. Keep ROM-derived data out of Git.
OOT_UPSTREAM ?=
OOT_ASSET_VERSION ?= ntsc-1.2

ifeq ($(DEBUG),1)
  OPT_FLAGS := -O0 -g
  PLATFORM_CFLAGS += -DRDP_GS_DEBUG=1 -DDEBUG_FEATURES=1 -DOOT_DEBUG=1
else
  OPT_FLAGS := -O2
  # DEBUG_FEATURES y OOT_DEBUG tienen que estar DEFINIDOS aunque valgan 0:
  # el juego los usa en #if y en expresiones normales (game.c, padmgr.c,
  # z_play.c...). Si no existen, esos archivos no compilan.
  PLATFORM_CFLAGS += -DNDEBUG=1 -DDEBUG_FEATURES=0 -DOOT_DEBUG=0
endif

# Defines comunes a debug y release
PLATFORM_CFLAGS += \
  -DMML_VERSION=MML_VERSION_OOT \
  -DOOT_VERSION=$(OOT_VERSION) \
  -DBUILD_CREATOR='"$(BUILD_CREATOR)"' \
  -DBUILD_DATE='"$(BUILD_DATE)"' \
  -DBUILD_TIME='"$(BUILD_TIME)"'

# -Wno-unknown-pragmas: el decomp usa #pragma increment_block_number,
#    que sólo entiende el compilador IDO original.
# -fno-strict-aliasing: obligatorio, el código del juego hace type punning
#    por todas partes (Gfx*, segmentos, uniones de actores).
CFLAGS := \
  -std=gnu11 \
  -Wall \
  -Wextra \
  -Wno-unused-parameter \
  -Wno-unknown-pragmas \
  -Wno-missing-braces \
  -fno-common \
  -fno-strict-aliasing \
  $(OPT_FLAGS) \
  $(PLATFORM_CFLAGS)

# -Iinclude/libc es OBLIGATORIO y faltaba.
# ultra64.h hace #include "assert.h", "math.h", "string.h"... esperando
# las versiones del decomp que viven en include/libc/. Sin esa ruta, el
# preprocesador cogía las de la libc del sistema y el build moría con
# "conflicting types for 'bzero'" en cada archivo.
INCLUDES := \
  -Iinclude \
  -Iinclude/libc \
  -Isrc \
  $(PLATFORM_INCLUDES)

ifneq ($(OOT_UPSTREAM),)
INCLUDES += -I$(BUILD_DIR)/generated -I$(OOT_UPSTREAM) -I$(OOT_UPSTREAM)/extracted/$(OOT_ASSET_VERSION) \
            -I$(OOT_UPSTREAM)/build/$(OOT_ASSET_VERSION)
endif

DEPFLAGS = -MMD -MP -MF $(@:.o=.d)

# ──────────────────────────────────────────────
#  Descubrimiento de fuentes
#
#  IMPORTANTE: src/overlays tiene subdirectorios (ovl_*), por eso
#  hace falta `find` recursivo y NO un simple wildcard.
# ──────────────────────────────────────────────
# IMPORTANTE: hay que excluir los *.inc.c.
# Son fragmentos que otros .c incluyen con #include, NO unidades de
# compilación. Compilarlos sueltos da errores como "initializer element
# is not constant" o "CAM_DATA_MAX undeclared", porque les faltan los
# #define del archivo que los incluye. Son 18 archivos en el proyecto.
find_c = $(shell find $(1) -name '*.c' ! -name '*.inc.c' 2>/dev/null | sort)
find_s = $(shell find $(1) -name '*.s' ! -name '*.inc.s' 2>/dev/null | sort)

GAME_SRCS := \
  $(call find_c, src/code) \
  $(call find_c, src/boot) \
  $(call find_c, src/overlays) \
  $(call find_c, src/audio) \
  $(call find_c, src/libu64) \
  $(call find_c, src/libc) \
  $(call find_c, src/libc64)

ifeq ($(PLATFORM),PS2)
# CPU-side math and native storage, not N64 OS/hardware implementations.
GAME_SRCS += $(call find_c, src/libultra/gu) $(call find_c, src/buffers)
endif

# The remaining libultra services are supplied by the PS2 platform layer.

# El juego trae DOS implementaciones del gestor de fallos y hay que
# elegir una según la plataforma. Con PLATFORM_N64=0, include/fault.h
# expone la variante GC, así que fault_n64.c sobra y además choca:
# redefine struct FaultMgr.
GAME_SRCS := $(filter-out src/code/fault_n64.c src/libc64/__osMalloc_n64.c \
    src/libu64/loadfragment2_n64.c, $(GAME_SRCS))

ifeq ($(PLATFORM),PS2)
ifneq ($(OOT_UPSTREAM),)
GAME_SRCS := $(filter-out src/audio/tables/%.c,$(GAME_SRCS))
endif
endif

ifeq ($(PLATFORM),PS2)
GAME_SRCS := $(filter-out src/boot/driverominit.c src/boot/is_debug.c src/boot/is_debug_ique.c \
    src/boot/cic6105.c src/code/code_n64dd_800AD410.c src/code/code_n64dd_800AD4C0.c \
    src/code/z_moji.c src/libc/memset.c src/libc/memmove.c,$(GAME_SRCS))
ifneq ($(filter $(OOT_VERSION),GC_JP_MQ GC_US_MQ GC_EU_MQ GC_EU_MQ_DBG),)
GAME_SRCS := $(filter-out src/overlays/misc/ovl_kaleido_scope/z_lmap_mark_data.c \
    src/overlays/misc/ovl_map_mark_data/z_map_mark_data.c,$(GAME_SRCS))
else
GAME_SRCS := $(filter-out %_mq.c,$(GAME_SRCS))
endif
endif

PS2_PLATFORM_SRCS := \
  src/platform/ps2/ps2_main.c \
  src/platform/ps2/ps2_boot.c \
  $(call find_c, src/platform/ps2/libultra_stub) \
  $(call find_c, src/platform/ps2/gfx) \
  $(call find_c, src/platform/ps2/io) \
  src/platform/ps2/audio/ps2_audio_driver.c \
  src/platform/ps2/audio/ps2_rsp_audio.c \
  $(call find_c, src/platform/ps2/audio/rsp_hle)

# Stubs de audio de alto nivel: chocan con src/audio del decomp.
ifeq ($(PS2_AUDIO_STUBS),1)
  PS2_PLATFORM_SRCS += \
    src/platform/ps2/audio/ps2_seq.c \
    src/platform/ps2/audio/ps2_sfx.c
endif

N64_PLATFORM_SRCS := \
  $(call find_c, src/platform/n64) \
  $(call find_s, src/platform/n64)

ifeq ($(PLATFORM), PS2)
  ALL_SRCS := $(GAME_SRCS) $(PS2_PLATFORM_SRCS)
else
  ALL_SRCS := $(GAME_SRCS) $(N64_PLATFORM_SRCS)
endif

C_SRCS := $(filter %.c, $(ALL_SRCS))
S_SRCS := $(filter %.s, $(ALL_SRCS))

OBJS := $(patsubst %.c, $(BUILD_DIR)/%.o, $(C_SRCS))
OBJS += $(patsubst %.s, $(BUILD_DIR)/%.o, $(S_SRCS))
DEPS := $(OBJS:.o=.d)

ifeq ($(PLATFORM),PS2)
OBJS += $(BUILD_DIR)/generated/audsrv_irx.o
ifneq ($(OOT_UPSTREAM),)
OBJS += $(BUILD_DIR)/generated/rom_metadata.o
ROM_LDFLAGS := -Wl,-T,$(BUILD_DIR)/generated/rom_segments.ld
endif
endif

# ──────────────────────────────────────────────
#  Linker script
# ──────────────────────────────────────────────
ifeq ($(OOT_CUSTOM_LINKER),1)
  LDSCRIPT_FLAG := -T linker/linkfile.ps2
else
  ifeq ($(PLATFORM),PS2)
    LDSCRIPT_FLAG := -T $(PS2SDK)/ee/startup/linkfile -Wl,-zmax-page-size=128
  else
    LDSCRIPT_FLAG :=
  endif
endif

# ──────────────────────────────────────────────
#  Reglas
# ──────────────────────────────────────────────
.PHONY: all clean info check-toolchain

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS) Makefile
	@echo "[LD] $@  ($(words $(OBJS)) objetos)"
	@mkdir -p $(dir $@)
	$(Q)$(LD) $(CFLAGS) $(LDSCRIPT_FLAG) $(ROM_LDFLAGS) -o $@ $(OBJS) \
	    $(PLATFORM_LIBS) -Wl,-Map=$(BUILD_DIR)/oot.map
	@echo "[OK] $@"

$(BUILD_DIR)/%.o: %.c
	@echo "[CC] $<"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

# SDK code must use newlib headers, not the N64 libc replacements.
$(BUILD_DIR)/src/platform/ps2/%.o: src/platform/ps2/%.c
	@echo "[CC PS2] $<"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -Iinclude -Isrc $(PLATFORM_INCLUDES) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/generated/audsrv_irx.c: $(PS2SDK)/iop/irx/audsrv.irx
	@mkdir -p $(dir $@)
	$(Q)$(PS2SDK)/bin/bin2c $< $@ audsrv_irx

$(BUILD_DIR)/generated/audsrv_irx.o: $(BUILD_DIR)/generated/audsrv_irx.c
	$(Q)$(CC) $(CFLAGS) $(PLATFORM_INCLUDES) -c $< -o $@

ifneq ($(OOT_UPSTREAM),)
$(BUILD_DIR)/generated/rom_metadata.c $(BUILD_DIR)/generated/rom_segments.ld \
$(BUILD_DIR)/generated/assets/audio/sequence_sizes.h \
$(BUILD_DIR)/generated/assets/audio/soundfont_sizes.h &: tools/generate_ps2_rom_metadata.py \
    $(OOT_UPSTREAM)/baseroms/$(OOT_ASSET_VERSION)/baserom-decompressed.z64
	$(Q)$(OOT_UPSTREAM)/.venv/bin/python tools/generate_ps2_rom_metadata.py \
	    --upstream $(OOT_UPSTREAM) --output $(BUILD_DIR)/generated --version $(OOT_ASSET_VERSION)

$(OBJS): | $(BUILD_DIR)/generated/rom_metadata.c

$(BUILD_DIR)/generated/rom_metadata.o: $(BUILD_DIR)/generated/rom_metadata.c
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/src/audio/game/session_init.o: $(BUILD_DIR)/generated/assets/audio/sequence_sizes.h \
    $(BUILD_DIR)/generated/assets/audio/soundfont_sizes.h
endif

$(BUILD_DIR)/%.o: %.s
	@echo "[AS] $<"
	@mkdir -p $(dir $@)
	$(Q)$(AS) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

check-toolchain:
	@test -x "$(CC)" || { \
	  echo "ERROR: no encuentro $(CC)"; \
	  echo "       Instala el toolchain y haz 'source ~/.ps2dev_env'"; \
	  exit 1; }
	@echo "Toolchain OK: $$($(CC) --version | head -1)"

clean:
	rm -rf build/

info:
	@echo "Platform     : $(PLATFORM)"
	@echo "CC           : $(CC)"
	@echo "Target       : $(BUILD_DIR)/$(TARGET)"
	@echo "Linker script: $(if $(LDSCRIPT_FLAG),linker/linkfile.ps2,ps2sdk por defecto)"
	@echo ""
	@echo "Fuentes del juego     : $(words $(GAME_SRCS))"
	@echo "  src/code            : $(words $(call find_c, src/code))"
	@echo "  src/boot            : $(words $(call find_c, src/boot))"
	@echo "  src/overlays        : $(words $(call find_c, src/overlays))"
	@echo "  src/audio           : $(words $(call find_c, src/audio))"
	@echo "Fuentes de plataforma : $(words $(PS2_PLATFORM_SRCS))"
	@echo "Total objetos         : $(words $(OBJS))"

-include $(DEPS)
