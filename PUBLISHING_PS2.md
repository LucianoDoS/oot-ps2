# Publicación del port

Repositorio: [LucianoDoS/oot-ps2](https://github.com/LucianoDoS/oot-ps2).
Se publican fuentes experimentales con los avisos de sus autores. Esto no
atribuye una licencia nueva al código original del juego ni a los terceros.

## Ejecutables y datos

El paquete de `tools/package_ps2_usb.py` es privado: contiene datos generados
a partir de una copia local del juego. No debe subirse como release.
El ELF habitual también contiene inicializadores del juego; separar sólo la
ROM convertida no basta para preparar un ejecutable público.

`PS2_EXTERNAL_ASSETS=1` usa un icono geométrico generado matemáticamente y un
catálogo de 16 KiB dentro del ELF. `tools/prepare_ps2_external_assets.py`
extrae los intervalos exactos a `PS2DATA.DAT`, pone esos bytes a cero en el
ELF y produce `native-assets.json` con identidades, direcciones, tamaños,
relocaciones y CRC, sin los contenidos de los recursos.

El catálogo de destinos pertenece al ejecutable. El DAT no proporciona
direcciones. El cargador valida cabecera, tamaño, EOF, CRC global y todos
los bloques antes de copiar nada; después sincroniza la caché. La carga
ocurre antes de conservar los datos iniciales de los actores.

La referencia probada contiene 223 recursos y 260.948 bytes. Para generar
datos compatibles con un ELF publicado, el generador lee las relocaciones
de los objetos MIPS de la compilación local y modifica sólo las palabras
marcadas. No interpreta números enteros como posibles punteros.
Se validan identidades, tamaños, offsets, contenidos finales y CRC del
catálogo del release; una versión o un ABI diferente debe fallar claramente.

## Generar datos privados para una futura release

Esta receta requiere la misma revisión de fuentes que el release, PS2SDK,
la extracción local **US NTSC 1.2** y el upstream indicado en `UPSTREAM.md`.
El SHA1 normalizado de la ROM es `41b3bdc48d98c48529219919015a1af22f5057c2`.
No descarga datos del juego. Los archivos `release/OOTPS2.ELF` y
`release/native-assets.json` deberán proceder del mismo paquete público.

```sh
export OOT_UPSTREAM="$HOME/oot-ps2-tools/oot-upstream"
python3 tools/prepare_ps2_includes.py --output build/PS2-ntsc12/generated
bash tools/build_ps2.sh OOT_VERSION=NTSC_1_2 BUILD_DIR=build/PS2-ntsc12 \
  OOT_UPSTREAM="$OOT_UPSTREAM" TARGET=oot_ps2.elf -j2
python3 tools/ps2_release_relocations.py \
  --metadata release/native-assets.json --release-elf release/OOTPS2.ELF \
  --reference build/PS2-ntsc12/oot_ps2.elf \
  --generated build/PS2-ntsc12/generated --output private/PS2DATA.DAT
"$OOT_UPSTREAM/.venv/bin/python" tools/prepare_ps2_rom.py \
  --upstream "$OOT_UPSTREAM" --profile full --output private/OOT.Z64
```

Para jugar, los dos archivos privados deberán estar junto al ELF USB.
`ESP.DAT` es opcional y se genera por separado según `PS2_LANGUAGE.md`.
Ninguno de esos tres archivos se publica.

## Comprobaciones realizadas

- 32 casos del cargador C compilado en host: datos válidos, corrupción,
  truncado, destinos inválidos, solapamientos y fallo de asignación; ningún
  error escribe parcialmente los recursos.
- Reconstrucción desde dos ELF de disposición diferente: 165 relocaciones,
  156 destinos cambian y el DAT resultante permanece byte a byte idéntico.
  Tres metadatos alterados se rechazan.
- PCSX2 con HostFs: datos válidos alcanzan el logo inicial; ausentes,
  corruptos y truncados se rechazan antes de dibujar el juego.
- Los mismos cuatro casos pasan con el controlador USB nativo emulado y
  una imagen FAT32 privada. Los datos de la imagen no cambian y la tarjeta
  del jugador conserva su hash.

Son pruebas de arranque y rechazo de datos; no verifican todavía las
transiciones de una partida ni el nuevo cargador en una PS2 real.

Para repetir el arranque tras preparar una referencia privada:

```powershell
./tools/run_ps2_external_assets_check.ps1 -Prepared build/PS2/external-prepared
# Sólo para una referencia compilada sin PS2_USB_HOST_TEST:
./tools/run_ps2_external_assets_check.ps1 -Prepared build/PS2/external-native-prepared -NativeUSB
```

## Pendientes de la release binaria

Quedan las transiciones y la prueba física del cargador externo, el paquete
público con hashes/fuentes correspondientes y la revisión de las licencias
de los objetos enlazados. El audio HLE tiene GPL2 o posterior; la captura
adaptada del SDK y varias bibliotecas tienen AFL2; audsrv tiene avisos LGPL2.
Conservar los textos o cambiar el nombre de la licencia no resuelve por sí
solo las condiciones de distribución del ejecutable combinado.
[Procedencia y avisos](THIRD_PARTY_NOTICES.md).

Hasta resolverlo no se sube el ELF habitual ni el DAT privado. La auditoría
de inicializadores no certifica todos los derechos de las fuentes del juego.
ROM, BIOS, datos extraídos, tarjetas, partidas y registros quedan excluidos.
