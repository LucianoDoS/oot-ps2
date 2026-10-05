# Comparación del suelo en la PS2

Ejecuta `mass:/OOTPS2/FLOOR.ELF` desde wLaunchELF y carga tu partida.
Busca el suelo donde aparecen las olas al girar la cámara.

Con el inventario cerrado, sin diálogo ni transición, pulsa una dirección
de la cruceta para elegir el modo. No hace falta reiniciar:

- Arriba: dibujo normal, con las dos capas del suelo.
- Izquierda: sólo la primera capa. El suelo cambia de aspecto; es intencional.
- Derecha: dos capas con perspectiva forzada. Si el material ya tenía
  perspectiva, la imagen debe mantenerse igual.

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

El log guarda `FLOOR_MODE`, la escena y el fotograma de cada selección.
Cada sesenta fotogramas con material de suelo recoge un triángulo: modos RDP,
escala, coordenadas UV, W y los valores STQ/XYZ enviados al GS. Son muestras
acotadas, no una captura de todos los polígonos. Las escrituras del logger
pueden introducir pausas; esta variante sirve para comparar la imagen.

El usuario confirma que las olas persisten tanto con DIAG224 como con la
corrección de precisión del recorte. FLOOR.ELF permite aislar su causa;
todavía no se presenta como una corrección confirmada en hardware.

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
