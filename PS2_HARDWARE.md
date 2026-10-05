# Primera prueba en PS2 real

Paquete privado para arrancar desde un USB FAT32 mediante Free McBoot y
wLaunchELF. Aún necesita comprobarse en una consola real. Los resultados de
PCSX2 no certifican el comportamiento en hardware.

Primera prueba del usuario: en una SCPH-90010, conectada mediante un
convertidor AV a HDMI, BOOTTEST muestra `DATOS OK`. OOTPS2.ELF queda negro.
La lectura inicial del USB está confirmada; la causa del bloqueo y el
funcionamiento del juego en esa consola siguen pendientes.

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
Todavía necesita la comprobación en hardware. Su ritmo sigue configurado con
`R_UPDATE_RATE = 1`, una actualización por retrace NTSC, para el logo a 60 FPS.

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
