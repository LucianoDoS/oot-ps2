# Changelog — Revisión y corrección del port a PS2

Este documento resume todo lo revisado, corregido y verificado sobre el
port de Ocarina of Time a PS2. Está pensado para que puedas retomar el
trabajo sabiendo exactamente qué se tocó, por qué, y qué falta.

## Renderer y síntesis nativa — 2026-09-30

El diagnóstico ejecutado en PCSX2 pasa PCM, mezcla, intercalado estéreo,
ADPCM, PCM de 8 bits e historial, además de geometría y sistema. Se prueba
el asignador permanente real y la agrupación de notificaciones de refresco.
Los tres tests de bancos de instrumentos pasan: estructuras compartidas,
bitfields de Sample y rechazo de punteros/libros de predicción inválidos.
**Todavía no es un juego jugable.** Tras las correcciones, el título sigue
animándose, alcanza 600 fotogramas enviados y entra al estado del selector de
partida al pulsar Start. La captura muestra el logo Zelda, cielo y parte de
Link/Epona; faltan escenario, texturas y rendimiento (unos 4 FPS).
El audio envía buffers no silenciosos: por ejemplo, 2240 bytes aceptados por
audsrv con amplitud máxima 12288. Esto verifica generación y envío de PCM,
no la fidelidad de la música. No se registraron excepciones TLB ni watchdog
en esa ejecución; se mantienen los logs privados en `build/PS2-ntsc12`.

- Corregido el orden de argumentos de los triángulos gouraud de gsKit: los
  colores se estaban interpretando como coordenadas. La captura posterior
  permite reconocer a Epona; aún tiene colores incorrectos y falta escenario.
- Recorte homogéneo de triángulos antes de dividir por W, comprobado con
  cruce del plano cercano y geometría completamente fuera del volumen.
- Corregidos campos del blender, ciclo final, control real de escritura Z,
  coordenadas de rectángulos y relación entre descriptores de carga/render
  de texturas, incluidos sus pasos entre filas.
- Combinador CPU para constantes prim/env y modulación por vértice; tabla de
  búsqueda y caché de VRAM para reducir trabajo. TEXEL1 sigue aproximado con
  TEXEL0; no reproduce todas las combinaciones, niebla ni perspectiva.
- Corregidas notificaciones de refresco que llenaban la cola del scheduler
  durante un fotograma lento y hacían perder los avisos SP/DP. El renderer
  había terminado, pero Graph esperaba hasta el watchdog. En PS2 se conserva
  un refresco pendiente; los demás mensajes mantienen su orden y semántica.
  El watchdog sigue siendo finito (60 segundos) y registra la última etapa.
- Los rectángulos en ciclos normales usan el combinador y la transparencia
  primitiva; antes los fundidos negros se dibujaban opacos. El ciclo COPY
  evita el combinador, como en el RDP.
- Perfil de datos ampliado a escenas de demostración y sus objetos por
  dependencias explícitas; se valida que todos los segmentos se hayan extraído.
- Conversión de fuentes de sonido por sus punteros y estructuras: instrumentos,
  percusión, efectos, envolventes, muestras, bucles y predictores. Datos de
  ondas comprimidas y bytecode de secuencias permanecen intactos.
- Intérprete de síntesis NEAD/OoT adaptado al EE, con código GPL-2.0-or-later
  de Mupen64Plus y procedencia en `src/platform/ps2/audio/rsp_hle/README.md`.
  El motor del juego sigue compilado como código nativo. Corregidos el orden
  de bytes de AudioCmd y los argumentos s8/u16, y la detección de punteros
  de muestras ya relocalizadas. Se registran tareas DSP y amplitud PCM.
- Corregido el retorno ausente en `AudioHeap_AllocPermanent` para PS2: el
  compilador devolvía la dirección de gAudioCtx y el DMA de instrumentos
  sobreescribía el estado del audio. La prueba EE verifica punteros, datos
  del caché y agotamiento del pool. Convertidos `title_static` y
  `parameter_static` para seguir probando el selector de partida.

## Enlace completo y carga de escenas — 2026-09-26

El juego completo ya enlaza con 707 objetos. PCSX2 completa `Play_Init` y la
ejecución de diagnóstico alcanzó 900 fotogramas del estado `Play` en la secuencia
del título sin excepciones registradas; **todavía no se ha validado como
jugable ni se ha confirmado visualmente su imagen**.

- Generador de metadatos ampliado con direcciones reales de mensajes y recursos,
  y microcódigo original usado para identificar tareas; sin recursos ficticios.
- Corregido el arranque que recursaba dentro de `memset`, usando newlib/SDK.
  Corregida la lectura de región de la cabecera para el orden de bytes EE.
- Completados servicios de estado SP/DP, FCSR y enumeración de hilos; los
  fallos explícitos del juego se escriben en consola antes de detenerse.
- Conversor privado `tools/prepare_ps2_rom.py`: usa las estructuras descubiertas
  por el extractor upstream, incluidos recursos sin una declaración XML inicial.
  Convierte campos por tamaño, preserva texturas y blobs de bytes, y registra
  cada recurso en un manifiesto. No convierte automáticamente toda la ROM.
- Conversión de Hyrule Field, recursos comunes, Link adulto, Epona y título.
  Incluye objetos del escenario y sus colisiones; el perfil `title` selecciona
  11 segmentos. Se corrigió el acceso inválido en `DynaPoly_AddBgActorToLookup`.
  Las cinemáticas distinguen palabras, medios enteros y campos de un byte.
  `tools/check_ps2_cutscenes.py` verifica cámara, posiciones, secuencias,
  tiempo, texto, transiciones y rechazo de datos truncados.
- `osContInit` conserva los puertos ya abiertos y devuelve una máscara de bits.
  Lectura y consulta del mando notifican la finalización SI que espera PadMgr.
- `tools/run_ps2_game.ps1` inicia la prueba con configuración aislada y conserva
  el log anterior; solo reemplaza el proceso de esta prueba identificado por ELF.
- Tabla de segmentos del renderer separada de la tabla del motor: los comandos
  `G_MW_SEGMENT` escriben bases físicas del RSP sin corromper los punteros CPU.
  La prueba EE confirma ambas tablas independientes y 120 fotogramas enviados.
  También volvieron a pasar matrices, printf, colas, temporizadores y DMA
  concurrente en `build/PS2/pcsx2-smoke.log`.

Pendiente: validación visual (la captura de ventana falló por timeout), control
interactivo y selección de partida, conversión de los demás escenarios/objetos,
guardado del juego y procesamiento DSP de audio. El avance de fotogramas del
título no sustituye estas verificaciones.

## Avance con herramientas reales — 2026-09-25 (histórico)

Esta sección sustituye las conclusiones de la revisión histórica con headers
falsos que aparece más abajo. En esta fecha el juego completo aún no enlazaba;
el avance del 26 de septiembre está documentado arriba.

- Instalado ps2dev/GCC 15.2.0 en Ubuntu WSL; corregidas llamadas incompatibles
  con ps2sdk/gsKit reales, flags del compilador y script de enlace del SDK.
- Recuperados audio, libc, libc64, libu64, buffers y rutinas C de matrices y
  trigonometría del upstream fijado en `UPSTREAM.md`.
- Extraída la ROM local US NTSC 1.2. Generadas tablas DMA/audio en formato
  nativo y direcciones VROM reales, sin sustituir recursos desconocidos por cero.
- Inicialización del heap/framebuffers adaptada al espacio de direcciones EE;
  ya no se copia código N64 sobre código nativo ni se borra el BSS desde boot.
- Tablas de actores, estados y efectos usan código residente. Player, pausa
  y marcas del mapa ya no intentan cargar sus instrucciones desde la ROM N64.
  Pendiente revisar la reinicialización de datos estáticos al cambiar overlays
  y medir el consumo total de RAM con todos los recursos integrados.
- Arregladas las matrices de libultra para usar los mismos medios enteros de
  16 bits que `Matrix_MtxFToMtx` y el renderer en little-endian.
- Temporizador EE real con desbordamiento de 64 bits, alta de temporizadores
  sin exigir memoria inicializada y envío de eventos fuera de la sección crítica.
- Añadidos `_Printf` mediante newlib y restauración anidada de interrupciones.
  Separado el símbolo de assert del juego del de newlib, que tiene otra firma.
- Eliminada la mezcla de fuentes normal/Master Quest e implementaciones de
  depuración N64/iQue en el build PS2.
- Conversión de 110 includes de texturas: se invierte el valor numérico de cada
  u64 para preservar los bytes originales al compilarlos en EE. No se aplica
  esta transformación a vértices ni matrices.
- Lecturas de ROM serializadas entre hilos mediante un semáforo. Se completan
  las lecturas parciales y se rechazan accesos fuera del archivo; ya no se
  presenta un relleno de ceros como una transferencia correcta. Las lecturas
  escalares PI convierten big-endian y aceptan el alias KSEG1 del cartucho.

### Verificación

PCSX2 2.8.0, con configuración aislada y BIOS existente, ejecutó el ELF de
diagnóstico compilado con ps2dev. El log confirmó:

```text
[PS2 MATH TEST] PASS: native matrix layout and trigonometry (0)
[PS2 PRINTF TEST] PASS: game varargs and callback (0)
[PS2 OS TEST] PASS: queue ABI, FIFO/full/empty, timer, monotonic clock
[PS2 ROM TEST] PASS: file DMA, boundaries and endian (0)
[PS2 ROM THREAD TEST] PASS: two concurrent readers (0)
[PS2 SMOKE] PASS: 120 frames submitted
```

La prueba OS también comprueba restauración anidada de interrupciones.
`tools/check_ps2_texture_words.py` verificó que las 110 texturas conservan
exactamente sus 212352 bytes originales dentro de un objeto compilado para EE.
No se ha validado visualmente el render ni probado hardware físico.
La última pasada compiló 709 objetos y llegó a la fase de enlace, pero falló
con 8633 símbolos sin resolver y ninguna definición duplicada. El registro local
es `build/PS2/game-native-final.log`; la lista deduplicada está en
`build/PS2/unresolved-symbols.txt`. Compilar las unidades C no significa tener
un ELF del juego funcional.

### Trabajo pendiente

Integrar símbolos y conversión de recursos (escenas, colisiones, esqueletos,
animaciones y display lists); sustituir las dependencias de microcódigo/RCP,
fallos y JPEG de N64; implementar DSP de audio y conversión de soundfonts;
validar arranque, pantalla de título, controles y guardado con el juego real.
La ruta de lectura de ROM todavía no proporciona todos los recursos en formato
nativo. Activar globalmente una opción de endian no resuelve la mezcla de
estructuras creadas por el CPU con datos leídos de la ROM.

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
