# Comparación del suelo en la PS2

Ejecuta `mass:/OOTPS2/FLOOR.ELF` desde wLaunchELF y carga tu partida.
Busca el suelo donde aparecen las olas al girar la cámara.

Con el inventario cerrado, sin diálogo ni transición, pulsa una dirección
de la cruceta para elegir el modo. No hace falta reiniciar:

- Arriba: coordenadas UV centradas de la segunda capa, conservando ambas texturas.
  Es el modo inicial de esta versión.
- Izquierda: sólo la primera capa. El suelo cambia de aspecto; es intencional.
- Derecha: segunda pasada anterior, para comparar con las olas originales.

Gira la cámara en el mismo lugar durante unos diez segundos con cada modo.
Anota en cuál desaparecen, disminuyen o siguen iguales las olas. Usa el
joystick para mover a Link; la cruceta selecciona estos modos durante la prueba.
El cambio se aplica al siguiente fotograma completo. No cambia objetos,
enemigos, partidas ni cinemáticas. Los controles del inventario y los diálogos
conservan su comportamiento.

Este ELF usa el guardado habitual del juego. Si consigues progreso nuevo,
guárdalo desde el menú antes de apagar la consola.

FLOOR.ELF reemplaza BOOT.LOG y DIAG.LOG al arrancar. La copia del diagnóstico
conserva los registros anteriores en el PC. Al acabar la comparación, conecta
el USB de nuevo al PC para recoger DIAG.LOG antes de ejecutar otro diagnóstico.

El log identifica esta versión mediante `FLOOR_LAB v3` y guarda `FLOOR_MODE`,
la escena y el fotograma de cada selección.
Cada sesenta fotogramas con material de suelo recoge un triángulo: modos RDP,
escala, coordenadas UV, W y los valores STQ/XYZ enviados al GS. Incluye la
profundidad, dimensiones, máscaras y desplazamiento de la segunda pasada.
`PS2 FLOOR UV` añade los máximos y las coordenadas fuera de rango de todos los
triángulos de suelo de un fotograma cada sesenta. Son muestras
acotadas, no una captura de todos los polígonos. Las escrituras del logger
pueden introducir pausas; esta variante sirve para comparar la imagen.

El usuario confirma que las olas persisten tanto con DIAG224 como con la
corrección de precisión del recorte. La primera comparación de FLOOR.ELF
mantiene las olas en los modos normal y perspectiva forzada; omitir la segunda
capa las elimina junto a parte de la textura. El registro confirma perspectiva
activada, selecciones en Kokiri y ausencia de errores USB o pérdidas.

La segunda versión comparó profundidad de sólo lectura con un margen mínimo
y la política anterior. El usuario confirma las mismas olas en ambas.
Su registro físico queda conservado en
`build/PS2/hardware-runtime/floor-v2-result-20261005-181656`.

La tercera versión recupera la profundidad original en ambos modos y cambia
sólo las coordenadas de la segunda capa. Resta a los tres vértices el mismo
número entero de repeticiones, antes de multiplicar por Q. Conserva escala,
perspectiva, mezcla, niebla y posición. No aplica módulo a cada vértice: eso
deformaría la textura entre esquinas. Respeta las repeticiones espejo ya
expandidas y deja sin desplazar los ejes de textura sujetos a clamp.

El GS limita las coordenadas de texel a ±2047 y descarta ocho bits bajos de
la mantisa de STQ. Las coordenadas grandes pueden perder precisión antes
del wrap ([manual del GS, sección 3.4.4](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/GS_Users_Manual.pdf)).
La muestra física de v2 alcanza unas 2058 unidades en la segunda capa de
32 píxeles; esto motiva la comparación, pero aún no demuestra que sea la
única causa de las olas. Centrar no puede reducir el intervalo de un triángulo
que ya abarque más de 4094 texeles: el log también registra esos casos.
La derecha recupera exactamente las UV anteriores. Sigue pendiente confirmar
la imagen en hardware; el ejecutable habitual no incorpora esta prueba.

Para volver al juego habitual ejecuta `mass:/OOTPS2/OOTPS2.ELF`.

## Preparación desde el código

```bash
bash tools/build_ps2_floor_diagnostic.sh
python3 tools/package_ps2_floor_diagnostic.py
```

En PowerShell, con el USB FAT32 conectado como D:

```powershell
.\tools\copy_ps2_floor_diagnostic.ps1 -DriveLetter D
```

El paquete adicional contiene FLOOR.ELF, SUELO.txt, sus hashes y un manifiesto.
Usa el mismo OOT.Z64 verificado que ya está en OOTPS2. El script comprueba
esos datos y los archivos que copia; conserva los ELF habituales.
