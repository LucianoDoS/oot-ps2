#!/usr/bin/env bash
# Source this file in WSL. Override PS2DEV to use another SDK installation.
export PS2DEV="${PS2DEV:-$HOME/oot-ps2-tools/ps2dev}"
export PS2SDK="${PS2SDK:-$PS2DEV/ps2sdk}"
export GSKIT="${GSKIT:-$PS2DEV/gsKit}"
export PATH="$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2DEV/bin:$PS2SDK/bin:$PATH"
