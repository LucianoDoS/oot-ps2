# oot-ps2 Web Builder

Interfaz web minimalista para empaquetar tu ROM legal de *The Legend of Zelda: Ocarina of Time* (`.z64`, `.n64`, `.v64`) con el motor de PlayStation 2 de `lucianodos/oot-ps2`.

## Estructura del proyecto
- `index.html`: Interfaz minimalista con soporte para tema claro/oscuro (`Oscuro`).
- `styles.css`: Estilos limpios y responsivos.
- `app.js`: Conversor de endianness (`.n64`/`.v64` a `.z64`), escáner de `dmadata`, validador de cabecera MIPS R5900 `.ELF` y generador de imagen `.ISO` (ISO9660 con `SYSTEM.CNF`, `OOT_PS2.ELF` y `ROM.Z64`).
- `.github/workflows/build-ps2.yml`: Workflow de GitHub Actions para compilar `oot-ps2.elf` automáticamente con `ps2dev/ps2dev`.
- `vercel.json`: Configuración lista para desplegar en Vercel.
