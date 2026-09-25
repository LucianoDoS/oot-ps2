# Changelog — Revisión y corrección del port a PS2

Este documento resume todo lo revisado, corregido y verificado sobre el
port de Ocarina of Time a PS2. Está pensado para que puedas retomar el
trabajo sabiendo exactamente qué se tocó, por qué, y qué falta.

## Cómo se verificó esto

No basta con leer el código para saber si compila. Para este port en
concreto:

1. Se creó un conjunto de headers falsos que imitan las firmas de
   ps2sdk/gsKit (`kernel.h`, `libpad.h`, `libmc.h`, `gsKit.h`...) para
   poder pasar el compilador por la capa de PS2 sin tener el toolchain
   real instalado.
2. Se compiló en **modo 32 bits** (`-m32`), porque el EE es de 32 bits;
   en 64 bits todos los `_Static_assert` de tamaño de struct dan falsos
   negativos.
3. Se compilaron los **structs reales del juego** (`include/ultra64/*.h`
   con `-D_LANGUAGE_C=1 -DNON_MATCHING=1`) y se comparó cada firma que
   implementa la capa PS2 contra el prototipo real. Esto encontró varios
   errores propios (ver abajo) que una simple lectura no habría pillado.
4. Se compilaron los **637 archivos fuente del juego** (`src/code`,
   `src/boot`, `src/overlays`) con los flags finales del Makefile.

**Resultado de la última pasada:** 0 errores de código en los 637
archivos del juego. Los que no compilan (462) fallan únicamente por
`assets/... No such file or directory` — assets no extraídos, que es
un paso previo de las herramientas del decomp, no un bug de código.

## Build system

- **Makefile**: compilaba 145 archivos; ahora compila los 656 reales.
  `src/overlays` (511 archivos: todos los actores, `z_title.c`,
  `z_select.c`, el menú de pausa) y `src/audio` no estaban en absoluto
  en el build.
- Excluidos correctamente los 18 archivos `*.inc.c` (fragmentos que
  otros `.c` incluyen, no unidades de compilación) y `fault_n64.c`
  (choca con `fault.c` cuando `PLATFORM_N64=0`).
- Flags que faltaban y bloqueaban el build entero: `-Iinclude/libc`,
  `-DOOT_VERSION=NTSC_1_0`, `-D_LANGUAGE_C=1`, `-DNON_MATCHING=1`,
  `-DDEBUG_FEATURES=0`, `-DOOT_DEBUG=0`, `BUILD_DATE`/`BUILD_CREATOR`.
- Generación automática de dependencias (`-MMD -MP`).

## Arranque (`ps2_main.c`, `ps2_boot.c` nuevo)

- Se llamaba a `Game_Main()`, una función que no existe en ningún sitio
  del decomp. Ahora se replica la cadena real:
  `bootproc() → Idle_ThreadEntry → Main() → Graph_ThreadEntry`.
- `Sched_Init()` se llamaba con 0 argumentos; la real lleva 6
  (`Scheduler*, stack, priority, viModeType, arg4, IrqMgr*`).
- Orden de inicialización del IOP corregido: reset del IOP primero,
  luego `SIO2MAN → PADMAN`, `MCMAN → MCSERV`, `LIBSD → audsrv.irx`
  (antes se cargaba PADMAN sin SIO2MAN y se llamaba a `audsrv_init()`
  sin cargar su IRX).
- `rdp_to_gs_init()` no la llamaba nadie → puntero de GS a `NULL`.

## Stubs de libultra (`src/platform/ps2/libultra_stub/`)

- **`osMessage.c`**: `OSMesgQueue` estaba inflado (~44 bytes en vez de
  los 24 reales), corrompiendo la memoria vecina en cada
  `osCreateMesgQueue()`. Ahora el struct es del tamaño exacto y los
  semáforos van en tabla lateral.
- **`osThread.c`**: pila del hilo confundía tope con base y usaba
  tamaño fijo de 4 KB. Ahora usa pilas propias de 32 KB gestionadas
  aparte, sin tocar el struct `OSThread` del juego.
- **`osTimer.c`** (antes roto): los timers se registraban en un array
  que nadie recorría; ahora hay una lista enlazada real y
  `ps2_timer_tick()` la procesa cada frame.
- **`osTask.c`**: `osSpTaskStart` no avisaba nunca de que la tarea
  gráfica había terminado → el hilo de gráficos se quedaba colgado para
  siempre en el primer frame. Ahora dispara los eventos SP/DP.
- **`osEvent.c`, `osVi.c`, `osRom.c`, `osMisc.c`** (nuevos, no
  existían): `osSetEventMesg`/retrace vertical, lectura de la "ROM" por
  fichero vía DMA, y vibración conectada a `osMotorStart/Stop` (que en
  realidad son **macros**, no funciones — error propio corregido tras
  compilar contra el header real).
- **`osContPad.c`**: doble inicialización de puertos eliminada
  (`ps2_main.c` y este archivo abrían los mismos puertos con buffers
  DMA distintos); mapeo de botones C corregido (Circle/R3 chocaban).

### Errores propios detectados sólo al compilar contra los headers reales

- `osAppNMIBuffer` estaba declarado `void*`; el real es
  `s32 osAppNMIBuffer[0x10]` (un array, no un puntero).
- `osMotorStart`/`osMotorStop` son macros que expanden a
  `__osMotorAccess()`, no funciones — implementarlas como funciones no
  las habría enlazado nunca.
- `osAfterPreNMI`, `osContStartReadData`, `osContStartQuery`,
  `osContSetCh` devuelven `s32`, no `void`.
- `G_MV_VIEWPORT`/`G_MV_L0` usados con los valores del F3DEX **antiguo**
  (`0x80`/`0x86`) en vez de los de F3DEX2 (`8`/`10`). `gbi.h` pone el
  comentario "F3DEX_GBI_2" justo en la rama contraria, es fácil
  equivocarse.

## Gráficos (`src/platform/ps2/gfx/`)

- **`ps2_segment.h`** (nuevo): traducción de direcciones segmentadas
  (`gSegments[]`), que antes no se hacía en ningún sitio — cualquier
  puntero a vértices/texturas en una display list real se interpretaba
  como dirección literal.
- **`rdp_to_gs.c`**: `G_VTX` (0x01) colisionaba con `G_SPNOOP` en el
  switch — dos `case` con el mismo valor ni compilan. La carga de
  vértices caía en la rama de "no hacer nada".
- **`f3dex2_ps2.c`**: viewport nunca se configuraba (buscaba el índice
  en el opcode equivocado), `G_POPMTX` leía la palabra que no era,
  geometry mode borraba los 8 bits altos por una máscara incompleta.
- **`rdp_color.c`**: faltaban `G_SETOTHERMODE_H/L` (los que OoT usa de
  verdad); dos archivos distintos escribían el registro `TEST` del GS
  por separado y se pisaban.

## Audio y guardado

- Colisión de símbolos con el decomp real: `Audio_Init` y
  `func_800F4C58` ya existen en `src/audio/game/general.c` con otra
  firma. Todo renombrado a `PS2Audio_*`.
- Remuestreo del mezclador perdía el acarreo (`&= 0xFFFF` descartaba
  bits en sonidos agudos).
- Guardado reescrito con la API real de `libmc` (`mcOpen`/`mcSync`...);
  antes usaba `open`/`read`/`write` que no aplican a `mc0:` en PS2, y
  nada de este archivo lo llamaba nadie.

## Linker (`linker/linkfile.ps2`, `tools/gen_segment_syms.py` nuevo)

- No definía ninguno de los ~165 símbolos `_xxxSegmentStart/End/...`
  que el juego referencia. `linkfile.ps2` define a mano los que hacen
  falta para arrancar; `gen_segment_syms.py` genera automáticamente el
  resto leyendo el código (`linker/segments.ld`).

## Endianness de assets (`tools/swap_assets.py`, nuevo)

El hallazgo más fácil de pasar por alto de todo el port: hay DOS clases
de display list — las de los **assets** (extraídas de la ROM,
big-endian) y las que el juego **construye en tiempo de ejecución**
(compiladas para PS2, little-endian). Ningún modo de lectura acierta
con las dos. La solución es convertir los assets a little-endian una
vez al extraerlos; `swap_assets.py` lo hace por tipo de dato (voltea
`.dlist`/`.mtx` en palabras de 16/32 bits, respeta los 4 bytes de color
de cada `.vtx`, y no toca en absoluto las texturas de 1 byte como I4/I8,
que se romperían si se voltean).

## Lo que NO está resuelto (y por qué no es un simple parche más)

- **Combinador de color del RDP** (`G_SETCOMBINE`): decide cómo se
  mezclan textura/color/sombreado en cada superficie. Sin esto casi
  todo sale con el color equivocado. Requiere traducir la máquina de
  combinación de N64 a los TFX del GS, e iterar mirando resultados en
  pantalla.
- **Síntesis de audio real** (música, no sólo efectos): el motor de
  audio del decomp (`src/audio/`) no está ni copiado al port. Portar la
  reproducción de secuencias MIDI-like de N64 es un subsistema entero.
- **Relocalización de overlays**: los actores se enlazan estáticamente
  en vez de cargarse en runtime a una dirección variable, que es como
  funciona realmente el sistema de overlays de N64.

Estos tres puntos son trabajo de meses, no bugs puntuales, y necesitan
verse correr en la consola/emulador para iterar — no se pueden terminar
"a ciegas".
