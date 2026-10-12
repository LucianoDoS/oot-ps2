# Procedencia y licencias

Este repositorio reúne el port aportado por el usuario, cambios de plataforma,
fuentes de la reconstrucción de OoT y componentes de terceros. No existe una
licencia única que cambie los derechos de todos esos archivos.

- La base de reconstrucción procede de [zeldaret/oot](https://github.com/zeldaret/oot),
  revisión `1bef952ff61a6dd1945c7887c1babd94efe95f72`. Véase `UPSTREAM.md`.
  No se atribuye una licencia propia al código original del juego.
- `src/platform/ps2/audio/rsp_hle` contiene código de Mupen64Plus RSP HLE.
  Sus cabeceras indican GPL versión 2 o posterior; se conserva el texto y
  la lista de autores en `LICENSES` dentro de ese directorio.
- La adaptación de captura gráfica conserva el aviso AFL 2.0 de PS2SDK en
  `src/platform/ps2/gfx/ps2_frame_capture.c` y el texto en
  `src/platform/ps2/gfx/LICENSE.ps2sdk`.
- El controlador IOP audsrv procede de PS2SDK, revisión
  `c318b136294ed225863b580e115988b05751cf59`. Sus [cabeceras originales](https://github.com/ps2dev/ps2sdk/blob/c318b136294ed225863b580e115988b05751cf59/iop/sound/audsrv/src/audsrv.c)
  indican GNU Library General Public License versión 2. Se conservan esos
  avisos, `src/platform/ps2/iop/audsrv/UPSTREAM.md` y el texto en
  `src/platform/ps2/iop/audsrv/LGPL-2.0.txt`. El archivo `LICENSE` de ese
  directorio conserva además la licencia general AFL 2.0 del SDK; no
  sustituye los avisos específicos de cada fuente.
- La cadena de herramientas y bibliotecas externas no se incluyen. El texto
  publicado por [PS2SDK](https://github.com/ps2dev/ps2sdk/blob/master/LICENSE)
  identifica su licencia AFL 2.0. La [lista de compatibilidad de GNU](https://www.gnu.org/licenses/license-list.html#AcademicFreeLicense)
  clasifica AFL como incompatible con GPL. La distribución del binario
  combinado requiere resolver esa compatibilidad; no basta cambiar el nombre
  de la licencia del port.

Los identificadores SPDX de herramientas nuevas se aplican sólo a esos
archivos. No conceden derechos sobre ROM, gráficos, modelos, textos, música,
microcódigo o marcas del juego. Los datos extraídos permanecen locales.
Las fuentes importadas de la reconstrucción contienen también algunos datos
en línea, como las fuentes de depuración y una máscara de sombra. Su
procedencia es la misma que la del código del juego: publicar las fuentes
no equivale a atribuirles una licencia nueva ni a afirmar que todo el árbol
carece de datos gráficos originales.
