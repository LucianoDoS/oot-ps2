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
el selector conserva 30 y el juego 20. Falta confirmar ese ritmo en la consola.

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

## Registro de una partida y optimizaciones USB

Para registrar una sesión abre `mass:/OOTPS2/DIAG.ELF` en lugar de OOTPS2.ELF.
Ejecuta el mismo juego con tu mando y guardado normal; crea `DIAG.LOG` junto a
los datos. BOOTTEST sigue siendo sólo la comprobación inicial.

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
cada cinco segundos. BOOT.LOG v2 y DIAG.LOG v3 usan transferencias de 512 bytes
con buffers alineados; los espacios al final de líneas o lotes son normales.
Las etapas de inicio del registro distinguen reloj, semáforo, hilo y apertura,
escritura y cierre del primer lote. Si falla el reloj del port, clockFallback=1 marca un
intervalo aproximado basado en retraces; no certifica los tiempos gráficos.
El archivo se limita a 2 MiB; al llenarse registra
`LOG_LIMIT` y continúa el juego sin registrar más. Una escritura que devuelve
error detiene sólo el registro; un controlador USB bloqueado dentro de una llamada
puede detener también las cargas. No guarda cada instrucción, triángulo o muestra de sonido.
Una excepción que detenga todos los hilos puede impedir el último volcado.
El logger puede introducir una pausa: `FLUSH durationMs` mide su propia escritura.

Prueba unos 20 segundos quieto, otros caminando, y entra/sal de una casa.
Tras ver un fallo, espera al menos diez segundos antes de apagar o reiniciar
para que se cierre el siguiente bloque. Guarda tu progreso desde el juego si
quieres conservarlo. Vuelve a conectar el USB al PC y conserva DIAG.LOG y
BOOT.LOG antes de iniciar otra sesión: DIAG.ELF reemplaza su registro al arrancar.
El ELF normal no crea ni sobrescribe DIAG.LOG.

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

Para comparar el juego sin el coste de escribir DIAG.LOG, ejecuta OOTPS2.ELF.
DIAG.ELF se usa cuando hay que recoger tiempos o un bloqueo. Sus escrituras
pueden introducir pausas adicionales: en las dos sesiones físicas anteriores
un volcado tardó hasta 184 y 221 ms, respectivamente.

## Análisis del registro

Análisis desde el PC:
```powershell
python tools/analyze_ps2_runtime_log.py D:\OOTPS2\DIAG.LOG --output build/PS2/hardware-runtime/report.json
```

Los tres ELF del juego USB incluyen una caché de muestras de 256 KiB y otra de cargas de
64 KiB. Las peticiones de carga de 8 KiB pueden reutilizar lecturas de 16 KiB;
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
