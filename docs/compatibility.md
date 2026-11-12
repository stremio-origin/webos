# Compatibility

| webOS | Status |
|---|---|
| 3.x (2016 to 2017 TVs) | Startup and H.264/AAC MP4 pipeline checked on OLED55E7N, webOS 3.9.2, with a rebuilt package; stability validation ongoing (see below) |
| 4.x (2018 to 2019 TVs) | Tested on webOS 4.4 |
| 5.x and 6.x | Should work, not tested yet |
| 22 to 25 | Should work, not tested yet |

## Install
- Homebrew Channel: add the repository `https://stremio-origin.github.io/webos/apps.json`, then install Stremio Origin.
- Developer Mode: install the `.ipk` from Releases (not tested yet).

## webOS 3 startup fix

The upstream 1.0.0 IPK crashes before `main()` on the OLED55E7N's glibc 2.19
loader. With `BIND_NOW`, that loader processes `DT_REL` and `DT_JMPREL` as one
contiguous range. The upstream binaries put `.ARM.exidx` between `.rel.dyn`
and `.rel.plt`, so unwind data is interpreted as relocations.

The webOS build scripts now use `tools/webos-relocs.ld` to keep the relocation
tables adjacent, without disabling `BIND_NOW` or changing system libraries.
This applies to the app and both optional libraries: the original
`libsbengine.so` and `libsbdts.so` have the same problem and must be rebuilt too.
Do not copy those libraries unchanged from the upstream IPK.

Build the app with the webOS Buildroot SDK and Zig available locally, for example
on Linux:

```sh
export ZIG="$HOME/origin-build/zig-x86_64-linux-0.15.2/zig"
export SDK="$HOME/origin-build/sdk/arm-webos-linux-gnueabi_sdk-buildroot/arm-webos-linux-gnueabi/sysroot"
rustup target add armv7-unknown-linux-gnueabi
tools/build-engine.sh webos
tools/build-ffmpeg.sh webos
tools/build.sh webos
python3 tests/elf/check.py
tools/package.sh
```

The optional-library builds need CMake, Ninja, Make, Rust and Cargo.
`ares-package` from `@webos-tools/cli` must be on `PATH` for packaging.
The ELF check also covers optional `build/webos/libsb*.so` libraries when present;
explicit binary paths can be passed to check other builds.

The complete rebuilt package includes both `libsbengine.so` (libtorrent 2.0.12)
and `libsbdts.so` (FFmpeg 7.1.1 and libdovi 3.3.2). Their ARM binaries have been
checked locally under QEMU with copies of this TV's glibc 2.19 runtime:

- Torrent metadata exchange, download from a localhost seeder, byte-exact HTTP
  range reads and engine shutdown/restart.
- DTS 5.1 and TrueHD 5.1 conversion to AC3 5.1, complete HLS decoding and
  out-of-order segment requests.
- The existing Dolby Vision metadata conversion tests pass on the build host.

Startup, the sign-in UI and the native H.264/AAC playback pipeline were checked
on the TV with the earlier app-only build. The complete package is installed
without launching it; torrent playback, converted audio, HDR/Dolby Vision and
subtitle behavior still need device validation.

The OLED55E7N playback logs also exposed an ACB coordinate mismatch: the app
sent 1280x720 destinations on a 1920x1080 video plane, leaving large black areas
at the right and bottom. webOS 3 now uses 1920x1080 ACB coordinates; webOS 4
retains its 1280x720 mapping. This affects both normal and cropped destinations,
not the source video's aspect ratio. `SB_PLANE=WxH` remains available for
device-specific development overrides. The geometry correction is checked
locally; its on-screen result awaits user testing.

A later crash with `SDL_OpenAudioDevice` on the stack was observed while debug
and launcher instances were overlapping. A subsequent single-instance run has
not reproduced it yet. Audio-device reopening and longer interactive sessions
still need investigation before this build can be called stable. For further
playback probes, use `SB_PLAY_NOSAVE=1` and an isolated app ID/data directory so
test clips do not enter the account's Continue Watching history.
