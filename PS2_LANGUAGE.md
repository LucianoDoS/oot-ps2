# Idioma del port

En el selector de partidas entra en **Options / Opciones**. Baja hasta
**LANGUAGE / IDIOMA** y usa izquierda o derecha para elegir **English** o
**Español**. Pulsa B para volver: se guarda junto al sonido y la opción de
fijar objetivo. No altera el formato ni los objetos de las partidas.

La traducción requiere `ESP.DAT` junto a los datos convertidos `OOT.Z64`.
El paquete privado USB lo incluye. Si falta o no pasa la comprobación, el
juego conserva inglés y las opciones originales. En PCSX2 debe estar junto
a `oot.z64` y el ELF. Se carga una vez en RAM; los glifos y los diálogos
traducidos no provocan una lectura USB por letra.

La fila nueva usa una fuente sans serif de menú con tamaño y contorno uniformes,
espaciado proporcional y formato IA8, en lugar de la fuente de los diálogos.
El archivo de idioma tiene versión 2; el runtime rechaza la versión anterior
que usaba etiquetas I8 para evitar interpretarlas con el formato equivocado.

La adaptación contiene 2.114 mensajes, la fuente con los caracteres españoles
y 380 recursos de textura, incluidos menús y nombres. Conserva el código,
modelos, colisiones y audio nativos del port. Las voces conservan el audio
original. No se considera una traducción íntegra: algunos textos escritos
en código y los créditos siguen en inglés; los identificadores ausentes
conservan su mensaje original.

Los datos proceden de la traducción **eduardo_aj2 v2.2**, disponible mediante
el parche `es.ppf` de [Patcher64Plus-Tool](https://github.com/Admentus64/Patcher64Plus-Tool).
El archivo de idioma, los parches y las ROM usadas para generarlo son datos
privados de compilación y no se incluyen en Git.

Para regenerar el archivo se necesitan copias propias descomprimidas NTSC 1.0,
original y traducida, la extracción original NTSC 1.2 y el manifiesto de datos
convertidos del port. Con el Python del entorno de extracción:

```sh
python tools/prepare_ps2_spanish.py \
  --original /ruta/ntsc10-original.z64 \
  --translated /ruta/ntsc10-espanol.z64 \
  --upstream /ruta/oot-upstream \
  --menu-font /ruta/fuente-sans-serif-bold.ttf
```

El entorno de generación necesita Pillow. La fuente del sistema sólo se usa
para rasterizar cuatro etiquetas; su archivo TTF no se copia al port ni a Git.
El informe registra su SHA256 para reproducir el aspecto de la fila.

La herramienta importa mensajes por identificador y exige que el nombre,
segmento, offset, tamaño, dimensiones y formato de cada textura coincidan en
los descriptores de ambas revisiones. El informe `ESP.json` registra hashes,
recursos y la imagen convertida compatible. El empaquetador comprueba esa
relación antes de copiar. No se debe sustituir OOT.Z64 por una ROM parcheada.

Las pruebas EE cubren todos los registros, lecturas parciales, memoria vecina,
fuente y retorno a inglés. Una prueba separada ejercita el logo real, la
selección en opciones, la escritura/lectura del encabezado SRAM, un diálogo
traducido y casa → Kokiri → casa, con tarjetas del jugador desactivadas.
La persistencia de idioma en una tarjeta física todavía requiere comprobación.
