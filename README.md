# Cogwill

**Cogwill** is a native C++20 & SFML 3 factory automation game inspired by *Factorio*, *Mindustry*, and *Satisfactory* — blending rotational clockwork torque (`PU` & `RPM`), high-voltage electrical grids (`kW` & `kJ`), express logistics & freight trains, nuclear fission & tokamak fusion, procedural seeded worlds, Sandbox mode, and a 2.5D ray-traced pixel-art renderer.

---

## Quick Install (`v0.0.1`)

Download and run [`cogwill-install.sh`](https://github.com/rappar222/cogwill/releases/download/v0.0.1/cogwill-install.sh) — it automatically installs required system libraries, downloads all game files & assets (automatically fetching **SFML 3** and **nlohmann/json** if your distro doesn't ship them yet), builds an optimized Release binary, and installs the `cogwill` launcher:

```bash
curl -fsSL https://github.com/rappar222/cogwill/releases/download/v0.0.1/cogwill-install.sh -o cogwill-install.sh
chmod +x cogwill-install.sh
./cogwill-install.sh
```

Then start the game anytime with:

```bash
cogwill
```

*(Pass `./cogwill-install.sh --help` for custom `--dir`, `--skip-deps`, `--run`, or `--uninstall` options.)*

---

## Manual Build from Source

### Requirements
- C++20 compiler (`g++ >= 11` or `clang++ >= 14`)
- `cmake >= 3.22` and `ninja` (or `make`)
- **SFML 3** & **nlohmann/json** *(optional on system — CMake `FetchContent` downloads them automatically if missing)*

```bash
git clone https://github.com/rappar222/cogwill.git
cd cogwill
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
./build/cogwill
```

---

## Highlights
- **Dual Power Physics**: Mechanical Cogwheel networks (`RPM`, `Torque`, Gearboxes, Axles, Flywheels, Lubricators) + High-Voltage Electrical Grids (Copper Poles, Substations, Alternators, Solar, Accumulators, Nuclear 3x3 & Tokamak Fusion 3x3).
- **Rooted 40-Node Branching Tech Tree (`[U]`)**: Pan & zoom across 4 primary boughs (*Mining & Matter*, *Clockwork & Craft*, *Power & Atomics*, *Belts & Railways*) and 7 tiers up to *Planetary Ascension*.
- **Seeded Procedural `192x144` Continents (`[N]`)**: 6 biome presets (*Clockwork Valley*, *Archipelago*, *Rich Pangaea*, *Basalt Canyons*, *Arid Wastes*, *Shattered*) with custom world seeds and resource/water/cliff sliders.
- **2.5D Extruded Mode & Ray-Traced Lighting (`[F9]` / `[F10]` / `[O]`)**: Real-time multi-ray GLSL global illumination, soft sun & cliff shadows, SSAO, water reflections, and day/night cycle presets.
- **5-Phase Grand Clockwork Spire & Matter Sink (`[V]`)**: Launch orbital Spire phases and shred surplus production for Factory Coupons.
- **Full Creative Sandbox Mode (`[B]`)**: Free building, instant research, infinite power, or 1-click *Unlock All 40 Techs*.
