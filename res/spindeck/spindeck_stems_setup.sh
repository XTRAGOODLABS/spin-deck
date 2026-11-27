#!/bin/bash
# Spin Deck by XTRAGOODLABS: one-time setup of the stem maker.
# Installs a private Python with Demucs (the free stem splitter) and its model
# into ~/Library/Application Support/Spin Deck/stems. About 2-3 GB. Nothing
# else on the Mac is changed. Delete that folder to remove it.
# Prints lines Spin Deck reads: STATUS <text>, DONE, ERROR <text>.
set -u
HOME_DIR="${SPINDECK_STEMS_HOME:-$HOME/Library/Application Support/Spin Deck/stems}"
mkdir -p "$HOME_DIR"
cd "$HOME_DIR" || { echo "ERROR cannot open $HOME_DIR"; exit 1; }
export UV_CACHE_DIR="$HOME_DIR/cache"
export UV_PYTHON_INSTALL_DIR="$HOME_DIR/python"
export TORCH_HOME="$HOME_DIR/models"

fail() { echo "ERROR $1"; exit 1; }

if [ ! -x "$HOME_DIR/bin/uv" ]; then
  echo "STATUS Downloading the installer"
  curl -LsSf https://astral.sh/uv/install.sh -o "$HOME_DIR/uv-install.sh" || fail "could not download the installer (check the internet connection)"
  env UV_INSTALL_DIR="$HOME_DIR/bin" UV_NO_MODIFY_PATH=1 INSTALLER_NO_MODIFY_PATH=1 sh "$HOME_DIR/uv-install.sh" >/dev/null 2>&1 || fail "the installer did not run"
fi
UV="$HOME_DIR/bin/uv"
[ -x "$UV" ] || fail "installer missing"

if [ ! -x "$HOME_DIR/venv/bin/python" ]; then
  echo "STATUS Setting up Python"
  "$UV" venv --python 3.11 "$HOME_DIR/venv" >/dev/null 2>&1 || fail "could not set up Python"
fi
PY="$HOME_DIR/venv/bin/python"

echo "STATUS Downloading the stem splitter (about 2 GB)"
# Intel Macs: PyTorch's last Intel Mac release is 2.2.2, which needs NumPy 1.x.
if [ "$(uname -m)" = "x86_64" ]; then
  TORCH=("torch==2.2.2" "torchaudio==2.2.2" "numpy<2")
else
  TORCH=("torch==2.5.1" "torchaudio==2.5.1")
fi
"$UV" pip install --python "$PY" \
  "${TORCH[@]}" "demucs==4.0.1" \
  "imageio-ffmpeg" "soundfile" >/dev/null 2>&1 || fail "could not download the stem splitter"

echo "STATUS Downloading the splitting model"
"$PY" -c "from demucs.pretrained import get_model; get_model('htdemucs')" >/dev/null 2>&1 || fail "could not download the model"

echo "STATUS Checking it works"
"$PY" -c "import torch, demucs, imageio_ffmpeg; imageio_ffmpeg.get_ffmpeg_exe()" >/dev/null 2>&1 || fail "the check failed"

touch "$HOME_DIR/ready"
echo "DONE"
