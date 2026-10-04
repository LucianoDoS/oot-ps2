# OoT PS2 Port

Port de **The Legend of Zelda: Ocarina of Time** a PlayStation 2, basado en la descompilación [zeldaret/oot](https://github.com/zeldaret/oot).

## Estado comprobado — 4 de octubre de 2026

**Port parcial, todavía con fallos visuales y de rendimiento.**
Se puede iniciar una partida y salir al bosque; falta validar el juego completo.
El usuario confirma audio correcto y mejora de rendimiento, pero todavía hay
caídas según la cámara. El objetivo de 20 FPS estables sigue pendiente. Las
tres salas de Kokiri tienen sus recursos convertidos; el usuario confirma
que ya puede acceder a la zona que antes se bloqueaba. También confirma el
retrato del equipamiento y los objetos sobre las manos al recibirlos; se
corrige el evento del último fotograma que los omitía en PS2. La captura del
framebuffer real mejora la apertura y transparencia del inventario, y el aura
del título ya se mueve al combinar sus dos texturas; el usuario confirma ambas
correcciones. La última optimización reduce el coste de generar las tablas
de materiales y comprobar texturas en RAM. En una vista exterior de 12–14 FPS,
el usuario confirma 14–16 tras la primera mejora; la versión que también
optimiza los materiales de dos ciclos conserva ese rendimiento y la imagen
y el audio correctos. Sigue pendiente
completar la validación del resto del juego y estabilizar los 20 FPS.
El registro detallado
de las correcciones y pruebas está en [CHANGELOG_PS2_PORT.md](CHANGELOG_PS2_PORT.md).
Ya se compila la capa PS2 con ps2dev real
(GCC 15.2.0), y el ejecutable de diagnóstico arranca en PCSX2 2.8.0.
Las pruebas en el EE verifican matrices, trigonometría, formato de texto,
secciones críticas, colas de mensajes y temporizadores. El renderer envía
120 fotogramas; esto aún no constituye una validación visual del juego.

El juego completo ya enlaza: 720 objetos generan `build/PS2-ntsc12/oot_ps2.elf`.
En PCSX2 completa `Play_Init` y la ejecución de diagnóstico alcanzó 900
fotogramas del estado `Play` en la secuencia del título sin excepciones registradas.
Esto no confirma una imagen correcta ni jugabilidad. El conversor local adapta estructuras, vértices, matrices, listas
gráficas y cinemáticas; las texturas conservan su secuencia de bytes original.
La conversión cubre el título, escenas de demostración, dependencias de objetos,
recursos del selector de partida y los 38 bancos de instrumentos. El perfil
`play` incluye 211 segmentos y las 573 tablas de animación del jugador,
además de recursos iniciales de Kokiri y la casa de Link. Los JPEG conservan
sus bytes comprimidos; PS2 ahora decodifica los fondos de 320×240 a RGBA16 con
libjpeg y reconoce los rectángulos S2DEX, dibujados en tiras mediante la caché.
Las pruebas EE verifican píxeles, límites, entrada dañada y despacho del fondo;
queda comprobar su composición en las cámaras del juego. El intérprete
DSP pasa pruebas de PCM, mezcla, estéreo, ADPCM y PCM de 8 bits. El juego ya
envía PCM no silencioso a audsrv, pero falta verificar su fidelidad musical.
La traducción gráfica se ejecuta en un hilo con prioridad inferior a AudioMgr,
conservando el aviso DP hasta terminar el fotograma. El primer registro de la
nueva compilación produce 14,939 segundos de PCM en 15 segundos, sin bloques
descartados ni intervalos superiores a dos bloques. Esto verifica continuidad
de producción, no la calidad musical ni un objetivo de FPS.
Las capturas muestran el título con sus colores, cielo, luna, terreno, murallas
y Link montado en Epona. Corregida la matriz identidad nativa que deformaba el
escenario y el caballo. El renderer usa STQ para conservar la repetición y
perspectiva de las texturas; las pruebas EE verifican coordenadas, shifts,
paletas CI4 e IA4. Añadida generación esférica/lineal de coordenadas con LookAt.
El framebuffer de color usa RGB5551 CT16S, conservando profundidad de 24 bits
después de inicializar gsKit. CT16 cambiaba automáticamente la profundidad a 16 bits.
La caché comparte hasta 1760 KiB de VRAM entre 512 entradas según su tamaño y reutiliza
tablas del combinador; valida los bytes de fuentes y paletas para detectar
texturas modificadas en RAM. Reutiliza variantes de fotogramas anteriores,
conservando las usadas juntas en el fotograma actual. Todavía faltan fidelidad
visual completa y rendimiento estable en todas las vistas.
Los triángulos con la etapa estándar de niebla usan los parámetros reales
del juego y niebla nativa del GS; las pruebas EE verifican factores y límites.
Start abre el selector de partida y sus textos y botones se ven correctamente.
Las operaciones SRAM ya usan
una imagen de 32 KiB y el backend de memory card, conservando las escrituras
parciales. Una tarjeta virtual nueva `oot-port.ps2` está activa en la configuración
aislada; la tarjeta anterior se conserva. El diagnóstico real con libmc pasa
escritura, recarga tras cerrar PCSX2 y recuperación de una copia dañada.
El juego escribe imágenes verificadas en esa tarjeta. Dos archivos alternos,
con revisión y checksum, preservan el último guardado completo. Falta validar
la progresión de una partida completa. Corregida la lectura big-endian de la
tabla de letras del nombre; la prueba enlaza el cargador de fuentes real.
Las letras de 16×16 reservan 1 KiB en la caché; el diagnóstico conserva 128
texturas pequeñas entre pasadas. Los fundidos compatibles aplican alfa en GS
sin volver a convertir ni subir la textura.
Las instrucciones históricas de abajo describen la intención del port,
no garantizan que el juego arranque.

### Reproducir el trabajo local

Desde Ubuntu WSL, en esta carpeta:

```sh
bash tools/build_ps2_smoke.sh
bash tools/check_ps2_platform.sh
bash tools/check_ps2_game.sh
python3 tools/check_ps2_texture_words.py
python3 tools/test_ps2_soundfont_layout.py
```

El diagnóstico sin ROM se genera en `build/PS2/oot_ps2_smoke.elf`.
`tools/run_ps2_smoke.ps1` lo inicia con una configuración aislada de PCSX2
en `build/PCSX2-diagnostic`; requiere una BIOS y configuración ya disponibles.
Activa `HostFs` en esa configuración aislada para la prueba de lectura; usa
`ps2-rom-smoke.bin`, un archivo de 4 KiB generado por el script, sin ROM del juego.
El log se guarda en `build/PS2/pcsx2-smoke.log`.

El diagnóstico de fuentes necesita las cabeceras de texto de la extracción
NTSC 1.2 local; `OOT_UPSTREAM` permite indicar otra ruta. No lee la ROM al ejecutarse.

Para comprobar la tarjeta real con el backend del port:

```sh
bash tools/build_ps2_memorycard_smoke.sh
```

Desde PowerShell 7, `tools/run_ps2_memorycard_smoke.ps1 -InitializeNewCard`
crea una tarjeta nueva en `build/PCSX2-memorycard` y rechaza sobrescribir una
existente. Usa la BIOS/configuración local ya disponible. El diagnóstico escribe
solo en `OOT_PS2_TEST`; el juego usa `OOT_PS2`. Ejecuta el script una segunda vez
sin ese parámetro para verificar persistencia tras un reinicio completo.
Solo el diagnóstico, con el token explícito creado para la tarjeta nueva,
puede formatearla; el juego exige una tarjeta PS2 ya formateada en el puerto 1.

El build del juego usa la extracción local US NTSC 1.2. La procedencia de las
fuentes, versión del SDK y hash de ROM están en [UPSTREAM.md](UPSTREAM.md).
ROMs, recursos extraídos y salidas de compilación quedan fuera de Git.

Para la prueba actual de título y recursos iniciales, después de extraerlos:

```sh
/home/lucia/oot-ps2-tools/oot-upstream/.venv/bin/python tools/prepare_ps2_rom.py \
  --upstream /home/lucia/oot-ps2-tools/oot-upstream \
  --profile play --output build/PS2-ntsc12/oot.z64
/home/lucia/oot-ps2-tools/oot-upstream/.venv/bin/python tools/check_ps2_cutscenes.py \
  --upstream /home/lucia/oot-ps2-tools/oot-upstream
```

Desde PowerShell, `tools/run_ps2_game.ps1` inicia el ELF. El log queda en
`build/PS2-ntsc12/pcsx2-game.log`. Los perfiles `title` y `play` son selecciones
para diagnóstico; `play` aún no contiene todos los recursos de una partida completa.
Incluye el bosque inicial, Bosques Perdidos, casas de los Kokiri y su tienda.
La síntesis de audio conserva 32 kHz y el port convierte su PCM de forma continua
a 48 kHz para audsrv; la calidad audible sigue en comprobación.

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
