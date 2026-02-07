TeeworldsArchive ![GitHub Actions](https://github.com/TeeworldsArchive/teeworlds/workflows/Build/badge.svg)
================
Where Teeworlds lives on.

---------------------------

Teeworlds is a free online multiplayer game, available for all major
operating systems. Battle with up to 16 players in a variety of game
modes, including Team Deathmatch and Capture The Flag. You can even
design your own maps!

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software. See license.txt for full license
text including copyright information.

Please visit https://www.teeworlds.com/ for up-to-date information about
the game, including new versions, custom maps and much more.

Originally written by Magnus Auvinen.

---

Teeworlds supports two build systems: CMake and bam.

Building on Linux, Windows or macOS (CMake)
==========================

Installing dependencies
-----------------------
```bash
    # Debian/Ubuntu
    sudo apt install build-essential cmake git libfreetype6-dev libsdl3-dev libpnglite-dev libopus-dev libopusfile-dev python3
    
    # Fedora
    sudo dnf install @development-tools cmake gcc-c++ git freetype-devel pnglite-devel python3 SDL3-devel opus-devel opusfile-devel
    
    # Arch Linux (doesn't have pnglite in its repositories)
    sudo pacman -S --needed base-devel cmake freetype2 git python sdl3 opus opusfile
    
    # macOS
    brew install cmake freetype sdl3 opus opusfile

    # MSYS2 (Windows)
    pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-openssl mingw-w64-ucrt-x86_64-freetype mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-opus mingw-w64-ucrt-x86_64-opusfile
```


Downloading repository
----------------------
```bash
    git clone https://github.com/teeworldsarchive/teeworlds --recurse-submodules
    cd teeworlds
    
    # If you already cloned the repository before, use:
    # git submodule update --init
```

Building
--------
```bash
    mkdir -p build
    cd build
    cmake .. -GNinja
    ninja
```

On subsequent builds, you only have to repeat the `ninja` step.

You can then run the client with `./teeworlds` and the server with
`./teeworlds_srv`.


Build options
-------------

The following options can be passed to the `cmake ..` command line (between the
`cmake` and `..`) in the "Building" step above.

`-GNinja`: Use the Ninja build system instead of Make. This automatically
parallelizes the build and is generally **faster**. (Needs `sudo apt install
ninja-build` on Debian, `sudo dnf install ninja-build` on Fedora, and `sudo
pacman -S --needed ninja` on Arch Linux.)

`-DDEV=ON`: Enable debug mode and disable some release mechanics. This leads to
**faster** builds.

`-DCLIENT=OFF`: Disable generation of the client target. Can be useful on
headless servers which don't have graphics libraries like SDL3 installed.

Building on Linux, Windows or macOS (Bam)
==========================

Installing dependencies
-----------------------
```bash
    # Debian/Ubuntu 25.04+
    sudo apt install bam git libfreetype6-dev libsdl3-dev libpnglite-dev libopus-dev libopusfile-dev python3
    
    # Fedora
    sudo dnf install bam gcc-c++ git freetype-devel pnglite-devel python3 SDL3-devel opus-devel opusfile-devel
    
    # Arch Linux (doesn't have pnglite in its repositories)
    sudo pacman -S --needed base-devel bam freetype2 git python sdl3 opus opusfile
    
    # macOS
    brew install bam freetype sdl3 opus opusfile

    # MSYS2 (Windows)
    pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-openssl mingw-w64-ucrt-x86_64-freetype mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-opus mingw-w64-ucrt-x86_64-opusfile
    
    # other (add bam to your path)
    git clone https://github.com/matricks/bam
    cd bam
    ./make_unix.sh
```

Downloading repository
----------------------
```bash
    git clone https://github.com/teeworldsarchive/teeworlds --recurse-submodules
    cd teeworlds
    
    # If you already cloned the repository before, use:
    # git submodule update --init
```

Building
--------
```bash
    bam
```

The compiled game is located in a sub-folder of `build`. You can run the client from there with `./teeworlds` and the server with `./teeworlds_srv`.


Build options
-------------

One of the following targets can be added to the `bam` command line: `game` (default), `server`, `client`, `content`, `masterserver`, `tools`.

The following options can also be added.

`conf=release` to build in release mode (defaults to `conf=debug`).

`arch=x86` or `arch=x86_64` to force select an architecture.
