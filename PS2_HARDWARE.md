# Primera prueba en PS2 real

Paquete privado para arrancar desde un USB FAT32 mediante Free McBoot y
wLaunchELF. Aún necesita comprobarse en una consola real. Los resultados de
PCSX2 no certifican el comportamiento en hardware.

1. Copia la carpeta `OOTPS2` completa a la raíz del USB.
2. En wLaunchELF abre `mass:/OOTPS2/BOOTTEST.ELF`.
3. Espera el resultado. Debe aparecer `DATOS OK` y el estado de la memory card
   del puerto 1. El diagnóstico sólo consulta la tarjeta; no guarda ni formatea.
4. Reinicia la consola, vuelve a wLaunchELF y abre
   `mass:/OOTPS2/OOTPS2.ELF`.
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
inicia la partida. Reinicia para regresar al lanzador. Si se queda negro antes
de ese mensaje, anota el último texto visible en BOOTTEST y el modelo/región
de la consola. También interesa saber si falla con otro USB compatible.

El informe `package.json` verifica las direcciones estáticas del ELF dentro
de los 32 MiB del EE. El espacio restante no garantiza por sí solo que todas
las reservas dinámicas, pilas, IOP y VRAM funcionen en hardware.

La comprobación del mando, audio, vídeo y guardado se hace jugando: BOOTTEST
no los certifica. Ninguna herramienta copia una partida de PCSX2 a la tarjeta
real automáticamente. La carpeta privada incluye recursos del juego y no
debe subirse a GitHub; se publicarán sólo código y herramientas cuando se
prepare esa fase.

Referencia técnica de los controladores: [ps2sdk USBHDFSD](https://github.com/ps2dev/ps2sdk/tree/master/iop/usb/usbhdfsd).
