# Changelog

All notable changes to this project are documented here. Each version
links to its full release notes on GitHub.

The format is based on [Keep a Changelog](https://keepachangelog.com/).

## 1.20.1 - 2026-08-22

- Update to upstream 1.20.1.
- Strip the bundled `mediamtx` binary, which upstream ships unstripped. The
  package is 16.7 MB instead of 29.0 MB.
- Fixed: the shipped `recordPath` pointed at
  `/var/spool/storage/areas/SD_DISK/MediaMTX/recordings`, which is not the SD
  card. Only `.../SD_DISK/root` is the mounted card; the level above it belongs
  to `/var/spool`, a RAM filesystem. Recordings were written to RAM, lost on
  every reboot, and consumed device memory, while the storage figure reported
  the size of the RAM disk so nothing looked wrong. The area is also named
  `HDD_DISK` rather than `SD_DISK` on recorder products, so no single default is
  correct. `recordPath` now ships unset, with both variants present as comments
  to uncomment, and the Recordings page says so instead of reporting a
  fabricated location. Existing installations keep their own configuration and
  are not modified: check the line against the new
  [Recording storage](README.md#recording-storage) section.
- Fixed: with `recordFormat: mpegts`, the Recordings page showed an empty
  archive. Only `.mp4` segments were listed, so `.ts` recordings could not be
  browsed, downloaded or deleted even though they were being written correctly.
  `.ts` segments are now listed and served as `video/mp2t`. The timeline, clip
  export and inline playback remain unavailable for them, because MediaMTX's
  playback server does not support MPEG-TS; the page now says so rather than
  showing nothing. `fmp4` is still the default and the recommended format.
- The bundled default configuration is regenerated from upstream 1.20.1. It had
  been carried forward from an older release, so it still used `runOnReady`,
  `runOnNotReady` and `runOnReadyRestart`, which MediaMTX renamed and now warns
  about on every start, and it was missing settings added since. It also
  predated the MoQ server, which meant MediaMTX enabled MoQ from its own default
  with no line in the configuration to turn it off.
- RTMP (`1935`), SRT (`8890/udp`) and MoQ (`8892`, `8893/udp`) are now disabled
  in the default configuration, so a fresh install no longer opens those ports.
  Set `rtmp`, `srt` or `moq` to `yes` to bring them back. Existing installations
  keep their own configuration and are unaffected.
- Removed the Raspberry Pi camera settings from the default configuration; that
  source type cannot exist on an Axis device.
- The README port table now matches what the package actually opens. It listed
  the metrics and pprof servers, which are disabled, and omitted SRT, MoQ and
  the WebRTC ICE port.

## [1.19.2-Signed] - 2026-07-21 - MediaMTX 1.19.2 (Signed)

- Packages are now signed with the Axis ACAP signing service and install
  normally on AXIS OS 12.10 and later.
- Vendor updated to `moshe@mohome.net` with the registered vendor ID.
- Upgrading from an earlier unsigned version can fail with "Couldn't
  install: app" (device log: "Vendor ID in manifest does not match the
  vendor ID of the previous version"). Back up your config, uninstall the
  old version, then install this one.

## [1.19.2-5] - 2026-07-07

## [1.19.2-4] - 2026-07-03

## [1.19.2-3] - 2026-07-02

## [1.19.2-2] - 2026-07-01

## [1.19.2] - 2026-06-29

## [1.6.0] - 2024-03-21

[1.19.2-5]: https://github.com/Mo3he/Axis_Cam_MediaMTX/releases/tag/v1.19.2-5
[1.19.2-4]: https://github.com/Mo3he/Axis_Cam_MediaMTX/releases/tag/v1.19.2-4
[1.19.2-3]: https://github.com/Mo3he/Axis_Cam_MediaMTX/releases/tag/v1.19.2-3
[1.19.2-2]: https://github.com/Mo3he/Axis_Cam_MediaMTX/releases/tag/v1.19.2-2
[1.19.2]: https://github.com/Mo3he/Axis_Cam_MediaMTX/releases/tag/v1.19.2
[1.6.0]: https://github.com/Mo3he/Axis_Cam_MediaMTX/releases/tag/V1.6.0
