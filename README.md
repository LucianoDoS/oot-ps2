# OoT PS2 Port

Port de **The Legend of Zelda: Ocarina of Time** a PlayStation 2, basado en la descompilación [zeldaret/oot](https://github.com/zeldaret/oot).

## Estado comprobado — 5 de octubre de 2026

**Port en desarrollo, con todos los recursos descritos del juego convertidos.**
La versión actual de PCSX2 usa el perfil `full`: 101 escenas, 388 salas y
378 bancos de objetos, dentro de 1013 segmentos de datos. El código del
juego ya se compila para el EE; no ejecuta las instrucciones de los overlays N64.
Esto amplía la carga al resto del mapa, pero todavía falta validar la
progresión completa, los combates, las cinemáticas y la fidelidad visual.

En la SCPH-90010 el usuario confirma intro, menú, carga de la partida, salida
de la casa y juego hasta conseguir la espada desde USB. El fallo anterior de
DIAG.ELF al salir de casa se reproduce y corrige:
el registro cambiaba la identidad de la función que selecciona la siguiente
escena. La regresión casa → Kokiri → casa ya pasa con el controlador USB
nativo de PCSX2 y el usuario confirma la salida en la consola. El usuario
confirma además que centrar las coordenadas de la segunda capa elimina las
olas del suelo al mover la cámara en la PS2 real. Esa corrección ya se aplica
automáticamente en el juego habitual, preservando ambas texturas y su escala.
Quedan el guardado físico, el audio durante caídas de rendimiento y la
validación del resto de zonas en hardware.

El usuario confirma la partida inicial, el bosque Kokiri, casas y tienda,
la entrada exterior al Árbol Deku y el movimiento en su sala principal.
También confirma el audio, los objetos recibidos sobre las manos, el retrato
de equipamiento, la apertura y transparencia del inventario, el aura animada
del título y el brillo recortado de la ficha de Skulltula dorada.
La Skulltula sobre un desnivel tiene un ajuste de retirada específico del
port para evitar que cancele la subida junto al borde. Las pruebas verifican
su altura mínima, el regreso al techo y el contacto; el usuario confirma que
el problema de la araña ya está corregido en la partida.
El guardado muestra su título y una Trifuerza dorada 3D animada en el navegador
de la memory card. El usuario confirma la posición y el giro de la versión de
ocho poses en la BIOS de PCSX2, y acepta también el ajuste final de velocidad
del giro a 0.35 e iluminación, con fondo de degradado dorado.
Se genera desde el modelo extraído localmente; sus archivos no se incluyen
en Git. Pasan escritura, lectura completa, reparación y recarga tras cerrar
el emulador, conservando SRAM. Falta comprobarlo en una PS2 física.
El rendimiento llega a 20 FPS en muchas vistas; quedan caídas al caminar
según la cámara. La optimización vuelve a estar en curso para hardware real,
reutilizando ahora las coordenadas del suelo sin perder la corrección física.
DIAG.ELF y DIAG224.ELF incluyen un contador de FPS en pantalla para medir
las vistas grandes directamente en consola y registrar una sesión nueva.
El ejecutable USB habitual OOTPS2.ELF registra automáticamente la sesión en
DIAG.LOG: FPS, escenas, cargas, dibujo y audio. Agrupa las escrituras cada
15 segundos y conserva tiempos generales sin perfilado de triángulos ni indicador
en pantalla. El registro se limita a 2 MiB y se reemplaza al arrancar otra sesión.
El logo inicial elimina el límite de 30 del planificador: se miden 59,9 FPS
presentados con USB nativo emulado. El selector conserva 30 y el juego 20;
falta confirmar el logo y el rendimiento de esta revisión en la consola.

El selector incorpora idioma English/Español cuando encuentra el paquete
privado `ESP.DAT`. Traduce diálogos, fuente y texturas de menús conservando
los índices y las partidas nativas. Algunos textos de código y los créditos
siguen en inglés. Uso, generación y créditos: [PS2_LANGUAGE.md](PS2_LANGUAGE.md).

La corrección más reciente conserva el blanco de los fundidos, la textura
animada de los huevos de Gohma, las dos fuentes del brillo de la esmeralda
Kokiri y la repetición del cuello/cuerpo del búho. Pasan las pruebas EE de
campos, comandos y texels; queda comprobar estos casos visualmente en la partida.

El perfil completo pasa una comparación independiente de 49560 recursos y
67 tipos de estructuras o secuencias de bytes. Verifica dependencias, límites,
colisiones, rutas y listas de salas. Los 233 segmentos del perfil previo y
los bancos de audio conservan sus datos. Las texturas y JPEG conservan sus
bytes originales. La prueba con los datos previos sin convertir falla.
La ficha de Skulltula combina ahora sus dos texturas con su máscara de alfa;
el diagnóstico EE pasa 40960 canales y 3785 píxeles transparentes.

El diagnóstico de carga usa un ejecutable independiente, guardados de prueba,
entradas originales, mando desactivado y una configuración sin memory cards.
Ejercita `Play_Init`, 60 actualizaciones y `Play_Destroy` por caso. Su alcance
es comprobar cargas y reinicios. Pasan 191 casos que cubren las 101 escenas,
con las variantes de edad/hora de sus entradas, sin excepciones del EE.
También pasan las pruebas de restauración de los 426 overlays de actores:
el port conserva su código nativo y restablece los datos de una carga nueva,
evitando que sobrevivan cámaras y otros estados de una escena anterior.
Estas pruebas no sustituyen una partida completa.
El usuario da por resuelto el Árbol Deku. La batería se amplía ahora con un
ejecutable separado que selecciona las 388 salas, comprueba actores, objetos
y funciones de eventos, y guarda capturas del GS en una galería local. Usa
guardados de prueba y ambas tarjetas desactivadas; no verifica por sí sola
todos los puzles, combates o transiciones de la historia. Uso y límites:
[TESTING_PS2.md](TESTING_PS2.md).
En la primera ejecución completa, dividida para continuar después de fallos,
387 de las 388 salas completan carga, actualización, eventos y captura. Se
reproduce un bloqueo en la sala 2 del Campo de Entrenamiento Gerudo, incluso
arrancando sólo esa sala. Queda registrado en [BUGS_PS2.md](BUGS_PS2.md).
La primera versión para USB incorpora los controladores necesarios después
del reset del IOP, selecciona los datos junto al ELF y muestra un error si
faltan o no corresponden. Incluye un diagnóstico de lectura. La prueba real
se hará con Free McBoot y wLaunchELF; instrucciones y límites en
[PS2_HARDWARE.md](PS2_HARDWARE.md). El usuario ya confirma el arranque y una
partida en su SCPH-90010 tras corregir la caché del EE. Las olas del suelo
se resuelven al centrar las UV de la segunda capa y el usuario confirma esa
corrección en hardware. Quedan pausas que se recuperan y audio distorsionado
con caídas de FPS. El
paquete incluye DIAG.ELF, que guarda tiempos y eventos en DIAG.LOG durante la
partida, y cachés específicas para reducir lecturas repetidas del USB.
DIAG224.ELF conserva el diagnóstico de vídeo a 224 líneas duplicadas; esa
comparación no solucionó las olas. BOOT.LOG y DIAG.LOG usan escrituras
alineadas por sectores, tras corregir los primeros bloqueos del registro.
El juego habitual también registra la sesión automáticamente; agrupa las
escrituras cada 15 segundos en transferencias de hasta 4 KiB. El primer log
físico identifica el dibujo como el coste sostenido en una vista lenta de
Kokiri, incluso sin lecturas USB. La comprobación de texturas ahora acelera
también fuentes sin alineación a dieciséis bytes y conserva todos sus contenidos.
Para jugar usa OOTPS2.ELF, que ya incorpora la corrección del suelo sin los
controles de comparación ni las mediciones de FLOOR.ELF.
El registro detallado está en [CHANGELOG_PS2_PORT.md](CHANGELOG_PS2_PORT.md).

El juego completo ya enlaza: 724 objetos generan `build/PS2-ntsc12/oot_ps2.elf`.
El conversor local adapta estructuras, vértices, matrices, listas
gráficas y cinemáticas; las texturas conservan su secuencia de bytes original.
La conversión completa cubre escenas, salas, dependencias de objetos,
recursos del selector de partida y los 38 bancos de instrumentos. El perfil
`play` incluye 233 segmentos y las 573 tablas de animación del jugador,
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
/home/lucia/oot-ps2-tools/oot-upstream/.venv/bin/python tools/check_ps2_compiled_cutscenes.py \
  --elf build/PS2-ntsc12/oot_ps2.elf
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

Para ejecutar las comprobaciones habituales desde PowerShell 7:

```powershell
./tools/run_ps2_regression.ps1
```

Compila las pruebas, comprueba gráficos/audio/SRAM, crea una tarjeta de prueba
nueva, verifica sus guardados e icono tras reiniciar y prueba siete Skulltulas
con la IA del port y las colisiones reales. También contrasta las poses con
los datos originales y comprueba la retirada sobre desniveles. Cierra únicamente
los emuladores de prueba.
Guarda resultados, hashes y registros separados en
`build/PS2/regression/<fecha>/report.json`. `-SkipBuild` reutiliza los ejecutables
existentes; `-IncludeSceneSweep` añade los 191 casos de entradas a escenas.
Requiere los datos completos locales, la extracción y las configuraciones/BIOS
de prueba ya preparadas. Los límites y las comprobaciones manuales restantes
están en [TESTING_PS2.md](TESTING_PS2.md).

El build del juego usa la extracción local US NTSC 1.2. La procedencia de las
fuentes, versión del SDK y hash de ROM están en [UPSTREAM.md](UPSTREAM.md).
ROMs, recursos extraídos y salidas de compilación quedan fuera de Git.

Para generar los datos actuales del juego, después de extraerlos:

```sh
/home/lucia/oot-ps2-tools/oot-upstream/.venv/bin/python tools/prepare_ps2_rom.py \
  --upstream /home/lucia/oot-ps2-tools/oot-upstream \
  --profile full --output build/PS2-ntsc12/oot.z64
/home/lucia/oot-ps2-tools/oot-upstream/.venv/bin/python tools/check_ps2_cutscenes.py \
  --upstream /home/lucia/oot-ps2-tools/oot-upstream
```

Desde PowerShell, `tools/run_ps2_game.ps1` inicia el ELF. El log queda en
`build/PS2-ntsc12/pcsx2-game.log`. Los perfiles `title` y `play` son selecciones
para diagnóstico; `play` aún no contiene todos los recursos de una partida completa.
Incluye el bosque inicial, Bosques Perdidos, casas de los Kokiri y su tienda.
También incluye el interior del Árbol Deku y las salas de Gohma; su progresión
completa aún está en comprobación.
El perfil `full` convierte todos los bancos de datos descritos por la extracción
local, incluidos los recursos de las demás mazmorras y jefes. Los segmentos
de código N64 quedan sin convertir porque sus funciones ya están enlazadas
como código nativo del EE.

Para repetir la prueba automática de carga de escenas:

```sh
bash tools/build_ps2_scene_smoke.sh
```

Desde PowerShell, `tools/run_ps2_scene_smoke.ps1` abre el diagnóstico oculto
y silenciado. Usa `build/PS2-full-test` y `build/PCSX2-scene-sweep`, con ambas
ranuras de memory card desactivadas. Comprueba su registro con:

```sh
/home/lucia/oot-ps2-tools/oot-upstream/.venv/bin/python tools/check_ps2_scene_sweep.py \
  --log build/PS2-full-test/pcsx2-scene-sweep.log \
  --output build/PS2/scene-sweep-results.json
```

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
