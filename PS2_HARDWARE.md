# Primera prueba en PS2 real

Paquete privado para arrancar desde un USB FAT32 mediante Free McBoot y
wLaunchELF. El usuario ya confirma el arranque y la entrada a la partida en
una consola real. Los resultados de PCSX2 no certifican todos los casos en hardware.

Primera prueba del usuario: en una SCPH-90010, conectada mediante un
convertidor AV a HDMI, BOOTTEST muestra `DATOS OK`. El primer juego quedaba negro;
tras corregir la invalidación global de caché, inicia y permite jugar.
Quedan pausas de carga que se recuperan y distorsión del audio cuando caen los
FPS. Las olas del suelo al mover la cámara ya se corrigen al centrar las
coordenadas de la segunda capa; el usuario confirma la solución en esta
consola con FLOOR v3. OOTPS2.ELF aplica ese cálculo automáticamente, sin
seleccionar modos. El guardado físico sigue pendiente.

La última revisión del registro ya permite cargar la partida en hardware.
Se corrigió además un fallo propio de DIAG.ELF que terminaba la partida al
salir de la casa: el registro alteraba la identidad de una función de
inicialización. La prueba automática casa → Kokiri → casa pasa con el
controlador USB nativo de PCSX2. El usuario confirma la salida en la consola
y continúa hasta conseguir la espada. Los registros de esa sesión se conservan
en `build/PS2/hardware-runtime/scph90010-first-sword-20261005-162143`, sin errores
USB ni pérdidas de registros.

1. Copia la carpeta `OOTPS2` completa a la raíz del USB.
2. En wLaunchELF abre `mass:/OOTPS2/BOOTTEST.ELF`.
3. Espera el resultado. Debe aparecer `DATOS OK`, `MEMORIA OK` y el estado de la memory card
   del puerto 1. El diagnóstico sólo consulta la tarjeta; no guarda ni formatea.
   Si aparece `ERROR MEMORIA`, comunica el número de prueba y no continúes.
4. Reinicia la consola, vuelve a wLaunchELF y abre
   `mass:/OOTPS2/OOTPS2.ELF`.
   Esta versión muestra la etapa de arranque antes de activar el vídeo del
   juego. Si se detiene ahí, anota la última etapa visible.
5. Comprueba el logo, el título, el mando, la música y la carga de una partida.
   Después prueba Kokiri y el interior del Árbol Deku.
6. Para comprobar el guardado, usa una partida de prueba en una tarjeta PS2
   con espacio libre en el puerto 1. Guarda desde el juego, reinicia y verifica
   que puedes recuperar ese progreso. Revisa también su icono en la BIOS.

El ELF incorpora USBD y USBHDFSD del SDK y vuelve a cargarlos después de
reiniciar el IOP. No necesita archivos IRX externos. Si wLaunchELF facilita su
ruta de arranque, busca `OOT.Z64` en esa misma carpeta; la ruta predeterminada
es `mass:/OOTPS2/OOT.Z64`. Los nombres del paquete deben conservarse.

`OOT.Z64` contiene los datos **convertidos para este port**. Una ROM original
con el mismo nombre no sirve. El arranque comprueba su tamaño y doce bloques
distribuidos mediante CRC32; esto detecta archivos equivocados y parte de la
corrupción, pero no sustituye el SHA256 completo de `HASHES.sha256`.

Ante un error de datos, el juego muestra una explicación en pantalla y no
inicia la partida. Reinicia para regresar al lanzador.

Si queda negro después de activar el vídeo, reinicia y abre BOOTTEST.ELF
otra vez. Mostrará las diez últimas etapas de `mass:/OOTPS2/BOOT.LOG`, sin
modificar ese archivo. Comunica la última línea o toma una foto de la pantalla.
También se puede leer el archivo desde el PC. Cada nuevo arranque del juego
reemplaza BOOT.LOG después de validar los datos; BOOTTEST lo conserva.
Si no aparece ningún mensaje desde el principio y BOOTTEST indica que no hay
registro, informa de ese caso: el juego podría no haber llegado a su entrada.

El registro se limita a etapas del arranque, estados del juego y su primer
fotograma. Desactiva el texto de diagnóstico antes de que gsKit controle la
pantalla. No escribe un registro en cada fotograma. Si el USB no permite crear
el archivo, muestra esa condición mientras aún está activa la pantalla de texto.

La primera pantalla negra se localiza antes de completar la inicialización
del logo Nintendo 64. Esta versión corrige la invalidación global de caché y
registra la reserva, carga del segmento, matrices, vista y buffer SRAM del logo.
El usuario confirma que ahora arranca. Se elimina el mínimo de dos vblank
del planificador sólo durante el logo. Una prueba de presentación real con
USB nativo emulado mide 59,9 FPS después de los primeros 30 fotogramas;
el selector conserva 30 y el juego 20. El registro automático físico de la
SCPH-90010 confirma tres intervalos del logo a 59,9–60 FPS.

## Español y rendimiento

El paquete añade `ESP.DAT`. En el selector abre Options/Opciones, baja a
LANGUAGE/IDIOMA y elige Español con izquierda o derecha. Pulsa B para guardar
la opción y volver. Incluye diálogos, fuente y texturas de menús; algunos textos
escritos en código y los créditos conservan inglés. Datos y créditos completos:
[PS2_LANGUAGE.md](PS2_LANGUAGE.md).

El juego reutiliza las coordenadas y la preparación de textura del suelo
dentro de cada grupo de triángulos. Conserva la corrección física de las olas
y pasa la comparación de los puntos enviados al GS. Esto reduce cálculos
repetidos, pero todavía hay que medir la mejora de FPS en la consola y no
certifica 20 FPS estables en todas las vistas. DIAG.ELF toma una muestra de
los componentes del render cada 60 fotogramas para localizar los costes que
quedan. Para comparar el rendimiento habitual usa OOTPS2.ELF: el registro
de diagnóstico añade trabajo y escrituras USB.

La segunda sesión automática de la SCPH-90010, conservada en
`build/PS2/hardware-runtime/auto-play-physical-20261006-194851`, confirma lotes
de 4 KiB y una pausa máxima de escritura de 128 ms frente a 949 ms en la
sesión anterior. El último minuto sin lecturas ni escrituras solapadas registra
una mediana de 13,45 FPS y 60,55 ms de traducción por fotograma; la cámara no
está registrada, por lo que no certifica una ganancia comparable de FPS.
Hay un sector de 512 bytes nulos alrededor del segundo 445, sin error de
escritura comunicado. El analizador señala ese daño y el salto de las muestras
443 a 446, y descarta sus líneas; el último minuto usado no contiene ese sector.
La causa de esa escritura incompleta sigue pendiente. La revisión actual
prepara envoltura, límites y filtro una vez por capa/material. Conserva el
registro de cada uso del caché para no omitir una espera posterior del GS.

## Registro de una partida y optimizaciones USB

La caché de datos del juego retiene 256 KiB y reutiliza también las lecturas
cortas de animaciones; mantiene una caché independiente de muestras de audio.
Cada prelectura sigue limitada a 16 KiB. Reduce solicitudes repetidas al
caminar y al reutilizar recursos, pero una zona nueva aún necesita leerse
desde el USB. Los paquetes de triángulos también omiten las direcciones
repetidas de los registros y reutilizan su preparación por material.
La equivalencia pasa en el EE emulado; falta medir los FPS de esta revisión
en la consola, especialmente en zonas grandes.

El juego USB habitual `mass:/OOTPS2/OOTPS2.ELF` registra ahora tu sesión
automáticamente en `DIAG.LOG`, sin abrir otro programa. Incluye FPS cada
segundo, identificadores de escenas y salas, tiempos de carga/USB, dibujo y
observaciones de audio. La marca `AUTO-PLAY` aparece después de la fecha del
encabezado. Usa un buffer RAM de 256 registros, escribe lotes cada 15 segundos
y registra los tiempos generales sin activar los cronómetros por triángulo
o vértice. No muestra un contador.
Para recoger una caída basta con jugar en esa zona un minuto, guardar tu
progreso y esperar al menos 20 segundos antes de apagar. Después conecta
el USB al PC. El archivo se reemplaza al iniciar otra sesión; consérvalo primero.

Para separar los costes del dibujo sin el indicador ni el muestreo frecuente
de DIAG, abre `mass:/OOTPS2/PERF.ELF`. Incorpora las mismas optimizaciones y
el registro automático, con una medición detallada sólo cada 300 fotogramas.
Los otros 299 no leen los cronómetros por vértice/triángulo. La marca
`[PS2 PERF SAMPLING] every=300` distingue esta sesión; los tiempos del fotograma
muestreado incluyen el coste de los cronómetros y otras interrupciones,
excluyendo la síntesis DSP registrada. Sirve para localizar costes, no para
atribuir cada caída a una fase sin una comparación controlada. Usa una vista
exterior lenta durante unos 30 segundos, camina un minuto, guarda y espera
20 segundos antes de apagar. También reemplaza BOOT.LOG y DIAG.LOG al arrancar.
El juego habitual OOTPS2.ELF conserva el perfilado detallado desactivado.
La revisión actual añade `[PS2 COMBINER ANALYSIS PROFILE]`: cuenta las
reconstrucciones y los análisis reutilizados al cambiar de material. Su tiempo
está incluido en las otras fases y no debe sumarse de nuevo. La caché mantiene
32 entradas y usa 2.716 bytes; conserva la validación de las claves y los
colores dinámicos. Su ganancia de FPS debe comprobarse en la misma cámara.
`[PS2 DRAW DETAIL PROFILE]` separa la búsqueda de claves, los paquetes GS
y el reajuste del suelo; sus tiempos ya están incluidos en las otras fases.
La sesión anterior contiene una línea dañada y dos muestras duplicadas.
Se conservan los originales y el analizador excluye los intervalos duplicados;
la causa de esa escritura sigue pendiente. La sesión reciente, conservada en
`build/PS2/hardware-runtime/setup-physical-20261007-205251`, contiene 285
muestras íntegras y 13 perfiles, sin certificar la corrección de ese fallo
intermitente. Entre 111 y 126 segundos, 14 intervalos sin escrituras
solapadas recogen 169 fotogramas a 11,9 FPS de mediana: traducción 68,02 ms,
envío final 1,96 y presentación 13,11 por fotograma. No hay lecturas ni
observaciones de cola de audio vacía; el renderer sigue siendo el coste
sostenido. Los tiempos generales incluyen la ejecución de otros hilos.
`[PS2 TEXTURE CONTENT]` distingue `smallBypass`, `largeBypass`, `mutations`,
`newSnapshots`, `descriptorEvictions` y `overlapEvictions`: límites pequeños
o grandes, cambios renovados, nuevas copias y expulsiones por descriptores
o por espacio del anillo. `[PS2 DRAW SETUP PROFILE]` mide preparación inicial,
aplicación de tiles y metadatos anteriores a la búsqueda. Sus tiempos están
incluidos en envío/búsqueda. Los contadores describen el fotograma muestreado,
no toda la sesión. En cinco perfiles grandes, 358 de 372 expulsiones (96,2 %)
son de descriptores. La candidata amplía su número a 128, con 3.840 bytes
adicionales de metadatos; mantiene el anillo de 128 KiB, los límites de
256 bytes/16 KiB y la comparación de todos los bytes de ambas fuentes.
También conserva 17 huellas de paleta en 72 bytes, más 12 de contadores:
sólo invalida el banco y la paleta completa cuando cambia una entrada
decodificada. Todas las cargas TLUT siguen leyendo RAM. No se ha aislado
aún su parte del coste físico de metadatos, de 3,3–4,9 ms en esos cinco perfiles.
`[PS2 PALETTE HASH PROFILE]` mide las huellas dentro de metadatos, excluyendo
las actualizaciones TLUT. `[PS2 MATERIAL RUN PROFILE]` cuenta comandos que
delimitan grupos; no todos corresponden a vaciar un lote con primitivas.
Pasan la batería EE aislada, once pruebas del analizador y ambos recorridos
con USB nativo emulado: logo a 59,9 FPS, opciones/diálogo en español y
casa → Kokiri → casa. Las capturas conservan el menú; el diálogo sólo cambia
35 píxeles del parpadeo de los ojos. Los registros, datos y tarjeta del jugador
se verifican intactos. Las pruebas usan tarjetas desactivadas. Sus ciclos
emulados no establecen una ganancia de FPS físicos ni del fotograma completo.
Detalles: [CHANGELOG_PS2_PORT.md](CHANGELOG_PS2_PORT.md). La mejora en consola
y los 20 FPS estables siguen pendientes. La sesión posterior de 255 muestras
confirma cero expulsiones de descriptor en los perfiles grandes; su tramo
sin lecturas registra 13 FPS y 60,83 ms de traducción. No se ha verificado
idéntica cámara entre pruebas. La nueva candidata conserva el grupo al
recorrer piezas y cargar vértices, invalidando sólo los slots sobrescritos.
Sus 24 comparaciones de puntos/vértices y ambos recorridos con USB nativo
emulado pasan; conserva las capturas, el logo a 59,9 FPS y las transiciones.
Necesita una nueva medición física en la misma vista. PERF mide los límites
que realmente cierran los grupos.

Para una prueba con contador visible abre `mass:/OOTPS2/DIAG.ELF`.
Ejecuta el mismo juego con tu mando y guardado normal; crea `DIAG.LOG` junto a
los archivos del juego. Desde la revisión FPS-HUD muestra también `FPS 20.0`
en una esquina, actualizado aproximadamente cada segundo: mide los fotogramas
presentados, no los 60 retraces de vídeo. Al arrancar muestra `FPS --.-` hasta
la primera muestra. Un asterisco indica la estimación por retraces si el reloj
del EE deja de avanzar. El contador continúa aunque el registro alcance su
límite o falle una escritura. DIAG224.ELF incluye el mismo contador.

Para medir una zona grande basta con mirar una vista lenta durante 20 segundos,
caminar otros 20 y esperar diez antes de apagar. Anota el mínimo y el valor
habitual del contador. Al volver a conectar el USB al PC, el encabezado
`runtime log v3` con la marca `FPS-HUD` después de la fecha distingue esta
prueba de los registros anteriores, conservando compatibilidad con el lector.
El contador dibuja sprites sólidos después del juego: no carga fuentes ni
texturas y no entra en el retrato del inventario. OOTPS2.ELF registra con menor
frecuencia de escritura y sin añadir ese indicador a la imagen.

La primera sesión automática en consola confirma 12 FPS de mediana en el
tramo final de Kokiri incluso en intervalos sin lecturas ni escritura del
logger. La traducción gráfica ocupa 66,82 ms por fotograma, incluida la
interrupción por otros hilos; no identifica todavía qué fase gráfica domina.
La nueva comprobación de texturas acelera también las fuentes sin alineación
a dieciséis bytes, conservando todos sus contenidos y la corrección del suelo.
El resultado de esta revisión aún necesita compararse en esa misma vista.

BOOTTEST sigue siendo sólo la comprobación inicial.

DIAG.LOG registra etapas y errores del EE, cambios de estado, solicitudes y
finalización de salas, lecturas lentas o fallidas, y muestras de rendimiento
aproximadamente cada segundo. Incluye fotogramas presentados, tiempos de
traducción/GS/presentación, ciclos de síntesis, bytes y esperas de lectura,
aciertos de caché y observaciones de la cola de audio. `fps10=180` equivale a
18 FPS. `zeroPolls` no es un contador exacto de faltas de audio en el IOP.

Si pasan dos muestras sin fotogramas terminados, registra STALL_SNAPSHOT:
fase de inicialización/actualización/destrucción, escena y transición,
cola gráfica, comando actual, estado y espera de los hilos EE y registros
del GS/DMA. Después repite la captura en los intervalos 5, 10, 15… sin avance.
Puede aparecer durante una carga larga normal; por sí sola no demuestra
una excepción. El registro no cambia los callbacks de las escenas.

Los productores registran en RAM; un semáforo despierta el hilo del registro
desde el vblank, sin usar usleep ni escribir archivos desde el vblank. Se guarda
el estado inicial inmediatamente y después se escriben y cierran bloques
cada cinco segundos en DIAG y cada quince en OOTPS2. BOOT.LOG v2 escribe sectores
de 512 bytes; DIAG.LOG v3 agrupa hasta 4 KiB por llamada, con buffers alineados
y tamaños/offsets múltiplos de 512. Los espacios al final de los lotes son normales.
Las etapas de inicio del registro distinguen reloj, semáforo, hilo y apertura,
escritura y cierre del primer lote. Si falla el reloj del port, clockFallback=1 marca un
intervalo aproximado basado en retraces; no certifica los tiempos gráficos.
El archivo se limita a 2 MiB; al llenarse registra
`LOG_LIMIT` y continúa el juego sin registrar más. Una escritura que devuelve
error detiene sólo el registro; un controlador USB bloqueado dentro de una llamada
puede detener también las cargas. No guarda cada instrucción, triángulo o muestra de sonido.
Una excepción que detenga todos los hilos puede impedir el último volcado.
El logger puede introducir una pausa: `FLUSH durationMs` mide su propia escritura.
La primera partida automática física registra un máximo de 949 ms. Agrupar
sectores reduce llamadas; la segunda sesión con 4 KiB registra un máximo de
128 ms. Sus sectores incompletos se señalan en el análisis y no se interpretan
como mediciones válidas.

Prueba unos 20 segundos quieto, otros caminando, y entra/sal de una casa.
Tras ver un fallo, espera al menos veinte segundos antes de apagar o reiniciar
para que se cierre el siguiente bloque. Guarda tu progreso desde el juego si
quieres conservarlo. Vuelve a conectar el USB al PC y conserva DIAG.LOG y
BOOT.LOG antes de iniciar otra sesión: DIAG.ELF reemplaza su registro al arrancar.
El ELF habitual también crea y reemplaza DIAG.LOG al arrancar.

Para comprobar el controlador USB en PCSX2 sin usar HostFs existe
`tools/run_ps2_usb_stack_check.ps1`. Usa una imagen FAT32 privada de 128 MiB,
el dispositivo de almacenamiento USB del emulador y los IRX incluidos en el ELF.
No formatea el USB físico. Desactiva las tarjetas del jugador y comprueba los
registros, errores del controlador y el SHA256 de los datos tras cerrar la prueba.
La imagen y los resultados quedan en `build/PS2/usb-stack-checks`.
Requiere el entorno Python local indicado por `-Python`, con `pyfatfs` y
`setuptools<81`, además de la BIOS/configuración de prueba existente.
La emulación de ese controlador no certifica la temporización del USB real.

Para la regresión de transiciones se compila por separado
`bash tools/build_ps2_usb_transition_smoke.sh` y se ejecuta
`tools/run_ps2_usb_stack_check.ps1 -Transition -Seconds 55`.
Carga una partida sintética, completa 120 actualizaciones en casa, bosque
Kokiri y casa, y utiliza las transiciones normales. No prueba el contacto
con la puerta ni el progreso de una partida real y no forma parte del USB.

La sesión física `traversal-physical-20261008-164351` mantiene una vista a
unos 13 FPS sin lecturas USB. Se aprovechan 224 muestras completas: el último
sector de 512 bytes coincide con el registro anterior y se excluye junto a
las líneas empalmadas. La causa de esa escritura sigue sin resolver.
El perfil sigue señalando traducción y preparación del dibujo; los tiempos
anidados no deben sumarse. PERF desglosa ahora sus límites por tipo de comando
en dos líneas completas por fotograma muestreado, compatibles con el buffer
del registro. El analizador sólo une ambas si pertenecen al mismo fotograma
y no están duplicadas; no certifica por sí solo todos los daños de un archivo.

La candidata compara las texturas en bloques completos de 256 bytes y
conserva la ruta anterior para las colas, sin saltarse contenido. Los puntos
texturados de una capa se escriben directamente en la parte libre del lote;
las dos capas del suelo conservan su orden. Los cuatro comandos RDP SYNC,
ya ignorados por el intérprete, dejan de cortar los grupos; cargas, materiales,
capturas y fin de la lista raíz siguen siendo límites. Pasan las comparaciones
de puntos, paquetes y contenido en EE emulado. Los ahorros aislados no certifican todavía
una mejora física de FPS ni los 20 estables; la siguiente prueba usa PERF.ELF.
En las dos escenas del recorrido automático, los límites SYNC desaparecen,
pero el siguiente cambio de material mantiene el total de grupos. Ese cambio
no demuestra por sí solo un ahorro de lotes en esas escenas.

## Comparación del temblor del suelo

`mass:/OOTPS2/DIAG224.ELF` ejecuta el mismo diagnóstico, pero dibuja a
640x224 y duplica las líneas hasta 448. Conserva la señal NTSC entrelazada
para probar con el mismo convertidor AV a HDMI. Tiene menos detalle vertical;
es una comparación para identificar el origen del temblor, no una corrección
confirmada de las texturas. OOTPS2.ELF y DIAG.ELF conservan el render a 640x448.

El primer DIAG224 quedaba negro después del logo por los fallos ya corregidos
del diagnóstico. El usuario ya compara la versión revisada y las olas siguen
igual. Su registro confirma render 640x224, salida de 448 líneas y carga del
bosque Kokiri, sin errores USB ni pérdidas. Evidencia privada en
`build/PS2/hardware-runtime/scph90010-224-waves-20261005-163739`.

Prueba el mismo suelo con Link y la cámara quietos y después caminando.
Compara con lo observado en la versión normal. Indica si el temblor desaparece,
disminuye o sigue igual y si cambia la imagen. Esta prueba permite contrastar
el parpadeo entre líneas; no certifica que la conversión de texturas o profundidad
sea correcta. DIAG.LOG incluye las dimensiones del render y de la salida de vídeo.

DIAG224.ELF también reemplaza DIAG.LOG al iniciar. Conserva ese archivo en el
PC antes de arrancar DIAG.ELF si quieres guardar los resultados de ambas sesiones.

La siguiente revisión conserva fracciones en las coordenadas de textura de
los vértices recortados. Antes las truncaba a un entero en cada plano del
recorte, cambiando el mapeado con la cámara. Una prueba independiente de
interpolación perspectiva pasa 256 cámaras y 1323 esquinas, junto a las pruebas
de las dos capas del suelo. Esto demuestra la corrección del cálculo, pero el
usuario ya confirma que las olas siguen igual después de esta revisión física.

La primera comparación adicional `mass:/OOTPS2/FLOOR.ELF` mantuvo las olas
con perspectiva forzada; quitar la segunda capa las eliminó junto a parte de
la textura. El log confirma los modos en Kokiri y perspectiva activa, sin
errores USB ni pérdidas. Se conserva en
`build/PS2/hardware-runtime/floor-lab-result-20261005-175141`.

La comparación física de FLOOR v2 tampoco mejora las olas al cambiar la
profundidad de la segunda pasada. Registro conservado en
`build/PS2/hardware-runtime/floor-v2-result-20261005-181656`.
FLOOR v3 permite alternar con la cruceta entre UV centradas de la segunda capa
(arriba), una capa (izquierda) y las UV anteriores (derecha), sin reiniciar.
Recupera la profundidad original; desplaza cada triángulo por repeticiones
completas para reducir el tamaño de STQ y evitar el límite de texeles del GS.
Registra el modo, máximos UV por fotograma muestreado y muestras UV/W/STQ/XYZ,
incluidas dimensiones y desplazamientos de la segunda capa. Instrucciones en
`PS2_FLOOR_DIAGNOSTIC.md` y `SUELO.txt` del paquete adicional. El ELF habitual
ya incorpora automáticamente el mismo cálculo confirmado por el usuario.
La captura física de FLOOR v3 se conserva con hashes en
`build/PS2/hardware-runtime/floor-v3-confirmed-20261005-184050`.
Sus 24 fotogramas muestreados reducen el máximo UV de 2222.5 a 411.188 texeles,
con 58 coordenadas originalmente fuera del rango ±2047 y ninguna después
del ajuste. Los 105 intervalos no contienen errores USB, pérdidas o NUL.
Esto documenta la vista comprobada; no certifica todo el juego en hardware.

Para compilar una comparación sin escritura periódica, usa
`PS2_USB_AUTO_LOG=0 bash tools/build_ps2_usb.sh`; por defecto el paquete USB
incluye el registro automático solicitado. DIAG.ELF ofrece más muestras de
render y el contador visible. Las escrituras de ambas variantes
pueden introducir pausas adicionales: en las dos sesiones físicas anteriores
un volcado tardó hasta 184 y 221 ms, respectivamente.

## Análisis del registro

Análisis desde el PC:
```powershell
python tools/analyze_ps2_runtime_log.py D:\OOTPS2\DIAG.LOG --output build/PS2/hardware-runtime/report.json
```

Los tres ELF del juego USB incluyen una caché de muestras de 256 KiB y otra de cargas de
256 KiB. Las peticiones de carga de 8 KiB pueden reutilizar lecturas de 16 KiB;
las cargas grandes no expulsan los sectores de audio. Se omiten seeks cuando
la siguiente lectura empieza donde acabó la anterior. La lectura anticipada
está limitada y respeta el final del archivo; las primeras cargas aún pueden tardar.
El audio conserva 60 ms de reserva en la variante USB, frente a 20 ms anteriores,
sin cambiar la frecuencia de síntesis ni repetir muestras. Estos cambios
necesitan compararse en la consola; no corrigen por sí solos todo el lag ni
el temblor del suelo. El ELF habitual de PCSX2 conserva su configuración.

`MEMORIA OK` comprueba conservación de buffers vecinos y pila, publicación
de bytes de CPU a RAM, lectura de bytes nuevos desde el alias físico y el
estado de interrupciones. No comprueba todas las reservas del juego ni toda
la RAM física. La ejecución en PCSX2 no certifica la caché del EE real.

El informe `package.json` verifica las direcciones estáticas del ELF dentro
de los 32 MiB del EE. El espacio restante no garantiza por sí solo que todas
las reservas dinámicas, pilas, IOP y VRAM funcionen en hardware.

La comprobación del mando, audio, vídeo y guardado se hace jugando: BOOTTEST
no los certifica. Ninguna herramienta copia una partida de PCSX2 a la tarjeta
real automáticamente. La carpeta privada incluye recursos del juego y no
debe subirse a GitHub; se publicarán sólo código y herramientas cuando se
prepare esa fase.

Referencia técnica de los controladores: [ps2sdk USBHDFSD](https://github.com/ps2dev/ps2sdk/tree/master/iop/usb/usbhdfsd).
La agrupación evita repetir el RPC por cada sector; se conserva la interfaz
POSIX del SDK. Referencia de la llamada subyacente:
[ps2sdk fioWrite](https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/src/fileio.c).
