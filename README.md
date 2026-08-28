# MediaMTX ACAP for Axis Cameras

[![Release](https://img.shields.io/github/v/release/Mo3he/Axis_Cam_MediaMTX?style=flat)](https://github.com/Mo3he/Axis_Cam_MediaMTX/releases)
[![License](https://img.shields.io/github/license/Mo3he/Axis_Cam_MediaMTX?style=flat)](LICENSE)
[![CI](https://github.com/Mo3he/Axis_Cam_MediaMTX/actions/workflows/ci.yml/badge.svg)](https://github.com/Mo3he/Axis_Cam_MediaMTX/actions/workflows/ci.yml)
[![Super-Linter](https://github.com/Mo3he/Axis_Cam_MediaMTX/actions/workflows/super-linter.yml/badge.svg)](https://github.com/Mo3he/Axis_Cam_MediaMTX/actions/workflows/super-linter.yml)
[![Sponsor](https://img.shields.io/badge/Sponsor%20My%20Work-EA4AAA?style=flat&logo=github&logoColor=white)](https://github.com/sponsors/Mo3he)
[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-FFDD00?style=flat&logo=buy-me-a-coffee&logoColor=black)](https://www.buymeacoffee.com/mo3he)

This ACAP packages the [MediaMTX](https://github.com/bluenviron/mediamtx)
real-time media server so it can be installed and run directly on Axis cameras,
with a built-in web interface for editing its configuration.

> **Disclaimer:** Independent, community-developed ACAP package. Not an official
> Axis product and not affiliated with, endorsed by, or supported by Axis
> Communications AB or the MediaMTX project. Use at your own risk.

## Table of Contents

- [Overview](#overview)
- [Compatibility](#compatibility)
- [Installation](#installation)
- [Configuration](#configuration)
- [Recording storage](#recording-storage)
- [Viewing live streams](#viewing-live-streams)
- [Viewing recordings](#viewing-recordings)
- [Ports & security](#ports--security)
- [Build from source](#build-from-source)
- [Links](#links)
- [License](#license)

## Overview

MediaMTX (formerly rtsp-simple-server) is a ready-to-use and zero-dependency
real-time media server and media proxy that allows you to publish, read, proxy,
record and play back video and audio streams. It has been conceived as a "media
router" that routes media streams from one end to the other.

- Runs MediaMTX as a non-root ACAP (no root filesystem changes required).
- **Built-in web configuration editor:** edit `mediamtx.yml` directly from the
  browser with YAML syntax highlighting, then save and restart the server to
  apply changes. No SSH or SFTP needed.
- **Live view page:** lists the stream paths in your configuration with
  one-click links to MediaMTX's built-in HLS and WebRTC players and copyable
  RTSP URLs.
- **VMS-style playback timeline:** the Recordings page shows a per-stream,
  per-day timeline of recorded footage. Click any moment to play from that exact
  time (stitched across segment files), drag to select a range, and export the
  selection as a standard MP4, addressed by time, not by file.
- **Recordings browser:** list recorded segments with a date/time filter, play
  or download them in the browser, delete individual segments, and see the
  storage card's disk usage.
- **Upgrade-safe configuration:** your `mediamtx.yml` is stored in the app's
  persistent `localdata` directory, so it is preserved across ACAP updates.
- **Config backup and crash-loop protection:** every save keeps a backup of the
  previous configuration (restorable with **Load backup**), and after a restart
  the editor watches the server and warns if the new configuration makes
  MediaMTX crash-loop.
- **Recording:** disabled by default and enabled per path. Before enabling it,
  choose a storage location: see [Recording storage](#recording-storage).
- Supervised process: MediaMTX is automatically relaunched if it exits.

## Compatibility

- **AXIS OS:** 11.x through 13.
- **Verified on AXIS OS 13** (13.0.0, aarch64).
- **Architectures:** `aarch64` and `armv7hf`.

## Installation

> **Signed packages:** Release `.eap` files are signed with the Axis ACAP
> signing service and install normally on AXIS OS 12.10 and later.
>
> **Upgrading from an earlier version?** The signing vendor changed, so
> installing over a previously installed unsigned build can fail with
> **"Couldn't install: app"** (device log: *"Vendor ID in manifest does not
> match the vendor ID of the previous version"*). To upgrade: back up your app
> configuration, **uninstall** the old version, then install the signed one.

The recommended way to install is to use the pre-built `.eap` file from the
[Releases](https://github.com/Mo3he/Axis_Cam_MediaMTX/releases) page:

1. Download the `.eap` matching your architecture (`aarch64` or `armv7hf`).
2. On the camera, go to **Apps** and click **Add app**.
3. Select the downloaded `.eap` and install.
4. Start the application.

## Configuration

A working default configuration is installed automatically the first time the
app runs, so the server is usable immediately.

To change the configuration, open the app's settings page (click **Open** on the
Apps page, or browse to `https://<device ip>/local/MediaMTX/index.html`). The
page provides a full editor for `mediamtx.yml`:

- **Reload:** reload the current configuration from the device.
- **Save:** write your changes to `mediamtx.yml` (the previous version is kept
  as a backup).
- **Save & Restart:** save and restart MediaMTX to apply the changes. The page
  then watches the server for a few seconds and warns if the new configuration
  makes it crash-loop.
- **Load defaults:** load the bundled default configuration into the editor
  (not saved until you click Save).
- **Load backup:** load the configuration as it was before the last save, for
  recovering from a bad edit.
- **Show log:** view the application system log.

The editor is admin-access only and authenticates against the device user pool,
the same as VAPIX.

### Changes made through the MediaMTX Control API are not saved

MediaMTX's own Control API on port `9997` (`/v3/config/global/patch`,
`/v3/config/paths/add/...` and similar) applies changes to the **running**
server only. MediaMTX never writes them back to `mediamtx.yml`, so they are
lost the next time the server restarts or the camera reboots. This is upstream
MediaMTX behaviour, not a limitation of this package.

Use the settings page, or its `config.cgi` endpoint, for any change that has to
survive a restart:

```sh
# read the current configuration
curl -k --anyauth -u USER:PASS \
  "https://<device ip>/local/MediaMTX/config.cgi" > mediamtx.yml

# edit mediamtx.yml, then write it back
curl -k --anyauth -u USER:PASS -X POST --data-binary @mediamtx.yml \
  "https://<device ip>/local/MediaMTX/config.cgi"

# apply it
curl -k --anyauth -u USER:PASS -X POST \
  "https://<device ip>/local/MediaMTX/config.cgi?action=restart"
```

The Control API remains useful for inspecting state and for temporary runtime
changes; just treat anything set there as volatile.

### Enabling a commented-out example

The bundled configuration contains ready-made examples that are commented out.
Each one is written so that **deleting the `#` character alone** leaves valid,
correctly indented YAML, because the `#` sits in a column that would otherwise
be indentation. Do not delete the space after it as well: that shifts the line
out of alignment and MediaMTX will refuse to start.

Three settings are the exception: `webrtcICEServers2`, `alwaysAvailableTracks`
and `forward`. MediaMTX rejects an empty value for these, so they ship as
`[]` and their examples cannot be switched on in place. Replace the `[]` line
with the commented block that follows it, uncommented.

## Recording storage

Recording is disabled by default, and **`recordPath` is deliberately left unset**
in the bundled configuration. There is no value that is correct on every device,
because the storage area is named after the disk that is fitted. Choose one
before you enable recording.

| Device | Storage area | `recordPath` |
| --- | --- | --- |
| Camera with an SD card | `SD_DISK` | `/var/spool/storage/areas/SD_DISK/root/MediaMTX/recordings/%path/%Y-%m-%d_%H-%M-%S-%f` |
| Recorder with an internal disk (AXIS S30 series and similar) | `HDD_DISK` | `/var/spool/storage/areas/HDD_DISK/root/MediaMTX/recordings/%path/%Y-%m-%d_%H-%M-%S-%f` |

Both lines are present, commented out, in the bundled configuration: delete the
`#` character from the one that matches your device. The example indentation is
already valid when uncommented this way. If neither does, the app's **Show log** view
and the device shell both list the mounted areas under
`/var/spool/storage/areas/`.

> **Only the `root` directory is the real disk.** Everything above
> `.../<AREA>/root` belongs to `/var/spool`, which is a RAM filesystem.
> Recording there is lost on every reboot and can exhaust the device's memory.
> The giveaway is the storage figure on the Recordings page: if it reports a few
> hundred megabytes rather than the size of your card or disk, `recordPath` is
> pointing at RAM. Versions up to and including 1.20.1 shipped a default that
> did exactly this; if you are upgrading, check the line and correct it.

### Recording format

`recordFormat: fmp4` (the default) writes `.mp4` segments and is what this
application is built around: the playback timeline, clip export and inline
playback all require it.

`recordFormat: mpegts` writes `.ts` segments. These are listed on the Recordings
page and can be downloaded and deleted, but MediaMTX's own playback server
rejects them (*"MPEG-TS format is not supported yet"*), so the timeline and clip
export are unavailable, and browsers cannot play a raw MPEG-TS file, so the
**Play** button is disabled. Use `mpegts` only if something downstream needs it;
otherwise stay on `fmp4`.

## Viewing live streams

The **Live** page (`https://<device ip>/local/MediaMTX/live.html`) lists every
path in the configuration with links to MediaMTX's built-in HLS and WebRTC
players and a copyable RTSP URL. The players are served by MediaMTX itself on
their own ports (8888 for HLS, 8889 for WebRTC by default), so they open in a new
tab; make sure those ports are reachable from your browser.

### Example: allow anonymous viewing of an RTSP stream

Adding the following to `mediamtx.yml`:

```yaml
paths:
  proxied:
    source: rtsp://user:password@IPAddress/axis-media/media.amp?videocodec=h264&resolution=640x480
```

makes the stream available at `rtsp://IPAddress:8554/proxied` with no
authentication.

### RTSP source transport

The bundled configuration sets `rtspTransport: tcp` for pulled RTSP sources,
where MediaMTX itself defaults to `automatic` (which prefers UDP). A camera
pulling its own sensor over `127.0.0.1` loses RTP packets over UDP, which
corrupts the H.264 stream and restarts the recorder every few seconds, leaving
recordings in short fragments. UDP does not fail outright in that situation, so
the automatic fallback to TCP never happens. Set it back to `automatic` if you
have a source that does not support TCP.

## Viewing recordings

When recording is enabled for a path, segments are written to the storage
location set by `recordPath`, which you must choose for your device: see
[Recording storage](#recording-storage). Open the **Recordings** page from the
link in the settings page header, or browse to
`https://<device ip>/local/MediaMTX/recordings.html`. Like the config editor, it
is admin-access only.

### Playback timeline

The top of the page is a VMS-style timeline: pick a stream and a day, and the
recorded periods are drawn on a 24-hour bar (gaps stay empty). Click anywhere in
a recorded period to start playback from that moment; MediaMTX's playback server
extracts the footage by time and stitches it across segment files, so playback
runs seamlessly past file boundaries and jumps over gaps automatically. Drag on
the bar to select a range, then play it or download it as a standard MP4 clip.

The timeline is powered by the MediaMTX playback server, which the default
configuration enables on `127.0.0.1:9996` (localhost only, it is never exposed
to the network; the web UI reaches it through the authenticated `config.cgi`
proxy). If you upgraded with an existing configuration, add these two lines and
Save & Restart to activate the timeline:

```yaml
playback: yes
playbackAddress: 127.0.0.1:9996
```

### Segment files

Below the timeline, the recorded segments are listed with a stream and date/time
filter; each can be played inline, downloaded, or deleted. `.ts` segments
(`recordFormat: mpegts`) are listed and can be downloaded or deleted but not
played. The page also shows how full the recording storage is.

Your configuration is stored in the app's persistent `localdata` directory and
is kept across application upgrades. Uninstalling the ACAP removes all files,
including the configuration and any recordings stored under the app's recordings
path.

## Ports & security

These are the ports the bundled configuration actually opens on the device:

| Service | Port | Enabled by default |
|---|---|---|
| RTSP / RTSPS | `8554` / `8322` | Yes |
| RTP / RTCP | `8000` / `8001` (UDP) | Yes |
| HLS | `8888` | Yes |
| WebRTC | `8889`, `8189` (UDP/ICE) | Yes |
| Control API | `9997` | Yes, the web pages need it |
| Playback | `127.0.0.1:9996` | Yes, localhost only |
| RTMP | `1935` | No |
| SRT | `8890` (UDP) | No |
| MoQ | `8892`, `8893` (UDP) | No |
| Metrics | `9998` | No |
| pprof (debug) | `9999` | No |

MediaMTX itself enables RTMP, SRT and MoQ; this package turns them off so a
camera does not open those ports unless you ask it to. Set `rtmp`, `srt` or
`moq` to `yes` in `mediamtx.yml` if you need them.

> **Security:** firewall the media ports to trusted networks. The control API on
> `9997` is unauthenticated and is required by the settings and Recordings
> pages: if you do not need those pages, set `api: no`. The playback server is
> bound to localhost and is reached only through the authenticated `config.cgi`
> proxy.

Note that configuration changes made through the control API are not written to
`mediamtx.yml`: see
[Changes made through the MediaMTX Control API are not saved](#changes-made-through-the-mediamtx-control-api-are-not-saved).

## Build from source

The MediaMTX binary is **not** stored in this repository. It is downloaded from
the official [bluenviron/mediamtx](https://github.com/bluenviron/mediamtx/releases)
release and verified against its published `checksums.sha256` during the Docker
build.

The quickest way is the convenience script, which auto-detects docker/podman,
builds both architectures, and drops the `.eap` files in the repository root:

```sh
./build.sh
```

To bundle a specific MediaMTX version, set `MEDIAMTX_VERSION` (without the
leading `v`):

```sh
MEDIAMTX_VERSION=1.19.2 ./build.sh
```

Or drive the build manually. Both architectures build from the single
`Dockerfile` in the repository root; select one with the `ARCH` build argument
(`aarch64` or `armv7hf`):

```sh
docker build --build-arg ARCH=aarch64 --tag <package name> .
docker cp $(docker create <package name>):/opt/app ./build
```

The `.eap` package is created under `/opt/app` inside the image.

### Tests

Host-side unit tests cover the MP4 box parsing used by the recordings player and
the request helpers in `config.c`. They build against a stub FastCGI header, so
no dependencies are needed:

```sh
cc -Wall -Wextra -Werror -fsanitize=address,undefined -Itests/fcgi_stub tests/test_mp4.c -o tests/test_mp4
./tests/test_mp4
```

They also run in CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) on
every push and pull request, together with a full Docker build of both
architectures.

### Automated releases

A GitHub Actions workflow
([`.github/workflows/build-on-mediamtx-release.yml`](.github/workflows/build-on-mediamtx-release.yml))
runs daily, detects new upstream MediaMTX releases, bumps the packaged version,
builds both architectures, and publishes a matching release with the `.eap` files
attached. It can also be run manually from the Actions tab, optionally targeting
a specific version or, with the **force** option, rebuilding and republishing the
current version after a packaging change.

## Links

- [Roadmap](ROADMAP.md)
- [MediaMTX](https://github.com/bluenviron/mediamtx)
- [Axis Communications](https://www.axis.com/)

## License

The packaging code in this repository is licensed under BSD 3-Clause (see
[LICENSE](LICENSE)). The bundled MediaMTX binary is MIT-licensed (aler9 /
bluenviron); see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for full
attribution and links (MediaMTX: <https://github.com/bluenviron/mediamtx>).
