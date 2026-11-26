# Banner Physics (fork with Skyrim VR support)

An SKSE plugin that gives Skyrim's banners cloth physics through
Faster HDT-SMP (FSMP): wind, weapon and hand
contact, arrows, magic, and wear from damage.

This is a fork of [jpsteel/skyrim-hdt-banners](https://github.com/jpsteel/skyrim-hdt-banners), the original
Banner Physics by jpsteel. All credit for the mod itself goes to jpsteel. This fork adds Skyrim VR support
and a few fixes on top of it.

## What this fork changes

- **Skyrim VR (1.4.15) support.** The plugin loads under SKSEVR and keeps running while the desktop mirror
  window is unfocused.
- **VR hand and weapon interaction** (requires VRIK).
  The player's body comes from VRIK's third-person skeleton, held weapons from the first-person skeleton, and
  melee hits are detected from how fast the weapon is swung. The swing threshold can be tuned in
  `BannerPhysics.ini`:

  ```ini
  [VR]
  ; Weapon speed in game units per second, relative to the player's body, that counts as a swing
  fSwingSpeed=250
  ```

- **Interior room-bounds fix (all runtimes).** CommonLibSSE-NG declares `BSMultiBoundRoom`'s members at
  fixed offsets, which are wrong in builds that target several runtimes. The plugin now reads
  `joinedMultiBoundList` at the correct offset (0x168 on SE/AE, 0x190 on VR). This fixed a crash in
  Helgen Keep on VR.

## Requirements

- SKSE (SE/AE) or SKSEVR
- Address Library for SKSE Plugins, or the VR Address Library for SKSEVR
- Faster HDT-SMP 4.1.1, 4.0.1, 4.0.0 or 3.5.0, AVX or AVX2 build. The plugin hooks into FSMP directly and
  recognizes these builds by their exact file. With any other build it disables itself and banners stay
  static, unless that build's `hdtsmp64.pdb` is installed next to `hdtsmp64.dll`, in which case the plugin
  tries to locate what it needs from FSMP's symbols. Supporting a new FSMP release means adding its build to
  the table in `src/Utility.h`.
- VRIK (Skyrim VR only). Any version that drives the player's third-person body works: the plugin reads that
  skeleton by bone name and doesn't call into VRIK itself.
- The original Banner Physics mod, for its meshes, physics configs, textures and `BannerPhysics.ini`.
  This fork only replaces `SKSE/Plugins/BannerPhysics.dll`.

## Versioning

Versions have four parts. The first three follow jpsteel's upstream release that this fork is based on;
the fourth counts releases of this fork on top of it. For example, 1.3.0.0 is the first release based on
upstream 1.3, 1.3.0.1 would be a fix to it, and an upstream 1.3.1 would be followed by 1.3.1.0.

## Building

Requires Visual Studio 2022 or later (or the Build Tools) with the C++ workload, which includes MSVC, CMake,
Ninja and vcpkg.

```
git clone --recurse-submodules https://github.com/AirWolf359/skyrim-hdt-banners.git
cd skyrim-hdt-banners
build.cmd
```

The output is `build\release\BannerPhysics.dll` and `BannerPhysics.pdb`. The first build also builds the
vcpkg dependencies, which takes a while. `build.cmd` builds through a substituted drive (`B:` by default; set
`BP_DRIVE` to use another letter) so the binaries don't record your local folder path.

CommonLibSSE-NG ([alandtse/CommonLibVR](https://github.com/alandtse/CommonLibVR)) and MinHook are pinned as
submodules under `extern/`.

## License

GPL-3.0, the same as the original. See [LICENSE](LICENSE).
