# Roadmap

Planned work for the MediaMTX ACAP. Items are ideas and intentions, not
commitments, and nothing here has a fixed release date. Shipped items move to
[CHANGELOG.md](CHANGELOG.md).

## Planned

### Disk-space janitor for recordings

**Problem.** MediaMTX only supports time-based retention (`recordDeleteAfter`).
There is no size or quota setting upstream. When the storage area fills up, the
recorder simply fails to write and recording stops, even though the configured
retention window has not elapsed. Sizing `recordDeleteAfter` to the disk by hand
is fragile because the recorded bitrate varies with scene activity and Zipstream.

**Proposal.** Add a janitor to the ACAP that enforces a free-space floor
independently of `recordDeleteAfter`:

- New setting `minFreePercent` (default 10), stored alongside the other ACAP
  settings and editable from the web UI.
- Background loop in the startup script [app/MediaMTX](app/MediaMTX), running
  every ~60 s next to the existing supervisor loop.
- When free space on the `recordPath` filesystem drops below the floor, delete
  recorded segments oldest-mtime-first until the floor is met again.
- Never delete the newest segment of a path, since that is the one an active
  recorder is still writing into.
- Log every deletion to syslog so the behaviour is auditable after the fact.
- Surface the janitor's last run and reclaimed bytes in the Recordings page.

**Notes.** The building blocks already exist: `get_record_base()` resolves the
recording root from `recordPath`, `send_storage()` already calls `statvfs()` on
it, and `list_recordings()` already walks the segment tree with mtimes. Deletion
must stay confined to the resolved record base and to `*.mp4` / `*.ts` files, and
must refuse to run at all when `recordPath` is unset.

## Under consideration

- Storage pre-flight check that warns when `recordPath` points at the
  `/var/spool` RAM filesystem instead of a real storage area.
- Estimated retention readout in the UI: measured bytes/day per path versus free
  space, so `recordDeleteAfter` can be chosen with real numbers.
