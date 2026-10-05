# Fallos y límites encontrados por las pruebas

## Pantalla negra al arrancar en PS2 real

Estado: arranque corregido y confirmado por el usuario. El primer
OOTPS2.ELF quedaba negro en una SCPH-90010 con convertidor AV a HDMI.
La variante USB incluye mensajes antes de iniciar el GS y un BOOT.LOG junto
a los datos; BOOTTEST muestra el final del registro tras reiniciar.

El segundo intento alcanza todos los módulos, GS, mando, guardado, audio,
vblank y apertura de datos. Completa `Setup_Init` (`0x003a7708` en ese ELF),
pero no completa `ConsoleLogo_Init` (`0x00486b60`, instancia de 488 bytes).
Se conserva el archivo físico en
`build/PS2/hardware-boot/scph90010-before-cache/BOOT.LOG`.

Se encuentra y corrige un fallo independiente de caché: `osInvalDCache`
ignoraba el rango e invalidaba toda la caché de datos, descartando posibles
escrituras pendientes de pilas, colas y payloads producidos por CPU. Ahora
sincroniza las líneas del rango mediante SyncDCache. Una prueba con modelo
CPU/RAM detecta la pérdida de vecinos de la implementación anterior y acepta
la nueva. BOOTTEST prueba además vecinos, alias físico y estado de interrupciones.
En PCSX2 pasan la prueba y el logo hasta el primer fotograma. El usuario
confirma que la versión corregida inicia y permite entrar a jugar. Se conserva
el nuevo BOOT.LOG físico en `build/PS2/hardware-boot/scph90010-first-gameplay`.

## Pausas, suelo y audio en la consola

Estado: las olas persisten después de la corrección del recorte en hardware. Las cargas
se recuperan solas después de esperar. El usuario precisa en su última prueba
que el suelo hace olas al mover la cámara y no las observa con la cámara quieta;
el audio se distorsiona con FPS bajos. No está demostrada una
causa única. DIAG.ELF añade DIAG.LOG con cargas, tiempos de USB, vídeo y cola de
audio. La caché USB se amplía y separa las lecturas de muestras de las cargas;
se añade lectura anticipada acotada y se evita el seek consecutivo redundante.
La reserva de audio USB aumenta a 60 ms. DIAG224.ELF permite comparar un render
de 640x224 con líneas duplicadas, manteniendo la señal NTSC entrelazada del
convertidor. Los ELF normales conservan 640x448. La comparación física a 224
líneas sigue mostrando las mismas olas. Se corrige una pérdida independiente
de precisión al truncar las UV interpoladas en cada plano del recorte. Pasan
256 cámaras y las pruebas del suelo, pero todavía no se da por corregido el
defecto observado en la PS2; el usuario confirma que sigue igual tras ese cambio.
La comparación física de FLOOR v1 conserva las olas con perspectiva forzada;
omitir la segunda capa las elimina pero pierde parte del suelo. El log confirma
perspectiva activa y los modos en Kokiri, sin errores USB ni pérdidas. FLOOR v2
prueba esa segunda pasada con profundidad de sólo lectura y un margen mínimo,
manteniendo ambas texturas. Arriba selecciona la revisión; derecha recupera
la pasada anterior. Prueba e instrucciones en `PS2_FLOOR_DIAGNOSTIC.md`;
todavía no confirma una solución.
Uso y límites generales en `PS2_HARDWARE.md`.

## Pantalla negra después del logo con DIAG224

Estado: arranque corregido y confirmado en la última comparación física.
El primer DIAG224 llegaba al
primer fotograma del logo, pero DIAG.LOG sólo conserva su cabecera de 346 bytes;
no hay muestras periódicas. Se conservan ambos registros en
`build/PS2/hardware-runtime/scph90010-diag224-20261005-140823`.
No demuestran si falló la salida de vídeo, el juego o el nuevo hilo del registro.

Se elimina la espera usleep sin comprobación del logger: ahora un semáforo lo
despierta desde el hilo de vblank. Se guarda el arranque del logger inmediatamente,
se registra su entrada y se detiene el registro si falla la espera. Una ausencia
de avance del Timer 0 permite muestras con tiempo aproximado de retrace, marcadas
con clockFallback=1. Primero se comprobó DIAG.ELF con el vídeo normal de 448
líneas; después se confirmó también el arranque de DIAG224 y su carga de Kokiri.

El segundo intento físico, DIAG.ELF con 448 líneas, también queda negro. Su
BOOT.LOG termina en `REGISTRO: iniciando hilo por vblank`; DIAG.LOG conserva
sólo la cabecera v2 de 433 bytes. Registros privados verificados en
`build/PS2/hardware-runtime/scph90010-diag448-20261005-143535`.
El bloqueo está dentro del inicio del logger; no demuestra un fallo del modo 224.

La prueba nueva con una imagen FAT32 y el controlador USB emulado, sin HostFs,
reproduce corrupción del texto y errores USBHDFSD durante los volcados, aunque
el juego continúa. BOOT.LOG v2 y DIAG.LOG v3 escriben sectores de 512 bytes desde
buffers alineados y mantienen alineado el archivo; los reintentos de una escritura
parcial también conservan la alineación del buffer. BOOTTEST elimina el relleno
para mostrar las últimas diez etapas. Se añaden hitos separados del primer lote.
El usuario confirma que esta revisión permite pasar la intro, abrir el menú
y entrar a su partida en la SCPH-90010.
La prueba nativa del controlador USB en PCSX2 ya pasa en
`build/PS2/usb-stack-checks/20261005-151055-633/report.json`: ambos registros
íntegros, datos sin cambios, cargas de escena y fotogramas posteriores al logo.

## Pantalla negra al salir de la casa con DIAG.ELF

Estado: corregido en la prueba automática y confirmado físicamente por el usuario.
El último registro físico conserva muestras hasta 205 segundos: después de
salir de la casa, los fotogramas se detienen y continúan el audio y los volcados,
sin lecturas ni errores USB. Evidencia privada con hashes comprobados en
`build/PS2/hardware-runtime/scph90010-house-exit-20261005-153103`.

La prueba del controlador USB reproduce que Graph termina tras destruir la
casa. El wrapper de Play_Init alteraba su identidad en la comparación externa
de Graph_GetNextGameState, mientras que la transición interna de Play_Update
asignaba Play_Init original. El diagnóstico ahora envuelve GameState_Init;
conserva la identidad de los callbacks y puede continuar a la siguiente escena.
La prueba casa → Kokiri → casa completa 120 actualizaciones en cada escena,
sin errores USB ni pérdidas del registro, en
`build/PS2/usb-stack-checks/20261005-155909-872/report.json`.
El usuario confirma ahora la salida y el juego hasta conseguir la espada en
la SCPH-90010. Los registros se conservan en
`build/PS2/hardware-runtime/scph90010-first-sword-20261005-162143`.
Esta corrección del diagnóstico no corrige por sí sola las olas del suelo.

## Cadencia del logo Nintendo 64

Estado: pendiente. El estado del logo configura R_UPDATE_RATE=1, con objetivo
de 60 FPS, pero las muestras del nuevo diagnóstico en PCSX2 indican unos
30 fotogramas presentados por segundo. Hay que revisar la cadencia efectiva
del planificador y distinguirla de la frecuencia de retrace de la salida NTSC.

## Campo de Entrenamiento Gerudo, sala 2

Estado: pendiente. El nuevo recorrido de salas encuentra una excepción de
lectura en el primer fotograma. Se reproduce también arrancando sólo ese caso,
sin haber cargado salas anteriores:

```powershell
./tools/run_ps2_room_smoke.ps1 -Start 201 -Count 1
```

El registro sitúa la lectura en `EnGoroiwa_TeleportToWaypoint` y
`EnGoroiwa_FaceNextWaypoint`, actor de las rocas móviles. El banco de la escena
describe una ruta, pero dos actores de la sala solicitan índices 0 y 1.
El segundo descriptor sigue en un bloque no descrito del conversor, con su
puntero en el orden de bytes original. Esta es una pista concreta de conversión;
falta verificar la ruta completa y su movimiento antes de darla por corregida.

Registros privados: `build/PS2/room-regression/20261004-220503-633/runtime.log`
y `build/PS2/room-regression/20261004-223054-332/runtime.log`.

## Capturas oscuras o sin geometría visible

Estado: requieren revisión de cámara. La galería conserva capturas de salas
individuales con puntos sintéticos; algunas quedan fuera de la geometría,
delante de paredes o en salas que sólo sirven a una cinemática. Los casos con
más del 65 % de píxeles casi negros o menos de 32 colores se marcan para revisar.
Un resultado de carga correcto no certifica que la imagen sea fiel al original.

## Geometría ocultada por una demostración

En `hiral_demo`, `DemoKankyo_Init` asigna NULL a `curRoom.segment` para ocultar
el fondo. La primera prueba lo marcó como FAIL pese a tener actores, eventos
y captura completos. El diagnóstico admite esa geometría oculta sólo en
`SCENE_CUTSCENE_MAP`, con el buffer de carga presente y las demás condiciones
correctas. Esto cambia sólo el criterio de la prueba, no el juego ni la escena.

## Cobertura pendiente

Los recorridos inicializan salas con una edad elegida por escena y prueban
funciones de estado. No resuelven cada puzle, combate, diálogo, transición
natural, cambio de edad ni final. En PS2 real están confirmados la
comprobación inicial de datos y el arranque a la partida; quedan los fallos
de carga, vídeo/audio y la comprobación del guardado físico. El Árbol Deku y el audio
en PCSX2 se mantienen como casos confirmados por el
usuario; el rendimiento adicional sigue aplazado.
