#!/bin/sh
# charcoal installer: installs the build dependencies, builds charcoal from
# source and installs it with its man page and shell completions.
#
#   curl -fsSL https://raw.githubusercontent.com/Zemosis/charcoal/main/install.sh | sh
#
# Options (after "sh -s --" when piped):
#   --system        install to /usr/local (asks for sudo) instead of ~/.local
#   --prefix DIR    install to DIR
#   --ref REF       build a branch or tag instead of main (e.g. v0.1.0)
#   --src DIR       build this source folder (default: the clone this
#                   script is in, else download from GitHub)
#   --no-deps       do not install build dependencies
#   --uninstall     remove charcoal from the prefix
set -eu

REPO="Zemosis/charcoal"
PREFIX="$HOME/.local"
REF="main"
DEPS=1
UNINSTALL=0
SUDO_INSTALL=0
SRC=""

# Run from a clone (./install.sh), build that clone
case "$0" in
    */install.sh|install.sh)
        here=$(cd "$(dirname "$0")" && pwd)
        if [ -f "$here/src/app.c" ] && [ -f "$here/Makefile" ]; then
            SRC=$here
        fi ;;
esac

say() { printf '\033[1;33m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }
usage() {
    cat <<'USAGE'
Usage: install.sh [--system | --prefix DIR] [--ref REF] [--no-deps] [--uninstall]
  --system        install to /usr/local (asks for sudo) instead of ~/.local
  --prefix DIR    install to DIR
  --ref REF       build a branch or tag instead of main (e.g. v0.1.0)
  --src DIR       build this source folder instead of downloading
  --no-deps       do not install build dependencies
  --uninstall     remove charcoal from the prefix
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --system) PREFIX=/usr/local; SUDO_INSTALL=1 ;;
        --prefix) [ $# -ge 2 ] || die "--prefix needs a folder"; PREFIX=$2; shift ;;
        --ref) [ $# -ge 2 ] || die "--ref needs a branch or tag"; REF=$2; shift ;;
        --src) [ $# -ge 2 ] || die "--src needs a folder"; SRC=$2; shift ;;
        --no-deps) DEPS=0 ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown option $1 (try --help)" ;;
    esac
    shift
done

# Root runs things directly; everyone else goes through sudo
if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
elif have sudo; then
    SUDO="sudo"
elif have doas; then
    SUDO="doas"
else
    SUDO="none"
fi
as_root() {
    [ "$SUDO" = none ] && die "need root for: $*  (install sudo, or run as root)"
    $SUDO "$@"
}

if [ "$UNINSTALL" -eq 1 ]; then
    run=""
    [ "$SUDO_INSTALL" -eq 1 ] && run=as_root
    say "Removing charcoal from $PREFIX"
    $run rm -f "$PREFIX/bin/charcoal" \
        "$PREFIX/share/man/man1/charcoal.1" \
        "$PREFIX/share/bash-completion/completions/charcoal" \
        "$PREFIX/share/zsh/site-functions/_charcoal" \
        "$PREFIX/share/fish/vendor_completions.d/charcoal.fish"
    say "Done. Your settings stay in ~/.config/charcoal."
    exit 0
fi

# Build dependencies: a C compiler, make, pkg-config and FFmpeg's libraries
# (libwebp is optional, for animated WebP). Only what is missing is installed.
missing_libs() {
    have pkg-config || { echo all; return; }
    for m in libavformat libavcodec libavutil libswscale; do
        pkg-config --exists "$m" || { echo all; return; }
    done
}
install_deps() {
    if have cc && have make && [ -z "$(missing_libs)" ]; then
        say "Build dependencies already installed"
        return
    fi
    if have pacman; then
        say "Installing build dependencies with pacman"
        as_root pacman -S --needed --noconfirm base-devel ffmpeg libwebp ||
            die "pacman could not install them; if it said 'target not found', update first: sudo pacman -Syu"
    elif have apt-get; then
        say "Installing build dependencies with apt"
        as_root apt-get update -qq
        as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
            build-essential pkg-config libavformat-dev libavcodec-dev \
            libavutil-dev libswscale-dev libwebp-dev
    elif have dnf; then
        # pkgconfig() names take whichever FFmpeg is there (Fedora's
        # ffmpeg-free, or RPM Fusion's)
        say "Installing build dependencies with dnf"
        as_root dnf install -y gcc make pkgconf-pkg-config \
            'pkgconfig(libavformat)' 'pkgconfig(libavcodec)' \
            'pkgconfig(libavutil)' 'pkgconfig(libswscale)' \
            'pkgconfig(libwebpdemux)'
    elif have zypper; then
        say "Installing build dependencies with zypper"
        as_root zypper --non-interactive install gcc make pkg-config \
            'pkgconfig(libavformat)' 'pkgconfig(libavcodec)' \
            'pkgconfig(libavutil)' 'pkgconfig(libswscale)' \
            'pkgconfig(libwebpdemux)'
    elif have apk; then
        say "Installing build dependencies with apk"
        as_root apk add build-base pkgconf ffmpeg-dev libwebp-dev
    elif have brew; then
        say "Installing build dependencies with Homebrew"
        brew install pkg-config ffmpeg webp
    else
        die "unknown package manager; install a C compiler, make, pkg-config and the FFmpeg development files, then run again with --no-deps"
    fi
}
[ "$DEPS" -eq 1 ] && install_deps

# Fetch the source as a tarball (no git needed)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
if [ -n "$SRC" ]; then
    # A copy, so the folder keeps its own build
    say "Using the source in $SRC"
    cp -R "$SRC" "$TMP/charcoal"
    rm -rf "$TMP/charcoal/build" "$TMP/charcoal/charcoal"
else
    case "$REF" in
        v[0-9]*) URL="https://github.com/$REPO/archive/refs/tags/$REF.tar.gz" ;;
        *) URL="https://github.com/$REPO/archive/refs/heads/$REF.tar.gz" ;;
    esac
    say "Downloading charcoal ($REF)"
    if have curl; then
        curl -fsSL "$URL" | tar -xz -C "$TMP"
    elif have wget; then
        wget -qO- "$URL" | tar -xz -C "$TMP"
    else
        die "need curl or wget"
    fi
fi
SRC=$(find "$TMP" -mindepth 1 -maxdepth 1 -type d | head -n 1)

say "Building"
make -C "$SRC" release >/dev/null

say "Installing to $PREFIX"
if [ "$SUDO_INSTALL" -eq 1 ]; then
    as_root make -C "$SRC" install PREFIX="$PREFIX" >/dev/null
else
    make -C "$SRC" install PREFIX="$PREFIX" >/dev/null
fi

say "Installed $("$PREFIX/bin/charcoal" --version | head -n 1)"
case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) printf '\n%s/bin is not on your PATH. Add this to your shell config:\n  export PATH="%s/bin:$PATH"\n' "$PREFIX" "$PREFIX" ;;
esac
case "${SHELL:-}" in
    */zsh)
        if [ "$PREFIX" = "$HOME/.local" ]; then
            printf '\nFor zsh tab completion, add this before compinit in ~/.zshrc:\n  fpath=(~/.local/share/zsh/site-functions $fpath)\n'
        fi ;;
esac
printf '\nTry it:  charcoal ~/Pictures    (? shows every key, q quits)\n'
