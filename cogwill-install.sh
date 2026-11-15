#!/usr/bin/env bash
# ==============================================================================
# Cogwill Standalone Installer (v0.0.1)
# Repository: https://github.com/rappar222/cogwill
#
# Automatically installs required system build tools & multimedia libraries,
# downloads all Cogwill source files & assets (plus SFML 3 / nlohmann_json if
# not already provided by your OS), compiles an optimized Release binary, and
# sets up the `cogwill` command + desktop launcher.
# ==============================================================================
set -euo pipefail

REPO_URL="https://github.com/rappar222/cogwill.git"
ARCHIVE_BASE="https://github.com/rappar222/cogwill/archive/refs"
VERSION="${COGWILL_VERSION:-v0.0.1}"
INSTALL_DIR="${COGWILL_INSTALL_DIR:-$HOME/.local/share/cogwill}"
BIN_DIR="${COGWILL_BIN_DIR:-$HOME/.local/bin}"
SKIP_DEPS=0
RUN_AFTER=0
UNINSTALL=0

if [ -t 1 ]; then
    C_BOLD="\033[1m"
    C_GOLD="\033[1;33m"
    C_CYAN="\033[1;36m"
    C_GREEN="\033[1;32m"
    C_RED="\033[1;31m"
    C_RESET="\033[0m"
else
    C_BOLD=""
    C_GOLD=""
    C_CYAN=""
    C_GREEN=""
    C_RED=""
    C_RESET=""
fi

info()  { printf "${C_CYAN}[cogwill]${C_RESET} %s\n" "$*"; }
ok()    { printf "${C_GREEN}[  ok   ]${C_RESET} %s\n" "$*"; }
warn()  { printf "${C_GOLD}[ warn  ]${C_RESET} %s\n" "$*"; }
err()   { printf "${C_RED}[ error ]${C_RESET} %s\n" "$*" >&2; exit 1; }

usage() {
    cat <<EOF
Cogwill Installer (${VERSION})

Usage:
  ./cogwill-install.sh [options]

Options:
  --dir <path>       Installation directory (default: $INSTALL_DIR)
  --bin-dir <path>   Launcher symlink/script directory (default: $BIN_DIR)
  --version <ref>    Git tag or branch to install (default: $VERSION)
  --skip-deps        Skip system package manager dependency installation
  --run              Launch Cogwill immediately after installation
  --uninstall        Remove Cogwill installation and launcher
  -h, --help         Show this help message
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --dir)
            [ $# -ge 2 ] || err "--dir requires a path argument"
            INSTALL_DIR="$2"
            shift 2
            ;;
        --bin-dir)
            [ $# -ge 2 ] || err "--bin-dir requires a path argument"
            BIN_DIR="$2"
            shift 2
            ;;
        --version)
            [ $# -ge 2 ] || err "--version requires a tag or branch name"
            VERSION="$2"
            shift 2
            ;;
        --skip-deps)
            SKIP_DEPS=1
            shift
            ;;
        --run)
            RUN_AFTER=1
            shift
            ;;
        --uninstall)
            UNINSTALL=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            err "Unknown option: $1 (try --help)"
            ;;
    esac
done

if [ "$UNINSTALL" -eq 1 ]; then
    info "Uninstalling Cogwill..."
    rm -f "$BIN_DIR/cogwill"
    rm -f "$HOME/.local/share/applications/cogwill.desktop"
    if [ -d "$INSTALL_DIR" ]; then
        rm -rf "$INSTALL_DIR"
        ok "Removed $INSTALL_DIR"
    fi
    ok "Cogwill has been uninstalled."
    exit 0
fi

printf "\n${C_GOLD}${C_BOLD}=========================================================${C_RESET}\n"
printf "${C_GOLD}${C_BOLD}   COGWILL  -  Automated Installer (${VERSION})          ${C_RESET}\n"
printf "${C_GOLD}${C_BOLD}   https://github.com/rappar222/cogwill                  ${C_RESET}\n"
printf "${C_GOLD}${C_BOLD}=========================================================${C_RESET}\n\n"

run_priv() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    elif command -v doas >/dev/null 2>&1; then
        doas "$@"
    else
        err "Root privileges are required to install system libraries (install sudo/doas or pass --skip-deps)."
    fi
}

install_system_dependencies() {
    info "Detecting operating system and installing required libraries..."
    OS_NAME="$(uname -s)"

    if [ "$OS_NAME" = "Darwin" ]; then
        if ! command -v brew >/dev/null 2>&1; then
            err "Homebrew is required on macOS. Install it from https://brew.sh and re-run this script."
        fi
        brew install cmake ninja git sfml nlohmann-json
        ok "macOS libraries installed via Homebrew."
        return
    fi

    if [ "$OS_NAME" != "Linux" ]; then
        warn "Unrecognized OS '$OS_NAME'. Skipping OS package manager step and relying on CMake FetchContent."
        return
    fi

    if command -v pacman >/dev/null 2>&1; then
        info "Detected Arch-based Linux (pacman)..."
        run_priv pacman -Sy --needed --noconfirm \
            base-devel cmake ninja git curl tar pkgconf \
            sfml nlohmann-json \
            mesa libx11 libxrandr libxcursor libxi freetype2 libogg libvorbis flac
    elif command -v apt-get >/dev/null 2>&1; then
        info "Detected Debian/Ubuntu-based Linux (apt)..."
        run_priv apt-get update
        APT_PKGS=(
            build-essential g++ cmake ninja-build git curl tar ca-certificates pkg-config
            nlohmann-json3-dev
            libx11-dev libxrandr-dev libxcursor-dev libxi-dev libudev-dev
            libfreetype-dev libgl1-mesa-dev libdrm-dev
            libogg-dev libvorbis-dev libflac-dev
        )
        # Use distro libsfml-dev directly if it is SFML 3.x; otherwise CMake fetches SFML 3.0.2 automatically
        SFML_CAND="$(apt-cache policy libsfml-dev 2>/dev/null | awk '/Candidate:/ {print $2}' || true)"
        if [[ "$SFML_CAND" == 3.* ]]; then
            APT_PKGS+=(libsfml-dev)
        fi
        run_priv apt-get install -y "${APT_PKGS[@]}"
    elif command -v dnf >/dev/null 2>&1; then
        info "Detected Fedora/RHEL-based Linux (dnf)..."
        DNF_PKGS=(
            gcc-c++ make cmake ninja-build git curl tar pkgconf-pkg-config
            json-devel
            libX11-devel libXrandr-devel libXcursor-devel libXi-devel systemd-devel
            freetype-devel mesa-libGL-devel
            libogg-devel libvorbis-devel flac-devel
        )
        if dnf info SFML-devel 2>/dev/null | grep -qE 'Version\s*:\s*3\.'; then
            DNF_PKGS+=(SFML-devel)
        fi
        run_priv dnf install -y "${DNF_PKGS[@]}"
    elif command -v zypper >/dev/null 2>&1; then
        info "Detected openSUSE (zypper)..."
        run_priv zypper --non-interactive install \
            gcc-c++ make cmake ninja git curl tar pkg-config \
            nlohmann_json-devel \
            libX11-devel libXrandr-devel libXcursor-devel libXi-devel libudev-devel \
            freetype2-devel Mesa-libGL-devel \
            libogg-devel libvorbis-devel flac-devel
    elif command -v apk >/dev/null 2>&1; then
        info "Detected Alpine Linux (apk)..."
        run_priv apk add --no-cache \
            build-base cmake samurai git curl tar pkgconf \
            nlohmann-json \
            libx11-dev libxrandr-dev libxcursor-dev libxi-dev eudev-dev \
            freetype-dev mesa-dev \
            libogg-dev libvorbis-dev flac-dev
    elif command -v xbps-install >/dev/null 2>&1; then
        info "Detected Void Linux (xbps)..."
        run_priv xbps-install -Sy \
            base-devel cmake ninja git curl tar pkg-config \
            nlohmann-json \
            libX11-devel libXrandr-devel libXcursor-devel libXi-devel eudev-libudev-devel \
            freetype-devel MesaLib-devel \
            libogg-devel libvorbis-devel libflac-devel
    else
        warn "Could not detect a supported Linux package manager. Continuing with existing system toolchain..."
    fi

    ok "System build tools and multimedia libraries ready."
}

download_cogwill_files() {
    info "Downloading Cogwill (${VERSION}) into ${INSTALL_DIR}..."
    mkdir -p "$(dirname "$INSTALL_DIR")"

    # Back up existing user save/settings if reinstalling or upgrading
    TMP_BACKUP=""
    if [ -f "$INSTALL_DIR/savegame.json" ] || [ -f "$INSTALL_DIR/settings.json" ]; then
        TMP_BACKUP="$(mktemp -d)"
        [ -f "$INSTALL_DIR/savegame.json" ] && cp -p "$INSTALL_DIR/savegame.json" "$TMP_BACKUP/"
        [ -f "$INSTALL_DIR/settings.json" ] && cp -p "$INSTALL_DIR/settings.json" "$TMP_BACKUP/"
    fi

    if command -v git >/dev/null 2>&1; then
        if [ -d "$INSTALL_DIR/.git" ]; then
            info "Existing Git repository found; fetching latest updates..."
            git -C "$INSTALL_DIR" fetch --tags --force origin
            if git -C "$INSTALL_DIR" rev-parse --verify "${VERSION}^{commit}" >/dev/null 2>&1; then
                git -C "$INSTALL_DIR" checkout -f "$VERSION"
            elif git -C "$INSTALL_DIR" rev-parse --verify "origin/${VERSION}" >/dev/null 2>&1; then
                git -C "$INSTALL_DIR" checkout -f -B "$VERSION" "origin/${VERSION}"
            else
                warn "Ref '${VERSION}' not found; checking out origin/main..."
                git -C "$INSTALL_DIR" checkout -f -B main origin/main
            fi
        else
            rm -rf "$INSTALL_DIR"
            if ! git clone --depth 1 --branch "$VERSION" "$REPO_URL" "$INSTALL_DIR" 2>/dev/null; then
                warn "Tag/branch '${VERSION}' not found yet; cloning default branch from ${REPO_URL}..."
                git clone --depth 1 "$REPO_URL" "$INSTALL_DIR"
            fi
        fi
    else
        command -v curl >/dev/null 2>&1 || err "Neither git nor curl is installed."
        TMP_TAR="$(mktemp -d)"
        TARBALL="$TMP_TAR/cogwill.tar.gz"
        if ! curl -fsSL "${ARCHIVE_BASE}/tags/${VERSION}.tar.gz" -o "$TARBALL" 2>/dev/null; then
            curl -fsSL "${ARCHIVE_BASE}/heads/main.tar.gz" -o "$TARBALL"
        fi
        rm -rf "$INSTALL_DIR"
        mkdir -p "$INSTALL_DIR"
        tar -xzf "$TARBALL" -C "$INSTALL_DIR" --strip-components=1
        rm -rf "$TMP_TAR"
    fi

    if [ -n "$TMP_BACKUP" ] && [ -d "$TMP_BACKUP" ]; then
        [ -f "$TMP_BACKUP/savegame.json" ] && cp -p "$TMP_BACKUP/savegame.json" "$INSTALL_DIR/"
        [ -f "$TMP_BACKUP/settings.json" ] && cp -p "$TMP_BACKUP/settings.json" "$INSTALL_DIR/"
        rm -rf "$TMP_BACKUP"
        ok "Preserved existing savegame.json / settings.json."
    fi

    ok "All Cogwill source files, shaders, and fonts downloaded."
}

build_and_verify() {
    command -v cmake >/dev/null 2>&1 || err "cmake was not found on PATH."

    CMAKE_GEN_ARGS=()
    if command -v ninja >/dev/null 2>&1 || command -v samu >/dev/null 2>&1; then
        CMAKE_GEN_ARGS=(-G Ninja)
    fi

    JOBS=4
    if command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    elif [ "$(uname -s)" = "Darwin" ] && command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu)"
    fi

    info "Configuring CMake Release build (auto-fetching SFML 3 / nlohmann_json if needed)..."
    cmake -S "$INSTALL_DIR" -B "$INSTALL_DIR/build" \
        "${CMAKE_GEN_ARGS[@]}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5

    info "Compiling Cogwill using ${JOBS} parallel jobs..."
    cmake --build "$INSTALL_DIR/build" --config Release -j"$JOBS"

    info "Running simulation & worldgen verification tests..."
    ctest --test-dir "$INSTALL_DIR/build" --output-on-failure

    ok "Cogwill compiled and passed all verification tests!"
}

install_launchers() {
    mkdir -p "$BIN_DIR"
    LAUNCHER="$BIN_DIR/cogwill"

    cat > "$LAUNCHER" <<EOF
#!/usr/bin/env bash
cd "$INSTALL_DIR" || exit 1
exec "$INSTALL_DIR/build/cogwill" "\$@"
EOF
    chmod +x "$LAUNCHER"
    ok "Installed CLI launcher: $LAUNCHER"

    if [ "$(uname -s)" = "Linux" ]; then
        DESKTOP_DIR="$HOME/.local/share/applications"
        mkdir -p "$DESKTOP_DIR"
        cat > "$DESKTOP_DIR/cogwill.desktop" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Cogwill
Comment=Clockwork, Atomic & Quantum Factory Automation Game
Exec=$LAUNCHER
Path=$INSTALL_DIR
Terminal=false
Categories=Game;StrategyGame;Simulation;
StartupNotify=true
EOF
        chmod +x "$DESKTOP_DIR/cogwill.desktop"
        ok "Installed desktop menu entry: $DESKTOP_DIR/cogwill.desktop"
    fi
}

if [ "$SKIP_DEPS" -eq 0 ]; then
    install_system_dependencies
else
    info "Skipping system dependency installation (--skip-deps)."
fi

download_cogwill_files
build_and_verify
install_launchers

printf "\n${C_GREEN}${C_BOLD}=========================================================${C_RESET}\n"
printf "${C_GREEN}${C_BOLD}   Cogwill ${VERSION} is installed and ready to play!    ${C_RESET}\n"
printf "${C_GREEN}${C_BOLD}=========================================================${C_RESET}\n\n"
printf "  Install path : %s\n" "$INSTALL_DIR"
printf "  Launcher     : %s/cogwill\n\n" "$BIN_DIR"

case ":$PATH:" in
    *":$BIN_DIR:"*)
        printf "  Run the game anytime with:\n    ${C_BOLD}cogwill${C_RESET}\n\n"
        ;;
    *)
        printf "  Run the game with:\n    ${C_BOLD}%s/cogwill${C_RESET}\n" "$BIN_DIR"
        printf "  (Tip: add ${C_BOLD}export PATH=\"%s:\$PATH\"${C_RESET} to your ~/.bashrc or ~/.zshrc)\n\n" "$BIN_DIR"
        ;;
esac

if [ "$RUN_AFTER" -eq 1 ]; then
    info "Launching Cogwill..."
    exec "$BIN_DIR/cogwill"
fi
