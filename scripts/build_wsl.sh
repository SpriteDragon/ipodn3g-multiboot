#!/usr/bin/env bash
set -euo pipefail

# Build in WSL's Linux filesystem; only the finished .ipod is copied to C:.
PROJECT="$HOME/nano3_bootloader"
WORK="$HOME/nano3-build"
REPO="$WORK/Nano3Rockbox"
TOOLCHAIN="$HOME/.rockbox-toolchain"

sudo apt-get update
sudo apt-get install -y build-essential git curl wget flex bison texinfo \
    libtool autoconf automake patch bzip2 xz-utils python3 \
    libgmp-dev libmpfr-dev libmpc-dev

mkdir -p "$WORK" "$TOOLCHAIN" "$PROJECT/out"

if [[ ! -d "$REPO/.git" ]]; then
    git clone --depth 1 https://github.com/giek2000/Nano3Rockbox.git "$REPO"
else
    git -C "$REPO" fetch --depth 1 origin main
    git -C "$REPO" reset --hard origin/main
fi

cp "$PROJECT/bootloader/ipod-s5l87xx-menu.c" "$REPO/bootloader/"
cp "$PROJECT/bootloader/SOURCES" "$REPO/bootloader/SOURCES"

if [[ ! -x "$TOOLCHAIN/bin/arm-elf-eabi-gcc" ]]; then
    rm -rf "$WORK/toolchain-build"
    mkdir -p "$WORK/toolchain-downloads"
    cd "$REPO"
    printf 'a\n' | env \
        RBDEV_PREFIX="$TOOLCHAIN" \
        RBDEV_BUILD="$WORK/toolchain-build" \
        RBDEV_DOWNLOAD="$WORK/toolchain-downloads" \
        tools/rockboxdev.sh
fi

export PATH="$TOOLCHAIN/bin:$PATH"
arm-elf-eabi-gcc --version | head -n 1

rm -rf "$REPO/build-bootmenu"
mkdir "$REPO/build-bootmenu"
cd "$REPO/build-bootmenu"

../tools/configure --target=ipodnano3g --type=b \
    --extra-defines="FTL_NANO3G_V2"
make -j"$(nproc)"

cp -v ./*.ipod "$PROJECT/out/"
echo
echo "BUILD COMPLETE:"
ls -lh "$PROJECT"/out/*.ipod
