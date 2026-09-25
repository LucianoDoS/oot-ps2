# OoT PS2 Port

Port de **The Legend of Zelda: Ocarina of Time** a PlayStation 2, basado en la descompilación [zeldaret/oot](https://github.com/zeldaret/oot).

## Estado comprobado — 25 de septiembre de 2026

**Todavía no es un port jugable.** Ya se compila la capa PS2 con ps2dev real
(GCC 15.2.0), y el ejecutable de diagnóstico arranca en PCSX2 2.8.0.
Las pruebas en el EE verifican matrices, trigonometría, formato de texto,
secciones críticas, colas de mensajes y temporizadores. El renderer envía
120 fotogramas; esto aún no constituye una validación visual del juego.

El build completo llegó al enlace con 715 objetos. Sigue fallando por
referencias pendientes a recursos y servicios de N64. Se están adaptando
los recursos al orden de bytes de PS2 y el código de overlays a funciones
nativas residentes. El procesamiento DSP de las tareas de audio sigue pendiente.
Las instrucciones históricas de abajo describen la intención del port,
no garantizan que el juego arranque.

### Reproducir el trabajo local

Desde Ubuntu WSL, en esta carpeta:

```sh
bash tools/build_ps2_smoke.sh
bash tools/check_ps2_platform.sh
bash tools/check_ps2_game.sh
python3 tools/check_ps2_texture_words.py
```

El diagnóstico sin ROM se genera en `build/PS2/oot_ps2_smoke.elf`.
`tools/run_ps2_smoke.ps1` lo inicia con una configuración aislada de PCSX2
en `build/PCSX2-diagnostic`; requiere una BIOS y configuración ya disponibles.
El log se guarda en `build/PS2/pcsx2-smoke.log`.

El build del juego usa la extracción local US NTSC 1.2. La procedencia de las
fuentes, versión del SDK y hash de ROM están en [UPSTREAM.md](UPSTREAM.md).
ROMs, recursos extraídos y salidas de compilación quedan fuera de Git.

---

## Estructura del Proyecto

```
oot-ps2/
├── src/
│   ├── platform/
│   │   └── ps2/
│   │       ├── ps2_main.c              ← Entry point (reemplaza boot.s de N64)
│   │       ├── libultra_stub/
│   │       │   ├── osThread.c          ← Threads → EE kernel threads
│   │       │   ├── osMessage.c         ← Message queues → semáforos PS2
│   │       │   ├── osTimer.c           ← Timers → EE Timer 0
│   │       │   ├── osContPad.c         ← DualShock 2 → N64 controller
│   │       │   └── osMemory.c          ← Memoria, cache ops
│   │       ├── gfx/
│   │       │   ├── rdp_to_gs.c         ← Intérprete de Display Lists F3DEX2
│   │       │   ├── f3dex2_ps2.c        ← Comandos de geometría (vértices, triángulos, matrices)
│   │       │   └── ps2_texture.c       ← Conversión de texturas N64 → PS2 + upload VRAM
│   │       ├── audio/
│   │       │   └── ps2_audio_driver.c  ← Mixer de software + audsrv
│   │       └── io/
│   │           └── ps2_controller.c    ← Rumble DualShock 2 + saves en MemCard
├── include/
│   └── ps2/
│       ├── ps2_init.h                  ← Inicialización del sistema PS2
│       ├── ps2_types.h                 ← Tipos libultra + utilidades endianness
│       └── rdp_to_gs.h                 ← Estado del renderer
├── linker/
│   └── linkfile.ps2                    ← Linker script para ELF PS2
├── tools/
│   └── setup_ps2_toolchain.sh          ← Instala ps2toolchain, ps2sdk, gsKit
└── Makefile                            ← Build system multi-plataforma (N64 + PS2)
```

---

## Requisitos Previos

### Software
- **WSL2 / Linux** — el toolchain de PS2 solo compila en Linux
- **Python 3** + dependencias de `zeldaret/oot` (extracción de assets)
- **ROM de OoT** NTSC 1.0 (SHA1: `cee6bc3c2a634b41728f2af8da54d9bf8cc14099`)

### Toolchain PS2
```bash
# Instalar todo automáticamente (WSL2/Ubuntu):
./tools/setup_ps2_toolchain.sh

# Después:
source ~/.ps2dev_env
```

---

## Compilar

### Paso 1: Obtener el código fuente del juego

```bash
# Clonar OoT decomp
git clone https://github.com/zeldaret/oot.git

# Copiar el código del juego a este proyecto
# (src/code/, src/boot/, src/overlays/, assets/, etc.)
```

### Paso 2: Compilar para PS2

```bash
# Cargar el entorno PS2
source ~/.ps2dev_env

# Compilar
make PLATFORM=PS2

# Output: build/PS2/oot_ps2.elf
```

### Paso 3: Probar en PCSX2

```bash
~/pcsx2.AppImage build/PS2/oot_ps2.elf
```

---

## Estado Actual

| Subsistema | Estado | Notas |
|---|---|---|
| Build system | ✅ Completo | Makefile multi-plataforma |
| Linker script PS2 | ✅ Completo | Mapa de memoria EE |
| Entry point | ✅ Completo | ps2_main.c |
| Threads (osThread) | ✅ Completo | EE kernel threads |
| Message queues | ✅ Completo | Semáforos EE |
| Timers | ✅ Completo | EE Timer 0 @ 147 MHz |
| Controller input | ✅ Completo | DualShock 2 → N64 format |
| Memoria / cache | ✅ Completo | 8 MB heap simulado |
| Display List interpreter | ✅ Estructura | ~20 opcodes implementados |
| Geometría F3DEX2 | ✅ Estructura | VTX, TRI1, TRI2, MTX |
| Texturas | ✅ Estructura | RGBA16, RGBA32, IA, I, CI |
| Audio mixer | ✅ Estructura | 16 voces PCM + audsrv |
| Saves (MemCard) | ✅ Estructura | mc0:/OOT_PS2/save.dat |
| Lighting | 🔴 TODO | Luces del juego |
| Fog | 🔴 TODO | Niebla volumétrica |
| Z-buffer | 🟡 Parcial | Básico, sin depth bias |
| Overlays | 🔴 TODO | Sistema de overlays OoT |
| Audio sequences | 🔴 TODO | N64 Seq → SPU2 |

---

## Mapeo de Controles

| N64 | PS2 DualShock 2 |
|---|---|
| A | ✖ Cross |
| B | □ Square |
| Z | L2 |
| Start | Start |
| D-Pad | D-Pad |
| L/R Triggers | L1 / R1 |
| C-Up | △ Triangle |
| C-Down | R2 |
| C-Left | L3 |
| C-Right | ○ Circle |
| Joystick | Analog Left |

---

## Referencias

- [zeldaret/oot](https://github.com/zeldaret/oot) — Descompilación original
- [ps2dev/ps2toolchain](https://github.com/ps2dev/ps2toolchain) — Toolchain GCC para PS2
- [ps2dev/ps2sdk](https://github.com/ps2dev/ps2sdk) — SDK oficial
- [ps2dev/gsKit](https://github.com/ps2dev/gsKit) — Librería gráfica GS
- [F3DEX2 wiki](https://wiki.cloudmodding.com/oot/F3DEX2) — Referencia de microcode
- [SM64 port](https://github.com/sm64-port/sm64-port) — Proyecto similar de referencia
