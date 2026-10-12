# Comprobaciones del port

Desde PowerShell 7, en la carpeta del proyecto:

```powershell
./tools/run_ps2_regression.ps1
```

La ejecución compila y prueba gráficos, audio, SRAM, memory card y contacto de
Skulltulas. Usa el SDK en Ubuntu WSL, la extracción NTSC 1.2 y las configuraciones
locales de PCSX2 ya preparadas. Los recursos del perfil `full` deben existir en
`build/PS2-ntsc12/oot-full-next.z64`. No descarga ROMs ni BIOS.

Cada ejecución genera `build/PS2/regression/<fecha>/report.json`, con hashes de
los ejecutables, tiempos y registros de esa ejecución. Cada emulador tiene un
registro nuevo; un PASS anterior no sirve para una prueba nueva. Un fallo,
excepción del EE, cierre prematuro o tiempo agotado produce un informe FAIL.
Al terminar se cierran sólo los procesos de diagnóstico identificados por su
PID y ejecutable. La configuración de la partida sigue abierta.

La prueba de tarjeta crea una imagen nueva bajo ese mismo directorio. Verifica
dos escrituras alternas, recuperación de una copia dañada, persistencia tras
cerrar el emulador, lectura del icono 3D, reparación de sus archivos y conservación
de SRAM tras un fallo del icono. Sólo esa tarjeta nueva recibe el token de
formateo. Las pruebas de gráficos y enemigos desactivan ambas memory cards.

Para añadir el recorrido automático de entradas a todas las escenas:

```powershell
./tools/run_ps2_regression.ps1 -IncludeSceneSweep
```

Son 191 entradas que cubren 101 escenas y sus variantes de edad/hora. Cada caso
carga la escena, ejecuta 60 actualizaciones y la destruye. El informe adicional
`scenes.json` comprueba el registro y la restauración de los overlays. No recorre
todas las salas ni resuelve los combates y puzles. `-SkipBuild` permite repetir
una ejecución usando los ELF existentes y registra sus hashes.

## Casos concretos

### Rendimiento de la cinemática del título

```powershell
./tools/run_ps2_perf_scene.ps1 -Mode title_cinematic
```

Este modo compila un ELF separado y ejecuta el inicializador original del
título. Mide 480 fotogramas después de 30 de calentamiento en el Campo de
Hyrule, con la cámara en movimiento, Link adulto y Epona originales. Sus
posiciones, animaciones y reloj siguen el comportamiento original. La semilla
`0x12345678` se aplica también cuando `Play_Init` vuelve a sembrar el generador,
antes de inicializar los actores. El verificador exige avance de la cinemática,
movimiento de cámara y registros de Link montado en el caballo cargado.

Usa una partida sintética, HostFs y una configuración privada con ambas memory
cards y los ocho mandos desactivados. Comprueba que la tarjeta del jugador
conserve su SHA256. Cada ejecución guarda ELF, imagen de datos, configuración,
registro, captura del GS, `fixture.json`, `report.json` y `summary.json` en
`build/PS2/perf-scenes/<fecha>/`.

Las filas de tiempos por cuadro y movimiento se almacenan en memoria y se
escriben después de la ventana medida. La captura también se obtiene fuera
de esa ventana.
Los perfiles detallados se toman cada 60 fotogramas del caso; sus cuadros se
excluyen de los tiempos normales, y la cadencia excluye además el intervalo
siguiente. El resumen muestra traducción gráfica, envío, presentación, cadencia
y bloques de la cinemática para localizar las caídas.

Para comparar una versión nueva con una ejecución anterior:

```powershell
./tools/run_ps2_perf_scene.ps1 -Mode title_cinematic -Baseline 'build\PS2\perf-scenes\<fecha-base>\summary.json'
```

Este comando vuelve a compilar. Añade `-SkipBuild` para repetir el ELF ya
preparado; los cambios de código pendientes no se incorporan en ese caso.
`-FixtureRoot 'build\PS2\perf-scenes\<fecha>'` permite repetir un ELF y sus
datos conservados, junto con `-SkipBuild`. Se comprueban sus hashes y se rechaza
un modo diferente del pedido. El modo predeterminado `house_path` conserva las
tres vistas fijas del exterior de la casa; sus resultados no se comparan con
los de la cinemática.

Una comparación sólo marca `comparable=true` si coinciden los datos del caso,
ROM, emulador, configuración, índices de muestras y secuencias completas de
geometría y movimiento. `capture_equal` compara por separado los píxeles de la
captura final. Si las condiciones no coinciden, no calcula el porcentaje de
mejora de traducción. La captura necesita revisión visual: no prueba todos los
fotogramas ni la fidelidad completa. Son tiempos del EE emulado con datos por
HostFs; no acreditan el rendimiento de USB o PS2 física, todas las escenas,
estabilidad caminando ni una tasa de FPS garantizada.

### Sol y rendimiento de la entrada del castillo

```sh
PS2_PERF_SCENE_MODE=castle_gate bash tools/build_ps2_perf_scene.sh
```

```powershell
./tools/run_ps2_perf_scene.ps1 -Mode castle_gate -SkipBuild -TimeoutSeconds 150
```

El fixture mide tres cámaras con la entrada original 0x047e, escena 95,
hora 16:00 fija y guardado sintético. Desactiva mandos y ambas tarjetas.
Guarda tiempos, secuencia de geometría, entorno por tarea y captura del GS.
`-Baseline <summary.json>` exige la misma identidad y muestreo para comparar.
El entorno original avanza diez ticks antes de dibujar; se valida ese valor.

La referencia con el sol corregido obtiene 18,84 / 14,70 / 19,98 FPS en
PCSX2. No demuestra 20 estables ni un resultado en hardware. El oráculo del
sol compara 49.152 canales; el de bucles VTX conserva 192 estados exactos.
VU0 permanece desactivado: `PS2_SMOKE_VU0=1` ejecuta su diagnóstico separado,
que ha fallado la comparación exacta en 208 de 512 casos. Un SKIP de esa
candidata en la batería normal no significa que VU0 haya pasado.

### Paquetes de triángulos y caché de animaciones USB

```powershell
wsl -d Ubuntu -- bash -c 'PS2_SMOKE_LOCALE=1 PS2_SMOKE_USB_CACHE=1 bash tools/build_ps2_smoke.sh'
```

La prueba gráfica incluye `PS2 TRIANGLE PACKET TEST`: decodifica los paquetes
del SDK y los compactos y compara la secuencia completa de registros GS.
Prueba padding, niebla/Q, ambos contextos, filtros/paletas, tamaños impares,
alineación y cambio de bloque DMA. Su benchmark mide construcción de paquetes,
no FPS. `PS2 USB CACHE TEST` intercala fotogramas de animaciones pequeños con
muestras: exige 16 lecturas para 64 sectores, ninguna al repetir, y conserva
20 sectores de muestras. Usa un fichero generado, sin datos de juego.
Las pruebas de capas del suelo, recorte, audio y los 120 fotogramas del
diagnóstico deben pasar también. `PS2_SMOKE_USB_CACHE=1` es necesario para
comprobar la configuración USB, que difiere de la usada con HostFs.

### Idioma y ritmo del logo por USB nativo

Con `ESP.DAT` privado ya generado junto a la imagen convertida:

```powershell
wsl -d Ubuntu -- bash tools/build_ps2_locale_game_smoke.sh
./tools/run_ps2_locale_game_check.ps1 -Python 'C:\ruta\python.exe'
```

Requiere el mismo entorno Python de la prueba USB y Pillow. Ejecuta el logo
Nintendo 64 original y mide las presentaciones reales después de 30 fotogramas
de calentamiento. A continuación cambia las opciones a español, comprueba que
las partidas no cambian al guardar y recargar el encabezado SRAM, captura la
fila de idioma y un diálogo, y completa casa → Kokiri → casa. Ambas tarjetas
del jugador y HostFs quedan desactivados. El logger termina sus escrituras
antes de cerrar el emulador; se comprueban los registros y los SHA256 de ambos
archivos de datos. Guarda `language-game-check.json` junto al informe USB.
No demuestra la persistencia del idioma en una tarjeta física.

Para comprobar todos los mensajes, texturas, glifos y lecturas parciales en EE,
añade `PS2_SMOKE_LOCALE=1` al compilar `tools/build_ps2_smoke.sh`; sin ese flag,
la prueba de gráficos no necesita datos de traducción. El empaquetador también
rechaza una versión, tamaño, rango o CRC incorrectos de ESP.DAT.

Para comprobar los ejecutables USB y sus registros:

```powershell
./tools/run_ps2_usb_checks.ps1
./tools/run_ps2_usb_stack_check.ps1
```

El primer script prueba datos válidos/corruptos, arranque y lectura de los logs
antiguos y rellenos por sectores mediante HostFs. El segundo ejecuta el ELF USB
nativo con una imagen FAT32 privada y el dispositivo de almacenamiento USB de
PCSX2; HostFs y ambas tarjetas quedan desactivados. Contrasta las etapas guardadas
con la salida del EE, rechaza corrupción/errores USB y verifica que los datos
conserven su SHA256. Requiere el entorno Python indicado por `-Python`, con
`pyfatfs` y `setuptools<81`. Uso y límites físicos en `PS2_HARDWARE.md`.
No sustituyen una prueba en consola ni formatean el USB del usuario.

Para comprobar la salida de la casa y el regreso con el diagnóstico USB:

```powershell
wsl -d Ubuntu -- bash tools/build_ps2_usb_transition_smoke.sh
./tools/run_ps2_usb_stack_check.ps1 -Transition -Seconds 55
```

La prueba usa un ELF separado y una partida sintética, sin tarjetas ni mando.
Completa 120 actualizaciones en casa → bosque Kokiri → casa y pide los
fundidos y cambios de escena originales. El informe exige las tres paradas,
la finalización del recorrido, registros íntegros y SHA256 de datos conservado.
Detecta que el hilo del juego termine indebidamente al cambiar de estado.
No recorre físicamente la puerta ni valida el guardado del jugador.

Para recorrer todas las salas referenciadas y revisar sus capturas:

```powershell
./tools/run_ps2_room_smoke.ps1
```

Este diagnóstico separado selecciona las 388 salas a partir de las listas de
salas de la imagen convertida. Usa una edad elegida por escena, guardados de
prueba y posiciones sintéticas obtenidas de puntos de aparición, puertas o
actores. Mantiene a Link quieto para que no abandone la sala seleccionada.
El ejecutable de prueba bloquea peticiones automáticas de otra sala provocadas
por esas posiciones sintéticas y registra cuántas; no modifica el ELF normal.
Carga, actualiza y dibuja cada sala durante al menos 45 fotogramas; comprueba
las listas de actores y bancos de objetos cargados. Ejecuta las funciones
reales de recepción de espada/escudo/rupias y las banderas de interruptores y
cofres, restaurando inmediatamente todo el estado que modificó.

Genera `build/PS2/room-regression/<fecha>/report.json`, `rooms.json` y una
galería `index.html`, con búsqueda por sala, PNGs y hojas de contacto.
Las capturas proceden del framebuffer del GS durante el render, no de la
ventana de PCSX2. El validador exige un resultado y una imagen completa por
caso; detecta excepciones, cierres, casos faltantes y ausencia de progreso.
La tarjeta del jugador queda excluida: ambos puertos están desactivados.

`-Start 12 -Count 2` permite aislar una selección; `-SkipBuild` sólo acepta
el ejecutable y fixtures que correspondan a los parámetros pedidos. Los
reportes y capturas quedan en un directorio nuevo por ejecución. Las imágenes
casi negras se señalan para revisar: pueden deberse a la cámara sintética,
iluminación o un fallo gráfico. No se consideran automáticamente errores del
port ni prueba de fidelidad visual. Las banderas no simulan abrir cada cofre,
cruzar todas las puertas, resolver puzles ni vencer a los jefes.

Si falla una sala, la ejecución conserva el estado FAIL y genera una galería
parcial con las capturas anteriores comprobadas. Puede continuarse desde el
caso siguiente mediante `-Start` y reunir los informes con
`tools/merge_ps2_room_reports.py`. La reunión conserva la procedencia y los
casos sin completar; no convierte un fallo en PASS. `DemoKankyo` oculta la
geometría de `hiral_demo` en el juego original; el diagnóstico admite ese
caso si conserva el buffer cargado y pasan las demás comprobaciones.
Los bloqueos conocidos se anotan en
[BUGS_PS2.md](BUGS_PS2.md).

Resultado inicial del 4 de octubre: 388 casos intentados en ejecuciones
separadas, 387 salas completas de las 101 escenas y un bloqueo reproducible
en `men_room_2` (caso 201). La galería reunida conserva FAIL y ese caso pendiente.
El falso fallo de `hiral_demo_room_0` queda resuelto en una ejecución aislada
tras reconocer la geometría ocultada por `DemoKankyo`.

| Caso | Comprobación automática | Pendiente manual |
| --- | --- | --- |
| Skulltulas del Árbol Deku | Siete posiciones originales: bajar, posarse, contacto OC con el cilindro de Link, empuje y regreso al techo. Retirada sobre un desnivel a alturas 799/820. Poses contrastadas con la extracción original. | El usuario confirma corregida la araña de la sala 10; falta recorrer las demás salas y probar en PS2 física. |
| Memory card | Lectura de todos los bytes de `icon.sys` y `triforce.icn`, persistencia, reparación y datos del guardado. Instalador compara los dos archivos de SRAM completos antes y después. | Nombre, posición, giro de ocho poses y ajuste final de velocidad/iluminación confirmados en la BIOS de PCSX2; repetir en PS2 física. |
| Materiales | Texels, máscaras de alfa, scroll, caché y captura del framebuffer con entradas controladas. | Casos nuevos con combinaciones de materiales aún no cubiertas. |
| Progresión | Carga/destrucción de entradas y restauración de estados de actores. | Objetos clave, puertas, combates, puzles, cambios de edad, final y recarga de una partida avanzada. |

El registro de la partida en la sala 10 sitúa el suelo de la araña en Y=720 y
la plataforma del cofre en Y=820. La alternancia de bajada/regreso se reproduce
también con `EnSt` original. El usuario confirma que queda demasiado baja antes
y después de cruzar. El port añade una retirada completa al techo cuando el
siguiente paso descendente enterraría su cuerpo en la superficie más alta donde
está apoyado Link. Es un ajuste de comportamiento exclusivo de PS2, no una
corrección de datos del modelo ni del suelo. Conserva el descenso normal a nivel
del suelo, daños y cilindros de contacto; no crea una barrera invisible.
El usuario confirma que esta corrección ya resuelve el problema observado.

Los informes separan lo comprobado de la progresión completa y la consola física,
que siguen pendientes. El paquete USB comprueba datos y rangos estáticos de
memoria; la carga USB y la memoria durante el juego siguen pendientes en
consola. Véase [PS2_HARDWARE.md](PS2_HARDWARE.md).

## Paquete USB y comprobaciones de arranque

```powershell
wsl -d Ubuntu -- bash tools/build_ps2_usb.sh --host-test
./tools/run_ps2_usb_checks.ps1
python tools/package_ps2_usb.py
./tools/copy_ps2_usb.ps1 -DriveLetter D
```

La salida privada queda en `build/PS2/usb-ready/OOTPS2`. La variante USB usa
objetos de entrada y lectura separados; no reemplaza el ELF habitual de PCSX2.
El diagnóstico consulta la tarjeta y los datos sin escribir guardados. Las
pruebas locales usan una configuración nueva con ambos puertos desactivados:
datos válidos, tamaño incorrecto, bloque alterado y arranque del juego por la
ruta comprobada. La variante de prueba usa `host:` y lo dice en su registro;
no valida la lectura de un dispositivo USB físico. Otras dos comprobaciones
cargan los módulos USB reales en el IOP de PCSX2 y exigen el error acotado por
ausencia del archivo: en BOOTTEST y en el juego con los módulos de mandos,
tarjeta y sonido ya cargados. Comprueban el inicio de los controladores, sin dispositivo.
El arranque válido del juego también exige un BOOT.LOG con etapas de módulos,
vídeo, mando, guardado, audio, vblank, apertura de datos e inicialización del
primer estado y fotograma. BOOTTEST debe mostrar el final de un registro mayor
que su buffer y conservar su SHA256; los errores de datos no deben crear uno.
El archivo es evidencia del arranque, no una certificación de juego completo.
BOOTTEST exige además conservación de buffers vecinos, pila, alias físico,
payloads de CPU y estado de interrupciones. En PCSX2 esta comprobación no
certifica la caché del hardware. El modelo CPU/RAM se ejecuta con:

```powershell
wsl -d Ubuntu -- python3 tools/test_ps2_cache_ranges.py
```

Prueba rangos desalineados y límites exactos de línea, vecinos sin tocar,
writeback solicitado, tamaños nulos/negativos y writeback global explícito.
Una variante negativa con la invalidación global anterior debe fallar por
pérdida de datos vecinos. Las funciones de rango usan
[SyncDCache del SDK](https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/src/glue.c),
que redondea los límites a líneas del EE y conserva el estado de interrupciones.
La copia comprueba que la unidad elegida sea extraíble FAT32 y verifica todos
los archivos mediante SHA256. Sólo copia la carpeta OOTPS2; conserva otros
archivos y respalda archivos del paquete anteriores si van a cambiar. No
formatea el USB.

## Icono 3D

`tools/generate_ps2_save_icon.py` convierte el modelo local de la Trifuerza a un
icono de 96 triángulos, con volumen, biseles y hueco central. Escribe el título
en Shift-JIS: «Legend of Zelda» / «Ocarina of Time». El build lo incorpora al
ELF; el juego instala o repara sólo `icon.sys` y `triforce.icn` en `OOT_PS2`,
con lectura de verificación. Omite escrituras si los archivos ya coinciden.
Un fallo del icono no desactiva el backend del guardado. Los datos SRAM continúan
en sus dos archivos alternos y conservan su formato.

El formato se contrasta con el [ejemplo de PS2SDK](https://github.com/ps2dev/ps2sdk/blob/master/ee/rpc/memorycard/samples/mc_example.c),
el [conversor ps2iconsys](https://github.com/ticky/ps2iconsys) y el
[lector ps2mc-browser](https://github.com/caol64/ps2mc-browser).
`tools/check_ps2_save_icon.py` decodifica estructura, título, geometría, normales,
animación y textura; puede generar una vista previa con Pillow. Esa imagen no
es una captura del navegador de la BIOS. Los iconos, vértices y archivos C
generados quedan en `build/`, fuera de Git.

La presentación actual centra el modelo en Y=-1.5, usa un degradado dorado y
anima ocho poses alrededor del eje vertical. El ciclo cubre 160 fotogramas de
animación, con velocidad 0.35. El modelo ocupa 56208 bytes. El lector comprueba
que las curvas sumen uno entre claves, que el bucle sea continuo y que el giro
conserve la altura. Una variante de 16 poses pasó las comprobaciones de bytes
y lectura de tarjeta, pero la BIOS la presentó como datos dañados. La variante
de ocho poses fue aceptada y confirmada por el usuario. Esto no establece un
límite universal de la BIOS ni sustituye la prueba en una consola real.

Para actualizar sólo la presentación de un guardado existente:

```powershell
wsl -d Ubuntu -- bash tools/build_ps2_save_icon_install.sh
```

El ejecutable resultante, `build/PS2/oot_ps2_save_icon_install.elf`, requiere
una tarjeta PS2 ya formateada y una partida en `OOT_PS2`. No formatea ni escribe
SRAM: instala el modelo y `icon.sys`, verifica ambos y compara todos los bytes
de `save.dat` y `save.bak` con sus lecturas anteriores, incluyendo su ausencia.
Antes de usarlo en la tarjeta de la partida, cerrar el emulador y copiar la
imagen de tarjeta como respaldo. Probar primero sobre una copia con una
configuración de PCSX2 aislada. El mensaje `PS2 ICON INSTALL: PASS` sólo acredita
la instalación y conservación de los archivos; la visualización se comprueba
después abriendo la BIOS. El instalador no modifica otros directorios de la
tarjeta; un directorio antiguo de diagnóstico es independiente del guardado.
