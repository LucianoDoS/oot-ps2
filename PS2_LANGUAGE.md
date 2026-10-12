# Port language

In the file select screen, open **Options / Opciones**. Move down to
**LANGUAGE / IDIOMA** and use left or right to choose **English** or
**Español**. Press B to return: the selection is saved alongside the sound
and targeting options. It does not change the save format or inventory items.

The translation requires `ESP.DAT` alongside the converted `OOT.Z64` data.
The private USB package includes it. If it is missing or fails validation,
the game retains English and the original options. In PCSX2 it must be next
to `oot.z64` and the ELF. It is loaded into RAM once; translated glyphs and
dialogue do not cause a USB read for each letter.

The new row uses a sans serif menu font with uniform size and outline,
proportional spacing and IA8 format, rather than the dialogue font.
The language file is version 2; the runtime rejects the previous version,
which used I8 labels, to avoid interpreting them with the wrong format.

The adaptation contains 2,114 messages, the font with Spanish characters
and 380 texture resources, including menus and names. It retains the port's
native code, models, collision and audio. Voices retain the original audio.
It is not considered a complete translation: some text written in code and
the credits remain in English; missing identifiers retain their original message.

The data comes from the **eduardo_aj2 v2.2** translation, available through
the `es.ppf` patch in [Patcher64Plus-Tool](https://github.com/Admentus64/Patcher64Plus-Tool).
The language file, patches and ROMs used to generate it are private build
data and are not included in Git.

Regenerating the file requires your own decompressed NTSC 1.0 copies,
original and translated, the original NTSC 1.2 extraction and the port's
converted data manifest. Using the Python environment for extraction:

```sh
python tools/prepare_ps2_spanish.py \
  --original /ruta/ntsc10-original.z64 \
  --translated /ruta/ntsc10-espanol.z64 \
  --upstream /ruta/oot-upstream \
  --menu-font /ruta/fuente-sans-serif-bold.ttf
```

The generation environment requires Pillow. The system font is only used
to rasterize four labels; its TTF file is not copied to the port or Git.
The report records its SHA256 to reproduce the appearance of the row.

The tool imports messages by identifier and requires each texture's name,
segment, offset, size, dimensions and format to match in the descriptors
for both revisions. The `ESP.json` report records hashes, resources and the
compatible converted image. The packager checks that relationship before
copying. OOT.Z64 must not be replaced with a patched ROM.

The EE tests cover all records, partial reads, neighboring memory, the font
and switching back to English. A separate test exercises the actual logo,
option selection, SRAM header writes/reads, translated dialogue and
house → Kokiri → house, with the player's cards disabled.
Language persistence on a physical card still requires verification.
