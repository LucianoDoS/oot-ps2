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

Estado: pendiente de comprobar con el registro durante la partida. Las cargas
se recuperan solas después de esperar; el suelo tiembla tanto quieto como en
movimiento, y el audio se distorsiona con FPS bajos. No está demostrada una
causa única. DIAG.ELF añade DIAG.LOG con cargas, tiempos de USB, vídeo y cola de
audio. La caché USB se amplía y separa las lecturas de muestras de las cargas;
se añade lectura anticipada acotada y se evita el seek consecutivo redundante.
La reserva de audio USB aumenta a 60 ms. DIAG224.ELF permite comparar un render
de 640x224 con líneas duplicadas, manteniendo la señal NTSC entrelazada del
convertidor. Los ELF normales conservan 640x448. Falta la comparación física;
no se da por corregido el temblor. Uso y límites en `PS2_HARDWARE.md`.

## Pantalla negra después del logo con DIAG224

Estado: reproducido por el usuario en SCPH-90010. El primer DIAG224 llega al
primer fotograma del logo, pero DIAG.LOG sólo conserva su cabecera de 346 bytes;
no hay muestras periódicas. Se conservan ambos registros en
`build/PS2/hardware-runtime/scph90010-diag224-20261005-140823`.
No demuestran si falló la salida de vídeo, el juego o el nuevo hilo del registro.

Se elimina la espera usleep sin comprobación del logger: ahora un semáforo lo
despierta desde el hilo de vblank. Se guarda el arranque del logger inmediatamente,
se registra su entrada y se detiene el registro si falla la espera. Una ausencia
de avance del Timer 0 permite muestras con tiempo aproximado de retrace, marcadas
con clockFallback=1. La siguiente prueba física debe usar DIAG.ELF con el vídeo
normal de 448 líneas; la prueba del suelo con DIAG224 queda aplazada hasta confirmar
que el registro y la partida funcionan en hardware.

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
