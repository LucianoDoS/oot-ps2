// Configuración del repositorio GitHub de LucianoDoS
const GITHUB_REPO = "lucianodos/oot-ps2";
const CANDIDATE_ELF_URLS = [
  "./oot-ps2.elf",
  `https://github.com/${GITHUB_REPO}/releases/latest/download/oot-ps2.elf`,
  `https://raw.githubusercontent.com/${GITHUB_REPO}/main/oot-ps2.elf`,
  `https://raw.githubusercontent.com/${GITHUB_REPO}/master/oot-ps2.elf`,
  `https://raw.githubusercontent.com/${GITHUB_REPO}/main/build/oot-ps2.elf`
];

const WORKFLOW_YAML = `name: Build OOT PS2 ELF

on:
  push:
    branches: [ main, master ]
  workflow_dispatch:

jobs:
  build:
    runs-on: ubuntu-latest
    container: ps2dev/ps2dev:latest
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4

      - name: Compile oot-ps2.elf
        run: |
          apk add --no-cache make python3 git
          make -j$(nproc)

      - name: Upload Release ELF
        uses: softprops/action-gh-release@v2
        if: startsWith(github.ref, 'refs/tags/')
        with:
          files: oot-ps2.elf
`;

let state = {
  romBytes: null,
  romMeta: null,
  elfBytes: null,
  elfMeta: null
};

// Elementos DOM
const htmlEl = document.documentElement;
const themeBtn = document.getElementById("themeBtn");
const workflowBtn = document.getElementById("workflowBtn");
const workflowModal = document.getElementById("workflowModal");
const closeModalBtn = document.getElementById("closeModalBtn");
const workflowCode = document.getElementById("workflowCode");
const copyWorkflowBtn = document.getElementById("copyWorkflowBtn");

const repoDot = document.getElementById("repoDot");
const engineStatus = document.getElementById("engineStatus");
const elfInput = document.getElementById("elfInput");

const dropzone = document.getElementById("dropzone");
const romInput = document.getElementById("romInput");
const dropzoneEmpty = document.getElementById("dropzoneEmpty");
const romSummary = document.getElementById("romSummary");
const changeRomBtn = document.getElementById("changeRomBtn");

const romFormatBadge = document.getElementById("romFormatBadge");
const romFileName = document.getElementById("romFileName");
const romTitle = document.getElementById("romTitle");
const romGameId = document.getElementById("romGameId");
const romCrc = document.getElementById("romCrc");
const romDma = document.getElementById("romDma");

const progressWrap = document.getElementById("progressWrap");
const progressText = document.getElementById("progressText");
const progressPct = document.getElementById("progressPct");
const progressFill = document.getElementById("progressFill");
const alertBox = document.getElementById("alertBox");

const buildIsoBtn = document.getElementById("buildIsoBtn");
const buildElfBtn = document.getElementById("buildElfBtn");

// Tema del sistema / manual ("Oscuro")
function initTheme() {
  const saved = localStorage.getItem("oot_ps2_theme");
  if (saved === "light") {
    htmlEl.classList.remove("dark");
  } else if (saved === "dark") {
    htmlEl.classList.add("dark");
  } else if (window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches) {
    htmlEl.classList.remove("dark");
  } else {
    htmlEl.classList.add("dark");
  }
}

themeBtn.addEventListener("click", () => {
  htmlEl.classList.toggle("dark");
  localStorage.setItem("oot_ps2_theme", htmlEl.classList.contains("dark") ? "dark" : "light");
});

// Modal GitHub Action
workflowCode.textContent = WORKFLOW_YAML;
workflowBtn.addEventListener("click", () => workflowModal.classList.remove("hidden"));
closeModalBtn.addEventListener("click", () => workflowModal.classList.add("hidden"));
copyWorkflowBtn.addEventListener("click", async () => {
  await navigator.clipboard.writeText(WORKFLOW_YAML);
  copyWorkflowBtn.textContent = "Copiado";
  setTimeout(() => (copyWorkflowBtn.textContent = "Copiar YAML"), 1500);
});

function showAlert(msg) {
  alertBox.textContent = msg;
  alertBox.classList.remove("hidden");
}

function hideAlert() {
  alertBox.classList.add("hidden");
}

function setProgress(text, pct) {
  progressWrap.classList.remove("hidden");
  progressText.textContent = text;
  progressPct.textContent = `${Math.round(pct)}%`;
  progressFill.style.width = `${Math.min(100, Math.max(0, pct))}%`;
}

function updateButtons() {
  const ready = Boolean(state.romBytes && state.elfBytes);
  buildIsoBtn.disabled = !ready;
  buildElfBtn.disabled = !ready;
}

// Validación estricta de ejecutable MIPS R5900 (PS2)
function parseAndValidatePs2Elf(u8) {
  if (u8.byteLength < 52) {
    throw new Error("El archivo .ELF es demasiado pequeño.");
  }
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  const magic = dv.getUint32(0, false);
  if (magic !== 0x7f454c46) {
    throw new Error("Cabecera ELF inválida (no empieza con 0x7F 'ELF').");
  }
  const eiClass = u8[4]; // 1 = 32-bit
  const eiData = u8[5];  // 1 = Little-Endian
  const eMachine = dv.getUint16(18, true); // 8 = MIPS
  const eEntry = dv.getUint32(24, true);

  if (eiClass !== 1 || eiData !== 1 || eMachine !== 8) {
    throw new Error("El ELF no corresponde a un binario MIPS32 Little-Endian (PS2 R5900).");
  }
  if ((eEntry & 0x3) !== 0 || eEntry < 0x00080000) {
    throw new Error(`Entry point desalineado o inválido: 0x${eEntry.toString(16)}`);
  }

  return { entry: eEntry, size: u8.byteLength };
}

// Búsqueda automática del motor compilado en lucianodos/oot-ps2
async function autoFetchEngineElf() {
  for (const url of CANDIDATE_ELF_URLS) {
    try {
      const res = await fetch(url, { cache: "no-store" });
      if (!res.ok) continue;
      const buf = await res.arrayBuffer();
      const u8 = new Uint8Array(buf);
      const meta = parseAndValidatePs2Elf(u8);
      state.elfBytes = u8;
      state.elfMeta = meta;
      repoDot.classList.add("online");
      engineStatus.textContent = `oot-ps2.elf listo (Entry 0x${meta.entry.toString(16).toUpperCase()})`;
      hideAlert();
      updateButtons();
      return;
    } catch (_) {
      // Continúa probando la siguiente ruta candidata
    }
  }

  // Si el repositorio aún no tiene el binario compilado publicado, informar claramente
  engineStatus.textContent = "Pendiente de oot-ps2.elf compilado (sube tu .ELF o activa GitHub Action)";
  showAlert(
    "Tu repositorio lucianodos/oot-ps2 contiene el código fuente en C. Sube tu oot-ps2.elf compilado con el botón 'Subir .ELF local' o añade el GitHub Action para que la web lo descargue sola."
  );
}

elfInput.addEventListener("change", async (e) => {
  const file = e.target.files[0];
  if (!file) return;
  try {
    const buf = await file.arrayBuffer();
    const u8 = new Uint8Array(buf);
    const meta = parseAndValidatePs2Elf(u8);
    state.elfBytes = u8;
    state.elfMeta = meta;
    repoDot.classList.add("online");
    engineStatus.textContent = `${file.name} cargado (Entry 0x${meta.entry.toString(16).toUpperCase()})`;
    hideAlert();
    updateButtons();
  } catch (err) {
    showAlert(err.message);
  }
});

// Normalización de ROM N64 (.z64, .v64, .n64 -> Big-Endian .z64) y lectura de dmadata
function processN64Rom(arrayBuffer, fileName) {
  const src = new Uint8Array(arrayBuffer);
  if (src.byteLength < 0x1000) {
    throw new Error("El archivo es demasiado pequeño para ser una ROM válida de Nintendo 64.");
  }

  const dvIn = new DataView(src.buffer, src.byteOffset, src.byteLength);
  const magic = dvIn.getUint32(0, false);
  const out = new Uint8Array(src.byteLength);
  let sourceFormat = "Z64";

  if (magic === 0x80371240) {
    out.set(src);
    sourceFormat = "Z64 (Big-Endian)";
  } else if (magic === 0x37804012) {
    // .v64 (Byte-swapped 16-bit)
    for (let i = 0; i < src.byteLength; i += 2) {
      out[i] = src[i + 1];
      out[i + 1] = src[i];
    }
    sourceFormat = "V64 -> Z64";
  } else if (magic === 0x40123780) {
    // .n64 (Little-Endian 32-bit)
    for (let i = 0; i < src.byteLength; i += 4) {
      out[i] = src[i + 3];
      out[i + 1] = src[i + 2];
      out[i + 2] = src[i + 1];
      out[i + 3] = src[i];
    }
    sourceFormat = "N64 -> Z64";
  } else {
    throw new Error(`Formato de ROM no reconocido (Magic: 0x${magic.toString(16).toUpperCase()}).`);
  }

  const dv = new DataView(out.buffer, out.byteOffset, out.byteLength);
  const crc1 = dv.getUint32(0x10, false).toString(16).toUpperCase().padStart(8, "0");
  const crc2 = dv.getUint32(0x14, false).toString(16).toUpperCase().padStart(8, "0");

  const decoder = new TextDecoder("ascii");
  const title = decoder.decode(out.subarray(0x20, 0x34)).trim() || "THE LEGEND OF ZELDA";
  const gameId = decoder.decode(out.subarray(0x3b, 0x3f)).trim() || "CZLE";
  const version = out[0x3f];

  // Búsqueda de tabla dmadata (z_std_dma.c)
  let dmaOffset = 0;
  let dmaEntries = 0;
  for (let off = 0x1000; off < Math.min(out.byteLength - 64, 0x020000); off += 16) {
    const vStart0 = dv.getUint32(off, false);
    const vEnd0 = dv.getUint32(off + 4, false);
    const pStart0 = dv.getUint32(off + 8, false);
    const pEnd0 = dv.getUint32(off + 12, false);

    const vStart1 = dv.getUint32(off + 16, false);
    const vEnd1 = dv.getUint32(off + 20, false);

    if (
      vStart0 === 0x00000000 &&
      vEnd0 === 0x00001060 &&
      pStart0 === 0x00000000 &&
      pEnd0 === 0x00000000 &&
      vStart1 === 0x00001060 &&
      vEnd1 > vStart1
    ) {
      dmaOffset = off;
      let cursor = off;
      while (cursor + 16 <= out.byteLength && dmaEntries < 2048) {
        const vs = dv.getUint32(cursor, false);
        const ve = dv.getUint32(cursor + 4, false);
        if (vs === 0 && ve === 0 && dmaEntries > 0) break;
        dmaEntries++;
        cursor += 16;
      }
      break;
    }
  }

  return {
    bytes: out,
    meta: {
      fileName,
      sourceFormat,
      title,
      gameId: `${gameId} (Rev ${version})`,
      crc: `${crc1} / ${crc2}`,
      dmaOffset,
      dmaEntries
    }
  };
}

// Eventos de carga de ROM
dropzone.addEventListener("click", (e) => {
  if (e.target === changeRomBtn) return;
  if (!state.romBytes) romInput.click();
});

changeRomBtn.addEventListener("click", (e) => {
  e.stopPropagation();
  romInput.click();
});

dropzone.addEventListener("dragover", (e) => {
  e.preventDefault();
  dropzone.classList.add("dragover");
});

dropzone.addEventListener("dragleave", () => {
  dropzone.classList.remove("dragover");
});

dropzone.addEventListener("drop", async (e) => {
  e.preventDefault();
  dropzone.classList.remove("dragover");
  const file = e.dataTransfer.files[0];
  if (file) await handleRomFile(file);
});

romInput.addEventListener("change", async (e) => {
  const file = e.target.files[0];
  if (file) await handleRomFile(file);
});

async function handleRomFile(file) {
  try {
    hideAlert();
    setProgress("Analizando y normalizando ROM a .Z64...", 35);
    const buf = await file.arrayBuffer();
    const { bytes, meta } = processN64Rom(buf, file.name);
    state.romBytes = bytes;
    state.romMeta = meta;

    romFormatBadge.textContent = meta.sourceFormat;
    romFileName.textContent = meta.fileName;
    romTitle.textContent = meta.title;
    romGameId.textContent = meta.gameId;
    romCrc.textContent = meta.crc;
    romDma.textContent = meta.dmaOffset
      ? `0x${meta.dmaOffset.toString(16).toUpperCase()} (${meta.dmaEntries} segs)`
      : "Detectado";

    dropzoneEmpty.classList.add("hidden");
    romSummary.classList.remove("hidden");
    setProgress("ROM validada correctamente", 100);
    updateButtons();
  } catch (err) {
    progressWrap.classList.add("hidden");
    showAlert(err.message);
  }
}

// Generador ISO9660 real (sectores de 2048 bytes) para PS2 (PCSX2 / OPL)
function buildPs2IsoImage(elfBytes, romBytes) {
  const SECTOR = 2048;
  const alignSectors = (len) => Math.ceil(len / SECTOR);

  const systemCnfText =
    "BOOT2 = cdrom0:\\OOT_PS2.ELF;1\r\n" +
    "VER = 1.00\r\n" +
    "VMODE = NTSC\r\n";
  const systemCnfBytes = new TextEncoder().encode(systemCnfText);

  const rootDirLba = 18;
  const pathTableLba = 19;
  const pathTableMLba = 20;
  const cnfLba = 21;
  const elfLba = cnfLba + alignSectors(systemCnfBytes.byteLength);
  const romLba = elfLba + alignSectors(elfBytes.byteLength);
  const totalSectors = romLba + alignSectors(romBytes.byteLength) + 16;

  const iso = new Uint8Array(totalSectors * SECTOR);
  const dv = new DataView(iso.buffer);

  const writeBoth16 = (off, val) => {
    dv.setUint16(off, val, true);
    dv.setUint16(off + 2, val, false);
  };
  const writeBoth32 = (off, val) => {
    dv.setUint32(off, val, true);
    dv.setUint32(off + 4, val, false);
  };
  const writeAscii = (off, str, len) => {
    for (let i = 0; i < len; i++) {
      iso[off + i] = i < str.length ? str.charCodeAt(i) : 0x20;
    }
  };

  const makeDirRecord = (targetBuf, offset, lba, size, flags, name) => {
    const nameLen = name.length;
    const recLen = 33 + nameLen + (nameLen % 2 === 0 ? 1 : 0);
    const view = new DataView(targetBuf.buffer, targetBuf.byteOffset + offset, recLen);
    targetBuf[offset] = recLen;
    targetBuf[offset + 1] = 0;
    view.setUint32(2, lba, true);
    view.setUint32(6, lba, false);
    view.setUint32(10, size, true);
    view.setUint32(14, size, false);
    targetBuf[offset + 18] = 126; // Año 2026
    targetBuf[offset + 19] = 11;
    targetBuf[offset + 20] = 12;
    targetBuf[offset + 25] = flags;
    view.setUint16(28, 1, true);
    view.setUint16(30, 1, false);
    targetBuf[offset + 32] = nameLen;
    for (let i = 0; i < nameLen; i++) {
      targetBuf[offset + 33 + i] = name.charCodeAt(i);
    }
    return recLen;
  };

  // Directorio raíz (LBA 18)
  const rootOff = rootDirLba * SECTOR;
  let cursor = rootOff;
  cursor += makeDirRecord(iso, cursor, rootDirLba, SECTOR, 0x02, "\x00");
  cursor += makeDirRecord(iso, cursor, rootDirLba, SECTOR, 0x02, "\x01");
  cursor += makeDirRecord(iso, cursor, cnfLba, systemCnfBytes.byteLength, 0x00, "SYSTEM.CNF;1");
  cursor += makeDirRecord(iso, cursor, elfLba, elfBytes.byteLength, 0x00, "OOT_PS2.ELF;1");
  cursor += makeDirRecord(iso, cursor, romLba, romBytes.byteLength, 0x00, "ROM.Z64;1");
  cursor += makeDirRecord(iso, cursor, romLba, romBytes.byteLength, 0x00, "OOT.Z64;1");

  // Path Tables (LBA 19 L-Table, LBA 20 M-Table)
  const ptL = pathTableLba * SECTOR;
  iso[ptL] = 1;
  dv.setUint32(ptL + 2, rootDirLba, true);
  dv.setUint16(ptL + 6, 1, true);

  const ptM = pathTableMLba * SECTOR;
  iso[ptM] = 1;
  dv.setUint32(ptM + 2, rootDirLba, false);
  dv.setUint16(ptM + 6, 1, false);

  // Primary Volume Descriptor (LBA 16)
  const pvd = 16 * SECTOR;
  iso[pvd] = 0x01;
  writeAscii(pvd + 1, "CD001", 5);
  iso[pvd + 6] = 0x01;
  writeAscii(pvd + 8, "PLAYSTATION", 32);
  writeAscii(pvd + 40, "OOT_PS2", 32);
  writeBoth32(pvd + 80, totalSectors);
  writeBoth16(pvd + 120, 1);
  writeBoth16(pvd + 124, 1);
  writeBoth16(pvd + 128, SECTOR);
  writeBoth32(pvd + 132, 10);
  dv.setUint32(pvd + 140, pathTableLba, true);
  dv.setUint32(pvd + 148, pathTableMLba, false);
  makeDirRecord(iso, pvd + 156, rootDirLba, SECTOR, 0x02, "\x00");
  writeAscii(pvd + 318, "LUCIANODOS", 128);
  writeAscii(pvd + 446, "OOT_PS2_BUILDER", 128);
  writeAscii(pvd + 813, "2026111200000000", 16);
  iso[pvd + 881] = 0x01;

  // Volume Descriptor Set Terminator (LBA 17)
  const term = 17 * SECTOR;
  iso[term] = 0xff;
  writeAscii(term + 1, "CD001", 5);
  iso[term + 6] = 0x01;

  // Escribir archivos de datos intactos
  iso.set(systemCnfBytes, cnfLba * SECTOR);
  iso.set(elfBytes, elfLba * SECTOR);
  iso.set(romBytes, romLba * SECTOR);

  return iso;
}

function downloadBlob(u8, fileName, mime) {
  const blob = new Blob([u8], { type: mime });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = fileName;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}

buildIsoBtn.addEventListener("click", () => {
  if (!state.romBytes || !state.elfBytes) return;
  setProgress("Empaquetando imagen ISO9660 (OOT_PS2.ELF + ROM.Z64)...", 60);
  setTimeout(() => {
    const isoBytes = buildPs2IsoImage(state.elfBytes, state.romBytes);
    setProgress("Imagen .ISO generada", 100);
    downloadBlob(isoBytes, "oot-ps2.iso", "application/octet-stream");
  }, 50);
});

buildElfBtn.addEventListener("click", () => {
  if (!state.romBytes || !state.elfBytes) return;
  setProgress("Preparando ejecutable oot-ps2.elf y ROM.Z64...", 75);
  setTimeout(() => {
    downloadBlob(state.elfBytes, "oot-ps2.elf", "application/octet-stream");
    downloadBlob(state.romBytes, "ROM.Z64", "application/octet-stream");
    setProgress("Archivos .ELF + ROM.Z64 descargados", 100);
  }, 50);
});

initTheme();
autoFetchEngineElf();
