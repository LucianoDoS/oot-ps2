# Fallos y límites encontrados por las pruebas

## Pantalla negra al arrancar en PS2 real

Estado: pendiente de localizar. El usuario confirma `DATOS OK` en BOOTTEST,
pero OOTPS2.ELF queda negro en una SCPH-90010 con convertidor AV a HDMI.
No hay evidencia aún para atribuirlo al vídeo, al mando, al audio o a otra etapa.
La variante USB incluye mensajes antes de iniciar el GS y un BOOT.LOG junto
a los datos; BOOTTEST muestra el final del registro tras reiniciar.

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
natural, cambio de edad ni final. En PS2 real sólo está confirmada la
comprobación inicial de datos; el juego aún no arranca. El Árbol Deku y el audio
en PCSX2 se mantienen como casos confirmados por el
usuario; el rendimiento adicional sigue aplazado.
