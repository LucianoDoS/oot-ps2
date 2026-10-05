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
que siguen pendientes. Para preparar la primera prueba en PS2 faltan también
validar carga fuera de `host:`, memoria disponible y recuperación de errores.

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
