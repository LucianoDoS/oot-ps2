# Changelog — Revisión y corrección del port a PS2

Este documento resume todo lo revisado, corregido y verificado sobre el
port de Ocarina of Time a PS2. Está pensado para que puedas retomar el
trabajo sabiendo exactamente qué se tocó, por qué, y qué falta.

## Cinemáticas compiladas del Árbol Deku — 2026-10-04

- El usuario informa de un bloqueo al entrar a la zona exterior del árbol.
  El registro deja de producir fotogramas, pero el audio sigue funcionando.
  Esa sala de Kokiri ya está convertida; el interior de la mazmorra todavía
  no forma parte del perfil `play`.
- Las cinemáticas del actor del árbol se compilan desde C. Los macros de
  comandos empaquetaban bytes y medios words como en N64; en el EE el
  parser leía tiempos, posiciones y marcas de fin en otros campos. En
  particular, el byte `CS_CAM_STOP` quedaba al final del word y la búsqueda
  de cámaras podía salir de la lista.
- Los macros PS2 colocan esos miembros en sus offsets nativos. Los words
  completos, flotantes y punteros conservan su valor; la ruta N64 conserva
  su orden. Inicio y parada de música usan los dos bytes reales de su
  cabecera, en vez de un medio word que invierte el ID en el EE.
- `check_ps2_compiled_cutscenes.py` compila las cuatro secuencias reales y
  una muestra con cámara, actor, luz, vibración, texto, hora, transición,
  destino y música. Sus 3448 bytes coinciden con el esquema independiente
  del conversor de ROM; trece listas de cámara terminan dentro de sus
  límites. También verifica los cuatro arrays del ELF final. El control
  con los headers anteriores falla por el orden de los campos.
- Pasa el diagnóstico EE completo de gráficos y audio y enlazan 721
  objetos. Registros: `build/PS2/check-compiled-deku-linked.log` y
  `build/PS2/pcsx2-deku-native-commands-pass.log`. La versión está abierta
  para comprobar la conversación y la entrada a la zona en la partida.
  Se conserva el ELF anterior como `oot_ps2-ee-memcpy-confirmed.elf` y una
  copia de la memory card previa al reinicio. No se modifica la imagen de
  recursos ni se da por validado el interior del árbol.
- El logo de Nintendo 64 mantiene la frecuencia de su código original:
  60 FPS, frente a 30 del selector y 20 del juego. El usuario confirma
  ahora 20 FPS quieto en casi todas las vistas; quedan caídas al caminar.

## Copias de paquetes en el EE — 2026-10-04

- El ejecutable enlazaba la copia del SDK que recorre palabras de 32 bits.
  gsKit la utiliza para copiar los puntos empaquetados de cada dibujo a
  la cola GIF. El enlace PS2 ahora redirige esas llamadas mediante
  `--wrap=memcpy` a una implementación que copia bloques completos con
  LQ/SQ si ambos extremos están alineados a dieciséis bytes, o LD/SD si
  están alineados a ocho. Las colas y demás alineaciones usan el SDK.
- Se mantienen el valor de retorno, el caso de longitud cero y los límites
  de lectura/escritura. El SDK original sigue enlazado para la ruta de
  respaldo y la comparación de control. Los puntos, el orden de los
  dibujos y los estados GS conservan sus datos originales.
- Pasan 19200 copias con alineaciones independientes 0–15, longitudes
  0–65 y tamaños de paquetes hasta 2880 bytes, comparadas con una copia
  por bytes y comprobando las zonas de guarda. También pasan audio,
  captura, transparencias, suelo, aura y 120 fotogramas.
- En 8192 repeticiones, 144 bytes alineados pasan de 1678908 a 815892
  ciclos; con fuente desplazada ocho bytes, de 1682994 a 1055593.
  El bloque de 2880 bytes pasa de 27395480 a 6707563. Son medidas de
  copia, no porcentajes de FPS. Registro: `build/PS2/pcsx2-ee-memcpy-pass.log`.
- El juego enlaza 721 objetos. El usuario confirma 18–20 FPS quieto
  y aproximadamente 16–18 caminando, con algunas caídas según la vista;
  imagen y audio correctos. Sigue pendiente estabilizar los 20 FPS.
  La anterior, confirmada en 16–18 FPS, se conserva en
  `build/PS2-ntsc12/oot_ps2-glow-dependency-confirmed.elf`.

## Dependencias del brillo de las hadas — 2026-10-04

- El perfil del exterior muestra hasta veinte conversiones y subidas de
  texturas I8 de 32×64 por fotograma, con los mismos colores RGB y distinto
  ENV_ALPHA. Son las dos mitades del brillo de las hadas. En el evaluador
  genérico actual, TEXEL1 usa TEXEL0, por lo que su diferencia en ese
  material es exactamente cero y el factor ENV_ALPHA no cambia sus píxeles.
- El análisis de dependencias excluye los términos de un producto con
  diferencia cero, tanto de la clave de textura como de las tablas por
  canal. La ruta explícita de dos fuentes para caminos conserva sus
  dependencias; el aura del título conserva su clave completa. Las
  ecuaciones de color, alfa y el comportamiento de los vértices se mantienen.
- La prueba EE compara los 256 valores de 26 materiales, nueve cambios
  de constantes y dieciséis máscaras parciales (3744 combinaciones).
  Una textura real del material de brillo conserva sus 8192 bytes y su
  dirección VRAM durante 255 cambios de alfa, sin subirla de nuevo.
  Cambiar el tinte o un byte de la fuente provoca una nueva subida.
- Pasa el diagnóstico completo, incluido el fence de descriptores, audio,
  suelo, transparencias, aura y 120 fotogramas. Registro:
  `build/PS2/pcsx2-glow-dependency-pass.log`. El control con el combinador
  anterior falla al comprobar la reutilización en el primer cambio de alfa:
  `build/PS2/pcsx2-glow-dependency-before-control.log`.
- El juego enlaza 720 objetos. En el mismo punto exterior de 14–16 FPS,
  el usuario confirma 16–18 FPS y Navi, suelo, sombras y audio correctos.
  El registro conserva sólo 3–4 subidas por fotograma en esa vista y
  desaparecen las veinte subidas del brillo. El objetivo de 20 FPS
  estables sigue pendiente. Se conserva la versión
  anterior en `build/PS2-ntsc12/oot_ps2-ground-reuse-confirmed.elf`.

## Reutilización de las dos capas del suelo — 2026-10-04

- Los materiales del suelo mantienen un pase base y otro de mezcla por
  cada triángulo, en el mismo orden. Ahora ambas capas reutilizan sus
  vértices compartidos durante una secuencia sin cambios de estado.
  Se conservan los colores y la profundidad originales de la primera
  capa para preparar la segunda. El clamp base se aplica después de cada
  capa superpuesta; no se agrupan los pases ni se altera su orden.
- Cada comando de estado invalida ambos conjuntos de puntos. Los
  vértices temporales de recorte y las profundidades calculadas por
  triángulo para decals mantienen su preparación independiente.
- La prueba EE compara todos los puntos y el número de paquetes con
  la ruta inmediata en ocho casos: alfa cero/parcial/opaco, niebla,
  perspectiva, dimensiones y shifts distintos, decals, recorte,
  índices repetidos, culling y límites entre comandos de estado.
  Pasa sin diferencias, junto con audio, captura, aura y 120 fotogramas.
  La prueba aislada baja de 1893328 a 1261975 ciclos; registro
  `build/PS2/pcsx2-ground-reuse-pass.log`.
- El usuario confirma suelo, sombras y audio correctos, pero la vista
  pesada sigue en 14–16 FPS. El objetivo de 20 FPS estables continúa
  pendiente; se conserva el ejecutable anterior como
  `build/PS2-ntsc12/oot_ps2-material-mmi-confirmed.elf`.

## Tablas de materiales y comprobación de texturas — 2026-10-04

- Se retoma la optimización del exterior antes de ampliar la zona del
  Árbol Deku. El usuario señala una vista de 12–14 FPS. Tras la primera
  optimización confirma 14–16 FPS y colores y audio correctos. La siguiente
  versión conserva sombras, transparencias y audio, pero el usuario
  confirma que sigue en 14–16 FPS quieto. Todavía no se da por alcanzado
  el objetivo de 20 FPS estables.
- Las tablas del combinador expanden el producto del primer ciclo una
  vez y recorren sus 256 valores mediante diferencias finitas. Conservan
  la división entera con signo y la saturación original. Los materiales
  de dos ciclos conservan RGBA8 intermedio, incluido COMBINED_ALPHA; el
  alfa sólo se convierte a la escala del GS al acabar el último ciclo.
- XXH32 procesa las cuatro cadenas de las fuentes alineadas mediante
  instrucciones MMI del EE. PMULTUW calcula dos productos por instrucción
  y PPACW reúne sus partes de 32 bits; se contrastó su semántica con
  [PCSX2](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MMI.cpp).
  Sólo usa cargas LQ para bloques completos alineados a 16 bytes. Las
  fuentes sin esa alineación y las colas conservan la lectura escalar.
  Se siguen comprobando todos los bytes en cada enlace de textura.
- Las pruebas EE pasan 25 materiales × 9 cambios de constantes × 16
  máscaras parciales, comparados con el evaluador escalar para todos los
  texels. Cubren productos negativos y cuadráticos, alfa del ciclo anterior,
  saturación, RGBA16 empaquetado y sustitución del donante de la caché.
  También pasan los vectores independientes de XXH32, alineaciones 0–15,
  mutación de todos los bytes, audio, captura, aura y los 120 fotogramas.
- Comparación de control con las mismas pruebas y el combinador anterior:
  las tablas completas pasan de 23961838 a 8668335 ciclos; con reutilización
  de canales, de 9028600 a 4946683. El material de dos ciclos reales pasa
  de 39557399 a 28547484 ciclos, con el mismo checksum 193581.
  La comprobación de fuentes alineadas de 2/4 KiB baja aproximadamente un
  13–14 %. Son medidas aisladas; no representan ese porcentaje de FPS.
  Registros: `build/PS2/pcsx2-material-mmi-pass.log` y
  `build/PS2/pcsx2-material-mmi-before-control.log`. La primera sesión
  confirmada queda en `build/PS2/pcsx2-material-polynomial-game-measured.log`.
- El juego enlaza 720 objetos. Se guardan los ejecutables anteriores en
  `build/PS2-ntsc12/oot_ps2-before-material-polynomial.elf` y
  `build/PS2-ntsc12/oot_ps2-material-polynomial.elf`.

## Apertura de pausa, transparencia y aura del título — 2026-10-04

- El usuario señala corrupción y espera al abrir la pausa, falta de
  transparencia del inventario y un aura inmóvil alrededor del título.
  Confirma después que ambas cosas del inventario mejoraron y que el aura
  ya se mueve como una llama.
- La copia N64 del fondo operaba sobre `curFrameBuffer`, que el renderer
  nativo no rellena: el dibujo vive en GS VRAM. Sus pases de cobertura se
  dibujaban en la pantalla y el filtro CPU leía un búfer sin imagen válida.
  `Play_Draw` ahora emite una captura nativa al acabar la escena, antes del
  HUD. Espera su terminación y utiliza ese fondo para la pausa; en PS2 se
  omiten los pases de cobertura y el filtro de reconstrucción N64.
- La captura descarga filas del framebuffer activo y publica 320×240
  RGBA5551 big-endian. Usa sólo 40 KiB de memoria temporal, sin reservar
  más VRAM. La transferencia VIF1 sigue el
  [SDK de ps2dev](https://github.com/ps2dev/ps2sdk/blob/master/ee/debug/src/screenshot.c),
  adaptada al formato CT16S y al ancho del framebuffer. Conserva el fence
  FINISH de gsKit para que la siguiente cola pueda continuar.
- Los paneles originales IA8 ya contienen alfa parcial y usan el mezclador
  XLU. Por ejemplo, 2228 de los 2560 texels de `gPauseSelectItem11Tex`
  tienen alfa 11/15. Al restaurar una captura válida, esas partes permiten
  ver el escenario. Se conserva su alfa y el comportamiento original.
- El título usa una máscara I4 de 64×64 y ruido I8 de 32×32, con shifts y
  desplazamiento de medio texel. Antes TEXEL1 se evaluaba como TEXEL0.
  Ahora ese material combina ambos datos, su fracción LOD, colores y alfa
  de los dos ciclos. Una tabla reutilizada evita resolver el combinador
  por cada píxel. La caché incluye desplazamiento y contenido de ambas
  fuentes; la ruta queda limitada a este material y sus dimensiones.
- Las pruebas EE comprueban 230400 píxeles descargados, orden de bytes,
  límites, comandos inválidos, segunda captura sin cambiar la imagen y
  continuidad de la cola. El GS también pasa el dibujo de paneles IA8 con
  alfa cero, parcial y opaco sobre una imagen conocida.
  El aura pasa 81920 comprobaciones de canales con scroll fraccionario,
  distintas tintas, alfa, reutilización y mutación de la segunda textura.
  También pasan audio, retrato y los 120 fotogramas del diagnóstico.
  Registros: `build/PS2/pcsx2-pause-flame-pass.log` y
  `build/PS2/pcsx2-pause-flame-final-pass.log`. La sesión en juego muestra
  varias capturas correctas al reabrir el inventario; se conserva en
  `build/PS2/pcsx2-pause-flame-game-confirmed.log`.
- El juego enlaza 720 objetos. Se conserva
  `build/PS2-ntsc12/oot_ps2-before-pause-flame.elf` como copia anterior.
  El objetivo de 20 FPS estables y la validación completa de escenas
  siguen pendientes; estas correcciones no los dan por alcanzados.

## Modelos recibidos y retrato de equipamiento — 2026-10-04

- El usuario confirma el retrato de equipamiento, aunque conserva su
  resolución original de 64×112. Los objetos seguían ausentes: convertir
  sus recursos era necesario, pero no corregía el evento que los activa.
- El diagnóstico registra la carga de la rupia terminada (`queue=1`),
  mientras `draw=0` permanece al acabar la animación de 22 fotogramas.
  `Animation_OnFrameImpl` reconstruía el fotograma actual mediante
  `(current - speed) + speed`. Con la velocidad ajustada 2/3 y tasa 1.5,
  el redondeo EE pierde precisión justo en el último fotograma, 21, y
  omite el evento. Se comparan los extremos del intervalo con el fotograma
  actual real, manteniendo la convención original de vuelta y fotograma 0.
  El código N64 queda intacto. Referencia del redondeo predeterminado:
  [configuración de PCSX2](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Pcsx2Config.cpp).
- La prueba del predicado real en EE falla exactamente en ese caso con
  el código anterior (`pcsx2-animation-before-item-fix.log`) y pasa los
  102401 casos tras corregirlo: velocidades positivas/negativas, fracciones,
  distintas tasas y vueltas. También pasan audio, gráficos, retrato y
  120 fotogramas en `build/PS2/pcsx2-item-animation-pass.log`.
- En juego se registra `draw=110`, carga completada y referencia entre
  las manos al recibir la rupia. El usuario confirma: «Sí, aparece».
  Se conservan `pcsx2-item-frame-21-not-shown.log` y
  `pcsx2-item-animation-confirmed.log`; se retiran las trazas repetidas y
  quedan únicamente dos registros por recepción, carga y aparición.
  Siguiente revisión solicitada: corrupción al abrir la pausa, transparencia
  del inventario y aura animada del título.
- El usuario confirma que las texturas de Link y el acceso a la nueva sala
  funcionan. Señala modelos ausentes al recibir la espada y otros objetos,
  y una imagen corrupta en el lugar del retrato de Link del equipamiento.
  Aclara después que el corazón de recuperación funciona correctamente.
- Los objetos recibidos se cargan desde Player, sin figurar necesariamente
  en las listas de objetos de las salas. El perfil play ahora convierte los
  77 segmentos `object_gi_*`, usando sus rangos explícitos de extracción.
  `check_ps2_received_items.py` verifica 214 listas gráficas, 143 grupos de
  vértices y 57 texturas; conserva los 145 segmentos previos byte por byte.
  La prueba con los datos anteriores falla por faltar `object_gi_key`.
  Registros: `received-items-check.log`, `received-items-before-check.log`
  y `kokiri-after-items-check.log`, dentro de `build/PS2`.
- El renderer sólo seguía el puntero de color, dibujando siempre en el
  framebuffer mostrado. Por eso el prerender de 64×112 nunca llegaba a la
  textura del equipamiento. Se añade un destino separado CT32 con Z24,
  reservado antes de la caché, y coordenadas a escala 1 durante ese dibujo.
  Dos comandos nativos de la lista delimitan el retrato y restauran después
  el framebuffer y la profundidad principales. No cambian la salida normal
  de 640×448 ni la interpolación de texturas.
- La lectura por VIF1 usa `ps2_screenshot` del SDK. Se publica RGBA5551 en
  el orden de bytes que consume el decodificador, con el fondo transparente.
  Se sustituye únicamente el proceso N64 de cobertura/VI de este retrato;
  no se aplica el filtro CPU a un búfer que el GS no había escrito.
  El retrato se reutiliza mientras no cambien el segmento, edad o equipo.
- La comprobación inicial detectó que el descriptor nativo en la pila EE
  se interpretaba como dirección segmentada; ahora usa su dirección física.
  Otra comprobación detectó la pérdida de FINISH: el SDK reconoce y borra
  el evento, mientras gsKit lo espera antes de enviar la siguiente cola.
  Se envía un FINISH independiente después de la lectura para conservar
  esa secuencia. Referencias: [lectura del SDK](https://github.com/ps2dev/ps2sdk/blob/master/ee/debug/src/screenshot.c)
  y [envío de colas de gsKit](https://github.com/ps2dev/gsKit/blob/master/ee/gs/src/gsCore.c).
- La prueba de GS dibuja dos imágenes distintas en el mismo destino y
  compara sus 14336 píxeles, alfa, límites de RAM, viewport, restauración
  de profundidad y actualización de la textura. Pasa con cero diferencias,
  junto al resto de gráficos/audio y 120 fotogramas posteriores; registro
  `build/PS2/pcsx2-portrait-items-pass.log`. Se conserva el ensayo sin FINISH
  en `pcsx2-portrait-before-fence-fix.log`: completa la primera lectura y
  se bloquea al intentar enviar el segundo dibujo.
- Juego enlazado con 719 objetos. ELF y datos instalados con SHA256 idéntico
  a los archivos verificados. El usuario guarda la partida antes de reiniciar.
  Se abre la versión nueva; las confirmaciones posteriores del equipamiento
  y de un objeto recibido están descritas arriba. Los respaldos `oot_ps2-before-item-portrait.elf`
  y `oot-before-item-models.z64/json` quedan en `build/PS2-ntsc12`.
  La sesión real ya completa tres lecturas del retrato, sigue respondiendo
  y mantiene PCM sin retrasos ni descartes. La caché dispone de 1760 KiB
  tras reservar los dos destinos de 32 KiB; conserva las 512 entradas.

## Ventanas de textura de Link y salas restantes de Kokiri — 2026-10-03

- El usuario percibe otra mejora de rendimiento, con caídas al mirar a
  ciertas cámaras, y señala una mancha rosa detrás de la oreja de Link y
  un patrón verde oscuro en el torso. No aporta una nueva cifra de FPS.
  También vuelve a encontrar un bloqueo al pasar a otra parte del bosque.
- Las listas originales cargan el pelo como RGBA16 de 16×16, pero fijan
  una ventana de 32×16 con máscara horizontal de 16 texels. La túnica usa
  I8 de 16×32 con una ventana de 32×32, espejo y clamp. El port confundía
  la ventana con la imagen y leía bytes del siguiente recurso como texels;
  además sólo reconocía espejo cuando el bit de clamp estaba apagado.
- Se decodifica el periodo que puede seleccionar la máscara, conservando
  el stride de LOADTILE o la fila TMEM de LOADBLOCK. Luego se materializan
  la repetición o el espejo dentro de la ventana limitada del GS. Las
  coordenadas se interpolan como antes, y la caché incluye las dimensiones
  de la imagen enviada, además de los texels y paletas que sí se usan.
  El orden clamp→mask/mirror se contrastó con el código de referencia de
  [angrylion-rdp-plus](https://github.com/ata4/angrylion-rdp-plus/blob/master/src/core/n64video/rdp/tex.c).
- La nueva prueba usa los formatos, máscaras y filas del pelo y la túnica,
  con ventanas de 24 y 32 texels, repetición, espejo y bytes de relleno que
  delatan lecturas del recurso siguiente. También comprueba los límites
  GS, la clave de caché y cambios de RAM dentro y fuera del periodo usado.
  Pasan sus 5376 texels, el resto de gráficos y audio y 120 fotogramas en
  `build/PS2/pcsx2-link-tile-window-pass.log`. La primera comprobación tenía
  14 diferencias de alfa en el valor I8 255: la referencia nueva esperaba
  128, pero el decodificador existente conserva 127. Se ajustó la referencia
  para verificar la corrección de filas sin alterar la cuantización de alfa.
- Los dos registros del bloqueo terminan en Scene_CommandObjectList y
  Actor_Spawn sin cargar una escena diferente de Kokiri. La imagen anterior
  sólo convertía `spot04_room_0`: faltaban `spot04_room_1` y `spot04_room_2`.
  El perfil play ahora incluye las tres salas y sus dependencias. Se añade
  un registro por carga de sala para identificar futuras transiciones.
- `check_ps2_kokiri_rooms.py` valida comandos, objetos, actores y texturas
  contra la ROM original: 33 recursos de escena y 68/46/22 de las salas,
  además de sus direcciones VROM. Los 139 segmentos previos son idénticos
  byte por byte. La misma comprobación falla en la imagen anterior por
  faltar `spot04_room_1`. Registros: `build/PS2/kokiri-rooms-check.log` y
  `build/PS2/kokiri-rooms-before-check.log`. Juego enlazado con 718 objetos
  e instalado junto a la nueva imagen de datos; ambos coinciden por SHA256
  con sus archivos verificados. La ventana responde y el arranque no registra
  excepciones ni bloques PCM perdidos. El usuario confirma la corrección de
  Link y que ya puede entrar en la zona; el registro muestra la carga de
  `spot04_room_2` y el regreso a la sala 0 sin excepciones. Al recoger la
  espada aparece otro fallo del modelo recibido y la vista de equipamiento
  muestra corrupto el retrato de Link; se investigan por separado.
- Respaldos de la versión anterior: `oot_ps2-before-link-windows.elf` y
  `oot-before-kokiri-rooms.z64/json`, dentro de `build/PS2-ntsc12`.

## Mezcla NEAD y ciclo final de materiales — 2026-10-03

- El usuario confirma 18 FPS quieto y 16 caminando en la misma vista de
  vallas y plantas, conservando imagen y audio correctos. El nuevo perfil
  mide unos 30–31 ms de traducción descontando DSP, con 7–10 ms de DSP
  intercalados y otras interrupciones todavía incluidas. Las tablas de
  material cuestan 1,5–2 ms; ENVMIX_NEAD cuesta 1,3 ms por tarea de audio.
  Captura: `build/PS2/pcsx2-channel-kernels-kokiri-profile.log`.
- La mezcla NEAD usa productos con signo de 32 bits: s16 × u16 cabe en
  ese rango y conserva los 16 bits retenidos por la conversión anterior.
  Se despliegan las ocho muestras de cada grupo, manteniendo el orden de
  lecturas, escrituras, saturación, XOR y rampas, también con alias entre
  los búferes. Los controles solapados conservan la ruta anterior.
- El builder de tablas omite el segundo ciclo cuando todos los canales
  evaluados sólo copian su valor del primer ciclo, ya limitado a 0–255.
  RGB sigue incluyendo el alfa intermedio; cualquier otra ecuación
  conserva ambos ciclos. No se cambian las constantes ni las claves de
  caché ni se omiten comprobaciones de texturas modificadas en RAM.
- Pruebas EE/GS completas: 53248 casos de mezcla, incluidos búferes largos
  y solapados; 20 materiales y 2880 máscaras parciales comparados con la
  referencia escalar para todos los valores de texel; 1408 pasadas con
  ciclo final omitido y 1046 conservadas. Pasan RGBA16, LRU, RESAMPLE,
  FILTER, iluminación, recorte, diálogo y 120 fotogramas sin diferencias.
  Registro: `build/PS2/pcsx2-envmix-identity-pass.log`.
- Micropruebas comparables con la versión anterior: mezcla
  20159461→15479007 ciclos (23 % menos), caché de tablas por canal
  11978894→9004768 (25 %). Son mejoras de trabajos aislados y no una
  predicción de FPS. Juego enlazado con 718 objetos, instalado con SHA256
  idéntico al ELF nuevo y abierto sin los perfiles optativos. Pendiente
  comparación del rendimiento del juego. La ventana responde y el primer
  minuto de producción PCM no pierde bloques ni registra retrasos.
  Respaldo:
  `build/PS2-ntsc12/oot_ps2-before-signed-envmix.elf`.
- Al final de la sesión anterior se registran accesos inválidos en la
  carga de objetos de escena y en Actor_Spawn. El usuario confirma que
  entró a una zona todavía no preparada y pide dejarla para después de
  optimizar los FPS. Se conserva el registro completo en
  `build/PS2/pcsx2-channel-kernels-end-session.log`; ampliar la conversión
  de recursos de esa zona sigue pendiente.

## Perfil de la vista lenta de Kokiri — 2026-10-03

- El usuario confirma que desaparecieron las letras sobre la pared de la
  tienda. La nueva captura del exterior, mirando hacia las vallas y plantas,
  marca 14 FPS; la respuesta sitúa esa vista en 14–16 FPS. Sigue pendiente
  mantener los 20 FPS naturales de la partida en todas las cámaras.
- El perfil de esa cámara mide unos 41 ms de traducción tras descontar
  14–18 ms de síntesis DSP; otras interrupciones siguen incluidas. Las
  tablas del combinador consumen 3,5–4,2 ms con 9–10 construcciones por
  fotograma. Dos fuentes I8 de 32×64 se vuelven a convertir con siete
  colores de entorno cuyo alfa cambia entre fotogramas. RESAMPLE cuesta
  0,7–1,6 ms por tarea de audio y FILTER alrededor de 1 ms. No se registran
  bloques PCM perdidos ni nuevos errores de colas en la captura conservada
  en `build/PS2/pcsx2-kokiri-before-channel-kernels-profile.log`.
- Las tablas del combinador reutilizan por separado R, G, B y alfa según
  las constantes que afectan a cada canal. Se propagan COMBINED y
  COMBINED_ALPHA entre ciclos, conservando el alfa intermedio que necesita
  RGB. Los canales donantes se copian antes de sustituir la entrada LRU,
  incluso cuando esa entrada es el único donante de un canal. El builder
  parcial conserva los canales no solicitados y las tablas RGBA16.
- RESAMPLE usa punteros consecutivos cuando toda la caminata y el historial
  caben en el búfer real; conserva la ruta enmascarada para desbordamientos
  e índices altos. FILTER despliega sus ocho productos por salida y usa
  sumas de 32 bits sin signo, preservando los bits 15–30 del resultado
  anterior de 64 bits. Se conserva la actualización secuencial de LUTs
  solapadas y el historial crudo antes de copiar el resultado.
- Se evita proyectar inicialmente vértices exteriores, que se rechazan o
  vuelven a proyectarse después del recorte. W degenerado, iluminación y
  texgen se conservan. El recorte reutiliza distancias por plano, evita
  divisiones enteras para recorrer aristas y omite copias interiores.
- Diagnóstico EE/GS completo sin diferencias: 10496 casos RESAMPLE con
  ambas rutas, 5121 FILTER y continuidad; 20 materiales y 2880 máscaras
  parciales con referencia escalar, RGBA16 y LRU; 20432 polígonos y 192
  comparaciones de puntos GS completos, niebla, decals y G_CULLDL. Pasan
  también la vida de descriptores, diálogo, iluminación, colas, resto de
  audio y 120 fotogramas en `build/PS2/pcsx2-channel-kernels-pass.log`.
- Micropruebas: RESAMPLE 16289975→11482973 ciclos (30 % menos), FILTER
  34074103→9350015 (73 %), tablas completas→caché por canal
  36307363→11978894 (67 %, incluida la copia de salida del helper),
  recorte 30798583→21546576 (30 %). El lote con 75 % de vértices exteriores
  baja de 9950956 a 8190698 (18 %); el totalmente visible es comparable
  (9647164→9695667). Son trabajos aislados y no se extrapolan a los FPS.
- Juego reconstruido con 718 objetos, instalado y abierto sin los perfiles
  optativos. El ELF instalado coincide por SHA256 con el recién enlazado;
  el arranque produce PCM sin bloques perdidos y la ventana responde.
  Comparación del usuario en la misma vista: 18 FPS quieto y 16 caminando,
  con imagen y audio correctos. Todavía no son 20 FPS estables.
  Respaldo de la versión anterior:
  `build/PS2-ntsc12/oot_ps2-before-channel-kernels.elf`.

## Letras sobre la pared y caída al seleccionar semillas — 2026-10-03

- El usuario percibe mejora general con la reutilización de geometría,
  pero detecta letras enormes en la pared al seleccionar semillas Deku en
  la tienda, con caída a 8 FPS. El diálogo inferior mantiene su tamaño.
  La sesión registra 271 cargas y sólo 28 hits por fotograma, cero retiros
  por textura y unos 94 ms de traducción. Registro anterior conservado en
  `build/PS2/pcsx2-lighting-queues-runtime.log`.
- La expulsión LRU podía perder un descriptor usado por dibujos GIF aún
  pendientes aunque la nueva carga ocupara otra región libre de VRAM.
  La región anterior quedaba sin historial y una fuente posterior podía
  sobrescribirla sin FINISH. Ahora se retiran los dibujos antes de perder
  ese descriptor, además de comprobar el solapamiento de VRAM.
- La capacidad sube de 256 a 512 descriptores, con 128 buckets y el mismo
  presupuesto de VRAM. La sombra y el color del texto generan variantes
  para cada fuente; el conjunto del diálogo superaba las 256 entradas y
  obligaba a recargarlas continuamente. Se sigue verificando todo el
  contenido RAM y las entradas de mezclas, sin congelar texturas animadas.
- Regresión en GS real con 513 glifos encolados: al descartar el primer
  descriptor, su región `0x230000` no solapa la nueva `0x2b0000`, pero se
  produce el FINISH necesario. Un conjunto de 300 glifos genera 300 cargas
  en el primer recorrido y cero en el segundo. El resto de renderizado,
  audio, iluminación, colas y 120 fotogramas pasa sin fallos en
  `build/PS2/pcsx2-dialogue-cache-pass.log`. Juego reconstruido con 718
  objetos. El mismo diagnóstico con la condición anterior, conservando
  las 512 entradas, falla exactamente en el retiro: cero FINISH en lugar
  de uno (`build/PS2/pcsx2-descriptor-old.log`). Eso verifica que ampliar
  capacidad por sí solo no corrige el fallo de vida de VRAM.
- Versión instalada y abierta sin perfil. Respaldo
  `build/PS2-ntsc12/oot_ps2-before-dialogue-cache.elf`. Pendiente comparación
  del mismo producto en la tienda y de FPS en otras vistas; no se afirma
  fidelidad completa al original a partir del smoke.

## Reutilización temprana de puntos e iluminación — 2026-10-03

- Resultado de la versión anterior confirmado por el usuario: tienda a
  20 FPS quieto y 14–16 caminando; exterior normalmente 14–16, llegando a
  20 según la cámara. La primera vista del exterior todavía provoca una
  caída mayor antes de estabilizarse. Se percibe mejora, pero aún no se
  mantiene la velocidad natural en todas las vistas.
- La búsqueda del punto GS texturado compartido se hace antes de repetir
  su color y profundidad. Los indicadores de SHADE y niebla se calculan
  una vez por grupo TRI. Los puntos temporales de recorte y decals siguen
  preparando sus valores completos; el suelo de dos pases conserva su
  orden. La referencia añade SHADE RGB/alfa, límites de profundidad,
  índices repetidos y comprobación efectiva del tag de niebla.
- La iluminación reutiliza RGB por normal bajo la misma tabla de luces,
  comprobada una vez por G_VTX. 256 entradas ocupan 3 KiB; la generación
  invalida en O(1), con borrado sólo al desbordarse el contador. Alfa y
  demás campos del vértice se conservan. Sin luces direccionales se copia
  directamente el ambiente. La conversión a entero con signo conserva
  el resultado de colores limitados a 0–255 y evita ramas del EE.
- La primera pasada compara 133650 casos de iluminación, cada uno con
  ruta escalar, caché y repetición con otro alfa: cero diferencias.
  Microprueba con las mismas luces/normales: 5003932 frente a 3476681 ciclos
  (30 % menos en ese trabajo aislado). Puntos GS, audio y 120 fotogramas
  pasan en `build/PS2/pcsx2-lighting-point-pass.log`. No se extrapola a FPS.
- Revisión independiente del lookup temprano: no se leen colores ni
  profundidades descartados cuando se reutiliza un punto; los cambios de
  estado siguen terminando los grupos TRI. La prueba del envío texturado
  en lotes baja de 309397 ciclos en la captura anterior a 187347 en esta;
  ambas capturas son micropruebas, no medidas del juego completo.

## Vida de colas temporales tras cargas y animaciones — 2026-10-03

- El registro de la sesión anterior acumula errores de tabla de colas
  llena al prolongar el juego. Las colas locales de DMA y las posiciones
  de la cola de animación conservaban su registro de semáforos después de
  completar la tarea; la liberación del heap no cubre sus vidas cortas.
- En PS2 se liberan tras su último mensaje las colas de cuatro rutas DMA,
  la espera de temporizador, la tarea de sincronización del scheduler y
  la carga de un fotograma de animación. Se incluyen las salidas de error
  DMA. No se amplía el límite de 64 ni se liberan colas dentro del receive
  genérico. La lógica de otras plataformas se conserva.
- La prueba de vida de colas añade 384 tareas terminadas en 96 direcciones
  distintas, manteniendo una cola pendiente independiente, además de las
  768 vidas de heap ya cubiertas. La última pasada pasa esos 1152 ciclos,
  iluminación, renderizado, audio y 120 fotogramas sin fallos:
  `build/PS2/pcsx2-lighting-queue-pass.log`. El juego con los siete callsites
  se reconstruye con 718 objetos. Versión instalada y abierta sin perfil;
  respaldo `build/PS2-ntsc12/oot_ps2-before-lighting-queues.elf`, registro
  anterior en `build/PS2/pcsx2-color-runs-runtime.log`. Pendiente comparar
  FPS y comprobar una sesión larga con los callsites corregidos.

## Perfil sin síntesis y lotes de geometría sin textura — 2026-10-03

- El usuario confirma la mejora de presentación: 16–18 FPS caminando,
  alrededor de 20 quieto (a veces 18 según cámara); tienda a 14–16 FPS.
  Registro de esa versión: `build/PS2/pcsx2-present-overlap-runtime.log`.
- El perfil optativo descuenta los ciclos de síntesis DSP de sus tiempos
  gráficos. Una lectura repetida del contador evita capturar un inicio
  interrumpido a mitad de la medición. Preparación de notas, otros hilos e
  interrupciones siguen incluidos; no son tiempos exclusivos completos.
  La captura de la tienda resta 8–13 ms de DSP: quedan aproximadamente
  45 ms, con 12 ms de carga/proceso de vértices y 25 ms de triángulos.
  Unas 2577 evaluaciones de color sin textura cuestan 4 ms por fotograma.
  Registro: `build/PS2/pcsx2-shop-self-profile-runtime.log`.
- Los grupos TRI consecutivos ahora agrupan también geometría sin textura,
  hasta 20 triángulos por paquete, y reutilizan el color de los vértices
  originales compartidos. Cualquier cambio de estado/carga/salto termina
  el grupo. La profundidad de decal sigue calculándose por triángulo;
  los vértices temporales de recorte conservan su evaluación independiente.
  El suelo de dos pases texturados mantiene su orden anterior.
- Referencia de puntos GS entre envío inmediato y lotes: color, alfa,
  niebla, profundidad de decal, recorte, culling, capacidad y cambio de
  material/vértice. La primera pasada detectó que el envío TRI2 inmediato
  debía restablecer su tipo después de un lote sin textura; se corrigió
  antes de instalar. Microprueba final: 397057 frente a 247976 ciclos (38 % menos
  en el envío aislado de geometría sin textura, sin extrapolar a FPS).
- La segunda pasada completa pasa renderizado, audio, fondo de inventario
  y 120 fotogramas sin fallos en `build/PS2/pcsx2-color-run-pass.log`.
  La transición posterior hacia TRI2 texturado también pasa: 100 paquetes
  inmediatos frente a 10 en lotes, sin diferencias. Juego reconstruido
  con 718 objetos. Comparación posterior: tienda quieto 20, caminando
  14–16; exterior normalmente 14–16, con algunas vistas a 20.
- Revisión independiente del cambio: se corrigió el modo de la fixture de
  niebla para activarla realmente y se comprueba que los puntos usan XYZF2.
  Esa última pasada completa también pasa. Versión abierta con perfil
  desactivado; respaldo `build/PS2-ntsc12/oot_ps2-before-color-runs.elf`.

## Actualización de CPU durante la presentación GS — 2026-10-02

- Resultado confirmado por el usuario tras las optimizaciones de audio:
  sonido correcto, 16–18 FPS quieto con máximos de 20; caminando, 10–14 FPS.
  El objetivo sigue pendiente durante el movimiento. El registro muestra
  actualización CPU de unos 4–6 ms y presentación de unos 10 ms, además
  de traducción gráfica y síntesis de audio.
- Se separan envío/retiro y presentación. El worker emite FINISH, ejecuta
  la cola y espera al GS antes de liberar los datos de la tarea y avisar
  DP_DONE. Libera primero el buzón para aceptar una tarea pendiente sin
  bloquear al scheduler. La CPU puede avanzar durante la espera del refresco;
  el worker termina de presentar antes de interpretar la siguiente lista.
  Se conservan prioridades de audio y límites de juego/menú de 20/30 Hz.
- Se arma el refresco antes de DP_DONE. La presentación conserva un refresco
  ocurrido mientras la CPU actualiza, evitando borrarlo y esperar otro al
  reanudarse el worker. El cambio de buffer y setactive siguen la secuencia
  del SDK instalado, comprobada también frente al código oficial de gsKit:
  https://github.com/ps2dev/gsKit/blob/master/ee/gs/src/gsCore.c.
- Prueba del puente real de tareas: 64 fotogramas con traducción y presentación
  artificialmente lentas. Audio antes de DP, envío pendiente sin esperar la
  presentación, buffers de comandos distintos y presentación serializada:
  cero fallos en `build/PS2/pcsx2-present-task-pass.log`.
- Diagnóstico GS real: tres presentaciones tras esperar 20 ms conservan
  el refresco y alternan los buffers sin otra espera de vblank (máximo
  153 ciclos medidos en esa llamada). Audio/renderizado completos y 120
  fotogramas pasan en `build/PS2/pcsx2-present-gs-pass.log`. Juego reconstruido
  con 718 objetos y abierto; respaldo
  `build/PS2-ntsc12/oot_ps2-before-present-overlap.elf`. Pendiente comparación
  del exterior e inventario por el usuario; no se afirma una mejora de FPS
  a partir de estas pruebas aisladas.

## Reutilización de controles del mezclador NEAD — 2026-10-02

- La mezcla conserva ganancias y máscaras XOR en variables locales durante
  cada grupo de ocho muestras. Sigue actualizando la envolvente entre grupos
  y conserva el orden de lectura/escritura cuando los buffers se solapan.
  Si los controles están dentro de un destino, se usa el bucle anterior.
  No se reduce la frecuencia, precisión, reverberación ni número de notas.
- Referencia independiente: 40960 casos con longitudes redondeadas, rampas
  de volumen, saturación, inversión XOR, intercambio de canales húmedos y
  solapamientos de muestras/controles/steps. Cero diferencias de memoria y
  estado final. 1000 bloques de 256 muestras: 22480504 ciclos anteriores
  frente a 20190423 del nuevo camino, aproximadamente 10 % menos en la
  mezcla aislada. No representa un porcentaje de mejora de FPS.
- Diagnóstico completo pasa audio y 120 fotogramas:
  `build/PS2/pcsx2-envmix-pass.log`. Juego reconstruido con 718 objetos y
  abierto; respaldo `build/PS2-ntsc12/oot_ps2-before-envmix-cached.elf`.
  Registro de la versión ADPCM anterior preservado en
  `build/PS2/pcsx2-adpcm-unrolled-runtime.log`. Pendiente comparación de FPS
  en el exterior y confirmación del sonido por el usuario.

## Coste de síntesis y predictor ADPCM — 2026-10-02

- El contador anterior de audio medía la preparación de notas, pero dejaba
  fuera la síntesis ejecutada por el scheduler. El nuevo contador mide esa
  tarea completa: aproximadamente 6–8 ms por bloque a 60 bloques por segundo
  en las escenas observadas. Ese trabajo interrumpe al renderer y también
  aparece dentro de sus tiempos. No son tiempos exclusivos de cada hilo.
- Perfil privado por opcode, activado sólo con
  `host:ps2-audio-profile.enable`: ADPCM cuesta 2,4–3,7 ms por tarea muestreada,
  ENVMIXER 1,8–2,7 ms y RESAMPLE 1,3–2 ms. Registros preservados en
  `build/PS2/pcsx2-dsp-profile-runtime.log` y
  `build/PS2/pcsx2-audio-op-profile-runtime.log`. El archivo de activación
  se retiró antes de abrir la versión para jugar.
- Predictor ADPCM de ocho muestras desplegado para EE: elimina el bucle
  triangular de productos, conserva suma de 32 bits, saturación e historial.
  Las entradas parciales y los buffers solapados usan el camino anterior.
  4096 casos, longitudes 0–8, extremos, desbordamiento y aliasing producen
  exactamente las mismas muestras. 10000 llamadas: 5541506 ciclos de la
  referencia frente a 3500004 del nuevo predictor, aproximadamente 37 % menos
  en esa operación aislada, sin extrapolarlo a los FPS del juego.
- Diagnóstico completo pasa audio y 120 fotogramas en
  `build/PS2/pcsx2-adpcm-unrolled-pass.log`. Juego reconstruido con 718 objetos
  y abierto; respaldo `build/PS2-ntsc12/oot_ps2-before-adpcm-unrolled.elf`.
  Pendiente comparar los FPS del exterior y comprobar el sonido al jugar.

## Reutilización del estado enviado al GS — 2026-10-02

- El usuario confirma que las lecturas nativas tampoco cambian los FPS.
  Nueva captura del exterior: el envío de triángulos ocupa aproximadamente
  13–38 ms por muestra; actualización del juego, unos 5 ms. Las muestras
  incluyen interrupciones y varían con la cámara. Registro:
  `build/PS2/pcsx2-native-read-profile-runtime.log`.
- ALPHA, TEST y ZBUF conservan el último valor realmente enviado por cada
  contexto GS. Se omiten paquetes repetidos; las copias de materiales RDP
  comparten esa referencia al hardware. Los pases de mezcla del suelo y
  el borrado temporal de profundidad actualizan la misma caché al cambiar
  y restaurar estado. ZBUF incluye dirección y formato de profundidad.
  La caché se invalida al iniciar cada tarea y al inicializar el renderer,
  para respetar los ajustes de buffers y contexto de gsKit.
- Prueba sobre la cola GS: 300 actualizaciones repetidas del mismo modo
  (incluidos comandos parciales y filtro) no añaden paquetes tras las tres
  primeros envíos. Se verifican cambio y retorno de contexto, cambio
  de dirección/formato ZBUF, restauración desde otra copia de RDPState y
  borrado de profundidad. Diagnóstico completo pasa 120 fotogramas sin
  fallos, incluidas las pruebas de texturas y audio:
  `build/PS2/pcsx2-gs-state-pass.log`.
- Contadores de escrituras y reutilizaciones por fotograma, más tiempo de
  othermode en el perfil optativo. Juego reconstruido con 718 objetos y
  abierto sin el perfil activado; respaldo
  `build/PS2-ntsc12/oot_ps2-before-gs-state.elf`. En una muestra del exterior
  se emiten 323 estados y se reutilizan 735. El usuario confirma FPS iguales;
  no se da por resuelto el objetivo de 20 FPS estables. Registro:
  `build/PS2/pcsx2-gs-state-runtime.log`.

## Lecturas nativas de comandos y geometría — 2026-10-02

- Los campos de vértices, matrices y viewport comunican al compilador su
  alineación real de dos bytes; las palabras usan cuatro. El intérprete lee
  ambos comandos con una sola decisión de alineación. El objeto EE utiliza
  LH/LHU y LW en los caminos alineados, evitando las lecturas byte a byte y
  copias a la pila anteriores. Se mantienen las fuentes sin alinear y el
  modo opcional de assets big-endian.
- Prueba de 4096 casos: 16 desplazamientos, valores con y sin signo,
  palabras individuales y parejas de comandos. GCC transformaba el primer
  oráculo en cargas LWL/LWR; las comparaciones fallaban pese a coincidir
  los valores impresos. La referencia independiente lee bytes volátiles y
  devuelve una palabra desde memoria (LW). Las comparaciones pasan sin diferencias;
  también pasan las referencias de PC en ambos modos de endianness.
- Para 50000 comandos alineados, 2037223 ciclos del lector anterior frente
  a 709046 del nuevo (aproximadamente 65 % menos en esta lectura aislada).
  Sin alinear: 2034519 frente a 2122237; ambos checksums coinciden. Ese
  pequeño coste adicional corresponde a comprobar alineación. Diagnóstico
  completo: 120 fotogramas y cero fallos en
  `build/PS2/pcsx2-native-read-pass.log`, incluidas texturas, audio y guardado.
- Juego reconstruido con 718 objetos; respaldo de la versión anterior:
  `build/PS2-ntsc12/oot_ps2-before-native-read.elf`. El efecto sobre los FPS
  del juego no mejoró según el usuario; no se extrapola la microprueba
  al rendimiento del exterior.

## Lecturas alineadas en comprobación de texturas — 2026-10-02

- Inspección del objeto EE: el memcpy de cuatro bytes del hash generaba
  cuatro LBU, cuatro SB a la pila y un LW para cada palabra, incluso para
  fuentes alineadas. El bucle ahora comprueba alineación una sola vez y
  comunica al compilador la alineación de cuatro bytes. El objeto generado
  usa un LW por palabra en ese camino; las fuentes sin alinear y las colas
  conservan la lectura anterior. No cambia el algoritmo XXH32 ni se omiten
  bytes, filas o fuentes del suelo.
- Vectores de referencia, 16 alineaciones, dos semillas, colas y mutaciones
  de cada byte pasan. Para 1000 hashes con semillas distintas y contenido
  idéntico: 2048 bytes requieren 5678896 ciclos alineados frente a 12931413
  del camino sin alinear; 4096 bytes, 11259006 frente a 25781022. Los checksums
  coinciden (aproximadamente 56 % menos ciclos en esa operación aislada).
- Diagnóstico completo pasa 120 fotogramas y cero fallos:
  `build/PS2/pcsx2-aligned-hash-pass.log`. Juego reconstruido con 718 objetos
  y abierto para medir el efecto real; respaldo
  `build/PS2-ntsc12/oot_ps2-before-aligned-hash.elf`. El usuario confirma
  FPS similares y una ligera mejora percibida al caminar. No se extrapola
  el porcentaje del hash al juego completo; siguen pendientes 20 FPS estables.

## Reutilización de vértices GS — 2026-10-02

- Los grupos consecutivos TRI reutilizan RGBAQ, STQ y XYZ de los vértices
  originales compartidos. También conservan su niebla y evitan repetir la
  aplicación del mismo tile. La caché dura sólo el grupo: cualquier comando
  de estado, carga de vértices, salto o fin de lista lo termina. Vaciar un
  paquete por capacidad conserva los datos. Los vértices temporales de
  recorte y decals con sesgo de profundidad por pendiente quedan excluidos;
  el suelo de dos pasadas mantiene su camino anterior.
- Captura exclusiva del diagnóstico compara los puntos GS completos con
  el camino inmediato, incluyendo perspectiva, niebla, alfa dinámico,
  decals, recorte y cambios de coordenadas/PRIM entre grupos. Coinciden sin
  diferencias. En 42 triángulos con tres vértices compartidos, 126
  preparaciones pasan a 3 y 123 reutilizaciones. El diagnóstico completo
  pasa 120 fotogramas: `build/PS2/pcsx2-point-reuse-pass.log`.
- El perfil privado separa compilación de material (incluida en conversión)
  y preparación de puntos (incluida en envío). Los contadores muestran
  puntos calculados y reutilizados. El juego compila con 718 objetos y se
  abre para comprobar rendimiento e imagen; respaldo
  `build/PS2-ntsc12/oot_ps2-before-point-reuse.elf`. El usuario confirma
  cinemática igual, 10–12 FPS caminando y 14–16 quieto: no hay una mejora
  perceptible de FPS con este cambio. Registro:
  `build/PS2/pcsx2-point-reuse-runtime.log`. No se da por conseguido el
  objetivo de 20 FPS.

## Colores de geometría sin textura — 2026-10-02

- Tras la conversión por tablas, el usuario confirma 14–16 FPS quieto y
  alrededor de 12 FPS al moverse; antes observaba 10–12 FPS. También indica
  caídas cuando Link aparece en la cinemática. No se atribuyen todas esas
  caídas al modelo sin medir otros factores.
- Los polígonos sin textura compilan selectores y constantes del combinador
  por material, conservando los cuatro canales de sombreado, dependencias
  de alfa combinado, dos ciclos y saturación en cada ciclo. SHADE se aplica
  directamente cuando la ecuación final lo permite. La clave comprueba los
  dos comandos, tipo de ciclo, PRIM y ENV incluso si se restaura el estado
  sin llamar a setters. El evaluador escalar anterior se conserva como oráculo.
- Prueba diferencial: 64 materiales, cuatro tipos de ciclo, todos los valores
  de cada canal de sombreado por separado y mezclados, sin diferencias. Para
  10000 evaluaciones, los tres casos medidos pasan de 8114418/7983042/15601124
  a 2469549/1188681/5104464 ciclos EE (aproximadamente 67–85 % menos trabajo
  en ese cálculo aislado; no representa el aumento de FPS del juego).
  Diagnóstico completo: `build/PS2/pcsx2-vertex-color-pass.log`, 120 fotogramas
  y cero fallos, incluido audio y las pruebas de texturas existentes.
- El perfil privado añade tiempo y número de evaluaciones de colores sin
  textura, incluidos en la preparación de triángulos. El juego compila con
  718 objetos; respaldo `build/PS2-ntsc12/oot_ps2-before-vertex-color.elf`.
  El usuario responde «No noto diferencia»: no se atribuye una mejora de
  FPS a este cambio. El perfil real muestra que el cálculo de colores sin
  textura ya ocupa poco tiempo; sigue investigándose el resto del envío.
  Registro: `build/PS2/pcsx2-vertex-color-runtime.log`.

## Conversión de intensidad y paletas — 2026-10-02

- Diagnóstico optativo de ocupación y motivos de recarga de texturas, limitado
  a 24 registros por fotograma muestreado. Se imprime después de medir el
  renderizado y permanece desactivado en arranques normales. Con Link quieto
  en el exterior, predominan cambios de color/alfa en I4/I8 (hadas y efectos),
  no cambios de los bytes fuente. La caché usa casi sus 1792 KiB; aumentar
  solamente el número de entradas no elimina ese trabajo. Algunas muestras
  gastan 20–24 ms en conversión, incluyendo interrupciones de otros hilos.
  Registro: `build/PS2/pcsx2-texture-cache-profile-runtime.log`.
- I4, I8, IA4, IA8, CI4 y CI8 preparan una tabla de 16 o 256 colores finales
  por carga. Cada texel se expande con una lectura de tabla y una escritura
  de 32 bits. IA16 prepara tablas separadas para intensidad y alfa. Se conserva
  exactamente la cuantización del alfa GS anterior, las paletas CI4, los
  espejos y los materiales de dos texturas; siguen comprobándose todos los
  bytes fuente para detectar animaciones y cambios del fondo del inventario.
- Prueba diferencial en EE: todos los códigos de entrada de los siete
  formatos, las 65536 parejas de intensidad/alfa IA16, cinco materiales y
  paletas CI4 0/3/15 coinciden con la conversión anterior. El diagnóstico
  completo pasa 120 fotogramas, incluido audio, guardado y renderer:
  `build/PS2/pcsx2-compact-textures-pass.log`.
- El usuario confirma 14–16 FPS quieto y unos 12 al moverse, frente a los
  10–12 FPS anteriores. Es otra mejora parcial; todavía no se alcanzan
  20 FPS estables. Registro: `build/PS2/pcsx2-compact-textures-runtime.log`.

## Franjas del inventario y agrupación de geometría — 2026-10-02

- El usuario confirma que el audio está corregido y que, con la nueva
  agrupación de franjas, «El inventario ahora va bien». El exterior sigue
  a 8–10 FPS y todavía requiere optimización. Primero se estabilizará la
  versión en PCSX2; las pruebas en PS2 real serán una fase posterior.
- PreRender deja de limitar sus cargas a los 4 KiB de TMEM de N64 en PS2:
  el traductor nativo lee la RAM fuente directamente. Usa franjas de hasta
  32 filas, acotadas por el búfer de conversión de 256 KiB. Una copia de
  320x240 pasa de 40 a 8 franjas; la composición con cobertura, de 60 a 8.
  En CT32, gsKit reserva 40 KiB tanto para una franja de 320x6 como para
  una de 320x32: el fondo pasa de 1600 KiB a 320 KiB. La cobertura utiliza
  otra dirección TMEM lógica para mantener separados ambos orígenes.
- TEXRECT descuenta el origen de LOADTILE antes de dibujar la subregión,
  como ya hacían los vértices 3D. Conserva las coordenadas del dibujo y
  evita muestrear fuera de una franja cuya fuente ya está desplazada.
- Prueba con las listas generadas por PreRender de producción: copia,
  composición con cobertura, tinte y copia de región cubren las 240 filas
  con ocho rectángulos. El primer dibujo carga ocho texturas; el siguiente
  carga cero, incluso después de introducir 64 iconos adicionales. Cambiar
  un píxel recarga únicamente una franja. Diagnósticos completos pasan
  120 fotogramas: `build/PS2/pcsx2-prerender-pass.log` y
  `build/PS2/pcsx2-indexed-cache-pass.log`.
- Las búsquedas de caché se indexan en 64 grupos por dirección de origen,
  conservando las comprobaciones de contenido, paleta y material. Los
  reemplazos desvinculan las entradas expulsadas antes de reutilizarlas.
  Las pruebas de colisiones, cambios de RAM, reemplazos y cercas GS pasan.
  El usuario no percibe una mejora adicional de FPS en el exterior con
  este índice; no se da por resuelto ese cuello de botella.
- Los comandos consecutivos TRI1/TRI2/QUAD comparten paquetes de hasta
  20 triángulos. Se vacían antes de cualquier cambio de estado, carga de
  vértices, salto o fin de lista; los materiales de dos pasadas del suelo
  siguen dibujándose por separado. Pruebas de comandos mezclados, límites
  de capacidad, polígonos descartados y cambios de estado pasan. Una
  comparación de 200 triángulos reduce 100 paquetes a 10 y pasa de 450379
  a 418223 ciclos EE (alrededor del 7 % en esa prueba aislada). El usuario
  sigue observando 8–10 FPS en el exterior: el ahorro no resuelve el lag.
  Diagnóstico: `build/PS2/pcsx2-triangle-runs-pass.log`.
- El perfil detallado separa matrices, transformación, luces, generación
  de coordenadas, recorte y preparación de triángulos. En una vista quieta
  del exterior, 3555 vértices requieren unos 2–3 ms de transformación y
  2–3 ms de luces; las matrices y el recorte rondan 1 ms cada uno. La
  preparación de triángulos consume 22–40 ms, incluidas búsquedas y
  conversiones de texturas. Las interrupciones pueden aumentar cualquier
  muestra; no deben sumarse tiempos de fases incluidas unas dentro de otras.
  Registro: `build/PS2/pcsx2-geometry-profile-runtime.log`.
- La conversión RGBA16 combina la expansión 5-5-5-1 y la tabla del material
  en una sola pasada, usando canales empaquetados; los caminos que combinan
  dos texturas conservan su cálculo anterior de alfa. Las escalas recíprocas
  de las dimensiones GS se preparan al cargar o recuperar una textura y los
  desplazamientos de coordenadas usan potencias de dos exactas. No se cambia
  la resolución ni el formato de color. Pruebas exhaustivas de 65536 entradas
  para copia, tinte y dos ciclos pasan, al igual que 512 combinaciones de
  desplazamientos y perspectiva con coordenadas negativas y orígenes de tile.
  El diagnóstico completo pasa 120 fotogramas, incluyendo la prueba del
  audio corregido: `build/PS2/pcsx2-fused-textures-pass.log`.
  Juego reconstruido con 718 objetos y abierto para medir FPS reales;
  respaldo `build/PS2-ntsc12/oot_ps2-before-fused-textures.elf`.
  Tras probarla, el usuario confirma «12 fps ahora 10-12», frente a los
  8–10 FPS anteriores. Es una mejora parcial; todavía no se alcanzan
  20 FPS estables. Registro: `build/PS2/pcsx2-fused-textures-runtime.log`.
  Se retira la activación privada del perfil para que los próximos arranques
  normales no realicen mediciones por comando.

## Perfil de renderizado, texturas y voz — 2026-10-02

- El arranque y los menús limitan ahora la admisión gráfica a 30 Hz como
  máximo (dos retraces). La partida conserva su updateRate=3, equivalente
  a un máximo de 20 Hz. VPS=60 del emulador mide retraces y no es la tasa
  de fotogramas del juego. El planificador de producción pasa los casos
  updateRate=0/1/2/3, prioridades, DP ocupado y vuelta del contador.
- Perfil optativo mediante `host:ps2-gfx-profile.enable`, muestreado cada
  60 fotogramas con COP0 Count. Separa vértices, triángulos, rectángulos,
  búsquedas de caché, conversión y carga GS. Los tiempos incluyen interrupciones
  de otros hilos; las fases de textura están incluidas en tri/rect. Sin el
  archivo privado no se lee el contador en cada comando.
- Cuello de botella observado: en el exterior una muestra emplea 55 ms
  en convertir texturas dentro de 93 ms de traducción gráfica; el inventario
  recarga 85–122 texturas por fotograma y consume 123–244 ms. La actualización
  de la partida es mucho menor que su espera por el renderizado. Registros
  previos: `build/PS2/pcsx2-before-lut-runtime.log`.
- Las tablas del combinador compilan los selectores una vez y evalúan sus
  referencias simplificadas. Caché ampliada de 16 a 64 materiales. Prueba
  diferencial contra las ecuaciones anteriores: 31 materiales, tres tipos
  de ciclo, todos los valores de cada canal, incluidos alfa y transparencias.
  Usuario confirma cierta mejora, pero sigue observando 12 FPS o menos y
  lag al abrir el inventario: el objetivo de rendimiento aún no está logrado.
- Segunda optimización: XXH32 por bloques reemplaza la cadena FNV por byte
  al comprobar el contenido de texturas. Se siguen leyendo todos los bytes,
  incluso filas no contiguas y el segundo material de los caminos. Vectores
  de libxxhash, 16 alineaciones y cambios en cada byte pasan en EE; 2000
  comparaciones independientes pasan en el host. No se omiten comprobaciones
  por fotograma que puedan recuperar el parpadeo del suelo.
- Una lectura inválida de vértices al abrir el inventario producía TLB misses
  en f3dex2_vtx. Se descarta la carga fuera de RAM EE y se invalidan los
  vértices afectados; una recarga válida posterior funciona. El origen de
  ese puntero corrupto todavía requiere diagnóstico.
- La captura privada acepta `voice` en `host:ps2-audio-capture.enable` y
  empieza con la primera petición del banco de voz. Se capturó la voz infantil
  0x6820 antes de la salida: el usuario confirma que también se escucha mal.
  Esto sitúa ese defecto antes del controlador IOP, aunque no excluye otros
  fallos de reproducción. Se añaden parámetros de las notas durante la captura.
- Corregido HILOGAIN: el juego define su argumento como UQ4.4 sin signo.
  El intérprete lo trataba como int8 y convertía ganancias >=0x80 en negativas.
  Pruebas de ambas polaridades y saturación para 0x10/0x7f/0x80/0xff pasan.
  No se da por solucionada la voz hasta confirmar su comportamiento real.
- Usuario confirma que la ganancia no cambia el corte de la voz ni elimina
  los golpes durante el inventario; la segunda optimización no mejora su
  percepción de FPS. El perfil confirma que el menú sigue recargando unas
  134–137 texturas por fotograma: falta reducir esa carga.
- Causa concreta encontrada en Audio_ProcessNotes: una comprobación N64
  rechazaba parentLayer por estar por debajo de 0x7fffffff. Todas las capas
  válidas de EE viven por debajo de 32 MiB, por lo que las notas activas no
  actualizaban inicialización, tono y volumen. En PS2 se valida el rango de
  RAM nativa; N64 conserva su comprobación. Se añade una prueba de producción
  con capa EE válida que verifica estado inicial, tono, volumen y silencio
  tras mutear en el siguiente tick.
  La prueba pasa (tono 0xa000, volumen 723/723 en la primera nota) y el
  diagnóstico completo pasa 120 fotogramas sin fallos en
  `build/PS2/pcsx2-live-notes-pass.log`. Versión reconstruida y abierta;
  respaldo `build/PS2-ntsc12/oot_ps2-before-live-notes.elf`.
  El usuario confirma: «el audio ya esta corregido» con esta última versión.
  El registro de la partida ya muestra notas inicializándose durante la
  captura (antes no aparecían): `build/PS2/pcsx2-live-notes-runtime.log`.
  Los archivos de activación privada se retiran tras capturar; en los
  siguientes arranques no se activa el perfil ni la grabación de sonido.
- Pruebas nuevas de ADPCM con predictor no nulo: decodificación entera frente
  a bloques separados, historial y A_LOOP. Diagnóstico completo pasa 120
  fotogramas y cero fallos: `build/PS2/pcsx2-hash-gain-pass.log`.
  Juego construido con 718 objetos y abierto con la memory card existente.
  Respaldo anterior: `build/PS2-ntsc12/oot_ps2-before-hash-gain.elf`.

## Sincronización de fotogramas GS — 2026-10-02

- Usuario confirma que ya oye algunos efectos antes ausentes; la voz de
  Link se corta mientras la música continúa. El audio no se da por corregido.
- Solo en PS2, el planificador deja de esperar por los buffers ficticios de
  N64 después del aviso DP_DONE: el hilo gráfico ya terminó la presentación
  del GS. La admisión de nuevas tareas respeta updateRate desde su inicio
  anterior (20 Hz con updateRate=3, 30 Hz con 2, 60 Hz con 1), sin acumular recuperaciones tras
  un fotograma lento. Se conservan la exclusión DP y prioridad de audio,
  además de la actualización lógica VI y del framebuffer de fallos.
- Prueba del planificador de producción pasa límites de 20/30 Hz,
  fotogramas tardíos, prioridad de audio, DP ocupado, tareas de sincronización
  y vuelta del contador. Diagnóstico completo: 120 fotogramas y cero fallos,
  `build/PS2/pcsx2-sched-pass.log`. Build del juego: 716 objetos.
- Comparación de la secuencia del título alrededor del fotograma 600,
  con las mismas cuentas de geometría (1608 triángulos, 691 dibujos): el
  período CPU pasa de 61,4 ms a 50,6 ms, cerca del límite de 20 FPS.
  Traducción gráfica permanece en unos 31 ms. La partida exterior (escena
  85) todavía tiene muestras de 66–102 ms por fotograma, según cámara y
  geometría: no se ha alcanzado una tasa estable de 20 FPS fuera de la casa.
  Este cambio no corrige la voz entrecortada.
- Versión abierta con ventana visible y memory card existente. Respaldo
  del ELF anterior: `build/PS2-ntsc12/oot_ps2-before-gs-pacing.elf`.

## Secuencias de sonido y caché de lecturas — 2026-10-02

- Usuario confirma que el audio interno capturado se escucha limpio, pero
  los chasquidos aumentan con el lag de salida y no oye las voces de Link.
  El exterior aún baja a 8–10 FPS; el objetivo de rendimiento no está logrado.
- Corregidos dos operandos de tablas de secuencias que se leían como u16
  nativos aunque los scripts permanecen big-endian. Las envolventes ENV
  dentro de secuencias también conservaban ese orden: ADSR ahora identifica
  su origen al inicializarse y lee sus puntos correctamente. Las fuentes
  de sonido ya convertidas y las envolventes compiladas conservan su lectura
  nativa. El estado ADSR mantiene su tamaño/layout. Pendiente confirmar
  jugando el efecto sobre las voces y el sonido; no se dan por solucionados.
- Caché LRU de 64 KiB para pequeñas lecturas repetidas de ROM, compartida
  bajo el mutex existente. Las cargas grandes no expulsan sectores de audio.
  El writeback de la lectura limita su rango usando SyncDCache del SDK.
  Se añade medición de actualización, espera, audio y lecturas/aciertos.
- Pruebas EE: envolvente BE sin alineación, alias de secuencia, salto ADSR,
  comparación con fuente nativa y reinicialización; lectura entre sectores,
  acierto sin E/S, expulsión, final parcial, fuera de rango, reapertura y
  lectores concurrentes. Diagnóstico completo pasa 120 fotogramas, cero
  fallos: `build/PS2/pcsx2-rom-cache-envelope-pass.log`.

## Cola PCM y esperas de texturas — 2026-10-01

- Usuario confirma corregido el parpadeo del suelo; sombras y escalones
  mejoraron. El módulo libsd del SDK redujo los chasquidos, pero el audio
  sigue defectuoso y el exterior aún puede caer a 10 FPS.
- El registro anterior mide traducción de gráficos de 55–77 ms en bloques
  de juego, con 7–19 recargas/esperas de texturas por fotograma muestreado.
  Hubo 15 bloques PCM descartados en unos siete minutos. Estos tiempos
  incluyen interrupciones de otros hilos; no son una medida aislada del GS.
- La caché recuerda qué accesos quedaron retirados por una barrera FINISH.
  Sobrescribir VRAM con lecturas pendientes conserva la espera; otras
  texturas retiradas por esa misma barrera evitan esperas duplicadas.
  Cada nueva vinculación de una textura vuelve a marcarla como pendiente,
  también si su descriptor seguía cargado. Se conserva Z24, alfa y espejo.
- Se cachea la decodificación de combinadores por sus palabras y modos,
  conservando las ecuaciones, colores y factores de cada material.
- TRI2 agrupa sus triángulos supervivientes con el mismo material en un
  paquete GS; no agrupa el suelo de dos pasadas ni cruza comandos. Pruebas
  pasan descarte parcial/total, siguiente TRI1 y orden de las dos pasadas.
  En una muestra exterior, 1325 dibujos usaron 887 paquetes y 17 subidas
  tuvieron una espera. El contador seguía en 10 FPS: menos paquetes no
  equivale a demostrar una mejora suficiente en la tasa de fotogramas.
- audsrv del SDK podía avanzar más allá de las muestras disponibles y
  reproducir datos antiguos, confundiendo cola vacía y llena. El proyecto
  compila una copia local del servidor IOP con ocupación explícita,
  acceso serializado, lectura circular y relleno de silencio al faltar PCM.
  Los cambios de formato reinician la cola; se consume al llegar la señal
  de transferencia SPU2. El SDK instalado permanece sin modificaciones.
  Fuentes y licencias: `src/platform/ps2/iop/audsrv/UPSTREAM.md`.
- Pruebas EE pasan cola llena, vuelta circular, veinte agotamientos,
  recuperación y reinicio, además de retiro compartido y reutilización
  posterior de texturas. Renderer/audio completo pasa 120 fotogramas:
  `build/PS2/pcsx2-pcm-ring-fence-pass.log`. Usuario confirma chasquidos
  iguales tras corregir la cola. En unos cuatro minutos no hubo descartes,
  pero aparecieron después; partidas distintas impiden comparar la tasa.
- Captura privada opcional acepta un retraso en bloques PCM; la muestra de
  partida de dos segundos tiene pico 7223/32768, cero saturaciones y salto
  máximo 3425. Usuario la escucha limpia. No descarta fallos de síntesis
  en otros momentos. Bandera de captura retirada tras la prueba.

## Grabación: sombras, repetición en espejo y salida de audio — 2026-10-01

- Revisada grabación local de 48,42 segundos: selección de partida, casa
  de Link y salida al bosque. El audio de Windows contiene saltos grandes
  y picos de escala completa. La captura nativa previa de dos segundos del
  título alcanza solo 570/32768 y no satura; son momentos diferentes, por
  lo que esa comparación todavía no demuestra dónde se genera el ruido.
- El renderer ignoraba `G_TX_MIRROR`, presente en materiales del bosque.
  Se cachea un período normal y otro invertido por eje solicitado, con
  coordenadas continuas y repetición GS sobre ambos períodos. Las variantes
  de espejo tienen claves independientes; se conserva alfa y no se genera
  la imagen ampliada en los aciertos de caché. Texturas demasiado grandes
  mantienen la aproximación anterior para respetar el buffer disponible.
- Sombras usan `SETUPDL_44` / `G_RM_ZB_OVL_SURF2` (`ZMODE_DEC`). Se añade
  sesgo de profundidad proporcional a la pendiente únicamente a decals
  con comparación y sin escritura Z. Se limita y satura en Z16/Z24; la
  geometría ordinaria conserva sus profundidades. Es una aproximación a
  la tolerancia delta-Z de N64; el usuario confirma mejoría jugando.
- Colores y combinadores repetidos sin cambios conservan las texturas
  cargadas, evitando hashes y búsquedas adicionales. Los cambios reales
  siguen invalidando; no se modifica la velocidad de simulación.
- Navi usa verde al señalar objetos de categorías PROP/BG/etc., según
  `sAttentionColors` y `Attention_SetNaviState` en `src/code/z_actor.c`.
  La grabación muestra la acción `Check` junto al cuadro de la casa.
  Ese cambio de color es coherente con el comportamiento original.
- Se incorpora el módulo `freesd.irx` / libsd del SDK antes de audsrv,
  conservando alternativa ROM si falla. Prueba de compatibilidad de salida,
  el usuario confirma después menos chasquidos. El build enlaza 716 objetos.
- Diagnóstico EE completo pasa 120 fotogramas; pruebas de espejo S/T,
  píxeles alfa, variantes de caché, coordenadas, pendientes de decals,
  límites y oclusión dan cero fallos. Registro:
  `build/PS2/pcsx2-shadow-mirror-pass.log`. Usuario confirma mejoría de
  sombras/escalones y reducción del ruido, todavía sin solución completa.

## Diagnóstico de chasquidos — 2026-10-01

- Usuario confirma que el árbol ya se oculta correctamente, pero los
  chasquidos siguen iguales también con salida continua a 48 kHz.
- Captura privada opcional del PCM estéreo de síntesis antes de enviarlo
  a audsrv. Crear `ps2-audio-capture.enable` junto al ELF activa una captura
  de 256000 bytes tras 900 bloques enviados. A 32 kHz son dos segundos.
  Se acumula en RAM y escribe una sola vez `ps2-audio-native.raw`; esa
  escritura puede detener brevemente el fotograma diagnóstico.
  Sin el archivo de activación no se reserva ni copia PCM.
- Compilación completa y diagnóstico EE pasan, incluyendo las pruebas de
  audio y 120 fotogramas: `build/PS2/pcsx2-pcm-capture-pass.log`.
  La captura permite separar defectos en síntesis de fallos de reproducción;
  aún no se considera resuelto el ruido.

## Recursos vecinos del bosque inicial — 2026-10-01

- El registro capturó un fallo en `Room_SetupFirstRoom` al entrar en
  `spot10` (Bosques Perdidos, entrada 011e). La escena no estaba convertida
  y su lista de habitaciones se leía como punteros nativos inválidos.
- Perfil `play` añade `spot10` con sus diez habitaciones, casas de los
  Kokiri y tienda; se extraen también sus dependencias de objetos.
  La imagen nueva contiene 139 segmentos nativos. Se verifican sus listas
  de habitaciones y rangos ROM, checksum y conservación byte por byte
  de los segmentos anteriormente adaptados. Falta recorrer esas escenas.

## Salida PCM continua a 48 kHz — 2026-10-01

- Usuario confirma que el FIR anterior no eliminó el ruido/chasquidos.
  No se considera resuelto el audio por el resultado de la prueba FIR.
- La síntesis conserva 32 kHz; la salida EE interpola PCM estéreo a 48 kHz
  manteniendo fase y muestra anterior entre bloques. audsrv recibe su formato
  nativo, evitando su conversión de 341 a 512 muestras repetidas por bloque.
  Referencia: [upsamplers audsrv](https://github.com/ps2dev/ps2sdk/blob/master/iop/sound/audsrv/src/upsamplers.c).
- El feedback AI convierte la cola IOP a bytes de la frecuencia del juego
  antes de restar su reserva. La capacidad se comprueba con el tamaño de
  salida completo antes de modificar la fase; se preservan ambos canales.
- Prueba EE verifica interpolación de una rampa estéreo, salida continua
  frente a corte tras 11 muestras, rechazo sin avanzar estado, 48 kHz
  directo y feedback AI. Diagnóstico completo pasa 120 fotogramas:
  `build/PS2/pcsx2-output-resample-pass.log`. El usuario confirma después
  que los chasquidos persisten: la conversión no resolvió el fallo audible.

## Oclusión tras ampliar VRAM y filtro de audio — 2026-10-01

- Corregida regresión: `gsKit_init_screen` cambia CT16/Z24 a CT16/Z16.
  El renderer seguía enviando Z de 24 bits y el truncamiento rompía el orden
  de profundidad. Se usa CT16S/Z24 para conservar RGB5551, precisión y caché
  de 1792 KiB. Se registra el formato efectivo tras inicializar la pantalla.
  Referencia: [gsKit init](https://github.com/ps2dev/gsKit/blob/master/ee/gs/src/gsInit.c).
- La conversión de profundidad consulta el formato efectivo, limita sus
  extremos y soporta también Z16 sin desbordamientos. Prueba EE recorre
  65537 profundidades en cuatro formatos y exige orden monótono y límites.
- El filtro FIR importado indexaba directamente pares de muestras y
  coeficientes con el orden de palabras de memoria emulada, aunque los
  buffers EE son consecutivos. La ruta PS2 usa muestras n-k y conserva
  los ocho samples de entrada anteriores. El impulso de dos bloques falla
  en 9 muestras antes del cambio y pasa con cero diferencias después.
- Diagnóstico EE completo pasa 120 fotogramas con color=0a, depth=01
  (CT16S/Z24), recortes, caché y audio. Registro:
  `build/PS2/pcsx2-depth-filter-pass.log`.
- La versión CT16 anterior redujo recargas en el fotograma 600 del título
  de 102 a 1; la media de traducción del bloque pasó de 54003 a 29027 us.
  Es una comparación del título, no una medición del exterior ni una
  confirmación de calidad audible. Oclusión confirmada por el usuario;
  falta validar los caminos jugando.

## VRAM y retención de texturas animadas — 2026-10-01

- Color GS pasa de CT24 (almacenado en palabras de 32 bits) a RGB5551 CT16,
  el formato del framebuffer original. Se conservan salida 640×448 y se solicita Z24;
  la regresión del formato efectivo se corrige en la entrada anterior. La reserva crece de
  704 a 1792 KiB, limitada al espacio realmente disponible menos 32 KiB.
  Referencia de tamaños: [gsKit](https://github.com/ps2dev/gsKit/blob/master/ee/gs/src/gsTexture.c).
- Una versión anterior de la misma fuente/material se reutiliza al cambiar
  sus bytes, paleta o factores constantes en otro fotograma. Las variantes
  utilizadas juntas se conservan; antes se acumulaban versiones históricas
  que expulsaban texturas estables. Se mantiene FINISH antes de sobrescribir.
- Prueba EE con 320 cambios de color conserva 32 texturas estables, la VRAM
  de la textura cambiante y dos colores simultáneos de la misma fuente.
  Pasan también recortes combinados, caché, audio y 120 fotogramas en CT16.
  Registro `build/PS2/pcsx2-cache-reuse-ct16-pass.log`, reserva real 1835008 bytes.
  La variante CT24 también pasa con 720896 bytes en
  `build/PS2/pcsx2-cache-reuse-ct24-pass.log`.
- Añadida prueba de remuestreo: una nota a pitch 0,75 dividida en dos bloques
  produce exactamente los mismos samples que un bloque continuo, conservando
  historia FIR y fase. No confirma todavía fidelidad musical del juego.
- Compilación completa de 715 objetos en `oot_ps2-next.elf`. Pendiente comparar
  la nueva versión en el exterior con una misma cámara y verificar los caminos.

## Materiales recortados de dos texturas, estéreo y medición — 2026-10-01

- Usuario confirma solucionados los huecos transparentes de la escalera.
  Continúan pendientes parpadeos en caminos exteriores, rendimiento y audio
  distorsionado; no se consideran resueltos por esa confirmación.
- Añadida combinación RGBA previa al recorte para el material de dos tiles
  RGBA16 con igual mapa usado en `spot04`. La clave de caché comprueba ambas
  fuentes, sus bytes, strides y PRIM_LOD_FRAC. Conserva la ruta normal cuando
  los tiles tienen mapas distintos. Pruebas EE pasan alfa cero/completo,
  mezcla parcial, mutación de la segunda fuente y retención de la caché.
- Retiradas dos consultas de SHADE por vértice y paquetes CLAMP redundantes
  cuando el valor/contexto no cambia. Medición de traducción y flip cada
  300 tareas para distinguir coste de CPU y espera de presentación.
- `StereoData` conserva en EE los bits originales de los bytes de secuencia
  y comandos; antes sus campos se interpretaban en orden inverso. La prueba
  verifica cada bandera de canal fuerte, auriculares y política estéreo.
  Esto corrige la interpretación; aún falta confirmar su impacto audible.
- Diagnóstico renderer/audio pasa 120 fotogramas y el puente de tareas pasa
  64 entregas con audio antes de DP. Registros:
  `build/PS2/pcsx2-path-stereo-pass.log`, `build/PS2/pcsx2-task-profile-pass.log`.
  El registro anterior produjo 370,136 s de PCM en 370,508 s, sin descartes;
  esa continuidad no confirma calidad musical. Se mantiene la temporización
  original: durante Play, R_UPDATE_RATE=3 (aproximadamente 20 FPS NTSC).

## Mezcla de suelo y reserva de audio — 2026-10-01

- El material del suelo de `spot04` mezcla TEXEL0 y TEXEL1 mediante
  ENV_ALPHA y después aplica SHADE. El renderer antes sustituía TEXEL1
  por TEXEL0. Ahora reconoce ese combinador opaco y dibuja dos pasadas GS,
  cada una con su tile, coordenadas, profundidad y niebla. Otros combinadores
  de dos texturas siguen pendientes. Cambiar ENV_ALPHA conserva ambas
  texturas en caché; con alfa cero solo dibuja la primera pasada.
- La longitud AI comunicada al motor excluye 20 ms de reserva de audsrv.
  Antes incluía toda la cola IOP, lo que mantenía al sintetizador en bloques
  mínimos. La prueba enlaza el driver real con un servidor simulado y verifica
  32/48 kHz, reserva, errores y rechazo de bloques que no caben completos.
- Pruebas EE nativas pasan mezcla de dos texturas, retención de caché al
  cambiar el factor, exclusión de materiales recortados y driver de audio.
  Compilación completa: 715 objetos. Registro:
  `build/PS2/pcsx2-ground-audio-pass.log`. La verificación visual del suelo
  y la continuidad del audio en el juego siguen pendientes.

## Recortes de alfa y coste de la caché — 2026-10-01

- El test GS ahora reconoce CVG_X_ALPHA usado por materiales TEX_EDGE,
  aunque G_AC_THRESHOLD esté desactivado. Aproxima cobertura parcial con
  umbral de medio alfa y descarta también color/profundidad en los huecos.
  Para el test explícito utiliza el alfa del blend color y actualiza su
  umbral al cambiarlo. Pruebas EE pasan modo de escalera, superficie opaca,
  umbral variable y conservación del test/escritura de profundidad.
- La elección de espacio VRAM usa edades por bloque y máximo deslizante,
  conservando el criterio anterior de expulsión con una búsqueda lineal.
  Evita esperar FINISH en regiones sin usos previos; mantiene la espera
  al sobrescribir una región o eliminar un descriptor válido. Pasan las
  pruebas de retención de 32 tiras, 128 glifos, textura grande y expulsión.
- Añadidos contadores por fotograma de aciertos, cargas y esperas para medir
  la presión de la caché. Aún falta verificar visualmente la escalera y
  comparar rendimiento en una misma cámara del juego.

## Audio independiente del renderer y fondos JPEG — 2026-10-01

- Conectada niebla nativa GS a triángulos con y sin textura, usando FGE,
  XYZF2 y FOGCOL. Antes solo existía un cálculo genérico sin conexión al
  dibujo. Ahora interpreta los parámetros reales G_MW_FOG sobre Z/W,
  invierte el factor para GS y limita la activación a la etapa estándar
  FOG_SHADE_A de dos ciclos. Conserva la profundidad de 24 bits y restaura
  el estado de niebla antes de dibujar fondos/textos. Pruebas EE pasan
  inicio, mitad, saturación, rango estrecho y desactivación por material.
  Referencia de emisión STQ: [gsKit](https://github.com/ps2dev/gsKit/blob/master/ee/gs/src/gsTexture.c).
- La traducción de gráficos pasa del scheduler a un hilo EE de prioridad
  libultra 10, inferior a Graph (11) y AudioMgr (12). Entrega SP al despachar
  y conserva DP ocupado hasta completar el dibujo y el cambio de pantalla.
  Retiradas las alarmas cada 128 comandos. La prueba nativa pasa 64 tareas
  lentas, con audio antes de DP y orden correcto de los avisos.
- Corregida durante las pruebas la prioridad inicial 13 del nuevo hilo:
  impedía atender AudioMgr a tiempo. Con prioridad 10, el juego envía
  14,939 segundos de PCM en 15 segundos, sin descartes ni intervalos mayores
  que dos bloques. El intento anterior enviaba 9,920 segundos en 68,251
  segundos, con 577 intervalos tardíos y 148 descartes. Las métricas miden
  entrega a audsrv; no confirman todavía fidelidad musical ni FPS sostenidos.
- Decodificador nativo con libjpeg ya instalado en el SDK: dimensiones y
  capacidad limitadas a 320×240, salida RGBA5551 big-endian y copia únicamente
  después de terminar. Entrada dañada conserva el JPEG original. Las pruebas
  EE pasan colores conocidos, repetición, dimensiones y corrupción.
- Reconocimiento de cambios F3DEX2/S2DEX y rectángulos de fondo COPY/1CYCLE.
  Fondos RGBA16 sin giro ni origen desplazado se dibujan en tiras de hasta
  32 filas. La prueba verifica ocho tiras para 320×240 y sus píxeles, incluso
  después de cargar otras texturas. Otros formatos/orígenes siguen pendientes.
- Diagnóstico acotado de cámara y fondo en habitaciones de imagen única.
  Los recursos de la casa contienen polígonos verdes sin textura; aún falta
  comprobar en la cinemática cuándo deben cubrirlos el fondo y los fundidos.
- El juego decodifica el JPEG real 320×240 de la casa y registra fondos
  S2DEX dibujados. PCSX2 indica 20 FPS en la cinemática observada. Una prueba
  larga continúa produciendo PCM cerca de tiempo real, aunque registra
  algunos descartes adicionales; no confirma audio perfecto ni un benchmark
  comparable de todas las escenas.
- Compilación completa: 715 objetos. Registros locales de pruebas:
  `build/PS2/pcsx2-jpeg-background-pass.log`,
  `build/PS2/pcsx2-task-priority-pass.log` y
  `build/PS2-ntsc12/pcsx2-game.log`.

## Letras del nombre, caché del teclado y memory card — 2026-10-01

- `Font_LoadOrderedFont` interpreta las mitades big-endian del mensaje que
  ordena las letras NTSC. Antes se intercambiaban los bytes de los códigos
  SHIFT-JIS y se cargaban otros glifos. PS2 también limita longitud y destino.
  La prueba EE enlaza el cargador real, comprueba letras latinas, salto de línea
  y un mensaje sin terminador, sin leer fuera de su longitud.
- Caché ampliada de 64 a 256 entradas, con bloques de 1 KiB en los mismos
  704 KiB de VRAM. El teclado provocaba conversiones repetidas al superar
  64 combinaciones de letra/color. La prueba conserva 128 glifos entre pasadas,
  además de tiras medianas, texturas grandes, expulsión y colores del combinador.
  Esto evita trabajo repetido; aún falta un benchmark comparable de FPS.
- Fundidos `TEXEL_ALPHA * PRIMITIVE_ALPHA` compatibles pasan el factor variable
  al vértice GS. Las claves de textura/LUT excluyen ese factor. La prueba cambia
  el alfa sin recargar VRAM y rechaza los estados que necesitan la ruta CPU.
  RGBA32, IA8 e IA16 convierten alfa 255 a 128, evitando el máximo 127 anterior.
- `libmc` recibe flags IOP directamente: sustituidos los flags newlib por
  `FIO_O_*`. La lectura antes devolvía -5. Verificado contra el código de
  [PS2SDK](https://github.com/ps2dev/ps2sdk/blob/master/ee/rpc/memorycard/src/libmc.c).
- Guardado alterno `save.dat`/`save.bak`, revisión y checksum, comprobación del
  cierre y relectura antes de declarar éxito. Lee también el formato anterior.
  Un error conserva la copia previa y permite reintentar la inicialización.
- Diagnóstico con MCMAN/MCSERV y una tarjeta virtual nueva: pasan dos escrituras,
  selección de la revisión reciente, daño de la copia nueva y recuperación
  de la anterior. Tras cerrar y reiniciar PCSX2 pasan los 32768 bytes persistidos.
  La prueba usa `OOT_PS2_TEST`, separada del guardado del juego `OOT_PS2`.
- Activada la nueva `oot-port.ps2` en la configuración aislada; la tarjeta que
  estaba configurada se conserva. El juego registra guardados verificados hasta
  la revisión 5 y entra en la escena 52. Esto no valida una partida completa.
- Logs privados: `build/PS2/pcsx2-smoke-glyph-cache.log`,
  `build/PS2/pcsx2-memorycard-write-recovery.log`,
  `build/PS2/pcsx2-memorycard-cold-restart.log` y
  `build/PS2-ntsc12/pcsx2-font-card-first-game.log`.

## Caché, reflejos y pruebas del selector — 2026-10-01

La captura del título muestra a Link montado en Epona, terreno, murallas y
el logo Zelda con sus colores. Start abre el selector y se ven sus textos,
botones y fondo. **Aún no se ha validado una partida jugable completa.**

- Caché de texturas con 64 entradas y asignación según tamaño sobre los mismos
  704 KiB de VRAM. Retira los dibujos antes de sobreescribir regiones y anula
  todos los descriptores que se solapan. El diagnóstico comprueba retención
  de 32 texturas medianas y carga de una textura de 256×256 dentro del límite.
- Caché de tablas del combinador: reutiliza sus 512 evaluaciones por estado.
  Las claves incluyen solo componentes prim/env que el combinador utiliza.
  Pruebas de cambios de color, expulsión y colores irrelevantes en ciclo COPY.
- Hash de los bytes de fuente y paleta conserva la caché entre fotogramas
  sin dejar obsoletas las texturas modificadas en la misma dirección RAM.
- Coordenadas esféricas y lineales de reflexión desde la normal transformada
  por modelview y los dos vectores LookAt de MOVEMEM. Pruebas con identidad,
  giro, modo lineal y un vector LookAt personalizado.
- El renderer cede brevemente el hilo cada 128 comandos para permitir muestreo
  del mando durante fotogramas lentos. Start se ha comprobado en el emulador.
- Perfil `play`: 104 segmentos, incluidas 573 tablas de animación del jugador
  en orden nativo y recursos iniciales. Se conservan JPEG comprimidos intactos;
  su decodificación y dibujo de fondos prerenderizados siguen pendientes.
- La prueba prolongada anterior agotaba la tabla lateral de colas al cambiar
  de escena. Al liberar una asignación o destruir una arena PS2 se eliminan
  sus semáforos. El diagnóstico enlaza el asignador real y pasa 768 ciclos,
  limpieza de arenas y preservación de una cola ajena.
- Logs privados: `build/PS2/pcsx2-smoke-cache-final.log` y
  `build/PS2-ntsc12/pcsx2-lut-filechoose.log`. En la versión intermedia se
  observaron 4–10 FPS en el título y 20 en el selector; no son un benchmark
  definitivo ni una validación de rendimiento en hardware PS2.

## Matriz del escenario, texturas y SRAM — 2026-09-30

El juego enlaza con 713 objetos. Las capturas posteriores a corregir la matriz
identidad muestran terreno, luna, murallas y el título Zelda con sus colores.
**Todavía no es jugable:** siguen pendientes fidelidad visual, rendimiento y
validación de la partida. La escena ronda 4 FPS.

- `gIdentityMtx` usaba `gdSPDefMtx`, que empaqueta los componentes en palabras
  big-endian. En el EE intercambiaba mitades contiguas, incluida la componente
  homogénea: muchos vértices del escenario quedaban detrás de la cámara. PS2
  inicializa la constante con mitades nativas. La prueba de matemáticas enlaza
  `sys_matrix.c` y comprueba la constante real, además de los conversores.
- Paletas CI4 cargadas en su posición TMEM, preservando otras paletas; se
  invalidan los descriptores CI afectados. Añadido el decoder IA4 de tres bits
  de intensidad y uno de alfa. Ambos pasan pruebas de píxeles en el EE.
- Coordenadas de triángulos enviadas mediante STQ, evitando el clamp de UV
  de los helpers de gsKit. Se conservan coordenadas negativas, repetición,
  shifts y perspectiva cuando el RDP la activa. Clamp regional excluye el
  relleno de las tiras que no son potencia de dos. Espejo, TEXEL1 y generación
  de coordenadas de reflexión siguen pendientes.
- `SsSram_ReadWrite` conectado a un shadow SRAM de 32 KiB con semáforo y
  acceso por offset. Una escritura parcial guarda la imagen completa para
  conservar opciones y otras partidas. Una tarjeta ausente deja SRAM vacía;
  una escritura fallida conserva los cambios en RAM y registra el error.
- El backend de memory card comprueba tipo/formato, creación del directorio,
  versión y tamaño del archivo antes de calcular su checksum. No se formatea
  ninguna tarjeta. La configuración aislada actual no tiene una tarjeta PS2
  formateada; falta validar persistencia real en ella.
- Diagnóstico PCSX2: SRAM vacía, escrituras parciales, recarga, error de backend,
  límites, identidad real, IA4, paletas, STQ y pruebas anteriores pasan. Logs
  privados: `build/PS2/pcsx2-smoke-stq.log`, `build/PS2-ntsc12/pcsx2-identity.log`.

## Renderer y síntesis nativa — 2026-09-30

El diagnóstico ejecutado en PCSX2 pasa PCM, mezcla, intercalado estéreo,
ADPCM, PCM de 8 bits e historial, además de geometría y sistema. Se prueba
el asignador permanente real y la agrupación de notificaciones de refresco.
Los tres tests de bancos de instrumentos pasan: estructuras compartidas,
bitfields de Sample y rechazo de punteros/libros de predicción inválidos.
**Todavía no es un juego jugable.** Tras las correcciones, el título sigue
animándose, alcanza 600 fotogramas enviados y entra al estado del selector de
partida al pulsar Start. La captura muestra el logo Zelda, cielo y parte de
Link/Epona; faltan escenario, texturas y rendimiento (unos 4 FPS).
El audio envía buffers no silenciosos: por ejemplo, 2240 bytes aceptados por
audsrv con amplitud máxima 12288. Esto verifica generación y envío de PCM,
no la fidelidad de la música. No se registraron excepciones TLB ni watchdog
en esa ejecución; se mantienen los logs privados en `build/PS2-ntsc12`.

- Corregido el orden de argumentos de los triángulos gouraud de gsKit: los
  colores se estaban interpretando como coordenadas. La captura posterior
  permite reconocer a Epona; aún tiene colores incorrectos y falta escenario.
- Recorte homogéneo de triángulos antes de dividir por W, comprobado con
  cruce del plano cercano y geometría completamente fuera del volumen.
- Corregidos campos del blender, ciclo final, control real de escritura Z,
  coordenadas de rectángulos y relación entre descriptores de carga/render
  de texturas, incluidos sus pasos entre filas.
- Combinador CPU para constantes prim/env y modulación por vértice; tabla de
  búsqueda y caché de VRAM para reducir trabajo. TEXEL1 sigue aproximado con
  TEXEL0; no reproduce todas las combinaciones, niebla ni perspectiva.
- Corregidas notificaciones de refresco que llenaban la cola del scheduler
  durante un fotograma lento y hacían perder los avisos SP/DP. El renderer
  había terminado, pero Graph esperaba hasta el watchdog. En PS2 se conserva
  un refresco pendiente; los demás mensajes mantienen su orden y semántica.
  El watchdog sigue siendo finito (60 segundos) y registra la última etapa.
- Los rectángulos en ciclos normales usan el combinador y la transparencia
  primitiva; antes los fundidos negros se dibujaban opacos. El ciclo COPY
  evita el combinador, como en el RDP.
- Perfil de datos ampliado a escenas de demostración y sus objetos por
  dependencias explícitas; se valida que todos los segmentos se hayan extraído.
- Conversión de fuentes de sonido por sus punteros y estructuras: instrumentos,
  percusión, efectos, envolventes, muestras, bucles y predictores. Datos de
  ondas comprimidas y bytecode de secuencias permanecen intactos.
- Intérprete de síntesis NEAD/OoT adaptado al EE, con código GPL-2.0-or-later
  de Mupen64Plus y procedencia en `src/platform/ps2/audio/rsp_hle/README.md`.
  El motor del juego sigue compilado como código nativo. Corregidos el orden
  de bytes de AudioCmd y los argumentos s8/u16, y la detección de punteros
  de muestras ya relocalizadas. Se registran tareas DSP y amplitud PCM.
- Corregido el retorno ausente en `AudioHeap_AllocPermanent` para PS2: el
  compilador devolvía la dirección de gAudioCtx y el DMA de instrumentos
  sobreescribía el estado del audio. La prueba EE verifica punteros, datos
  del caché y agotamiento del pool. Convertidos `title_static` y
  `parameter_static` para seguir probando el selector de partida.

## Enlace completo y carga de escenas — 2026-09-26

El juego completo ya enlaza con 707 objetos. PCSX2 completa `Play_Init` y la
ejecución de diagnóstico alcanzó 900 fotogramas del estado `Play` en la secuencia
del título sin excepciones registradas; **todavía no se ha validado como
jugable ni se ha confirmado visualmente su imagen**.

- Generador de metadatos ampliado con direcciones reales de mensajes y recursos,
  y microcódigo original usado para identificar tareas; sin recursos ficticios.
- Corregido el arranque que recursaba dentro de `memset`, usando newlib/SDK.
  Corregida la lectura de región de la cabecera para el orden de bytes EE.
- Completados servicios de estado SP/DP, FCSR y enumeración de hilos; los
  fallos explícitos del juego se escriben en consola antes de detenerse.
- Conversor privado `tools/prepare_ps2_rom.py`: usa las estructuras descubiertas
  por el extractor upstream, incluidos recursos sin una declaración XML inicial.
  Convierte campos por tamaño, preserva texturas y blobs de bytes, y registra
  cada recurso en un manifiesto. No convierte automáticamente toda la ROM.
- Conversión de Hyrule Field, recursos comunes, Link adulto, Epona y título.
  Incluye objetos del escenario y sus colisiones; el perfil `title` selecciona
  11 segmentos. Se corrigió el acceso inválido en `DynaPoly_AddBgActorToLookup`.
  Las cinemáticas distinguen palabras, medios enteros y campos de un byte.
  `tools/check_ps2_cutscenes.py` verifica cámara, posiciones, secuencias,
  tiempo, texto, transiciones y rechazo de datos truncados.
- `osContInit` conserva los puertos ya abiertos y devuelve una máscara de bits.
  Lectura y consulta del mando notifican la finalización SI que espera PadMgr.
- `tools/run_ps2_game.ps1` inicia la prueba con configuración aislada y conserva
  el log anterior; solo reemplaza el proceso de esta prueba identificado por ELF.
- Tabla de segmentos del renderer separada de la tabla del motor: los comandos
  `G_MW_SEGMENT` escriben bases físicas del RSP sin corromper los punteros CPU.
  La prueba EE confirma ambas tablas independientes y 120 fotogramas enviados.
  También volvieron a pasar matrices, printf, colas, temporizadores y DMA
  concurrente en `build/PS2/pcsx2-smoke.log`.

Pendiente: validación visual (la captura de ventana falló por timeout), control
interactivo y selección de partida, conversión de los demás escenarios/objetos,
guardado del juego y procesamiento DSP de audio. El avance de fotogramas del
título no sustituye estas verificaciones.

## Avance con herramientas reales — 2026-09-25 (histórico)

Esta sección sustituye las conclusiones de la revisión histórica con headers
falsos que aparece más abajo. En esta fecha el juego completo aún no enlazaba;
el avance del 26 de septiembre está documentado arriba.

- Instalado ps2dev/GCC 15.2.0 en Ubuntu WSL; corregidas llamadas incompatibles
  con ps2sdk/gsKit reales, flags del compilador y script de enlace del SDK.
- Recuperados audio, libc, libc64, libu64, buffers y rutinas C de matrices y
  trigonometría del upstream fijado en `UPSTREAM.md`.
- Extraída la ROM local US NTSC 1.2. Generadas tablas DMA/audio en formato
  nativo y direcciones VROM reales, sin sustituir recursos desconocidos por cero.
- Inicialización del heap/framebuffers adaptada al espacio de direcciones EE;
  ya no se copia código N64 sobre código nativo ni se borra el BSS desde boot.
- Tablas de actores, estados y efectos usan código residente. Player, pausa
  y marcas del mapa ya no intentan cargar sus instrucciones desde la ROM N64.
  Pendiente revisar la reinicialización de datos estáticos al cambiar overlays
  y medir el consumo total de RAM con todos los recursos integrados.
- Arregladas las matrices de libultra para usar los mismos medios enteros de
  16 bits que `Matrix_MtxFToMtx` y el renderer en little-endian.
- Temporizador EE real con desbordamiento de 64 bits, alta de temporizadores
  sin exigir memoria inicializada y envío de eventos fuera de la sección crítica.
- Añadidos `_Printf` mediante newlib y restauración anidada de interrupciones.
  Separado el símbolo de assert del juego del de newlib, que tiene otra firma.
- Eliminada la mezcla de fuentes normal/Master Quest e implementaciones de
  depuración N64/iQue en el build PS2.
- Conversión de 110 includes de texturas: se invierte el valor numérico de cada
  u64 para preservar los bytes originales al compilarlos en EE. No se aplica
  esta transformación a vértices ni matrices.
- Lecturas de ROM serializadas entre hilos mediante un semáforo. Se completan
  las lecturas parciales y se rechazan accesos fuera del archivo; ya no se
  presenta un relleno de ceros como una transferencia correcta. Las lecturas
  escalares PI convierten big-endian y aceptan el alias KSEG1 del cartucho.

### Verificación

PCSX2 2.8.0, con configuración aislada y BIOS existente, ejecutó el ELF de
diagnóstico compilado con ps2dev. El log confirmó:

```text
[PS2 MATH TEST] PASS: native matrix layout and trigonometry (0)
[PS2 PRINTF TEST] PASS: game varargs and callback (0)
[PS2 OS TEST] PASS: queue ABI, FIFO/full/empty, timer, monotonic clock
[PS2 ROM TEST] PASS: file DMA, boundaries and endian (0)
[PS2 ROM THREAD TEST] PASS: two concurrent readers (0)
[PS2 SMOKE] PASS: 120 frames submitted
```

La prueba OS también comprueba restauración anidada de interrupciones.
`tools/check_ps2_texture_words.py` verificó que las 110 texturas conservan
exactamente sus 212352 bytes originales dentro de un objeto compilado para EE.
No se ha validado visualmente el render ni probado hardware físico.
La última pasada compiló 709 objetos y llegó a la fase de enlace, pero falló
con 8633 símbolos sin resolver y ninguna definición duplicada. El registro local
es `build/PS2/game-native-final.log`; la lista deduplicada está en
`build/PS2/unresolved-symbols.txt`. Compilar las unidades C no significa tener
un ELF del juego funcional.

### Trabajo pendiente

Integrar símbolos y conversión de recursos (escenas, colisiones, esqueletos,
animaciones y display lists); sustituir las dependencias de microcódigo/RCP,
fallos y JPEG de N64; implementar DSP de audio y conversión de soundfonts;
validar arranque, pantalla de título, controles y guardado con el juego real.
La ruta de lectura de ROM todavía no proporciona todos los recursos en formato
nativo. Activar globalmente una opción de endian no resuelve la mezcla de
estructuras creadas por el CPU con datos leídos de la ROM.

## Cómo se verificó esto

No basta con leer el código para saber si compila. Para este port en
concreto:

1. Se creó un conjunto de headers falsos que imitan las firmas de
   ps2sdk/gsKit (`kernel.h`, `libpad.h`, `libmc.h`, `gsKit.h`...) para
   poder pasar el compilador por la capa de PS2 sin tener el toolchain
   real instalado.
2. Se compiló en **modo 32 bits** (`-m32`), porque el EE es de 32 bits;
   en 64 bits todos los `_Static_assert` de tamaño de struct dan falsos
   negativos.
3. Se compilaron los **structs reales del juego** (`include/ultra64/*.h`
   con `-D_LANGUAGE_C=1 -DNON_MATCHING=1`) y se comparó cada firma que
   implementa la capa PS2 contra el prototipo real. Esto encontró varios
   errores propios (ver abajo) que una simple lectura no habría pillado.
4. Se compilaron los **637 archivos fuente del juego** (`src/code`,
   `src/boot`, `src/overlays`) con los flags finales del Makefile.

**Resultado de la última pasada:** 0 errores de código en los 637
archivos del juego. Los que no compilan (462) fallan únicamente por
`assets/... No such file or directory` — assets no extraídos, que es
un paso previo de las herramientas del decomp, no un bug de código.

## Build system

- **Makefile**: compilaba 145 archivos; ahora compila los 656 reales.
  `src/overlays` (511 archivos: todos los actores, `z_title.c`,
  `z_select.c`, el menú de pausa) y `src/audio` no estaban en absoluto
  en el build.
- Excluidos correctamente los 18 archivos `*.inc.c` (fragmentos que
  otros `.c` incluyen, no unidades de compilación) y `fault_n64.c`
  (choca con `fault.c` cuando `PLATFORM_N64=0`).
- Flags que faltaban y bloqueaban el build entero: `-Iinclude/libc`,
  `-DOOT_VERSION=NTSC_1_0`, `-D_LANGUAGE_C=1`, `-DNON_MATCHING=1`,
  `-DDEBUG_FEATURES=0`, `-DOOT_DEBUG=0`, `BUILD_DATE`/`BUILD_CREATOR`.
- Generación automática de dependencias (`-MMD -MP`).

## Arranque (`ps2_main.c`, `ps2_boot.c` nuevo)

- Se llamaba a `Game_Main()`, una función que no existe en ningún sitio
  del decomp. Ahora se replica la cadena real:
  `bootproc() → Idle_ThreadEntry → Main() → Graph_ThreadEntry`.
- `Sched_Init()` se llamaba con 0 argumentos; la real lleva 6
  (`Scheduler*, stack, priority, viModeType, arg4, IrqMgr*`).
- Orden de inicialización del IOP corregido: reset del IOP primero,
  luego `SIO2MAN → PADMAN`, `MCMAN → MCSERV`, `LIBSD → audsrv.irx`
  (antes se cargaba PADMAN sin SIO2MAN y se llamaba a `audsrv_init()`
  sin cargar su IRX).
- `rdp_to_gs_init()` no la llamaba nadie → puntero de GS a `NULL`.

## Stubs de libultra (`src/platform/ps2/libultra_stub/`)

- **`osMessage.c`**: `OSMesgQueue` estaba inflado (~44 bytes en vez de
  los 24 reales), corrompiendo la memoria vecina en cada
  `osCreateMesgQueue()`. Ahora el struct es del tamaño exacto y los
  semáforos van en tabla lateral.
- **`osThread.c`**: pila del hilo confundía tope con base y usaba
  tamaño fijo de 4 KB. Ahora usa pilas propias de 32 KB gestionadas
  aparte, sin tocar el struct `OSThread` del juego.
- **`osTimer.c`** (antes roto): los timers se registraban en un array
  que nadie recorría; ahora hay una lista enlazada real y
  `ps2_timer_tick()` la procesa cada frame.
- **`osTask.c`**: `osSpTaskStart` no avisaba nunca de que la tarea
  gráfica había terminado → el hilo de gráficos se quedaba colgado para
  siempre en el primer frame. Ahora dispara los eventos SP/DP.
- **`osEvent.c`, `osVi.c`, `osRom.c`, `osMisc.c`** (nuevos, no
  existían): `osSetEventMesg`/retrace vertical, lectura de la "ROM" por
  fichero vía DMA, y vibración conectada a `osMotorStart/Stop` (que en
  realidad son **macros**, no funciones — error propio corregido tras
  compilar contra el header real).
- **`osContPad.c`**: doble inicialización de puertos eliminada
  (`ps2_main.c` y este archivo abrían los mismos puertos con buffers
  DMA distintos); mapeo de botones C corregido (Circle/R3 chocaban).

### Errores propios detectados sólo al compilar contra los headers reales

- `osAppNMIBuffer` estaba declarado `void*`; el real es
  `s32 osAppNMIBuffer[0x10]` (un array, no un puntero).
- `osMotorStart`/`osMotorStop` son macros que expanden a
  `__osMotorAccess()`, no funciones — implementarlas como funciones no
  las habría enlazado nunca.
- `osAfterPreNMI`, `osContStartReadData`, `osContStartQuery`,
  `osContSetCh` devuelven `s32`, no `void`.
- `G_MV_VIEWPORT`/`G_MV_L0` usados con los valores del F3DEX **antiguo**
  (`0x80`/`0x86`) en vez de los de F3DEX2 (`8`/`10`). `gbi.h` pone el
  comentario "F3DEX_GBI_2" justo en la rama contraria, es fácil
  equivocarse.

## Gráficos (`src/platform/ps2/gfx/`)

- **`ps2_segment.h`** (nuevo): traducción de direcciones segmentadas
  (`gSegments[]`), que antes no se hacía en ningún sitio — cualquier
  puntero a vértices/texturas en una display list real se interpretaba
  como dirección literal.
- **`rdp_to_gs.c`**: `G_VTX` (0x01) colisionaba con `G_SPNOOP` en el
  switch — dos `case` con el mismo valor ni compilan. La carga de
  vértices caía en la rama de "no hacer nada".
- **`f3dex2_ps2.c`**: viewport nunca se configuraba (buscaba el índice
  en el opcode equivocado), `G_POPMTX` leía la palabra que no era,
  geometry mode borraba los 8 bits altos por una máscara incompleta.
- **`rdp_color.c`**: faltaban `G_SETOTHERMODE_H/L` (los que OoT usa de
  verdad); dos archivos distintos escribían el registro `TEST` del GS
  por separado y se pisaban.

## Audio y guardado

- Colisión de símbolos con el decomp real: `Audio_Init` y
  `func_800F4C58` ya existen en `src/audio/game/general.c` con otra
  firma. Todo renombrado a `PS2Audio_*`.
- Remuestreo del mezclador perdía el acarreo (`&= 0xFFFF` descartaba
  bits en sonidos agudos).
- Guardado reescrito con la API real de `libmc` (`mcOpen`/`mcSync`...);
  antes usaba `open`/`read`/`write` que no aplican a `mc0:` en PS2, y
  nada de este archivo lo llamaba nadie.

## Linker (`linker/linkfile.ps2`, `tools/gen_segment_syms.py` nuevo)

- No definía ninguno de los ~165 símbolos `_xxxSegmentStart/End/...`
  que el juego referencia. `linkfile.ps2` define a mano los que hacen
  falta para arrancar; `gen_segment_syms.py` genera automáticamente el
  resto leyendo el código (`linker/segments.ld`).

## Endianness de assets (`tools/swap_assets.py`, nuevo)

El hallazgo más fácil de pasar por alto de todo el port: hay DOS clases
de display list — las de los **assets** (extraídas de la ROM,
big-endian) y las que el juego **construye en tiempo de ejecución**
(compiladas para PS2, little-endian). Ningún modo de lectura acierta
con las dos. La solución es convertir los assets a little-endian una
vez al extraerlos; `swap_assets.py` lo hace por tipo de dato (voltea
`.dlist`/`.mtx` en palabras de 16/32 bits, respeta los 4 bytes de color
de cada `.vtx`, y no toca en absoluto las texturas de 1 byte como I4/I8,
que se romperían si se voltean).

## Lo que NO está resuelto (y por qué no es un simple parche más)

- **Combinador de color del RDP** (`G_SETCOMBINE`): decide cómo se
  mezclan textura/color/sombreado en cada superficie. Sin esto casi
  todo sale con el color equivocado. Requiere traducir la máquina de
  combinación de N64 a los TFX del GS, e iterar mirando resultados en
  pantalla.
- **Síntesis de audio real** (música, no sólo efectos): el motor de
  audio del decomp (`src/audio/`) no está ni copiado al port. Portar la
  reproducción de secuencias MIDI-like de N64 es un subsistema entero.
- **Relocalización de overlays**: los actores se enlazan estáticamente
  en vez de cargarse en runtime a una dirección variable, que es como
  funciona realmente el sistema de overlays de N64.

Estos tres puntos son trabajo de meses, no bugs puntuales, y necesitan
verse correr en la consola/emulador para iterar — no se pueden terminar
"a ciegas".
