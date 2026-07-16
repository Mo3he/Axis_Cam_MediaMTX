# The MediaMTX ACAP

This ACAP packages the [MediaMTX](https://github.com/bluenviron/mediamtx) real-time
media server so it can be installed and run directly on Axis cameras, with a built-in
web interface for editing its configuration.

Current version: **1.19.2** (MediaMTX v1.19.2)

[![Sponsor](https://img.shields.io/badge/Sponsor%20My%20Work-grey?logo=github)](https://github.com/sponsors/Mo3he)  
[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-grey?style=flat&logo=buy-me-a-coffee)](https://www.buymeacoffee.com/mo3he)

## Disclaimer

This is an independent, community-developed ACAP package and is not an official Axis
Communications product. It is not affiliated
with, endorsed by, or supported by Axis Communications AB. Use it at your own risk. For
official Axis software, visit axis.com

## Purpose

MediaMTX (formerly rtsp-simple-server) is a ready-to-use and zero-dependency real-time
media server and media proxy that allows you to publish, read, proxy, record and play
back video and audio streams. It has been conceived as a "media router" that routes media
streams from one end to the other.

## Features

- Runs MediaMTX as a non-root ACAP (no root filesystem changes required).
- **Built-in web configuration editor** — edit `mediamtx.yml` directly from the browser
  with YAML syntax highlighting (enabled `yes`/`true` values shown in green, disabled
  `no`/`false` in red, dimmed comments, line numbers), then save and restart the server
  to apply changes. No SSH or SFTP needed.
- **Live view page** — lists the stream paths in your configuration with one-click
  links to MediaMTX's built-in HLS and WebRTC players and copyable RTSP URLs, at
  `https://<device ip>/local/MediaMTX/live.html`.
- **VMS-style playback timeline** — the Recordings page shows a per-stream, per-day
  timeline of recorded footage. Click any moment to play from that exact time
  (stitched across segment files), drag to select a range, and export the selection
  as a standard MP4 — footage is addressed by time, not by file.
- **Recordings browser** — below the timeline, list recorded segments with a
  date/time filter, play or download them in the browser, delete individual
  segments, and see the storage card's disk usage, served at
  `https://<device ip>/local/MediaMTX/recordings.html`.
- **Upgrade-safe configuration** — your `mediamtx.yml` is stored in the app's persistent
  `localdata` directory, so it is preserved when you update the ACAP to a newer version.
- **Config backup and crash-loop protection** — every save keeps a backup of the
  previous configuration (restorable from the editor with **Load backup**), and after a
  restart the editor watches the server and warns if the new configuration makes
  MediaMTX crash-loop. The supervisor backs off instead of restarting a broken config
  every second.
- A working default configuration is installed automatically on first run.
- **SD card recording** — the default `recordPath` points to the camera SD card
  (`/var/spool/storage/areas/SD_DISK/MediaMTX/...`); recording is disabled by default and
  can be enabled per path from the config editor.
- Supervised process: MediaMTX is automatically relaunched if it exits.

## Links

- MediaMTX: <https://github.com/bluenviron/mediamtx>
- MediaMTX configuration reference: <https://github.com/bluenviron/mediamtx?tab=readme-ov-file#configuration>
- Axis: <https://www.axis.com/>

## Compatibility

Compatible with Axis cameras with `arm` (armv7hf) and `aarch64` based SoCs. The packages
are built with the ACAP Native SDK 12.10.0 (Ubuntu 24.04 base), use ACAP manifest schema
2.0.0, and declare compatibility through AXIS OS 13.

The manifest includes the Axis ACAP Portal vendorId and is ready for portal signing.

To check your device architecture:

```sh
curl --anyauth "*" -u <username>:<password> <device ip>/axis-cgi/basicdeviceinfo.cgi --data "{\"apiVersion\":\"1.0\",\"context\":\"Client defined request ID\",\"method\":\"getAllProperties\"}"
```

where `<device ip>` is the IP address of the Axis device, `<username>` is the root
username and `<password>` is the root password. Please note that you need to enclose your
password in quotes (`'`) if it contains special characters.

## Installing

The recommended way to install is to use the pre-built `.eap` file from the
[Releases](https://github.com/Mo3he/Axis_Cam_MediaMTX/releases) page:

1. Download the `.eap` matching your architecture (`aarch64` or `armv7hf`).
2. On the camera, go to **Apps** and click **Add app**.
3. Select the downloaded `.eap` and install.
4. Start the application.

## Configuring MediaMTX

A working default configuration is installed automatically the first time the app runs,
so the server is usable immediately.

To change the configuration, open the app's settings page (click **Open** on the Apps
page, or browse to `https://<device ip>/local/MediaMTX/index.html`). The page provides a
full editor for `mediamtx.yml`:

- **Reload** — reload the current configuration from the device.
- **Save** — write your changes to `mediamtx.yml` (the previous version is kept as a
  backup).
- **Save & Restart** — save and restart MediaMTX to apply the changes. The page then
  watches the server for a few seconds and warns if the new configuration makes it
  crash-loop.
- **Load defaults** — load the bundled default configuration into the editor (not saved
  until you click Save).
- **Load backup** — load the configuration as it was before the last save (not saved
  until you click Save), for recovering from a bad edit.
- **Show log** — view the application system log.

The editor is admin-access only and authenticates against the device user pool, the same
as VAPIX.

## Viewing live streams

The **Live** page (`https://<device ip>/local/MediaMTX/live.html`) lists every path in
the configuration with links to MediaMTX's built-in HLS and WebRTC players and a
copyable RTSP URL. The players are served by MediaMTX itself on their own ports (8888
for HLS, 8889 for WebRTC by default), so they open in a new tab; make sure those ports
are reachable from your browser.

### Example: allow anonymous viewing of an RTSP stream

Adding the following to `mediamtx.yml`:

```yaml
paths:
  proxied:
    source: rtsp://user:password@IPAddress/axis-media/media.amp?videocodec=h264&resolution=640x480
```

makes the stream available at `rtsp://IPAddress:8554/proxied` with no authentication.

## Viewing recordings

When recording is enabled for a path, segments are written to the storage location set by
`recordPath` (the SD card by default; some recorder devices use an internal disk such as
`HDD_DISK`). Open the **Recordings** page from the link in the settings page header,
or browse to `https://<device ip>/local/MediaMTX/recordings.html`. Like the config
editor, it is admin-access only.

### Playback timeline

The top of the page is a VMS-style timeline: pick a stream and a day, and the
recorded periods are drawn on a 24-hour bar (gaps stay empty). Click anywhere in a
recorded period to start playback from that moment — MediaMTX's playback server
extracts the footage by time and stitches it across segment files, so playback runs
seamlessly past file boundaries and jumps over gaps automatically. Drag on the bar to
select a range, then play it or download it as a standard MP4 clip.

The timeline is powered by the MediaMTX playback server, which the default
configuration enables on `127.0.0.1:9996` (localhost only — it is never exposed to
the network; the web UI reaches it through the authenticated `config.cgi` proxy). If
you upgraded with an existing configuration, add these two lines and Save & Restart
to activate the timeline:

```yaml
playback: yes
playbackAddress: 127.0.0.1:9996
```

### Segment files

Below the timeline, the recorded `.mp4` segments are listed with a stream and
date/time filter; each can be played inline, downloaded, or deleted. The page also
shows how full the recording storage is.

Your configuration is stored in the app's persistent `localdata` directory and is kept
across application upgrades. Uninstalling the ACAP removes all files, including the
configuration and any recordings stored under the app's recordings path.

## Build from source

The MediaMTX binary is **not** stored in this repository. It is downloaded from the
official [bluenviron/mediamtx](https://github.com/bluenviron/mediamtx/releases) release
and verified against its published `checksums.sha256` during the Docker build.

The quickest way is the convenience script, which auto-detects docker/podman, builds
both architectures, and drops the `.eap` files in the repository root:

```sh
./build.sh
```

To bundle a specific MediaMTX version, set `MEDIAMTX_VERSION` (without the leading `v`):

```sh
MEDIAMTX_VERSION=1.19.2 ./build.sh
```

Or drive the build manually. Both architectures build from the single `Dockerfile` in
the repository root; select one with the `ARCH` build argument (`aarch64` or `armv7hf`):

```sh
docker build --build-arg ARCH=aarch64 --tag <package name> .
```

To build a specific MediaMTX version, pass the version build argument as well (without
the leading `v`):

```sh
docker build --build-arg ARCH=aarch64 --build-arg MEDIAMTX_VERSION=1.19.2 --tag <package name> .
```

Then copy the resulting `.eap` out of the image, e.g.:

```sh
docker cp $(docker create <package name>):/opt/app ./build
```

The `.eap` package is created under `/opt/app` inside the image.

## Tests

Host-side unit tests cover the MP4 box parsing used by the recordings player and the
request helpers in `config.c`. They build against a stub FastCGI header, so no
dependencies are needed:

```sh
cc -Wall -Wextra -Werror -fsanitize=address,undefined -Itests/fcgi_stub tests/test_mp4.c -o tests/test_mp4
./tests/test_mp4
```

They also run in CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) on every
push and pull request, together with a full Docker build of both architectures.

## Automated releases

A GitHub Actions workflow
([`.github/workflows/build-on-mediamtx-release.yml`](.github/workflows/build-on-mediamtx-release.yml))
runs daily, detects new upstream MediaMTX releases, bumps the packaged version, builds
both architectures, and publishes a matching release with the `.eap` files attached. It
can also be run manually from the Actions tab, optionally targeting a specific version
or, with the **force** option, rebuilding and republishing the current version after a
packaging change.
