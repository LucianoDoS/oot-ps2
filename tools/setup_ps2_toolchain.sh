#!/usr/bin/env bash
# =============================================================
#  setup_ps2_toolchain.sh — Install ps2toolchain for OoT PS2 Port
#
#  Run this script in WSL2 (Ubuntu) or native Linux.
#  It installs:
#    - ps2toolchain  (ee-gcc, ee-binutils, etc.)
#    - ps2sdk        (PS2 standard library)
#    - gsKit         (GS graphics library)
#    - audsrv        (audio server library)
#    - PCSX2         (emulator for testing)
#
#  Usage:
#    chmod +x setup_ps2_toolchain.sh
#    ./setup_ps2_toolchain.sh
#
#  After completion:
#    source ~/.ps2dev_env      (add to ~/.bashrc for persistence)
#    make PLATFORM=PS2         (in the oot-ps2 directory)
# =============================================================

set -euo pipefail

PS2DEV_HOME="$HOME/ps2dev"
INSTALL_PREFIX="/usr/local/ps2dev"

echo ""
echo "╔══════════════════════════════════════════════╗"
echo "║   OoT PS2 Port — Toolchain Setup Script     ║"
echo "╚══════════════════════════════════════════════╝"
echo ""

# ── 1. System dependencies ─────────────────────────────────
echo "[1/7] Installing system dependencies..."
sudo apt-get update -qq
sudo apt-get install -y \
    git wget curl build-essential cmake \
    libreadline-dev libsdl2-dev libgmp-dev \
    libmpfr-dev libmpc-dev texinfo zlib1g-dev \
    bison flex \
    python3 python3-pip unzip p7zip-full \
    2>/dev/null | tail -5

# ── 2. Clone ps2toolchain ──────────────────────────────────
echo ""
echo "[2/7] Cloning ps2toolchain..."
mkdir -p "$PS2DEV_HOME"
if [ ! -d "$PS2DEV_HOME/ps2toolchain" ]; then
    git clone --depth=1 https://github.com/ps2dev/ps2toolchain.git \
        "$PS2DEV_HOME/ps2toolchain"
else
    echo "  (already cloned, updating...)"
    git -C "$PS2DEV_HOME/ps2toolchain" pull --ff-only
fi

# ── 3. Build and install toolchain ────────────────────────
echo ""
echo "[3/7] Building ps2toolchain (this takes 20–60 min)..."
cd "$PS2DEV_HOME/ps2toolchain"
sudo mkdir -p "$INSTALL_PREFIX"
sudo chown "$USER" "$INSTALL_PREFIX"
export PS2DEV="$INSTALL_PREFIX"
export MAKEFLAGS="-j2"
./toolchain.sh 2>&1 | tee "$HOME/ps2toolchain_build.log" | grep -E "(Building|Error|Warning|Done)" || true

# ── 4. Clone and install ps2sdk ────────────────────────────
echo ""
echo "[4/7] Installing ps2sdk..."
if [ ! -d "$PS2DEV_HOME/ps2sdk" ]; then
    git clone --depth=1 https://github.com/ps2dev/ps2sdk.git \
        "$PS2DEV_HOME/ps2sdk"
fi
cd "$PS2DEV_HOME/ps2sdk"
export PS2SDK="$INSTALL_PREFIX/ps2sdk"
export PATH="$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PATH"
make install 2>&1 | tail -10

# ── 5. Clone and build gsKit ───────────────────────────────
echo ""
echo "[5/7] Building gsKit..."
if [ ! -d "$PS2DEV_HOME/gsKit" ]; then
    git clone --depth=1 https://github.com/ps2dev/gsKit.git \
        "$PS2DEV_HOME/gsKit"
fi
cd "$PS2DEV_HOME/gsKit"
make install 2>&1 | tail -10

# ── 6. Build audsrv ────────────────────────────────────────
echo ""
echo "[6/7] Building audsrv..."
if [ ! -d "$PS2DEV_HOME/audsrv" ]; then
    git clone --depth=1 https://github.com/ps2dev/ps2sdk.git \
        "$PS2DEV_HOME/audsrv_src" 2>/dev/null || true
fi
# audsrv is included in ps2sdk; check if it compiled
if [ -f "$PS2SDK/iop/irx/audsrv.irx" ]; then
    echo "  audsrv.irx found ✓"
else
    echo "  WARNING: audsrv.irx not found, audio may not work"
fi

# ── 7. Install PCSX2 (for testing) ─────────────────────────
echo ""
echo "[7/7] Installing PCSX2 emulator..."
if ! command -v pcsx2 &>/dev/null; then
    if grep -qi ubuntu /etc/os-release 2>/dev/null; then
        # PCSX2 AppImage (works on most Linux)
        PCSX2_URL="https://github.com/PCSX2/pcsx2/releases/latest/download/pcsx2-linux.AppImage"
        wget -q "$PCSX2_URL" -O "$HOME/pcsx2.AppImage" || \
            echo "  Could not download PCSX2 automatically."
        chmod +x "$HOME/pcsx2.AppImage" 2>/dev/null || true
        echo "  PCSX2 AppImage saved to ~/pcsx2.AppImage"
    fi
else
    echo "  PCSX2 already installed ✓"
fi

# ── Environment file ───────────────────────────────────────
cat > "$HOME/.ps2dev_env" << 'EOF'
# OoT PS2 Port — PS2 Development Environment
export PS2DEV=/usr/local/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export GSKIT=$PS2DEV/gsKit
export PATH=$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PATH

# Convenience aliases
alias ps2build='make PLATFORM=PS2'
alias ps2run='~/pcsx2.AppImage'
alias ps2clean='make clean'
EOF

echo ""
echo "╔══════════════════════════════════════════════╗"
echo "║   Setup complete!                            ║"
echo "╠══════════════════════════════════════════════╣"
echo "║   Next steps:                                ║"
echo "║   1. source ~/.ps2dev_env                    ║"
echo "║   2. echo 'source ~/.ps2dev_env' >> ~/.bashrc║"
echo "║   3. cd /path/to/oot-ps2                     ║"
echo "║   4. make PLATFORM=PS2                       ║"
echo "║   5. ~/pcsx2.AppImage build/PS2/oot_ps2.elf  ║"
echo "╚══════════════════════════════════════════════╝"
echo ""
