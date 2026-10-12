# OoT PS2

Port nativo experimental de **The Legend of Zelda: Ocarina of Time** a
PlayStation 2, basado en [zeldaret/oot](https://github.com/zeldaret/oot).
El código del juego se compila para el Emotion Engine; las listas gráficas
N64 se traducen al Graphics Synthesizer y el audio se sintetiza con RSP HLE.

## Estado — 12 de octubre de 2026

El perfil completo convierte **101 escenas, 388 salas y 378 bancos de
objetos**. Eso permite cargar los recursos del mapa, pero aún no demuestra
que toda la historia, los combates y las cinemáticas funcionen correctamente.

- **PCSX2:** el usuario ha probado Kokiri, casas, tienda y Árbol Deku. Las
  pruebas automáticas cubren 191 entradas a escenas; 387 de las 388 salas
  completan la batería de carga, actualización y captura. El bloqueo conocido
  de una sala del Campo de Entrenamiento Gerudo está en [BUGS_PS2.md](BUGS_PS2.md).
- **PS2 real:** probado por el usuario en SCPH-90010 mediante USB y wLaunchELF:
  intro, menú, partida, salida de la casa y obtención de la espada. Las olas
  del suelo al mover la cámara están corregidas y confirmadas en hardware.
- **Imagen y audio:** funcionan iluminación, niebla, transparencias, título
  animado, retrato de equipamiento y objetos recibidos. La corrección del sol
  dividido en tres partes pasa una captura reproducible del castillo y
  49.152 comparaciones de canales; falta confirmar este cambio en PS2 real.
- **Rendimiento:** objetivo de 20 FPS en juego, 30 en el selector y 60 en el
  logo inicial. Hay vistas que alcanzan 20 y vistas grandes que bajan. Las
  tres cámaras fijas del castillo miden 18,84 / 14,70 / 19,98 FPS en PCSX2.
  No se anuncian 20 FPS estables ni una optimización sin una mejora medida.
- **Guardado y mando:** memory card con título e icono 3D animado, recuperación
  de guardados y DualShock 2. Pasan pruebas de tarjeta virtual. La vibración
  está implementada; falta verificarla físicamente.
- **Español:** selector English/Español mediante un archivo privado `ESP.DAT`;
  quedan algunos textos en inglés. Véase [PS2_LANGUAGE.md](PS2_LANGUAGE.md).

El [changelog](CHANGELOG_PS2_PORT.md) conserva los cambios y sus mediciones.
[TESTING_PS2.md](TESTING_PS2.md) explica las pruebas y sus límites.

## Datos y publicación

Se necesitan los datos extraídos de una copia propia **US NTSC 1.2**. Este
repositorio no incluye ROM, BIOS, paquetes de datos generados, tarjetas ni
partidas. El origen de las fuentes se conserva en [UPSTREAM.md](UPSTREAM.md)
y las licencias de los componentes en [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Los avisos de terceros y del código original no se sustituyen por una
licencia única del port.

El paquete USB habitual es privado. Se está preparando un ejecutable con
inicializadores externos y un generador local de `PS2DATA.DAT`: pasan pruebas
de reconstrucción entre dos enlaces distintos y de arranque/rechazo de datos
dañados. **La release binaria todavía está pendiente** de validación y de
resolver la distribución de los componentes enlazados.
[Procedimiento y pendientes](PUBLISHING_PS2.md).

## Compilar desde los datos locales

Requiere Linux/WSL, Python 3.10+, PS2SDK/gsKit y un checkout de zeldaret/oot
en la revisión `1bef952ff61a6dd1945c7887c1babd94efe95f72`. Sigue sus instrucciones
para instalar dependencias y ejecutar `make setup VERSION=ntsc-1.2` con tu
ROM en `baseroms/ntsc-1.2/`. Su SHA1 normalizado debe ser
`41b3bdc48d98c48529219919015a1af22f5057c2`.

Con la extracción preparada, desde este repositorio:

```sh
export PS2DEV="$HOME/oot-ps2-tools/ps2dev"
export OOT_UPSTREAM="$HOME/oot-ps2-tools/oot-upstream"
source tools/ps2_env.sh
python3 tools/prepare_ps2_includes.py --output build/PS2-ntsc12/generated
bash tools/build_ps2.sh OOT_VERSION=NTSC_1_2 BUILD_DIR=build/PS2-ntsc12 \
  OOT_UPSTREAM="$OOT_UPSTREAM" TARGET=oot_ps2.elf -j2
"$OOT_UPSTREAM/.venv/bin/python" tools/prepare_ps2_rom.py \
  --upstream "$OOT_UPSTREAM" --profile full --output build/PS2-ntsc12/oot.z64
```

El ELF y los datos convertidos quedan en `build/PS2-ntsc12/`. Para PS2 real,
consulta [PS2_HARDWARE.md](PS2_HARDWARE.md); el paquete necesita preparación
adicional y un USB FAT32. No ejecutes los conversores sobre la única copia de
una partida o de la tarjeta.

## Pruebas de desarrollo

```sh
PS2_SMOKE_USB_CACHE=1 bash tools/build_ps2_smoke.sh
PS2_PERF_SCENE_MODE=castle_gate bash tools/build_ps2_perf_scene.sh
```

Los scripts de PowerShell en `tools/` usan configuraciones aisladas de PCSX2
con una BIOS local. Las baterías de escenas/rendimiento desactivan las tarjetas
del jugador y usan partidas sintéticas. Las candidatas de optimización
permanecen desactivadas hasta comprobar corrección y ganancia de rendimiento.

## Controles

| N64 | DualShock 2 |
|---|---|
| A | Cruz |
| B | Cuadrado |
| Z | L2 |
| Start | Start |
| D-Pad | Cruceta |
| L / R | L1 / R1 |
| Botones C | Stick derecho |
| C-Up (respaldo) | Triángulo |
| C-Down (respaldo) | Círculo |
| C-Right (respaldo) | R2 |
| Joystick | Stick izquierdo |
