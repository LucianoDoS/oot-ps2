# Changelog — Revisión y corrección del port a PS2

Este documento resume todo lo revisado, corregido y verificado sobre el
port de Ocarina of Time a PS2. Está pensado para que puedas retomar el
trabajo sabiendo exactamente qué se tocó, por qué, y qué falta.

## Caché de iluminación y versión con vibración — 2026-10-11

- Se corrige la instrumentación para que la caché de 256 entradas registre
  hits y reconstrucciones con el mismo coste que las candidatas de 512/1024.
  Los contadores sólo avanzan durante el perfilado; no se añaden lecturas
  del reloj al juego normal.
- Referencia nueva: `build/PS2/perf-scenes/20261011-134417-449`, 256 entradas.
  Las candidatas `20261011-140014-951` (512) y `20261011-135150-107` (1024)
  pasan, con la misma ROM, configuración, semilla, muestras, secuencia de
  geometría y capturas por vista. Las tarjetas del jugador permanecen
  deshabilitadas en la prueba y su archivo no cambia.

  | Caché RGB | RAM | Traducción por vista (ms) | FPS por vista |
  | --- | --- | --- | --- |
  | 256 | 3 KiB | 35,06 / 32,40 / 39,06 | 19,46 / 19,76 / 19,10 |
  | 512 | 6 KiB | 34,71 / 32,09 / 38,74 | 19,46 / 19,77 / 19,16 |
  | 1024 | 12 KiB | 34,48 / 31,97 / 38,65 | 19,48 / 19,82 / 19,06 |

- Se conserva la capacidad de 1024: reduce la traducción un 1,1–1,6 % y las
  reconstrucciones RGB de 1279/1306/1386 a 772/821/904 por fotograma perfilado,
  sin alterar alpha ni los cálculos originales. Consume 9 KiB adicionales.
  La mejora de tiempo es pequeña y no demuestra una ganancia sostenida de
  FPS ni 20 estables. Estas cámaras próximas a la casa tampoco sustituyen
  la vista física más pesada ni una prueba caminando por el mapa.
- La caché independiente de normales de la sesión anterior pasa su oracle,
  pero `20261010-144111-786` no obtiene hits y aumenta la traducción cerca
  de 0,9 %. `PS2_NORMAL_VECTOR_REUSE` permanece desactivado, igual que la
  candidata de invalidación por color que no dio ganancia.
- `floor-production-smoke-20261011-140625` pasa con 1024 entradas y comprueba
  colisiones/reemplazo, claves completas, cambios de luces, alpha, suelo,
  transparencias, audio y SRAM. El script smoke usa la capacidad del header
  de producción por defecto; permite un override explícito para comparar.
- Se repite `tools/test_ps2_pad_rumble.py`: PASS. La revisión confirma que
  `PadMgr_UpdateRumble` descubre el dispositivo mediante `CONT_CARD_ON` y
  utiliza la API de motor integrada. Falta comprobar la sensación de
  vibración en un mando físico.

## Sesión física posterior y pruebas repetibles — 2026-10-10

- Se conserva `build/PS2/hardware-runtime/compare-physical-20261008-203055`
  antes de cambiar archivos del USB. Sus 195 muestras contienen las tres
  filas SAMPLE/USB/AUDIO completas, continuas y ordenadas; no se detectan
  sectores antiguos en la cola de esta sesión. Esto no demuestra que el
  fallo intermitente de escritura de sesiones anteriores esté resuelto.
- Las vistas pesadas siguen cerca de 12–13 FPS. Los perfiles 1200/1500
  transforman 4439 vértices y registran 1816 comandos de triángulos; la
  traducción ronda 57,50–61,45 ms. No hay una mejora clara de FPS frente
  a la sesión anterior. La reducción de subidas de texturas explica más
  que la reducción total de tiempo: no se atribuye esa diferencia a la
  escritura directa de puntos. Los tramos finales de 18–20 FPS dibujan
  vistas más ligeras. El inventario y sus transiciones se excluyen del
  análisis de juego activo.
- El siguiente ciclo de optimización usa PCSX2 y cámaras fijas, con ROM,
  configuración y capturas propias de la prueba. La PS2 física se reserva
  para comprobar los cambios relevantes, sin sustituir su validación por
  cifras del emulador.
- Se revisa la ocarina contra el código original local: el modo de toque
  libre oculta deliberadamente el pentagrama y las notas, conservando a
  Link y el escenario. Los modos de aprendizaje/reconocimiento sí dibujan
  las notas. El usuario confirma que la confusión era sobre el toque libre;
  no se cambia esa conducta del juego.
- `tools/run_ps2_perf_scene.ps1` ejecuta tres vistas del bosque Kokiri con
  guardado sintético, mandos y tarjetas deshabilitados, 30 fotogramas de
  calentamiento y 120 medidos por vista. El punto de Link se apoya mediante
  la colisión cargada. `Play_Init` cambiaba la semilla con el reloj: el ELF
  de prueba intercepta también esa llamada y comprueba la semilla efectiva.
  El perfilado se limita a los fotogramas 60/120 y se excluye de los tiempos
  normales. Capturas y filas se escriben después del tramo medido.
- La referencia válida `build/PS2/perf-scenes/20261010-141140-235` pasa y
  mide 19,46 / 19,76 / 19,06 FPS, con traducción de 34,98 / 32,36 / 38,96 ms.
  Su repetición `20261010-141648-933` conserva la geometría de cada fotograma,
  las capturas y los tiempos. Las versiones iniciales de la prueba se
  conservan para diagnóstico, pero no sirven como referencia: primero el
  punto de Link flotaba y después la semilla aún dependía del reloj.
- La candidata de invalidación por colores pasa 25 casos/150 puntos y el
  smoke completo `floor-production-smoke-20261010-140500`. La comparación
  `20261010-141505-782` conserva exactamente geometría y capturas, pero no
  retiene texturas en estas vistas y añade aproximadamente 0,2–0,3 % al
  tiempo de traducción. `PS2_COLOR_INVALIDATION_REUSE` queda desactivado;
  no se presenta como una mejora de rendimiento.
- Vibración: `osContGetQuery` anuncia `CONT_CARD_ON` sólo después de
  confirmar modo analógico, alineación de actuadores y motores apagados.
  Antes el juego no descubría el Rumble Pak. La negociación avanza por
  consultas, sin espera activa; `osMotorInit` valida el puerto y el estado
  de `OSPfs`, y los pulsos originales se envían a ambos motores. Desconexión,
  errores y cierre intentan apagar los motores; reconexión empieza apagada.
  `tools/test_ps2_pad_rumble.py` pasa las pruebas de detección, solicitudes
  asíncronas fallidas, mando digital, pulsos, reconexión y cierre. Compilan
  las estructuras EE reales; todavía falta sentir la vibración en hardware.

## Comparación de texturas, escritura directa y sincronizaciones vacías — 2026-10-08

- La sesión `build/PS2/hardware-runtime/traversal-physical-20261008-164351`
  coincide con el paquete entregado: PERF SHA256
  `3BBC47BF012FEB61A533DCE12BC306175CCC46EBF0C235461EA6B7EBA314A09E`.
  Tiene 224 muestras completas fiables y 12 perfiles. El último sector,
  offset 163.840 de 164.352 bytes, coincide byte a byte con el registro
  anterior; empalma AUDIO 225 y contiene las muestras antiguas 234/235.
  Se conserva el archivo original y se excluye esa cola. La escritura
  intermitente sigue sin resolver; un contador de pérdidas cero no la descarta.
- Entre 111 y 126 segundos, sin lecturas ni escrituras solapadas, quedan
  13 intervalos/174 fotogramas: mediana 13 FPS, traducción 60,26 ms,
  envío final 2,43 y presentación 12,37 por fotograma. No se observan colas
  vacías de audio en esos intervalos. La cámara no está verificada idéntica;
  el resultado no demuestra una mejora sostenida frente a la sesión anterior.
  VTX/DL/END anidado ya no cierran grupos; el END raíz los vacía.
  Permanecen 381/382 límites de otros estados.
- Los perfiles grandes 1800/2100 gastan 34,50–39,01 ms en triángulos,
  incluidos 29,11–33,55 ms en su preparación/envío. Comparan 376–379 kB
  en 3,42–4,68 ms, sin nuevas copias, mutaciones o expulsiones. Las huellas
  restantes sólo corresponden a texturas pequeñas no admitidas. La paleta
  ocupa 0,163–0,173 ms; las subidas de 5/7 texturas cuestan 4,54–6,13 ms.
  Son categorías anidadas, no tiempos independientes que se puedan sumar.
- El comparador EE procesa ahora bloques completos de 256 bytes y conserva
  la ruta de 64/16 bytes y escalar para las colas. No omite validación de
  contenido, padding excluido, ninguna de las dos fuentes ni cambios de RAM.
  No amplía el anillo de 128 KiB ni los 128 descriptores. El comparador de
  128 bytes y el selector de referencia existen sólo en SMOKE. Los bloques
  mayores tardan más en detectar una mutación temprana; las vistas pesadas
  medidas no registran mutaciones, pero la corrección se comprueba igualmente.
- Los triángulos texturados de una capa escriben sus puntos directamente
  en la cola libre, evitando una copia de 144 bytes. Al llenarse el lote
  conservan el temporal y el instante original del vaciado. El suelo de dos
  capas, las capturas y los dibujos fallidos mantienen su orden; sólo se
  compromete la cola cuando la textura carga. No se agrupan las capas del suelo.
- El desglose previo a esta corrección, fixture
  `usb-stack-checks/20261008-172657-381`, encuentra 248 de 302 límites
  en los cuatro RDP SYNC del fotograma 600. Sus handlers ya eran vacíos;
  ahora tampoco cortan el grupo. LOAD/TLUT, cambios de material, capturas,
  objetivos y ENDDL raíz siguen vaciándolo antes de ejecutarse. No se
  elimina ni difiere ninguna sincronización efectiva del GS.
- Pasa `build/PS2/floor-production-smoke-20261008-174515`: 338.880
  comparaciones y 14.256 huellas adicionales para los tres bloques, alineaciones,
  cada byte, diferencias repetidas, colas, parejas y filas con padding; también
  pasa la batería previa de 233.216/42.430. El desensamblado confirma guardias
  de bloque/alineación, offsets finales y retorno a la ruta de cola. Los canarios
  de la prueba certifican resultados, no por sí solos las lecturas descartadas.
- En 20 grupos aislados el bloque de 256 supera al de 64: aproximadamente
  17–29 % menos ciclos en fuentes individuales y 18–25 % en un recorrido
  de 96 copias/48 KiB. La cola directa conserva 480 puntos y todos los bytes
  de paquetes en cuatro casos con desbordamiento, recorte, cargas parciales
  y fallo de textura. Con los cuatro SYNC, en 864 triángulos por ruta mide
  929.009 ciclos anteriores y 863.926 directos. Son ensayos emulados de
  operaciones aisladas, no ganancias de FPS del juego ni prueba física de 20 FPS estables.
- Los 24 casos de recorrido de listas pasan con SYNC, ramas, matrices y
  cargas parciales, completas y del slot final. El diferencial separado
  ENV/PRIM conserva 12 puntos en orden tanto en materiales texturados como
  de color; exige tres grupos donde la ruta anterior emitía cuatro.
- PERF desglosa los límites restantes en 19 categorías, repartidas en dos
  registros que caben en los 320 bytes del logger. Sólo cuenta en el fotograma
  muestreado; las categorías suman `otherFences`. El analizador reúne partes
  completas, únicas y del mismo fotograma; pasan sus 14 pruebas. El juego
  habitual mantiene desactivados los cronómetros por operación.


- Las pruebas finales con USB nativo emulado pasan en
  `build/PS2/usb-stack-checks/20261008-201143-015` y
  `build/PS2/usb-stack-checks/20261008-201419-773`: logo a 59,9 FPS,
  opción y diálogo en español, casa–Kokiri–casa con 120 actualizaciones
  por fase. Las cuatro capturas coinciden byte a byte con la referencia.
  Las tarjetas del jugador permanecen desactivadas durante estos recorridos.
- El perfil final confirma cero límites SYNC, pero los totales 135/302
  no bajan en los fotogramas 300/600: el siguiente cambio real de estado
  cierra el grupo. Por tanto este caso no demuestra un ahorro de lotes ni
  FPS. La nueva clasificación permite estudiar esos materiales en hardware.
- `build/PS2/compare-release-verification.json` vincula el ELF del smoke,
  los ELF de ambos recorridos y sus informes mediante SHA256. Recalcula
  independientemente la ventana física desde el log sin usar su resumen,
  comprueba límites estáticos del ELF, los datos convertidos, el idioma y
  la conservación del registro y la tarjeta. No certifica toda la memoria
  dinámica ni los FPS reales de esta candidata.
- OOTPS2 SHA256: `886825B975700707D3BCE234FD59DDBB584787BD75A12B951D55919BF2E0FD80`.
  PERF SHA256: `7F3B97CE20E43041F42A7D62ABCA14C3ADF31F35027278D1268B6FD45266E540`.

- Copiados y verificados los 12 archivos en `D:/OOTPS2`; respaldo en
  `D:/OOTPS2-backup-20261008-201937`. La comprobación posterior
  pasa con `copied=true`, registros físicos y tarjeta del jugador intactos.
  Queda probar esta candidata en la misma vista pesada de la SCPH-90010.

## Reutilización al recorrer modelos y comprobación física de la caché — 2026-10-08

- Se conserva la sesión en
  `build/PS2/hardware-runtime/retention-physical-20261007-214734`: 255 muestras
  utilizables y 13 perfiles, sin daño, duplicados, pérdidas ni errores USB.
  PERF coincide con el paquete anterior, SHA256
  `8DA83A36DC6A5F724EEFC8687163D54F88463B7E25C43F4DA4E4D7E25573319B`.
  Entre 111 y 126 segundos, excluyendo una escritura solapada, quedan
  14 intervalos y 186 fotogramas: mediana 13 FPS, traducción 60,83 ms,
  envío final 2,85 y presentación 12,01 por fotograma. No hay lecturas,
  colas de audio vacías ni aumento de su contador de retrasos en ese tramo.
  La cámara no está verificada idéntica: no se afirma una ganancia exacta
  frente a los 11,9 FPS/68,02 ms anteriores ni 20 FPS estables.
- En los perfiles grandes 1500/1800/2100 desaparecen las nuevas copias,
  expulsiones de descriptor y solapamientos. Las 46/45/45 huellas restantes
  pertenecen sólo al límite pequeño de admisión: 3.456/3.392/3.392 bytes,
  frente a unos 117 KiB anteriores. La validación completa compara ahora
  380–403 KiB y ocupa 3,66/3,82/4,92 ms; ampliar más la capacidad no elimina
  ese trabajo. La paleta usa 166–168 µs, dentro de los metadatos; la lectura
  y decodificación TLUT quedan fuera de esa medida. El coste mayor sigue
  en los triángulos: 31,37–33,31 ms, incluido su envío de 25,84–27,80 ms.
- Los tres perfiles grandes tienen 45 límites por VTX, tres por DL,
  178 por ENDDL y 201 por otros estados. Son oportunidades de reutilización,
  no una predicción del número de lotes que se ahorrará. La candidata conserva
  un grupo al recorrer llamadas/saltos, matrices y cargas de vértices;
  invalida exactamente los slots sobrescritos de color, puntos de ambas capas
  y UV. Las cargas de 64 slots evitan desplazar una máscara por 64 bits.
  Los paquetes pendientes contienen puntos copiados. Material, viewport,
  niebla, objetivos de render, recorte de listas y fin raíz siguen cerrando
  el grupo; se conserva también el cierre al abortar por la guardia.
  Las dos capas del suelo siguen dibujándose por triángulo y en su orden.
- Pasa la batería EE en `build/PS2/floor-production-smoke-20261008-142415`.
  Veinticuatro casos comparan el intérprete anterior y el nuevo: llamadas
  anidadas, saltos, matrices, cargas parciales/completas/slot 63, colores,
  texturas, suelo, niebla, profundidad, decals, recorte y cambios de material.
  Los puntos, vértices finales, orden y contadores de dibujo coinciden.
  Se miden 1.778.485 ciclos de referencia y 1.258.149 reutilizados: es un
  ensayo aislado emulado, no un aumento de FPS físicos. La mejora del juego
  en consola sigue pendiente.
- Pasan ambos recorridos con USB nativo emulado:
  `build/PS2/usb-stack-checks/20261008-143259-045` (habitual) y
  `build/PS2/usb-stack-checks/20261008-143554-364` (PERF). Conservan el logo
  a 59,9 FPS, opciones/diálogo en español y casa → Kokiri → casa. Ambas
  capturas coinciden con la versión entregada anterior; frente a la referencia
  antigua sólo hay 35 píxeles de parpadeo en los ojos. Los registros son
  íntegros y los perfiles nuevos aparecen completos sólo en PERF. Datos,
  idioma y tarjeta del jugador conservan sus hashes; las tarjetas permanecen
  desactivadas en las pruebas. El rango estático de OOTPS2 termina en
  0x01223C70: no certifica el uso dinámico de memoria. La comprobación queda
  en `build/PS2/traversal-release-verification.json`. Los ejecutables de
  producción no contienen el helper que fuerza las barreras anteriores.
- Paquete copiado y verificado en `D:\OOTPS2`, con 12 archivos y respaldo
  de los sustituidos en `D:\OOTPS2-backup-20261008-143944`. BOOT.LOG y
  DIAG.LOG conservan los hashes de la sesión archivada. OOTPS2 SHA256:
  `1DBDE26C6570175E38D4CE0C4A56632D8F6C452CF465D7D17C9BC9C1DAD10894`.
  El siguiente ensayo físico debe usar PERF.ELF, quieto y caminando en la
  misma vista lenta; los 20 FPS sostenidos siguen sin confirmar.

## Límite de descriptores y reutilización de huellas de paleta — 2026-10-07

- La sesión física se conserva en
  `build/PS2/hardware-runtime/setup-physical-20261007-205251`: 285 muestras
  utilizables y 13 fotogramas perfilados, sin daño, duplicados, saltos,
  pérdidas declaradas, errores USB ni fallos de scratchpad. Entre 111 y
  126 segundos, excluyendo el intervalo solapado con una escritura del
  registro, quedan 14 intervalos y 169 fotogramas: mediana 11,9 FPS,
  traducción 68,02 ms, envío final 1,96 y presentación 13,11 por fotograma.
  No hay lecturas ni observaciones de cola de audio vacía en ese tramo.
  El coste sostenido sigue concentrado en el renderer; estos tiempos
  generales incluyen las interrupciones y la ejecución de otros hilos.
- Los motivos nuevos separan el límite de descriptores del espacio de copias.
  En cinco fotogramas de la vista grande, 358 de 372 expulsiones (96,2 %)
  corresponden a descriptores; sólo 14 corresponden al espacio del anillo.

  | Fotograma | Nuevas copias | Expulsiones de descriptor | Solapamientos del anillo |
  | --- | ---: | ---: | ---: |
  | 1200 | 75 | 73 | 2 |
  | 1500 | 74 | 71 | 3 |
  | 1800 | 75 | 71 | 4 |
  | 2100 | 74 | 72 | 2 |
  | 2400 | 74 | 71 | 3 |

- La versión candidata conserva 128 descriptores en lugar de 64: añade
  3.840 bytes de metadatos y mantiene el anillo de 128 KiB, el máximo de
  16 KiB y el mínimo de 256 bytes por copia. Se siguen comparando todos
  los bytes cargados, incluidas ambas fuentes de las mezclas, antes de
  reutilizar una huella. La prueba de 96 fuentes de 512 bytes conserva
  384 aciertos tras calentarse y valida después 96 cambios de contenido.
- Se conservan 17 huellas FNV de paleta: 16 bancos CI4 y la paleta CI8
  completa. Sólo cambia su validez cuando cambia una entrada RGBA decodificada;
  todas las cargas TLUT siguen leyendo sus bytes de RAM. Usa 72 bytes de
  caché y 12 de contadores. Los metadatos de los cinco perfiles citados
  ocupan 3,3–4,9 ms, pero aún no se ha aislado qué parte corresponde a la
  paleta en consola.
- Pasa la batería EE en `build/PS2/floor-production-smoke-20261007-211045`,
  con 233.216 comparaciones de contenido y 42.430 comprobaciones de huellas.
  En 9.600 consultas calentadas a 96 fuentes, 64 descriptores registran
  41.295.030 ciclos, cero aciertos y 9.600 hashes; 128 registran 6.546.806
  ciclos, 9.600 aciertos y cero hashes, con checksum idéntico `a9acb698`.
  La paleta pasa 4.096 cambios de entrada/bit, 21 cargas parciales y siete
  cargas inválidas. En 8.192 huellas completas sin cambios registra
  17.268.078 ciclos de referencia y 303.277 con reutilización, conservando
  el checksum. Son operaciones aisladas en PCSX2, no medidas de todo el
  fotograma ni de la consola física.
- PERF añade `[PS2 PALETTE HASH PROFILE]`, incluido en metadatos, y
  `[PS2 MATERIAL RUN PROFILE]`, que cuenta límites de grupos por comandos
  de vértices, listas, fin y otros estados. Esos límites no equivalen
  necesariamente a vaciados de lotes con primitivas pendientes. Pasan once
  pruebas del analizador. Sigue pendiente medir la ganancia física de esta
  versión candidata y alcanzar 20 FPS estables en las vistas grandes.
- Pasan ambos recorridos con USB nativo emulado:
  `build/PS2/usb-stack-checks/20261007-212243-879` (habitual) y
  `build/PS2/usb-stack-checks/20261007-212625-070` (PERF). Verifican logo a
  59,9 FPS, menú/diálogo en español, casa → Kokiri → casa y registros íntegros.
  El menú conserva todos sus píxeles; las 35 diferencias del diálogo quedan
  limitadas al parpadeo de los ojos y coinciden con la captura ya inspeccionada
  de la versión anterior. Ambas variantes producen las mismas capturas.
  Los perfiles nuevos están completos en PERF y ausentes en el registro
  habitual. Se conservan los hashes de los datos y la tarjeta del jugador,
  desactivada durante las pruebas. Los símbolos de producción confirman
  7.680 bytes de descriptores, 131.072 de copias y 84 de paleta/contadores;
  no contienen los helpers de pruebas. El rango estático de OOTPS2 termina
  en 0x01223AF0; no certifica el uso dinámico de memoria. La comprobación
  se conserva en `build/PS2/retention-palette-release-verification.json`.
- El paquete queda copiado y verificado en `D:\OOTPS2` (12 archivos), con
  copia de los archivos sustituidos en `D:\OOTPS2-backup-20261007-213036`.
  BOOT.LOG y DIAG.LOG conservan sus hashes originales; los datos OOT.Z64,
  ESP.DAT y la tarjeta del jugador permanecen intactos. OOTPS2 SHA256:
  `D2A3D384E40A08D04C17308F3AD0C80D8BF50846B10BE45A86F77B1E56DAB965`.
  La nueva comprobación física debe usar PERF.ELF en la misma vista grande,
  quieto y caminando, para comparar presión de descriptores y tiempo de
  traducción sin atribuir cambios de cámara a una optimización.

## Coste físico de las vistas grandes y renovación de copias — 2026-10-07

- Se conservan BOOT.LOG, DIAG.LOG, el manifiesto y las identidades de los
  ejecutables en `build/PS2/hardware-runtime/draw-detail-physical-20261007-200623`.
  PERF coincide con el paquete entregado, SHA256
  `691F99E5CEAF5475BDE26F2DED272C8FEFB19A73634C13FD805C8525DC3D7D86`.
  Esta sesión tiene 225 muestras utilizables y 11 perfiles detallados, sin
  líneas dañadas, duplicados, errores USB ni pérdidas declaradas. No prueba
  que la corrupción intermitente de la sesión anterior esté corregida.
- Entre 75 y 103 segundos hay una mediana de 12 FPS. Excluyendo intervalos
  con lecturas y escrituras del registro solapadas, quedan 24 intervalos y
  285 fotogramas: 68,01 ms de traducción, 2,43 de envío final y 13,47 de
  presentación por fotograma. Sólo hay dos lecturas, 15 ms en todo ese tramo,
  sin cola de audio vacía ni aumento de su contador de retrasos. La espera
  del juego por gráficos es de 76–78 ms; su actualización ocupa 6–7 ms.
  Las caídas sostenidas de esa vista no se explican por cargar desde el USB.
- En los perfiles 1200 y 1500 la traducción ocupa 58,96/59,30 ms después
  de descontar el DSP medido. Otros hilos e interrupciones siguen incluidos.
  La preparación/envío de triángulos ocupa 30,26/28,54 ms, incluidos búsqueda
  de texturas y puntos. El perfil 1500 dibuja 1.720 primitivas, prepara 3.240
  puntos y no convierte ni sube ninguna textura. Claves, paquetes y ajuste
  del suelo ocupan 1,41/1,61/0,16 ms; no son el coste principal pendiente.
  Estas categorías están anidadas: no deben sumarse como fases independientes.
- La validación de contenido aún ocupa 5,66/6,11 ms y calcula 121/122 huellas
  pese a reutilizar casi todas las texturas en VRAM. Los contadores anteriores
  no distinguen límites, expulsiones y cambios; no se atribuye todo ese coste
  a un único motivo sin medirlo. El último tramo con menos geometría vuelve
  a 19,9 FPS; no se compara como si fuera la misma cámara de la vista lenta.
- Una copia de textura cuyos límites no cambian se renueva ahora en su
  mismo espacio al cambiar sus bytes. Antes cada cambio avanzaba el anillo
  y podía expulsar copias estables, aunque el conjunto vivo sólo ocupara
  8 KiB. Se siguen comparando todos los bytes de ambas fuentes y calculando
  la huella original con el mismo orden de filas y semillas. La capacidad
  permanece en 128 KiB y 64 descriptores; no se reserva más memoria.
- Se añaden motivos de validación/expulsión en `[PS2 TEXTURE CONTENT]` y
  `[PS2 DRAW SETUP PROFILE]` separa preparación inicial del triángulo,
  aplicación de tile y metadatos anteriores a la búsqueda. PERF conserva
  una muestra cada 300 fotogramas; el juego habitual no lee esos cronómetros.
- La conversión de cada canal de ocho bits a escala GS usa una identidad
  exacta, conservando el extremo 255 → 128. Pasan los 256 valores y las
  comparaciones de puntos del renderer. En 8.192 conversiones aisladas se
  miden 133.178 ciclos de referencia y 100.537 del helper en PCSX2. Esto no
  representa una ganancia del fotograma completo ni una medición física.
- Pasa la batería EE con 233.216 comparaciones de contenido, 41.694 huellas,
  cambios repetidos/filas/paletas/límites/expulsiones, suelo, profundidad,
  niebla, recorte y 120 fotogramas gráficos/audio en
  `build/PS2/floor-production-smoke-20261007-201638`. Pasan nueve pruebas del
  analizador, incluida la captura parcial de los nuevos perfiles. La mejora
  de esta versión en consola y los 20 FPS estables siguen pendientes.
- Pasan ambos recorridos con USB nativo emulado:
  `build/PS2/usb-stack-checks/20261007-202701-703` (habitual) y
  `build/PS2/usb-stack-checks/20261007-203004-217` (PERF). Verifican logo a
  59,9 FPS, menú/dialogo en español, casa → Kokiri → casa y registros íntegros.
  El menú conserva todos sus píxeles. Las 35 diferencias del diálogo quedan
  limitadas al parpadeo de los ojos, inspeccionadas; ambas variantes producen
  las mismas capturas. PERF incorpora las medidas y motivos nuevos; el juego
  habitual conserva el perfilado desactivado. Las tarjetas del jugador están
  desactivadas durante las pruebas. El rango estático de OOTPS2 termina en
  0x012226F0; no certifica el uso dinámico de memoria. La comprobación del
  paquete está en `build/PS2/mutation-reuse-release-verification.json`.
- Se copian y verifican los doce archivos del paquete en `D:\OOTPS2`, con
  respaldo en `D:\OOTPS2-backup-20261007-203554`. OOTPS2 tiene SHA256
  `82CD14F27DC614964AE9E6D3000B280842F5E596D8864EFD1C1ACCB3E09E83E3`.
  Se verifica que BOOT.LOG y DIAG.LOG físicos, los datos convertidos y la
  tarjeta del jugador no cambian. La verificación conjunta pasa con la copia
  comprobada; esta versión aún no se ha probado en la consola física.

## Segunda medición PERF y búsqueda de claves compartida — 2026-10-07

- Antes de cambiar el USB se conservan los registros, el paquete y las
  identidades de los ejecutables en
  `build/PS2/hardware-runtime/analysis-cache-physical-20261007-000954`.
  PERF coincide con la versión entregada, SHA256
  `1980B60778A3862F5E0DE48B6A6F60E1CCD8B24810F510C367A01222C76126E9`.
  Se leen 283 muestras completas y 14 fotogramas perfilados.
- Las vistas lentas siguen alrededor de 11–12 FPS. En los fotogramas
  1200/1500/1800 se reutilizan 131 análisis de materiales y sólo se
  reconstruyen ocho, con 64–115 microsegundos de reconstrucción. El ahorro
  existe, pero no confirma una ganancia sostenida de FPS. Los triángulos
  siguen costando 38–42 ms, incluida la búsqueda de texturas de 11–12 ms.
  La traducción sin el DSP medido ocupa 63–68 ms; otros hilos siguen incluidos.
- El último minuto limpio de lecturas y escrituras solapadas recoge 580
  fotogramas y mediana 19,9 FPS. La geometría visible es menor en esos
  perfiles finales; no se presenta como una comparación de la misma cámara.
  Sigue pendiente el objetivo de 20 FPS estables en las vistas grandes.
- El archivo contiene dos muestras con secuencia 80, una línea incompleta
  y saltos de secuencia. No hay errores USB/logger declarados ni pérdidas
  declaradas, lo que no certifica la integridad. Los fragmentos fuera de
  orden aparecen cerca de límites de 32 KiB; la causa aún no está resuelta.
  No se da por corregido el controlador ni se reescribe el registro original.
- El analizador conserva todas las muestras completas para la auditoría,
  pero excluye todas las copias de una secuencia repetida y los tiempos de
  muestra que no avanzan de las estadísticas FPS y las ventanas limpias.
  En esta sesión quedan 281 intervalos utilizables. Pasan ocho pruebas,
  incluyendo la corrupción observada y perfiles parciales.
- La búsqueda del descriptor de textura compara una sola vez los campos
  comunes del material. Sólo un acierto completo usa VRAM anterior: también
  exige contenido, paleta, LOD, origen de ventanas y colores. En un fallo,
  mantiene los criterios de reutilización y selección del candidato.
  Se siguen leyendo y validando todos los bytes de ambas fuentes.
- La prueba EE compara ambas clasificaciones en 5.640 casos, cambiando
  todos los bits de los descriptores, incluidos datos auxiliares y relleno
  que no forman parte de la clave. La operación aislada de 8.192 aciertos
  pasa de 1.983.902 a 1.374.749 ciclos emulados. No mide el fotograma completo.
  Pasa la batería del renderer con 120 fotogramas gráficos/audio en
  `build/PS2/floor-production-smoke-20261007-002545`.
- PERF añade `[PS2 DRAW DETAIL PROFILE]`: búsqueda de claves, construcción
  de paquetes GS y reajuste de coordenadas del suelo. Son tiempos incluidos
  en búsqueda/envío, no categorías para sumar. Se conserva el muestreo
  de un fotograma cada 300; el juego habitual no activa esos cronómetros.
- Pasan ambos recorridos con USB nativo emulado:
  `build/PS2/usb-stack-checks/20261007-004040-963` (habitual) y
  `build/PS2/usb-stack-checks/20261007-004401-082` (PERF). Comprueban el
  logo a 59,9 FPS, opciones en español, diálogo y casa → Kokiri → casa.
  El menú conserva todos sus píxeles. Las diferencias del diálogo quedan
  limitadas a los ojos durante el parpadeo, inspeccionadas visualmente.
  Ambos registros emulados son íntegros; el habitual no activa perfiles
  y PERF incluye las tres medidas nuevas en sus fotogramas muestreados.
  Las tarjetas del jugador están desactivadas durante estas pruebas.
- Se compilan los cuatro ejecutables del juego y BOOTTEST; doce archivos
  quedan verificados en `D:\OOTPS2`, con respaldo en
  `D:\OOTPS2-backup-20261007-004727`. OOTPS2 tiene SHA256
  `653A2C494E4E1CDDD845102A1DC4ADD42ED36F19D007037AE4026BE6C88797B6`.
  Su rango estático termina en 0x01222170; no certifica el uso dinámico de
  memoria. Se verifican la conservación de los registros físicos originales,
  los datos y la tarjeta del jugador. La prueba conjunta queda en
  `build/PS2/shared-key-release-verification.json`. La mejora física de
  esta búsqueda y los tiempos de las tres fases nuevas siguen pendientes.

## Perfil físico espaciado y caché de análisis de materiales — 2026-10-06

- Se conservan ambos registros y la identidad del paquete en
  `build/PS2/hardware-runtime/sparse-physical-20261006-232358`, antes de
  cambiar el USB. PERF coincide con el ejecutable entregado y registra
  una muestra detallada cada 300 fotogramas. Hay 270 intervalos y 14 perfiles,
  sin errores USB/logger, bytes nulos, líneas dañadas, saltos, pérdidas ni
  fallos de las guardas del scratchpad.
- La sesión contiene una vista de unos 12 FPS y termina con vistas cercanas
  a 20 FPS. El último minuto sin lecturas ni escrituras del registro solapadas
  incluye 740 fotogramas y una mediana de 19,9 FPS. No es una comparación
  de la misma cámara: los perfiles finales dibujan menos geometría. No se
  confirma aún el objetivo de 20 FPS estables en todas las vistas.
- Los perfiles lentos 1200/1500/1800 emplean 60–63 ms en traducción, una vez
  descontada la síntesis de audio medida: 37–38 ms en triángulos y 10–12 ms
  en vértices. La búsqueda de texturas, incluida en el envío, emplea 11–13 ms;
  no se deben sumar esas categorías. Otros hilos siguen incluidos. Las
  solicitudes USB no explican por sí solas este coste sostenido.
- El renderer conserva 32 análisis de selectores en ocho grupos de cuatro,
  manteniendo la ruta rápida del último material y comprobando las mismas
  cuatro claves que antes. Colores de primitiva/entorno y LOD no se guardan
  como resultados: se siguen evaluando en cada uso. La expulsión o colisión
  reconstruye el análisis original. El almacenamiento fijo es 2.716 bytes.
- La prueba EE compara 480 casos con la caché anterior de un análisis,
  incluyendo presión de 80 materiales, cambios de color/LOD, ciclos,
  mezclas y restauraciones. Al alternar ocho materiales, las reconstrucciones
  pasan de 256 a 8; la operación aislada emplea 719.741 frente a 3.440.332
  ciclos emulados. No equivale a una ganancia del fotograma completo.
- Pasa la batería del renderer en
  `build/PS2/floor-production-smoke-20261006-233803`, con 120 fotogramas
  gráficos/audio, capas, texturas mutables, puntos, recorte y transparencias.
  Pasan las seis pruebas del analizador. PERF añade el tiempo de
  reconstrucción y los contadores de último material, análisis reutilizados
  y fallos de caché; siguen siendo medidas incluidas en las otras fases.
- Pasan los recorridos nativos USB emulados habitual
  (`build/PS2/usb-stack-checks/20261006-235133-095`) y PERF
  (`build/PS2/usb-stack-checks/20261006-235523-706`): logo a 59,9 FPS,
  opciones en español, diálogo y casa → Kokiri → casa. El menú conserva
  todos los píxeles; en el diálogo sólo difieren 35 píxeles de los ojos
  durante el parpadeo aleatorio de Link. No hay errores, pérdidas ni guardas
  dañadas y las tarjetas del jugador están desactivadas.
- En el perfil emulado 600 se reutilizan 105 análisis entre cambios de
  material y sólo se reconstruyen 10; 4.037 usos conservan el último
  material. Los 4.152 usos quedan contabilizados y el coste de reconstrucción
  es 35 microsegundos, incluido en las demás fases. Los fotogramas 300 y
  900 no reconstruyen análisis. El juego habitual no activa estos perfiles.
- Se recompilan los cuatro ejecutables del juego y BOOTTEST; doce archivos
  quedan verificados en `D:\OOTPS2`, con respaldo en
  `D:\OOTPS2-backup-20261006-235954`. OOTPS2 tiene SHA256
  `7EA6E92ED44D9A4CFA77A7C6BA51AF5E368C9138464B1993F33160BB41205272`.
  El ELF termina su rango estático en 0x01221d70; esto no certifica el uso
  dinámico de memoria. El resultado está en
  `build/PS2/analysis-cache-release-verification.json`. Se conservan los
  registros físicos, los datos y la tarjeta del jugador. La mejora de FPS
  de la nueva caché queda pendiente de comparación en la misma vista física.

## Cuarta sesión física y preparación de triángulos — 2026-10-06

- Se conservan BOOT.LOG, DIAG.LOG y la identidad del paquete antes de copiar
  nada al USB en
  `build/PS2/hardware-runtime/auto-play-physical-20261006-223349`.
  El ELF jugado coincide con la revisión anterior: SHA256
  `91044823E7D56C5BFD7333A8BE4D297366498617BE6E7FA359570724B8ED29CD`.
  Hay 225 muestras en unos 226 segundos, título, casa y Kokiri. Las copias
  de texturas y el scratchpad están activos; no hay errores USB/logger,
  registros perdidos, bytes nulos, saltos de secuencia ni guardas dañadas.
- El último minuto contiene 41 intervalos sin solicitudes de lectura ni
  escrituras del logger solapadas: 484 fotogramas, mediana 12 FPS,
  traducción 70,74 ms, envío 1,82 ms y presentación 12,36 ms por fotograma.
  La pausa máxima de escritura es 54 ms y las escrituras suman 532 ms.
  La sesión no confirma una mejora sostenida de FPS. La cámara y la carga
  de juego varían entre sesiones y los tiempos incluyen otros hilos;
  tampoco permiten certificar una regresión frente a la sesión previa.
- TRI ya valida los índices de sus tres vértices. Se pasan directamente
  al envío en lugar de recuperarlos por rangos, resto y división del tamaño
  del vértice. Los polígonos recortados pasan -1 y mantienen la preparación
  completa para sus vértices temporales. No cambia la reutilización por
  material, el color, la niebla ni el orden de ambas capas del suelo.
- Los puntos GS alineados usan seis escrituras de 64 bits con los mismos
  bits de RGBAQ, ST y XYZ2. Se mantienen el orden de operaciones de coma
  flotante, el Q, la conversión y límites XY del SDK, la profundidad y los
  registros GS. Los destinos sin alineación conservan la ruta original.
- La batería EE pasa en
  `build/PS2/floor-production-smoke-20261006-225328`: 27.648 casos de puntos,
  256 destinos/alineaciones y límites XY/profundidad, 612 puntos comparados
  contra los índices calculados por la ruta anterior, esperas de descriptores,
  texturas mutables, capas, niebla, transparencia y 120 fotogramas gráficos/audio.
  En operaciones aisladas emuladas, los puntos alineados emplean 6.478.808
  frente a 7.000.818 ciclos de la ruta preparada sin alinear; el envío por
  índices directos emplea 397.750 frente a 426.591 ciclos. Estos resultados
  no equivalen a la mejora del fotograma completo ni de la consola física.
- PERF.ELF conserva el juego y registro automático con lotes de 15 segundos,
  transferencias de 4 KiB y sin HUD. Sólo un fotograma de cada 300 activa los
  cronómetros detallados. OOTPS2.ELF mantiene esos cronómetros desactivados.
  El analizador separa dibujo, geometría, contenido de texturas, estado,
  color, puntos/material e interrupciones; descarta registros incompletos.
  Pasan sus seis pruebas. Las fases se solapan y no deben sumarse como costes
  independientes. Falta recoger este perfil en la misma vista lenta física.
- Pasan ambos recorridos con USB nativo emulado:
  `build/PS2/usb-stack-checks/20261006-230240-257` (automático habitual) y
  `build/PS2/usb-stack-checks/20261006-230538-291` (PERF). Comprueban el logo
  a 59,9 FPS, opciones en español, diálogo y casa → Kokiri → casa. El modo
  habitual no registra perfiles detallados; PERF registra los fotogramas
  300 y 600, con todas las categorías previstas. El menú y el diálogo del
  modo habitual coinciden byte por byte con la referencia. En PERF sólo
  cambian 35 píxeles de los ojos de Link durante su parpadeo aleatorio.
  Las tarjetas del jugador están desactivadas y ambas sesiones cierran sus
  escrituras sin errores, pérdidas ni datos dañados.
- Se recompilan los cuatro ejecutables del juego y BOOTTEST, y se copian
  doce archivos verificados a `D:\OOTPS2`, con respaldo en
  `D:\OOTPS2-backup-20261006-230834`. OOTPS2.ELF tiene SHA256
  `BC968B2353F2D674C824C4FC5945D38DF10AFB0480E75F822FAFBFDE23A01EBE`.
  El resultado conjunto queda en
  `build/PS2/index-aligned-release-verification.json`. Se conservan los
  registros físicos, los datos, el idioma y la tarjeta del jugador. Las
  nuevas optimizaciones y el perfil espaciado quedan pendientes de prueba
  física; no se da por cumplido el objetivo de 20 FPS estables.

## Validación de texturas y memoria interna del EE — 2026-10-06

- El registro físico anterior sigue señalando el dibujo como coste sostenido,
  incluso sin lecturas USB. Esta revisión introduce dos optimizaciones del
  renderer; no cambia el límite de 20 FPS del juego ni reduce detalle visual.
- Un buffer fijo de 128 KiB conserva hasta 64 ventanas de texels. Cada uso
  compara todos los bytes cargados, sin incluir el relleno de las filas;
  sólo después de una igualdad completa reutiliza la huella XXH32 anterior.
  Los cambios, ventanas grandes y expulsiones ejecutan el hash original.
  Se conservan el orden de semillas/filas, las dos fuentes de las mezclas,
  las paletas y las esperas de retirada del GS. La comparación del EE procesa
  bloques de 64 bytes, con rutas alineadas y sin alinear y cola escalar.
- En la prueba EE aislada de 1.000 usos sin cambios, fuentes de 2–8 KiB
  consumen aproximadamente un 53–68 % menos de ciclos que volver a calcular
  el hash. Esta medida es de la operación emulada, no del fotograma completo
  ni de la PS2 física. El perfil separado del juego reduce la búsqueda de
  texturas, pero no demuestra por sí solo una ganancia comparable de FPS.
- El estado RSP, su caché de vértices y los puntos GS reutilizados de ambas
  capas pasan a un bloque de 10.704 bytes en el scratchpad de 16 KiB del EE.
  Un solo hilo gráfico lo utiliza; los paquetes se copian a la cola normal
  de gsKit antes del DMA. No cambian los cálculos, la geometría, el centrado
  del suelo ni el orden de capas. Hay aserciones de tamaño/alineación y
  guardas revisadas por fotograma. `PS2_GFX_SCRATCHPAD=0` conserva la ruta RAM.
- La batería EE pasa en `build/PS2/floor-production-smoke-20261006-220856`:
  233.216 comparaciones, 40.816 casos de hashes/ventanas y 120 fotogramas
  gráficos/audio, sin fallos ni guardas dañadas. Comprueba mutaciones en
  cualquier byte, diferencias que podrían cancelarse, filas, fuentes dobles,
  alineaciones, límites, reutilización de RAM y expulsiones del buffer.
- El juego automático pasa en
  `build/PS2/usb-stack-checks/20261006-221607-826`: logo a 59,9 FPS, selección
  de idioma, diálogos y casa → Kokiri → casa. No habilita perfilado de
  triángulos ni HUD; conserva lotes de 15 segundos y transferencias de 4 KiB.
  El menú coincide byte por byte con la referencia. La captura del diálogo
  difiere en 35 píxeles de los ojos de Link durante el parpadeo; el escenario,
  el texto y el resto del jugador coinciden. La semilla aleatoria de Play_Init
  depende del reloj, por lo que ese parpadeo no es una referencia determinista.
  No hay errores USB/logger, bytes nulos, saltos de secuencia ni errores del
  scratchpad. Las tarjetas del jugador están desactivadas durante las pruebas.
- El registro periódico incluye usos exactos, hashes y tamaño del bloque
  interno. El analizador los separa del perfil detallado y descarta líneas
  dañadas; pasan sus cinco pruebas. Los contadores de comparación expresan
  rangos solicitados, no lecturas de bus exactas cuando hay salida temprana.
  Falta medir ambas optimizaciones en la misma vista de la consola real
  para comprobar cuánto queda del presupuesto de 50 ms por fotograma.
- Los tres ejecutables del juego se recompilan y el paquete queda copiado,
  con respaldo, en `D:\OOTPS2`. OOTPS2.ELF tiene SHA256
  `91044823E7D56C5BFD7333A8BE4D297366498617BE6E7FA359570724B8ED29CD`.
  BOOT.LOG, DIAG.LOG, datos, idioma y tarjeta del jugador conservan sus hashes.
  El resultado conjunto está en
  `build/PS2/texture-scratchpad-release-verification.json`; la mejora de FPS
  físicos permanece pendiente de una nueva sesión con OOTPS2.ELF.

## Tercera sesión física: coste sostenido del dibujo — 2026-10-06

- Se conserva el nuevo registro antes de modificar el USB en
  `build/PS2/hardware-runtime/auto-play-physical-20261006-212132`.
  OOTPS2.ELF coincide con la revisión `ecedd06`: SHA256
  `4DE2BCAB437C53A9FE7B99B92603CAD9900F124D704055AD61D1F4A31F90BC35`.
  Hay 270 muestras durante unos 271 segundos, con paso del título a la casa
  y al bosque Kokiri. No hay errores USB o del logger, registros perdidos,
  bytes nulos, líneas dañadas, saltos de secuencia ni reloj fallido.
- El último minuto contiene 51 intervalos sin solicitudes de lectura ni
  escrituras del logger solapadas: 660 fotogramas, mediana 12,9 FPS,
  traducción 62,91 ms, envío 1,82 ms y presentación 12,67 ms por fotograma.
  La escritura más lenta del registro tarda 112 ms; las escrituras suman
  885 ms en la sesión. La ralentización sostenida persiste fuera de esas
  escrituras y cargas. La cámara no se registra y estos tiempos incluyen
  interrupciones y otros hilos: no certifican una regresión ni una mejora
  comparable frente a los 13,45 FPS de la sesión anterior.
- Se revisa el contador `clipped`: cuenta triángulos descartados por estar
  completamente fuera de una misma cara del volumen visible, además de los
  que desaparecen después del recorte. No cuenta las llamadas al recortador
  ni mide su coste. Por tanto, los 880 descartes del último resumen no
  justifican atribuir la ralentización al recorte. El registro automático
  identifica el camino gráfico como coste principal, pero no separa aún
  su preparación de vértices, materiales y texturas en esa vista física.
- No reaparece el sector nulo de la sesión anterior; su causa continúa
  pendiente y esta comprobación no demuestra que esté corregida. Las colas
  de audio no muestran sondeos vacíos, lo que tampoco sustituye una prueba
  auditiva. Se conservan los ejecutables del USB y la tarjeta del jugador;
  esta revisión del registro no introduce una nueva versión del juego.

## Segunda sesión física y preparación de los límites de textura — 2026-10-06

- Se conserva el USB antes de modificarlo en
  `build/PS2/hardware-runtime/auto-play-physical-20261006-194851`. OOTPS2.ELF
  coincide con el paquete anterior: SHA256
  `BC63FC32E85E4B76A9BC117F7043ECCA87949E744BD47A5CF9BF653C36F6C155`.
  Se registran unos 511 segundos, título, casa y bosque Kokiri. No hay errores
  USB comunicados, pérdidas del buffer, reloj detenido ni límite del archivo.
- Las transferencias de 4 KiB reducen la escritura máxima observada de 949
  a 128 ms. El último minuto contiene 56 intervalos sin lecturas ni escrituras
  solapadas: 753 fotogramas, mediana 13,45 FPS, traducción 60,55 ms, envío
  1,83 ms y presentación 11,93 ms por fotograma. Esos tiempos incluyen otros
  hilos; la cámara no se registra y no permite afirmar una ganancia comparable
  de FPS frente a la sesión anterior.
- Hay 512 bytes nulos en el offset 229376, dentro del lote que termina hacia
  el segundo 451, sin error devuelto por el controlador. El analizador señala
  esa línea y el salto de secuencia 443 → 446, conserva 508 muestras válidas
  y descarta los fragmentos dañados. El minuto final medido está fuera del
  sector afectado. La causa de esa escritura incompleta queda pendiente.
  Pasan los casos de lecturas, escrituras, reloj fallido, registros parciales
  y daño de un sector; nunca se reconstruyen FPS desde fragmentos.
- La prueba aislada del juego separa vértices, triángulos y preparación en
  `build/PS2/usb-stack-checks/20261006-195335-041`. Conserva ambas capturas
  de referencia y pasa casa → Kokiri → casa. Los tiempos de ese perfil EE
  emulado no equivalen al recorrido físico del usuario.
- Se preparan límites, envoltura, espejo y filtro una vez por capa durante
  los comandos consecutivos de triángulos. Cada cambio de estado o subida de
  esa capa invalida la preparación. Cada aplicación sigue registrando el uso del
  caché para preservar las esperas del GS después de una retirada previa.
  Se mantiene el orden base → segunda capa de cada triángulo y el centrado
  que elimina las olas en la consola.
- La primera prueba EE de esta revisión pasa en
  `build/PS2/floor-production-smoke-20261006-195955`: contextos, filtros,
  envoltura, espejo, recorte, alternancia y 4.096 aplicaciones registran
  1.127.846 ciclos inmediatos frente a 519.187 preparados. Pasan las capas,
  puntos, Q, niebla, transparencia, recorte y 120 fotogramas gráficos/audio.
  La medida de una operación no certifica los FPS de la consola.
- La revisión final pasa en
  `build/PS2/floor-production-smoke-20261006-200916/report.json`, incluyendo
  una textura que vuelve a usarse después de una retirada previa del GS.
  Las 4.096 aplicaciones registran 1.127.826 frente a 519.153 ciclos.
  Se recompilan los tres ejecutables USB. El recorrido USB nativo automático
  pasa en `build/PS2/usb-stack-checks/20261006-201033-831`: casa → Kokiri → casa,
  logo a 59,9 FPS, texto español, cierre del registro, lotes de 15 segundos y
  transferencias de 4 KiB. Ambas capturas coinciden byte por byte con la
  revisión anterior y las tarjetas del jugador están desactivadas.
- El paquete queda copiado y verificado en `D:\OOTPS2`, con respaldo de la
  versión anterior. BOOT.LOG, DIAG.LOG y la tarjeta del jugador conservan sus
  SHA256. La prueba conjunta queda en
  `build/PS2/tile-binding-release-verification.json`. Falta medir la nueva
  preparación del material en la misma vista lenta de la PS2 real.

## Primer registro automático en consola y comprobación de texturas — 2026-10-06

- Se conserva la partida física en
  `build/PS2/hardware-runtime/auto-play-physical-20261006-114209`:
  255 muestras, encabezado AUTO-PLAY, sin errores USB, pérdidas, reloj detenido
  ni límite del archivo. El ELF coincide con la versión copiada anteriormente.
- El último minuto contiene 46 intervalos sin lecturas ni solapamiento con
  escrituras del logger: 549 fotogramas, mediana de 12 FPS, 66,82 ms de
  traducción, 2,74 ms de envío y 13,98 ms de presentación por fotograma.
  Esos tiempos incluyen interrupciones y otros hilos; señalan el camino
  gráfico como el coste principal sostenido, sin atribuirlo a una fase concreta.
  El analizador ahora separa esa ventana de cargas y escrituras del registro.
- El logger acumula 2.885 ms de escrituras y una llega a 949 ms. Se agrupan
  hasta ocho sectores por llamada (4 KiB), manteniendo alineación a 64 bytes,
  offsets/tamaños múltiplos de 512, relleno, cierre del lote y límite de 2 MiB.
  El arranque registra `transferBytes=4096`. Pasan escrituras parciales,
  exactamente dos llamadas para un lote de unos 7 KiB, fallos, overflow y
  batching en los modos automático y diagnóstico. Falta contrastar la latencia
  de esta revisión con el USB físico.
- La comprobación XXH32 ahora usa MMI también para fuentes alineadas a ocho
  bytes y a bytes arbitrarios. Usa LD o LDL/LDR completos y PCPYLD, conserva
  el camino escalar de cuatro bytes y no redondea hacia arriba el final de una
  franja. Comprueba todos los bytes en cada binding: no omite datos mutables ni
  conserva hashes sólo por dirección o fotograma.
- El registro físico confirma tres intervalos del logo Nintendo 64 a
  59,9–60 FPS. Ya no queda pendiente esa comprobación en la SCPH-90010.
- La revisión final pasa los 120 fotogramas de gráficos/audio y los casos de
  las 16 alineaciones, colas, niebla, recorte y dos capas del suelo en
  `build/PS2/floor-production-smoke-20261006-120848/report.json`.
  Para 1.000 comprobaciones de 4 KiB, el camino de bytes arbitrarios registra
  11.606.022 ciclos frente a 25.939.162 en el fixture anterior; es una medida
  de esa operación en el EE emulado, no de FPS del juego ni de hardware.
- La prueba del juego con USB nativo emulado también pasa en
  `build/PS2/usb-stack-checks/20261006-121059-666/language-game-check.json`:
  logo a 59,9 FPS, opciones y diálogo español, casa → Kokiri → casa, datos
  intactos, cierre del registro, lotes de 15 segundos y `transferBytes=4096`.
  Las dos capturas coinciden byte por byte con las de la revisión anterior
  de referencia. Se desactivan las tarjetas del jugador y no se compila
  simultáneamente durante estas comprobaciones. La mejora de FPS en la
  consola aún requiere una partida comparable.
- La versión se copia y verifica en `D:\OOTPS2`, con respaldo en
  `D:\OOTPS2-backup-20261006-121605`. Los registros físicos y la tarjeta del
  jugador conservan sus hashes. La comprobación conjunta del paquete,
  transferencias, capturas y archivos conservados queda en
  `build/PS2/hash-bulk-release-verification.json`.

## Registro automático, contador de FPS y envío de vértices — 2026-10-06

- El usuario confirma que las vistas grandes siguen ralentizándose. BOOT.LOG
  y DIAG.LOG del USB tienen los mismos hashes que el registro anterior de
  FLOOR v3; no constituyen una medición de los paquetes/caché de la nueva
  revisión. Se conserva otra copia privada antes de actualizar el USB.
- DIAG.ELF y DIAG224.ELF muestran los fotogramas presentados por segundo,
  con una ventana de aproximadamente un segundo. La estimación por retraces
  se señala con un asterisco si falla el reloj. La cuenta es independiente
  del logger: continúa después de alcanzar el límite del archivo o de fallar
  una escritura. La marca `FPS-HUD` tras la fecha del encabezado v3 identifica
  un log nuevo conservando el formato compatible de los lectores existentes.
- El indicador usa sprites sólidos agrupados, sin fuentes, lecturas USB ni
  reservas de textura. Se añade al framebuffer principal después del juego;
  conserva los estados de alpha/profundidad y el scissor, y no dibuja en el
  retrato del inventario. OOTPS2.ELF conserva la imagen sin ese contador.
- A petición del usuario, el juego USB habitual ahora crea DIAG.LOG al arrancar
  con la marca `AUTO-PLAY`, sin abrir el diagnóstico. Conserva muestras de FPS,
  escena/sala, carga, audio y dibujo. Tiene 256 registros en RAM, agrupa las
  escrituras cada 15 segundos, sin activar cronómetros por triángulo. DIAG conserva
  sus lotes de cinco segundos y el perfil cada 60 fotogramas. El límite de
  2 MiB y la parada del logger ante errores no detienen el juego; los costes
  de escritura se identifican en FLUSH. Esperar 20 segundos antes de apagar
  permite cerrar el siguiente lote. El historial anterior ya está conservado.
  `PS2_USB_AUTO_LOG=0` permite compilar una comparación sin escritura periódica.
- Los puntos construidos por el renderer usan una entrada interna con
  alineación explícita y registros uniformes. Evita revisar por segunda vez
  las etiquetas RGBAQ/ST/XYZ de cada vértice. Las entradas generales mantienen
  sus validaciones y el fallback del SDK para layouts mixtos o desalineados.
  No altera registros enviados, capas del suelo, Q, niebla, color ni geometría.
- La comparación EE de 24 fixtures contra el SDK y los 120 fotogramas de
  gráficos/audio pasan en
  `build/PS2/floor-production-smoke-20261006-000224/report.json`.
  En la operación de envío, el camino validado registra 655.700 ciclos y el
  interno 483.820; no es una medida de FPS general. El contador registra
  12.250 ciclos y 1.648 bytes en su fixture. Su captura comprueba texto visible
  y restauración del rechazo alpha. Pasan 60/30/20/12/8 FPS, pausas largas y
  reloj detenido con wraparound de retraces. Falta medir esta revisión en PS2.
- La prueba final con registro automático ligero pasa mediante USB nativo
  emulado, sin HostFs, tarjetas del jugador ni compilación simultánea:
  `build/PS2/usb-stack-checks/20261006-112018-371/language-game-check.json`.
  Comprueba logo a 59,9 FPS, opciones, diálogo español y casa → Kokiri → casa,
  con 120 actualizaciones en cada fase. Confirma lotes de 15 segundos y ausencia
  de registros de perfil por triángulo. Datos/idioma no cambian, el registro
  termina sin errores ni pérdidas y las ranuras de guardado de prueba se conservan.
  La captura de opciones coincide byte por byte con la anterior; en el diálogo
  sólo cambian 51 píxeles de los ojos de Link por el parpadeo de su animación.
- Al reconectar el USB, BOOT.LOG sí había cambiado pero DIAG.LOG seguía siendo
  el FLOOR v3 antiguo: la partida habitual anterior no registraba rendimiento.
  Se conserva la nueva lectura en
  `build/PS2/hardware-runtime/before-auto-light-20261006-112309`.
  Esa evidencia no permite atribuir todavía las caídas a USB, dibujo o audio;
  la siguiente partida con OOTPS2.ELF permitirá contrastarlo.
- Paquete final copiado y verificado en `D:/OOTPS2` (11 archivos), con copia
  de la versión anterior en `D:/OOTPS2-backup-20261006-113145`. El ELF habitual
  contiene `AUTO-PLAY`; los diagnósticos contienen `FPS-HUD`. Las direcciones
  estáticas están dentro de los 32 MiB del EE. Se conservan BOOT.LOG/DIAG.LOG
  físicos y el SHA256 de la tarjeta del jugador en PCSX2. Informe privado:
  `build/PS2/automatic-light-release-verification.json`.

## Paquetes GS compactos y caché USB de animaciones — 2026-10-05

- Los triángulos usan listas de registros GS en lugar de repetir la dirección
  A_D por cada atributo. Conservan los valores RGBAQ, ST, XYZ2/XYZF2 y los
  estados de textura, profundidad, niebla, mezcla y contexto. PRIM continúa
  como escritura explícita: PRE no aplica en REGLIST. Las entradas con
  alineación o registros incompatibles usan la función original del SDK.
- Dentro de cada grupo de triángulos se preparan TEX0/PRIM por capa una sola
  vez y TEX1 sólo cuando cambia el filtro. Cada comando de estado termina
  el grupo; una nueva subida de textura invalida su preparación. Las dos
  capas del suelo siguen alternándose por triángulo. No se reduce resolución,
  contenido ni calidad de las texturas.
- La comparación EE contra el SDK instalado decodifica ambas representaciones
  y comprueba todos los registros, incluidos Q y niebla. Cubre contextos,
  paletas, tamaños no potencia de dos, filtros y listas de 3/6/60 vértices,
  además de alineación arbitraria y cambio de bloque DMA. El conjunto ocupa
  16.000 frente a 28.800 bytes. Dos ejecuciones de 64 triángulos por grupo
  muestran 636.876 frente a 748.329 ciclos y 628.292 frente a 827.407 ciclos
  acumulados. Son medidas de esta operación en PCSX2, con posibles
  interrupciones; no son una medida del FPS físico ni de todo el render.
- USB conserva dieciséis bloques de juego de 16 KiB (256 KiB de datos), frente
  a los cuatro anteriores. Las lecturas pequeñas de animaciones también los
  reutilizan. El manejador de audio identifica su caché explícitamente:
  música y animaciones comparten la prioridad NORMAL de libultra, por lo que
  esa prioridad no permite distinguirlas. Sus 64 sectores de 4 KiB se conservan.
  El incremento estático es aproximadamente 193 KiB, sin ampliar el tamaño
  máximo de prelectura ni quitar la sincronización de la caché del EE.
- La prueba intercalada de 64 sectores de animación y 20 sectores de muestras
  necesita 16 lecturas del archivo en la primera pasada y ninguna en la
  segunda; las muestras siguen residentes. Comprueba expulsión LRU, cruces,
  final de archivo, guardas y dos lectores concurrentes. Las cargas nuevas
  de escenas grandes siguen limitadas por el USB de la PS2.
- Pasan los 120 fotogramas de gráficos/audio y la caché USB en
  `build/PS2/floor-production-smoke-20261005-232942/report.json`.
  El recorrido USB nativo casa → Kokiri → casa pasa en
  `build/PS2/usb-stack-checks/20261005-233611-477/language-game-check.json`:
  ambas capturas tienen exactamente los mismos SHA256 que la revisión anterior.
  El logo sigue a 59,9 FPS en esa prueba y los datos/idioma permanecen intactos.
  El perfil completo no establece todavía una ganancia general de FPS.
  Se recompilan los tres ELF USB y el ELF para PCSX2; sus rangos estáticos
  caben en los 32 MiB del EE. Queda comprobar esta revisión en la consola.

## Idioma, ritmo del logo y reutilización del suelo — 2026-10-05

- El mínimo de dos vblank sólo se elimina durante el estado del logo Nintendo
  64. El planificador sigue limitando los menús a 30 y el juego a 20. La prueba
  de presentación del logo real, por USB nativo emulado, mide 59,9 FPS tras
  30 fotogramas de calentamiento. No equivale a confirmar 60 FPS en consola.
- Se añade ESP.DAT privado con 2.114 mensajes y 380 recursos de textura de
  la traducción eduardo_aj2 v2.2 (Patcher64Plus-Tool). Se relocalizan por ID y
  descriptores compatibles, sin importar código o geometría N64. El archivo
  se carga una vez fuera del arena; sus lecturas de glifos y recursos cubiertos
  usan RAM. CRC32, límites, registros ordenados y anchuras se validan antes de
  usarlo. Si falta o es inválido se conservan las opciones y mensajes ingleses.
- LANGUAGE/IDIOMA se elige con izquierda/derecha en Opciones y se guarda con
  B en el encabezado SRAM. Las tablas nativas mantienen su índice occidental
  y las partidas conservan su formato. La fila usa palabras completas sans
  serif, tamaño y contorno uniformes, espaciado proporcional e IA8 (archivo
  versión 2); evita mezclar la fuente de diálogos con las opciones. El TTF del
  sistema queda fuera del repo y su hash se registra en el informe privado.
- Se enlazan segmentos explícitos para los vértices y etiquetas de esta fila:
  las direcciones EE de más de 16 MiB podrían confundirse con direcciones
  segmentadas N64 y dejar el texto en blanco. Los textos en código y créditos
  no están traducidos por completo; voces y sonido conservan los originales.
- El render prepara los periodos y las coordenadas del suelo una vez por
  vértice compartido de cada grupo de triángulos. Conserva la traslación de
  periodos completos confirmada en hardware, el orden de capas, perspectiva,
  niebla, profundidad y recorte. La comparación de puntos GS pasa, junto a
  las pruebas de audio y gráficos de 120 fotogramas. No se afirma que esto
  garantice 20 FPS en todas las vistas; falta medir la mejora física.
- DIAG.ELF muestra tiempos de los componentes del render cada 60 fotogramas.
  OOTPS2.ELF omite ese perfil detallado. El test nativo de idioma captura las
  opciones y el diálogo, comprueba el encabezado sin cambiar las partidas,
  y completa casa → Kokiri → casa con HostFs y tarjetas del jugador
  desactivados. Su logger cierra las escrituras FAT antes de terminar la
  prueba. Uso, créditos y límites en PS2_LANGUAGE.md y TESTING_PS2.md.
- Pasa la comprobación EE completa con datos de idioma:
  `build/PS2/floor-production-smoke-20261005-222748/report.json`.
  El recorrido USB con la tipografía final y sus capturas pasa en
  `build/PS2/usb-stack-checks/20261005-223443-071/language-game-check.json`.
  Se recompilan los tres ELF USB y el ELF para PCSX2. El paquete con ESP.DAT
  se copia y verifica por SHA256 en D:\OOTPS2 (once archivos); la versión
  anterior queda en D:\OOTPS2-backup-20261005-223751. OOT.Z64, los registros
  físicos y la tarjeta del jugador conservan sus hashes. La confirmación
  física del idioma, el ritmo del logo y la mejora de FPS sigue pendiente.

## Suelo confirmado en PS2 real e integrado al juego — 2026-10-05

- El usuario confirma que Arriba de FLOOR v3 elimina las olas del suelo
  al mover la cámara en su SCPH-90010. Se conservan BOOT.LOG, DIAG.LOG,
  el ELF probado, el manifiesto y sus hashes en
  `build/PS2/hardware-runtime/floor-v3-confirmed-20261005-184050`.
  El log identifica el ELF v3 confirmado, SHA-256
  `49c72707b78e58c3153910d0eabc15379cf44957f4a59ce898da06bb482f2cec`.
- Los 24 fotogramas muestreados con el ajuste activo reducen el máximo UV
  de 2222.5 a 411.188 texeles: 58 coordenadas fuera del rango ±2047 antes y
  ninguna después. Los 105 intervalos no contienen errores USB, pérdidas o
  NUL. El máximo de 248 ms por escritura pertenece al diagnóstico y no
  demuestra una mejora de FPS ni certifica otras zonas.
- Se integra el mismo cálculo en la segunda capa de los materiales de
  suelo del juego habitual. Es automático en OOTPS2.ELF, DIAG.ELF,
  DIAG224.ELF y la compilación para PCSX2; no requiere pulsar la cruceta.
  Mantiene escala, perspectiva, ambas texturas, mezcla, profundidad y
  niebla. La variante de comparación conserva sus modos.
- El cálculo permite omitir las medidas UV pasando NULL para el resultado.
  El juego habitual omite esas medidas y el registro de FLOOR. Las pruebas
  comparan sus puntos GS con los que incluyen mediciones: son idénticos.
  También pasan repetición, espejo, clamp, intervalos anchos, perspectiva,
  recorte, reutilización de vértices, mezcla y límites de estado junto al
  diagnóstico completo de 120 fotogramas sin activar el laboratorio:
  `build/PS2/floor-production-smoke-20261005-184649/report.json`.
- Pasa casa → Kokiri → casa con USB nativo emulado, 120 fotogramas por fase,
  HostFs desactivado y tarjetas del jugador desactivadas:
  `build/PS2/usb-stack-checks/20261005-184942-622/report.json`.
  Se recompilan los tres ELF USB habituales y el ELF para PCSX2, se verifica
  su memoria estática y la correspondencia de datos y se copian y verifican
  los nueve archivos del paquete en D:, con copia de los archivos anteriores
  en `D:\OOTPS2-backup-20261005-185331`. El ELF de comparación confirmado
  se conserva y se actualizan sus instrucciones. Su script de copia omite
  archivos idénticos y usa una nota neutra al conservar registros previos.
  La captura física, OOT.Z64 y la tarjeta del jugador se conservan.
  Informe final: `build/PS2/floor-production-final-verification.json`.
  La confirmación
  del suelo corresponde al ELF de comparación; queda seguir validando
  zonas, audio, rendimiento y guardado físico con el paquete habitual.

## Coordenadas repetidas del suelo en hardware — 2026-10-05

- El usuario confirma que FLOOR v2 conserva las mismas olas tanto arriba
  (profundidad de sólo lectura) como a la derecha (pasada anterior). Esa
  hipótesis no mejora la imagen. Se conservan BOOT.LOG, DIAG.LOG, manifiesto
  y hashes en `build/PS2/hardware-runtime/floor-v2-result-20261005-181656`.
  El análisis recoge 130 intervalos, cero errores USB, pérdidas o NUL y un
  máximo de 175 ms por escritura del logger. No representa un benchmark
  de FPS de una única escena.
- Las muestras de la segunda capa de Kokiri alcanzan unas 2058 unidades
  de texel para sus imágenes de 32 píxeles. El GS limita UV a ±2047 antes
  del wrap y reduce la mantisa de STQ. Centrar los 25 triángulos muestreados
  por repeticiones completas reduciría su máximo absoluto a unas 399
  unidades; todavía no demuestra que explique todas las olas. La dimensión
  de la segunda capa se infiere de sus recursos en v2: v3 la registra.
- FLOOR v3 recupera la profundidad original y centra sólo las UV de la
  segunda capa. Sustrae a todo el triángulo un mismo múltiplo del periodo,
  antes de formar STQ, preservando perspectiva y escalas. Usa copias de
  dibujo sin modificar los vértices compartidos, respeta el periodo espejo
  expandido y no desplaza ejes con clamp. No aplica módulo por vértice ni
  omite texturas. Los triángulos demasiado anchos siguen identificados como
  fuera de rango; no se afirma que la traslación pueda reducir su intervalo.
- Arriba selecciona UV centradas, izquierda conserva el control con una
  capa y derecha recupera las UV anteriores. El log FLOOR_LAB v3 añade
  dimensiones, máscaras, desplazamientos y máximos antes/después de todos
  los triángulos de un fotograma muestreado cada sesenta.
- Pasan repetición, espejo, clamp, intervalos anchos, equivalencia del
  interpolante, XYZ/Q invariantes, caminos inmediato/preparado, recorte,
  niebla y límites de estado. En los casos acotados del test que reproducen
  el truncado del GS, el error acumulado baja de 0.0197754 a 0.0012207 texeles.
  El diagnóstico completo presenta 120 fotogramas:
  `build/PS2/floor-v3-smoke-20261005-182820/report.json`.
- Pasa el arranque por USB nativo emulado, con HostFs y tarjetas del jugador
  desactivados: `build/PS2/usb-stack-checks/20261005-182927-438/report.json`.
  Se comprueba FLOOR_LAB v3 y las coordenadas centradas en DIAG.LOG.
  El paquete opcional se prepara para D: con hashes verificados; los ELF
  habituales siguen intactos. Falta comprobar esta revisión en la PS2 real.

## Comparación física de capas y segunda pasada de sólo lectura — 2026-10-05

- El usuario compara FLOOR v1 en la SCPH-90010: arriba y derecha conservan
  las mismas olas; izquierda las elimina pero pierde la segunda textura.
  El log físico confirma selecciones en escena 85 y perspectiva activa en
  los materiales muestreados. Sus 160 muestras no contienen errores USB,
  pérdidas, NUL ni reloj aproximado. Evidencia privada conservada con hashes
  en `build/PS2/hardware-runtime/floor-lab-result-20261005-175141`.
- FLOOR v2 mantiene las dos texturas y prueba una segunda pasada de sólo
  lectura de profundidad. Para receptores que comparan y escriben Z añade
  dos unidades en Z24 (una en Z16), saturadas al máximo. Conserva GEQUAL,
  coordenadas UV/XYZ, mezcla y niebla, y restaura ZMSK al terminar la capa.
  Arriba selecciona la revisión; derecha vuelve a la pasada anterior.
  Esta comparación permanece limitada al ELF opcional: falta confirmar en
  hardware si elimina las olas y conserva las ocultaciones.
- La prueba extendida verifica las dos capas en los caminos inmediato y
  preparado, vértices recortados, niebla, saturación del margen, decal sin
  escrituras y restauración real de los comandos ZBUF/TEST. Pasa junto al
  diagnóstico completo de 120 fotogramas:
  `build/PS2/floor-v2-smoke-20261005-180608/report.json`.
- Pasa el arranque con USB nativo emulado, sin HostFs ni memory cards del
  jugador: `build/PS2/usb-stack-checks/20261005-180608-550/report.json`.
  DIAG.LOG identifica FLOOR_LAB v2 y registra la segunda capa con comparación
  activa y escrituras desactivadas. Se prepara el paquete opcional v2 con
  los controles nuevos; el juego habitual conserva sus ejecutables.

## Comparación de capas y perspectiva del suelo — 2026-10-05

- El usuario confirma que las olas siguen igual con la revisión del recorte.
  Se conserva esa corrección matemática, pero no se considera solución del
  defecto observado en la SCPH-90010.
- FLOOR.ELF es una variante USB separada: arriba vuelve al dibujo normal,
  izquierda omite la segunda capa del suelo y derecha fuerza su perspectiva.
  La selección se aplica al principio del siguiente fotograma. Los controles
  se mantienen en menús, diálogos y transiciones; no sustituye callbacks de estado.
- El logger registra las selecciones con escena/fotograma y una muestra
  acotada UV/W/STQ/XYZ cada sesenta fotogramas que dibujan ese material.
  No añade trazas al juego habitual. No confirma una causa por sí solo.
- Las pruebas del renderer pasan el aislamiento de capas, ambos caminos
  de vértices, los límites de fotograma y los materiales ajenos, junto a los
  120 fotogramas del diagnóstico completo. Informe privado:
  `build/PS2/floor-lab-smoke-20261005-173936/report.json`.
- Pasa el arranque con el controlador USB nativo emulado, sin HostFs ni
  tarjetas del jugador: `build/PS2/usb-stack-checks/20261005-173936-172/report.json`.
  DIAG.LOG conserva el marcador FLOOR_LAB y muestras del suelo. Falta la
  comparación física de los modos; no se afirma que desaparezcan las olas.
- Se añaden scripts reproducibles de construcción, paquete y copia del
  diagnóstico opcional. La copia verifica OOT.Z64 y los hashes, conserva los
  registros físicos anteriores y mantiene los ELF habituales. Instrucciones
  en `PS2_FLOOR_DIAGNOSTIC.md`.

## Prueba física hasta la espada y precisión del recorte — 2026-10-05

- El usuario confirma que DIAG.ELF revisado inicia, carga la partida, sale de
  la casa y permite jugar hasta conseguir la espada en la SCPH-90010.
  BOOT.LOG y DIAG.LOG se conservan con hashes comprobados en
  `build/PS2/hardware-runtime/scph90010-first-sword-20261005-162143`.
  Las 300 muestras no registran errores USB, pérdidas ni reloj aproximado.
  Hay caídas de FPS y pausas de carga; un volcado del logger tarda hasta 184 ms.
- La descripción del suelo se precisa: parece hacer olas al mover la cámara,
  y no las observa con la cámara quieta. DIAG224 revisado también carga Kokiri,
  pero el usuario confirma que las olas siguen igual. Sus 65 muestras se
  conservan en `build/PS2/hardware-runtime/scph90010-224-waves-20261005-163739`,
  sin errores USB ni pérdidas. El modo de 224 líneas no se adopta como solución.
- Se encuentra una pérdida de precisión independiente en el recorte homogéneo:
  RSPVertex guardaba las UV interpoladas como s16, truncándolas de nuevo en cada
  plano. Ahora mantiene los valores en float, incluyendo las fracciones; los
  enteros 10.5 originales se cargan sin pérdida y la generación de reflejos
  conserva su cuantización anterior. No añade subdivisiones ni pasadas gráficas.
- La prueba nueva usa interpolación perspectiva sobre el triángulo original
  como referencia independiente. La versión anterior falla en 584 esquinas de
  128 cámaras, con error máximo de 1.874319 unidades UV crudas. La revisión
  ampliada pasa 256 cámaras y 1323 esquinas, con error máximo 0.004191.
  Pasan además 20432 casos de recorte, 27648 puntos y las pruebas de las dos
  capas del suelo, transparencia y perspectiva. Informe privado:
  `build/PS2/clip-camera-after-20261005-164931/report.json`.
  La comprobación física posterior mantiene las mismas olas; este cambio
  corrige el cálculo independiente, pero no resuelve ese defecto observado.
- Pasan los nueve casos de arranque con HostFs en
  `build/PS2/usb-host-checks/20261005-165704-330/report.json` y la transición
  casa → Kokiri → casa con el controlador USB emulado, sin HostFs, en
  `build/PS2/usb-stack-checks/20261005-165817-988/report.json`.
  El ELF del jugador de PCSX2, su memory card y los datos convertidos conservan
  sus hashes. Se preparan los tres ELF USB con la corrección de precisión.

## Salida de la casa con el diagnóstico USB — 2026-10-05

- El usuario confirma intro, selector y carga de la partida en la SCPH-90010;
  DIAG.ELF queda negro al salir de la casa de Link. Se conservan BOOT.LOG y
  DIAG.LOG, con hashes verificados, en
  `build/PS2/hardware-runtime/scph90010-house-exit-20261005-153103`.
  El registro y el audio siguen activos durante más de un minuto sin nuevos
  fotogramas, lecturas ni errores del USB. No era sólo una carga lenta.
- Una prueba automática con el controlador USB nativo reproduce el fin del
  hilo Graph tras destruir la casa. El wrapper de Play_Init cambiaba la
  dirección que compara Graph_GetNextGameState; Play_Update, definido en el
  mismo objeto que Play_Init, conservaba la dirección original. Al salir,
  Graph no encontraba el siguiente estado y terminaba. Evidencia previa en
  `build/PS2/usb-stack-checks/20261005-154512-304`.
- El diagnóstico ahora registra GameState_Init y conserva todas las
  direcciones originales de inicialización. Añade hitos de transición,
  actualización y destrucción, y una captura acotada de los hilos EE y de
  la espera gráfica cuando no se presentan fotogramas durante dos muestras.
  No escribe archivos desde los hilos del juego ni desde interrupciones.
- La prueba corregida completa 120 actualizaciones en cada parada de
  casa → bosque Kokiri → casa, usando fundidos, destrucción y cargas normales.
  Pasa sin HostFs, con tarjetas desactivadas, registros íntegros, sin pérdidas
  ni errores USB y datos con el mismo SHA256. Informe privado:
  `build/PS2/usb-stack-checks/20261005-155909-872/report.json`.
  Falta confirmar esta salida en la consola física.
- La prueba independiente se compila con
  `tools/build_ps2_usb_transition_smoke.sh` y se ejecuta con
  `tools/run_ps2_usb_stack_check.ps1 -Transition -Seconds 55`.
  Usa un guardado sintético y no se incluye en el paquete del jugador.
- Pasan también las nueve comprobaciones de arranque y logs mediante HostFs
  en `build/PS2/usb-host-checks/20261005-160634-489/report.json`, y el test
  del logger conserva los límites de escritura y comprueba que las capturas
  de espera se activan, se limitan y se reinician cuando vuelve un fotograma.
  OOTPS2.ELF conserva el SHA256 anterior; la corrección se aplica a los dos
  ELF del registro. Los datos convertidos no cambian.

## Escrituras del diagnóstico y prueba del controlador USB — 2026-10-05

- El segundo intento físico también queda negro, esta vez con DIAG.ELF y
  render normal de 448 líneas. BOOT.LOG termina en la etapa 45,
  `REGISTRO: iniciando hilo por vblank`; DIAG.LOG v2 contiene sólo su cabecera
  de 433 bytes. Se conservan ambos con hashes verificados en
  `build/PS2/hardware-runtime/scph90010-diag448-20261005-143535`.
- Se añade una prueba del ELF nativo con una imagen FAT32 y el dispositivo
  USB de PCSX2, sin HostFs. La versión anterior permite seguir ejecutando el
  juego en esa prueba, pero produce 288 errores de transferencia, 675 bytes
  nulos en DIAG.LOG y 61 en BOOT.LOG, además de sobrescribir la cabecera.
  Evidencia privada en `build/PS2/usb-stack-checks/20261005-144729-837`.
- DIAG.LOG v3 escribe sectores de 512 bytes desde un buffer alineado a
  64 bytes; también mantiene alineado el siguiente append. Los reintentos
  de escrituras parciales desplazan los datos dentro del buffer, conservando
  su dirección alineada. El límite de 2 MiB cuenta el relleno realmente
  escrito y reserva un sector para LOG_LIMIT.
- Corregir sólo DIAG.LOG elimina los errores de transferencia, pero aún
  permite que BOOT.LOG corrompa la cabecera vecina. BOOT.LOG v2 usa también
  un sector por línea. BOOTTEST lee hasta 8 KiB del final y elimina los
  espacios de relleno para mostrar diez etapas, conservando los logs v1.
  Se registran por separado reloj, semáforo, hilo y apertura/escritura/cierre
  del primer lote para acotar cualquier bloqueo físico restante.
- La prueba completa ya pasa en
  `build/PS2/usb-stack-checks/20261005-151055-633/report.json`: ambos registros
  íntegros, cero errores USB, 25 muestras sin pérdidas ni reloj aproximado,
  carga de Hyrule Field y fotogramas posteriores al logo. El SHA256 de los
  datos permanece intacto. El mayor volcado tarda 95 ms en ese emulador;
  no certifica el tiempo en hardware ni confirma todavía el arranque físico
  de esta revisión. El analizador marca cabeceras inválidas y bytes nulos.
- Las pruebas del logger pasan con buffers alineados, transferencias
  acotadas, escrituras parciales de 31 bytes y el límite exacto del archivo,
  además de desbordamiento del anillo, errores y preservación de interrupciones.
  La comparación del suelo con DIAG224 sigue aplazada hasta confirmar DIAG.ELF
  en la SCPH-90010.
- Pasan los nueve casos del paquete en
  `build/PS2/usb-host-checks/20261005-151810-528/report.json`, incluidos los logs
  de arranque antiguos y rellenos por sectores. La lectura del banco de pruebas
  comparte los archivos abiertos por Windows y reintenta bloqueos transitorios.
  DIAG.ELF probado con el controlador USB: SHA256
  `c6eb78f8578f43d6e808d297366325e2431d5cf93f6fd95c025b5220df68343a`.
- Los nueve archivos se copian y verifican en `D:\OOTPS2`, con respaldo en
  `D:\OOTPS2-backup-20261005-152225`. Se conservan los dos registros físicos,
  el ELF habitual de PCSX2 y la tarjeta del jugador, comprobados por SHA256.
  Los segmentos estáticos terminan en `0x011afc70` para OOTPS2.ELF y
  `0x011c2970` para DIAG.ELF, dentro de los 32 MiB del EE.

## Diagnóstico detenido tras el logo — 2026-10-05

- La primera prueba física de DIAG224 muestra el logo y después queda negra.
  Se conservan BOOT.LOG y DIAG.LOG del USB, con hashes verificados, en
  `build/PS2/hardware-runtime/scph90010-diag224-20261005-140823`.
  BOOT.LOG llega al primer fotograma del logo; DIAG.LOG sólo contiene su
  cabecera de 346 bytes. No hay datos para atribuirlo al modo de vídeo.
- El hilo del registro deja de usar usleep con retorno ignorado. Un semáforo
  de capacidad uno recibe una señal cada 60 llamadas del hilo de vblank,
  que no hace E/S de archivos. El estado inicial se guarda antes de arrancar
  el hilo y se registra también su entrada. Un fallo de semáforo o espera
  detiene el registro. Los bloques periódicos siguen cerrándose cada cinco
  segundos; los retraces permiten un volcado aunque el Timer 0 no avance.
- Las muestras incluyen retraces, clockMs y clockFallback. Cuando falta
  tiempo del Timer 0 se indica explícitamente el intervalo aproximado; no
  se certifican los tiempos gráficos obtenidos con un reloj detenido.
  El analizador reconoce un archivo con sólo cabecera, entrada del hilo,
  errores del logger y uso de ese tiempo aproximado.
- Las pruebas del logger pasan con saturación del semáforo, reloj detenido,
  fallo de espera y fallo al crear el semáforo, además de los casos anteriores.
  La siguiente prueba física debe usar DIAG.ELF a 448 líneas; se aplaza la
  comparación del suelo con DIAG224 hasta confirmar el diagnóstico normal.
- Pasan los ocho casos de arranque en
  `build/PS2/usb-host-checks/20261005-142346-592/report.json`. El diagnóstico
  normal registra entrada del hilo, diez muestras y carga de Hyrule Field,
  sin pérdidas, errores ni uso del reloj aproximado. Los volcados tardan
  como máximo 1 ms en PCSX2; esto no mide el USB físico. El ELF normal USB,
  el ELF habitual de PCSX2 y la tarjeta del jugador conservan sus hashes.
- Se verifica la copia actualizada en `D:\OOTPS2`, con respaldo en
  `D:\OOTPS2-backup-20261005-142621`, conservando BOOT.LOG y DIAG.LOG físicos.
  DIAG.ELF SHA256 `271cfe133d78b07c75f08706972df88265126c66a6162fa6dbb433545d18df31`;
  los diagnósticos terminan sus segmentos estáticos en `0x011c22b0`.

## Registro USB, cargas y comparación del suelo — 2026-10-05

- El usuario confirma que el juego inicia y permite jugar en su SCPH-90010
  después de la corrección de caché. Se conserva el BOOT.LOG físico en
  `build/PS2/hardware-boot/scph90010-first-gameplay`. Las pausas de carga se
  recuperan, el suelo tiembla incluso quieto y el audio falla con FPS bajos.
- DIAG.ELF registra en DIAG.LOG estados, cargas de salas, lecturas lentas o
  fallidas, FPS presentados, tiempos gráficos, síntesis y cola de audio.
  Los hilos del juego sólo añaden registros en RAM; otro hilo escribe y
  cierra bloques cada cinco segundos. Se limita a 2 MiB y registra su propio
  coste de escritura y las pérdidas de registros. Un error de escritura o
  llegar al límite detiene el registro, sin detener el juego.
- Los ELF USB amplían la caché de sectores de audio a 256 KiB y añaden una
  caché independiente de 64 KiB para lecturas anticipadas de 16 KiB. Evitan
  seeks redundantes y respetan EOF. Los buffers se alinean a 64 bytes para
  separar datos y metadatos durante DMA. La prueba de 64 KiB de cargas en
  trozos de 8 KiB necesita cuatro lecturas físicas, ninguna al repetir y
  conserva los sectores de audio. El audio USB reserva 60 ms en el IOP,
  frente a los 20 ms anteriores. Su mejora física queda pendiente.
- DIAG224.ELF compara un render 640x224 con líneas duplicadas, manteniendo
  la salida NTSC entrelazada de 448 líneas para el convertidor AV a HDMI.
  OOTPS2.ELF y DIAG.ELF conservan 640x448. Reduce detalle vertical; falta
  comprobar si cambia el temblor en hardware. No se da el suelo por corregido.
- Pasan el modelo de caché, las pruebas del logger (incluidos fallos,
  escrituras parciales, desbordamiento e interrupciones) y la suite nativa
  con las políticas USB de ROM/audio. El registro de esta última está en
  `build/PS2/usb-cache-smoke/runtime.log`. Pasan ocho casos de arranque en
  `build/PS2/usb-host-checks/20261005-005339-046/report.json`, incluidos ambos
  diagnósticos hasta la carga de una escena. DIAG224 confirma render 224,
  salida 448 y MagV=1; el normal confirma render/salida 448 y MagV=0.
  No se pierden registros; los volcados en PCSX2 tardan como máximo 1 ms.
- Se añade `tools/analyze_ps2_runtime_log.py` para resumir las sesiones sin
  jugar todo el juego. Los tiempos USB de PCSX2 no representan los del USB
  físico. El nuevo registro revela unos 30 FPS reales en el logo N64 pese
  a su configuración R_UPDATE_RATE=1; su objetivo de 60 FPS sigue pendiente.
- El paquete incluye los tres ELF del juego, BOOTTEST y sus datos/documentos.
  La copia evita reescribir los datos idénticos, respalda archivos sustituidos
  y conserva los logs adicionales del usuario. No modifica el ELF habitual
  de PCSX2 ni la tarjeta del jugador.
- Se copian y verifican nueve archivos en `D:\OOTPS2`, con respaldo en
  `D:\OOTPS2-backup-20261005-005608`. El BOOT.LOG físico conserva su hash.
  Los segmentos estáticos terminan en `0x011afa70` para OOTPS2 y
  `0x011c1eb0` para los diagnósticos, dentro de los 32 MiB del EE. Juego SHA256
  `4d8f97f2ee171d4c6f9699bd637964f7e700521551ccb0f41fa7dedd01baa8d1`.
  Informe de copia: `build/PS2/usb-copy-report.json`.

## Diagnóstico del arranque en consola — 2026-10-04

- El segundo intento en la consola termina en `[PS2 STATE] Init 0x486b60`.
  Ese símbolo corresponde a ConsoleLogo_Init, no al arranque del GS. Se
  conserva BOOT.LOG del USB en `build/PS2/hardware-boot/scph90010-before-cache`.
- Se sustituye la invalidación de toda la caché de datos por sincronización
  del rango solicitado. Conserva escrituras de CPU y los vecinos de líneas
  parciales. Las funciones se separan en ps2_cache.c; la versión anterior
  podía perder pilas y mensajes de otros hilos al cargar el segmento del logo.
  El registro aún no permite afirmar que sea la única causa del bloqueo.
- BOOTTEST añade una prueba de buffers vecinos/pila, alias físico, datos
  producidos por CPU e interrupciones. El modelo de caché CPU/RAM pasa y
  rechaza expresamente la invalidación global anterior. Pasan también los
  seis casos de arranque, incluido el logo y primer fotograma, en
  `build/PS2/usb-host-checks/20261004-234222-172/report.json`. El comportamiento
  de caché en consola queda pendiente de la nueva prueba.
- Se registran pasos de la arena y constructor del estado y de la reserva,
  DMA, matrices, vista y SRAM del logo, sólo en la variante USB. El logo
  conserva su configuración de 60 FPS (R_UPDATE_RATE=1), como pide el usuario.
- El paquete corregido se copia y verifica en `D:\OOTPS2`, conservando el
  BOOT.LOG anterior. Juego SHA256
  `d4c7c98dfa1a41ec0c0c31ab49fb718b0692ae90555d52bc8f4dabf39f0022ab`,
  segmentos estáticos hasta `0x0116e730`. Se respaldan los archivos sustituidos
  en `D:\OOTPS2-backup-20261004-234548`. El ELF habitual y la tarjeta del
  jugador conservan sus hashes.

- El usuario confirma `DATOS OK` en BOOTTEST en una SCPH-90010 conectada
  mediante convertidor AV a HDMI. OOTPS2.ELF queda negro. Está confirmada
  la lectura inicial de datos en hardware, pero no el arranque del juego
  ni la causa del bloqueo. Se registra en `BUGS_PS2.md`.
- La variante USB muestra la etapa desde la entrada de main y registra
  resultados de módulos, reset, memory card, vídeo, mando, audio, heap,
  vblank y apertura de datos. Conserva las etapas previas en RAM y crea
  `BOOT.LOG` junto a OOT.Z64 después de validarlo. Cierra cada escritura
  para conservar la última etapa al reiniciar. El texto de pantalla se
  desactiva antes de gsKit; el registro se limita a 96 etapas y sólo al
  primer fotograma, sin registrar todos los fotogramas del juego.
- BOOTTEST muestra las diez últimas líneas del registro anterior con
  lectura acotada y sin modificarlo. También informa si aún no existe.
  Los objetos de boot y osSyncPrintf son independientes en la variante
  USB; el ELF habitual de PCSX2 y la tarjeta del jugador no se sustituyen.
- Pasan seis comprobaciones en PCSX2: datos válidos/incorrectos, inicio
  del juego hasta el primer fotograma con BOOT.LOG completo y los dos
  arranques sin dispositivo USB. La prueba de BOOTTEST conserva el hash
  de un registro mayor que su buffer y muestra correctamente su final.
  Informe: `build/PS2/usb-host-checks/20261004-232202-361/report.json`.
- El nuevo juego USB termina sus segmentos estáticos en `0x0116e430`,
  dentro de la RAM del EE. Los datos convertidos no cambian. La prueba
  del nuevo diagnóstico en la consola real queda pendiente.
- Se copian y verifican siete archivos en `D:\OOTPS2`. El juego tiene
  SHA256 `ca2d317e3b01b6cbd3691745c58168e0183f9bb4c2540d81d3c680c491122040`.
  Los archivos sustituidos se conservan en
  `D:\OOTPS2-backup-20261004-232349`; informe `build/PS2/usb-copy-report.json`.

## Recorrido de salas y preparación USB — 2026-10-04

- El usuario da por resuelto el Árbol Deku y pide ampliar las pruebas y
  preparar después una prueba en PS2 real mediante USB, Free McBoot y
  wLaunchELF. Autoriza copiar el paquete terminado a la unidad D:.
- Se añade un ELF de pruebas independiente: selecciona las 388 salas de
  las listas originales convertidas, con una edad por escena. Carga y
  ejecuta al menos 45 fotogramas, comprueba actores y objetos, ejercita
  recepción de equipo/rupias y banderas de interruptores/cofres, y restaura
  inmediatamente el estado de prueba. Ambas tarjetas y el mando quedan
  desactivados. No sustituye progresión, puzles, combates ni puertas reales.
- Captura el GS en PPM 320x240, verifica longitud/hash y genera PNGs,
  hojas de contacto y una galería buscable. Las imágenes oscuras se marcan
  para revisar la cámara; no se certifica automáticamente fidelidad visual.
  Los informes parciales conservan FAIL y pueden reunirse con procedencia.
  El validador rechaza resultado ausente, excepción posterior al PASS,
  evento faltante y sala incorrecta.
- Un anclaje sintético activa un portal en el Templo del Fuego. Se bloquean
  peticiones de otras salas sólo en el ELF de pruebas. `DemoKankyo` oculta
  deliberadamente el fondo de `hiral_demo`; se reconoce esa condición con
  el buffer cargado, sin modificar la escena. La prueba aislada pasa.
- Se intentan las 388 salas: 387 completan carga, eventos y captura en las
  101 escenas. `men_room_2`, caso 201, produce una excepción en las rocas
  móviles. Se reproduce en una ejecución en frío; la segunda ruta está en
  un bloque todavía no descrito del conversor. Sigue pendiente y se anota
  en `BUGS_PS2.md`. La galería reunida queda en
  `build/PS2/room-gallery-20261004/index.html`, con estado FAIL.
- El arranque actual reinicia el IOP, perdiendo los controladores de USB
  del lanzador. La nueva variante USB incorpora USBD y USBHDFSD, los carga
  después del reset y busca los datos junto al ELF. Comprueba el tamaño
  y doce bloques con CRC32; muestra un error y detiene el arranque si
  faltan o no corresponden. No acepta una ROM original como datos del port.
- Se añade BOOTTEST, que comprueba datos y consulta la tarjeta del puerto 1
  sin escribirla ni formatearla. El paquete incorpora ELF, datos convertidos,
  SHA256, informe de direcciones estáticas del ELF e instrucciones. El juego
  termina sus segmentos estáticos en 0x01168570, dentro de los 32 MiB del EE;
  esto no certifica todas las reservas dinámicas en hardware.
- Pasan las comprobaciones locales de datos válidos, tamaño incorrecto,
  bloque alterado, arranque del juego por la ruta verificada y carga real
  de los dos módulos en el IOP de PCSX2 con error acotado por archivo USB
  ausente. La carga se comprueba en el diagnóstico y en el juego con sus
  módulos de sonido, tarjeta y mandos ya preparados. Primer informe:
  `build/PS2/usb-host-checks/20261004-223424-326/report.json`.
  La repetición final pasa los seis casos, incluido el ELF USB del juego:
  `build/PS2/usb-host-checks/20261004-225212-578/report.json`.
  Ninguna prueba valida un dispositivo USB físico ni una PS2 real.
- El paquete privado queda en `build/PS2/usb-ready/OOTPS2`. El ELF de juego
  tiene SHA256 `cd03ea959336f5d9ac9951823ed1afd35739302d7c450f4c4b27758e9d3c438b`;
  los datos conservan `034fcb6d0a95154d8f96b5b8e11711d5781e2e6950b3cc99b28c3de9a641a78f`.
  Se conserva el ELF habitual de PCSX2 y el guardado del jugador. La prueba
  en consola, el rendimiento adicional y la publicación en GitHub siguen
  pendientes; el paquete con recursos del juego queda fuera de Git.
- Se copian siete archivos a `D:\OOTPS2` en el USB FAT32 LUCBYN y se comparan
  sus SHA256 con los del paquete. Informe `build/PS2/usb-copy-report.json`.

## Posición, fondo y animación del icono — 2026-10-04

- El usuario confirma corregida la araña sobre el desnivel. En el navegador
  de la BIOS confirma el título y la Trifuerza, pero el modelo está demasiado
  bajo. Se prueba primero Y=-3; queda demasiado alto en la lista de guardados.
  Se establece Y=-1.5 como posición intermedia para la lista y el panel de
  copiar/eliminar. El fondo cambia a un degradado dorado.
- Se añaden poses alrededor del eje vertical y curvas de mezcla que suman
  uno. La variante de 16 poses (75024 bytes) pasa la decodificación y lectura
  completa con libmc, pero la BIOS la muestra como datos dañados. Se reduce
  a ocho poses (56208 bytes), que el usuario confirma correctamente visibles
  y girando. La causa de la incompatibilidad de la variante mayor no queda
  aislada; no se atribuye a un límite específico de tamaño o de poses.
- Tras esa confirmación, se reduce la velocidad de 1.0 a 0.35 y la intensidad
  de las luces y del ambiente para distinguir mejor el relieve y los biseles.
  Se conserva la geometría local de 96 triángulos y el hueco central.
- Se añade un instalador EE que sólo actualiza `icon.sys` y `triforce.icn`
  sobre una tarjeta ya formateada con una partida existente. No escribe SRAM
  ni formatea: compara ambos archivos completos, `save.dat` y `save.bak`,
  antes y después de la instalación. Primero se prueba sobre una copia de
  la tarjeta habitual con una configuración aislada. El PASS confirma los
  archivos del navegador y que los dos guardados permanecen idénticos.
- El lector independiente verifica las ocho poses, curvas, mezcla entre
  claves, continuidad del ciclo, altura constante, título, iluminación,
  textura y límites. El reporte registra la velocidad y conserva
  `bios_display_validated=false`: esa herramienta no observa la BIOS.
- Pasan las pruebas con el ajuste final: lectura de todos los bytes, dos
  escrituras alternas, recuperación de copia dañada, reparación del modelo
  truncado y de metadatos ausentes, conservación de SRAM ante error y recarga
  tras cerrar PCSX2. Registros nuevos:
  `build/PS2/pcsx2-icon-slow-card-write.log` y
  `build/PS2/pcsx2-icon-slow-card-cold.log`.
- La instalación sobre la tarjeta habitual pasa y confirma ambos archivos
  completos de la partida idénticos, incluidos sus encabezados. Registro:
  `build/PS2/pcsx2-icon-slow-install-player.log`. Se conservan los respaldos
  `oot-port-before-icon-rotation.ps2` y `oot_ps2-before-icon-rotation.elf`.
  El ELF normal se actualiza tras completar el enlace con 724 objetos;
  SHA-256 `13AC8E1E6E51AEAEB12C05FA69558C79008B8B526996D140BC04DC269D1BCEAA`.
  El usuario confirma también que la última velocidad e iluminación están bien.
  La prueba en PS2 física sigue pendiente. El directorio antiguo de diagnóstico
  `OOT_PS2_TEST` se detecta por separado y no se modifica en la tarjeta habitual.

## Icono de memory card y pruebas automáticas — 2026-10-04

- El usuario pide el nombre del juego y una Trifuerza 3D para el navegador
  de la memory card. El build genera `icon.sys` y `triforce.icn` desde
  `gTriforceVtx`/`gTriforceDL` de la extracción local: 96 triángulos, biseles,
  volumen y hueco central, con material dorado. El título usa Shift-JIS en
  dos líneas: «Legend of Zelda» / «Ocarina of Time». El modelo ocupa 39736
  bytes y los metadatos 964; el ELF normal enlaza 724 objetos. Los datos
  generados y vértices del juego permanecen en `build/`, fuera de Git.
- El backend instala el modelo antes de los metadatos, verifica su lectura
  completa y omite reescrituras si los bytes ya coinciden. Sólo opera sobre
  esos dos archivos; conserva los nombres, versión, checksums y revisiones
  de los guardados alternos. Un error del icono no desactiva SRAM.
- La prueba con libmc usa una tarjeta nueva aislada. Pasan dos escrituras,
  recarga de la última revisión, recuperación de una copia dañada, lectura
  de los dos archivos del navegador y persistencia al cerrar/reabrir PCSX2.
  También pasan reparación de un modelo truncado y de metadatos ausentes,
  conservación del guardado tras un fallo del icono e instalación sin cambios.
  El lector independiente comprueba geometría, normales, título, referencias,
  animación, textura y límites. La vista previa es un render del modelo;
  falta confirmar su escala e iluminación en la BIOS y en PS2 física.
- `tools/run_ps2_regression.ps1` reúne gráficos/audio/SRAM, memory card y
  Skulltulas, con un informe JSON, hashes y registros nuevos por ejecución.
  Rechaza PASS antiguos, detecta fallos/excepciones/tiempos agotados y cierra
  sólo los procesos de prueba identificados. Cada ejecución crea su tarjeta
  propia; la configuración de la partida queda excluida. El parámetro
  `-IncludeSceneSweep` agrega los 191 casos de entradas a las 101 escenas.
  No se declara comprobada la progresión completa ni la PS2 real.
- Sobre la araña de la sala 10: el registro de la partida mantiene suelo
  Y=720, plataforma Y=820, cilindros originales y masa normal de Link.
  El recorrido observado pasa fuera de los cilindros de contacto. Siete
  posiciones originales pasan bajada, aterrizaje, empuje y vuelta al techo.
  La prueba adicional reproduce la alternancia de bajada/regreso a la
  altura de la plataforma y confirma contacto frontal con respuesta al
  golpe (`frontalHit=1`, `pushAndResponse=1`). Ese diagnóstico no se consideró
  una corrección: el usuario confirma el problema antes y después de cruzar.
  Se añade un ajuste de IA exclusivo de PS2: ante una superficie más alta
  donde Link está apoyado, anticipa el siguiente paso descendente, cancela
  la velocidad hacia abajo y completa el regreso al techo antes de volver
  a bajar. No cambia el suelo, los daños ni los cilindros y no añade barreras.
  Las pruebas contrastan 9990 canales de pose con la extracción original,
  alturas 799/820, contacto frontal y regreso al techo. La comprobación
  visual en la partida queda pendiente.
  La prueba EE posterior pasa: mínimos Y=831.094 y Y=856.834, ambos por
  encima de la superficie más 32 unidades, con cuatro regresos al techo
  en cada caso; el contacto frontal sigue activando empuje y respuesta.
  Las siete posiciones a nivel del suelo siguen pasando. Registro:
  `build/PS2/pcsx2-skulltula-ledge-final.log`.
  Ejecución del comando completo con compilación:
  `build/PS2/regression/20261004-184640/report.json`.
  Registro de partida: `build/PS2/pcsx2-skulltula-live-final.log`.
- El juego normal con icono arranca en PCSX2: instala y verifica ambos
  archivos en `OOT_PS2`, carga los 32768 bytes de la revisión 41 y verifica
  una escritura posterior como revisión 42. La tarjeta anterior se conserva
  en `oot-port-before-memorycard-icon.ps2`. Su presentación en el navegador
  aún no está confirmada.
- La versión normal con el ajuste de retirada vuelve a arrancar y carga la
  revisión 42 sin reescribir los archivos del icono, que ya coinciden. Se
  conserva otra copia de la tarjeta en `oot-port-before-skulltula-ledge-fix.ps2`.
  SHA-256 del ELF abierto:
  `1CE8CD742CB0D52DBF390BA664A888B4EC088ACF7530848863DC4080CE75E03B`.

## Fundidos, huevos de Gohma, esmeralda y búho — 2026-10-04

- Los colores empaquetados `RRGGBBAA` de las transiciones se asignaban
  directamente a una unión de bytes RGBA. En el EE, little-endian, el
  blanco original `160,160,160,255` se convertía en `255,160,160,160`;
  al actualizar el alfa quedaba un fundido rosa. Un helper convierte
  explícitamente entre palabras empaquetadas y campos RGBA, sin cambiar
  el layout de las estructuras. Se aplica a Fade, Wipe, Circle y Triforce,
  manteniendo sus tiempos y el tono original. La prueba EE usa los setters
  reales: 48 casos y 7008 listas de dibujo de Fade, con colores, duraciones,
  velocidades y ambos sentidos de fundido; pasa sin diferencias.
- Los huevos de Gohma llaman a `func_80094E78` para desplazar una textura
  mediante `SETTILESIZE` de tamaño cero. La subida lo interpretaba como
  ausencia de texels. Cuando existe una máscara de repetición, se conserva
  el periodo de la imagen cargada. El diagnóstico reproduce los comandos
  del huevo y el wrap de 12 bits: antes fallan 124 de 128 casos; después
  pasan todos, incluyendo 32768 texels RGBA. Registros:
  `build/PS2/pcsx2-egg-scroll-before.log` y
  `build/PS2/pcsx2-visual-material-fixes-pass.log`.
- La esmeralda Kokiri combina una intensidad I4 de 64x64 con ruido I8 de
  32x32. Su alfa interpola ambas muestras; la ruta genérica usaba el mismo
  texel para las dos. Se añade esa combinación, con scroll independiente,
  shifts, tinte verde, factor LOD y alfa. Se conserva la imagen de ruido
  completa aunque su ventana de scroll sea 16x16. La caché comprueba ambas
  fuentes y orígenes, ignorando el padding. Pasan 114688 canales frente a
  un evaluador independiente, cambios de tintes, scrolls y datos.
- El cuello y cuerpo del búho repiten una imagen de 16x16 en una ventana
  de clamp de 640x192. El buffer anterior no podía expandir esa ventana y
  subía sólo 16x16 conservando un clamp de 640x192: el GS podía muestrear
  otras texturas en VRAM. Los dos buffers temporales admiten ahora esa
  expansión; el incremento total de RAM estática es 512 KiB. Pasan los
  122880 texels del patrón repetido, límites de clamp y reutilización.
- Pasan también las pruebas existentes de audio, sombras, transparencias,
  suelo, inventario, aura del título y Skulltula, con los 120 fotogramas del
  diagnóstico completo. Estas pruebas verifican campos, comandos y texels;
  la comprobación visual de los cuatro casos en la partida sigue pendiente.
  Las cinemáticas compiladas del Árbol Deku conservan sus 3448 bytes de
  campos mixtos verificados, en `build/PS2/check-visual-fixes-cutscenes.log`.
  Tras el guardado confirmado por el usuario se conserva la tarjeta en
  `oot-port-before-visual-fixes.ps2` y el ELF anterior en
  `oot_ps2-full-assets-native-actors-confirmed.elf`. La nueva versión visible
  carga la partida y el Campo de Hyrule sin excepciones. Los cambios no
  modifican los datos del juego ni su progresión.

## Recursos del mapa completo y reinicio de actores — 2026-10-04

- El perfil `full` del conversor incluye todos los segmentos de datos
  descritos por la extracción local: 101 escenas, 388 salas y 378 bancos
  de objetos, en 1013 segmentos. Añade 780 al perfil anterior, conservando
  sus direcciones VROM. Excluye el código N64 y sus overlays: el juego
  utiliza sus funciones compiladas para el EE.
- `check_ps2_full_assets.py` contrasta 49560 recursos de 67 tipos con las
  estructuras nativas y los bytes originales. Comprueba dependencias de
  objetos, salas, colisiones, rutas, headers alternos, esqueletos, animaciones,
  fondos JPEG y punteros. Los 233 segmentos anteriores y toda la imagen
  fuera de los nuevos rangos permanecen idénticos, incluido el audio.
  El control con la imagen previa, un manifiesto completo y checksum válido
  falla por un comando de escena sin convertir. Registros:
  `build/PS2/check-full-assets.log` y `build/PS2/check-full-before-control.log`.
- El diagnóstico enlaza un ELF independiente que llama a `Play_Init`,
  `GameState_Update` y `Play_Destroy`. Usa guardados de prueba y entradas
  reales; desactiva el mando y ambas memory cards. Selecciona la entrada
  base y la hora que corresponden a cada variante de escena y edad; no
  trata las entradas de noche o ruinas del mercado como entradas base.
- La primera pasada descubre errores TLB de cámara en la segunda carga
  de Barinade. Su variable estática de cámara seguía apuntando a una cámara
  del estado anterior. Los overlays compilados del EE permanecían residentes
  y no restablecían sus datos, a diferencia de una carga de overlay N64.
- El linker agrupa `.data` y `.bss` de los 426 overlays de actores residentes.
  Antes del arranque se conservan 391600 bytes de datos inicializados; la
  carga de un overlay sin clientes los restaura y borra su BSS. Mantiene el
  estado compartido mientras quedan actores vivos y respeta los overlays
  persistentes hasta cambiar de escena. Los actores internos no se reinician.
  Se corrige también el decremento de clientes al destruir actores residentes.
- La prueba EE modifica los datos y BSS reales de los 426 bancos antes de
  crear actores. Verifica la restauración de 390115 bytes de datos y 141375
  bytes de BSS, la conservación mientras hay clientes y la conservación de
  overlays persistentes hasta cambiar de escena. Pasa sin diferencias.
- La repetición de la sala de Barinade, con niño y adulto, pasa 60 fotogramas
  por entrada sin las excepciones de cámara. Registros:
  `build/PS2/pcsx2-scenes-before-actor-reset.log` y
  `build/PS2/pcsx2-barinade-reset-pass.log`.
- La pasada completa posterior supera 191 casos que cubren las 101 escenas,
  usando niño/adulto y día/noche según las variantes de las entradas. Cada
  caso completa 60 actualizaciones del estado real, sin excepciones del EE;
  se comprueban la escena esperada y la sala cargada. No aparecen fallos de
  textura ni vértices inválidos en las muestras gráficas del registro.
  Registros: `build/PS2/pcsx2-all-scenes-native-pass.log`,
  `build/PS2/check-scene-sweep-results.log` y `build/PS2/scene-sweep-results.json`.
  El juego normal enlaza 722 objetos; el diagnóstico añade sus wrappers
  sólo a su propio ELF. Las cuatro cinemáticas del árbol siguen coincidiendo
  con sus campos nativos en el ELF final:
  `build/PS2/check-full-game-actor-cutscenes.log`.
  Sigue pendiente la progresión completa y la comprobación en una PS2 real.

## Máscara del brillo de la ficha de Skulltula — 2026-10-04

- La ficha emplea ruido I8 de 32×32 con desplazamiento como TEXEL0 y una
  silueta I8 de 32×64 como TEXEL1. La ruta genérica usaba la primera fuente
  para ambas, convirtiendo el ruido en un rectángulo visible. La nueva ruta
  reconoce ese combinador y sus tiles, combina ambas fuentes y conserva las
  coordenadas y el clamp de la silueta. La caché incluye los desplazamientos,
  tintes, factor LOD y bytes de ambas texturas.
- La prueba EE compara 40960 canales con un evaluador independiente,
  cinco desplazamientos y constantes, 3785 texels transparentes, límites,
  fuentes con padding, coordenadas inmediatas/preparadas e invalidación al
  cambiar la máscara. Pasan también el suelo, inventario, aura del título,
  audio y los 120 fotogramas del diagnóstico completo. Registro:
  `build/PS2/pcsx2-token-flame-pass.log`.
- El usuario confirma que la ficha ya se ve recortada. La versión visible
  incorpora los datos completos; se conservan la imagen de 233 segmentos,
  el ELF confirmado del interior del árbol y una copia de la memory card
  tomada después del guardado, antes de este reinicio.

## Interior del Árbol Deku y preparación de vértices — 2026-10-04

- Tras confirmar el diálogo exterior, el registro muestra la entrada
  `0000`, escena 0. Su lista de salas se leía en el orden N64 y producía
  una carga desde VROM cero y fallos TLB. El perfil `play` ahora convierte
  `ydan`, sus doce salas, `ydan_boss` y sus dos salas. También añade seis
  dependencias de objetos descubiertas por las listas originales. La
  imagen pasa de 211 a 233 segmentos; conserva todos sus rangos VROM.
- `check_ps2_deku_rooms.py` contrasta 852 recursos nuevos con los campos
  de las estructuras C: comandos, listas de salas y actores, colisiones,
  geometría, esqueletos y animaciones. Comprueba límites y dependencias;
  las texturas conservan sus bytes. Toda la imagen fuera de los 22 nuevos
  segmentos permanece idéntica, incluidos los 211 anteriores y los bancos
  de audio. El control con los datos previos falla por vértices en orden
  incorrecto. También pasa de nuevo la comprobación de las salas de Kokiri.
- La preparación de puntos gráficos calculaba por vértice las dimensiones
  GS, shifts y offsets del tile, y consultaba otra vez el alfa del material.
  Ahora prepara esos valores una vez después de subir la textura, con un
  contexto independiente para cada capa del suelo. Se invalida ante todo
  comando de estado. Conserva el orden de las operaciones flotantes,
  perspectiva, profundidad, niebla y el orden de los pases.
- En el EE, 27648 puntos coinciden en sus 48 bytes con la ruta inmediata:
  dieciséis shifts, escalas positivas/negativas/cero, tamaños de respaldo,
  perspectiva alrededor de su umbral y alfa dinámico. En 32768 puntos,
  la preparación pasa de 12089934 a 6663507 ciclos. Esto mide esa operación,
  no el porcentaje de mejora de FPS del juego.
- Pasa el diagnóstico completo, incluidos los puntos y paquetes del suelo,
  recortes, decals, fundidos, audio, inventario, aura y 120 fotogramas. El
  ELF conserva las cuatro cinemáticas corregidas. Registros:
  `build/PS2/check-deku-rooms.log`, `build/PS2/pcsx2-point-setup-pass.log`
  y `build/PS2/check-deku-and-point-linked.log`. El usuario confirma que
  carga la sala principal y puede moverse, y que los FPS son más estables.
  El registro confirma escena 0, salas 0 y 1, y más de 3300 fotogramas sin
  las excepciones de la imagen anterior. Se conservan la
  imagen anterior en `oot-before-deku.z64`/`.json` y el ELF confirmado del
  diálogo exterior en `oot_ps2-deku-cinematics-confirmed.elf`. La progresión
  completa de la mazmorra y los 20 FPS estables siguen pendientes. A petición
  del usuario, la optimización adicional se aplaza para priorizar la
  conversión del resto del juego.

## Cinemáticas compiladas del Árbol Deku — 2026-10-04

- El usuario informa de un bloqueo al entrar a la zona exterior del árbol.
  El registro deja de producir fotogramas, pero el audio sigue funcionando.
  Esa sala de Kokiri ya estaba convertida. La conversión del interior se
  añadió después, como se detalla en el apartado anterior.
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
  `build/PS2/pcsx2-deku-native-commands-pass.log`. El usuario confirma que
  puede entrar a la zona exterior y que los diálogos continúan. Se conserva
  esa sesión en `build/PS2/pcsx2-deku-cinematics-game-confirmed.log` y su ELF
  en `build/PS2-ntsc12/oot_ps2-deku-cinematics-confirmed.elf`.
  Se conserva el ELF anterior como `oot_ps2-ee-memcpy-confirmed.elf` y una
  copia de la memory card previa al reinicio. No se modifica la imagen de
  recursos en esa primera corrección.
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
