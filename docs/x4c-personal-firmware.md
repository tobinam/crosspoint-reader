# Personal X4 Classic firmware

This downstream firmware targets the ESP32-S3 X4 Classic with 8 MB PSRAM.
SAI dashboards, a persistent inbox and Ink stories are downstream goals, not
features proposed for upstream CrossPoint. Host-side development is in the
separate SAI repository's `xteink/` package.

## Dock sleep policy (initial implementation)

Only X4 Classic builds change automatic sleep behavior:

- While a native USB host bus is active, automatic sleep is inhibited, including
  after the battery finishes charging and without opening the serial port.
- Charging/external-power telemetry also inhibits automatic sleep.
- After detected dock power disappears, the configured inactivity timeout must
  pass before automatic sleep. A later user input restarts that timeout normally.
- Manual power-button sleep remains unchanged.
- CPU idle power-saving still uses the original user-activity timer.
- No new setting, persisted state, task or heap allocation is introduced. The
  policy stores one 32-bit timestamp in the Arduino loop. Other board profiles
  retain their original automatic sleep policy.

The HAL uses ESP-IDF's `usb_serial_jtag_is_connected()`, which observes host SOF
packets. ESP-IDF documents extra work on each FreeRTOS tick for this detector;
actual power impact has not been measured. A power-only charger emits no SOF,
so after charge termination it cannot reliably inhibit sleep on this hardware.
A sleeping/suspended host may also stop emitting SOF. This implementation does
not promise to detect external power under those conditions.

The USB host signal is separate from `HalGPIO::isUsbConnected()`: boot/wake
classification keeps its existing behavior. The dock signal is not an
authenticated SAI connection and does not switch modes or accept commands.

### Verification

Host regression tests:

```sh
cmake -S test -B /tmp/xteink-host-tests
cmake --build /tmp/xteink-host-tests --target DockSleepPolicyTest
ctest --test-dir /tmp/xteink-host-tests -R DockSleepPolicy --output-on-failure
```

Firmware build: `pio run -e x4c`.

Hardware checks, after an explicitly approved test flash:

1. Set a short nonzero Time to sleep. With no cable, confirm normal auto-sleep.
2. Connect to an awake USB host and leave its serial port closed. Stay awake
   longer than the configured timeout. Repeat with a fully charged battery.
3. Unplug after a long dock interval. Confirm it stays awake for a full timeout,
   then sleeps; input during that interval should restart the timeout.
4. Confirm manual power-button sleep still works while docked and on battery.
5. Repeat with the host suspended and with a power-only charger, recording the
   detection limits rather than treating them as guaranteed always-awake cases.
6. Confirm CPU-idle power-saving still occurs; measure actual draw if available.

### Current handoff status

Compared with `develop` at `818a1540`, changes are limited to `HalGPIO.h/.cpp`,
the auto-sleep block in `src/main.cpp`, `src/util/DockSleepPolicy.h`, the host test
registration and `test/dock_sleep/`, plus this document. The SDK and partitions
are unchanged.

Checks completed:

- Five `DockSleepPolicy` CTest cases pass, including unplug timeout and rollover.
- `./bin/clang-format-fix -g` passes using clang-format 21; diff whitespace check passes.
- `pio run -e x4c` succeeds. Log: `/tmp/xteink-dock-build.log`.
- Baseline static RAM: 111,412 bytes; changed: 111,420 bytes (+8).
- Baseline firmware flash: 5,406,639 bytes; changed: 5,406,711 bytes (+72).
- Runtime heap, power consumption and actual host detection remain unmeasured.

Device behavior is not hardware-verified. Four independent review agents required by the repository's
handoff procedure are unavailable in the current tool set; independent reviews
remain outstanding. The user accepted the dock timer ownership and detection
fallback described above. This is not a release-ready or hardware-verified build.

## Six-tile launcher (initial implementation)

X4 Classic Home now opens a two-column, three-row launcher:

| Left | Right |
| --- | --- |
| Reader | Stories (soon) |
| Hosts (soon) | TV remote (soon) |
| Inbox (soon) | Settings |

Reader opens the existing CrossPoint Home as a reader hub. Its books, library,
file browser, plugins/OPDS and file-transfer options remain accessible. Back from
that hub returns to the launcher instead of resuming a book; the button hint now
says Back. The global Home gesture also returns to the launcher. Other board
profiles keep the original Home flow.

Settings opens the existing settings screen. The four unfinished destinations
are explicitly labelled `(soon)` and show `Not implemented yet` when selected;
activating them does not start a fake mode, network connection or story player.

Both side and front navigation use ButtonNavigator's logical next/previous
mapping: one step per press, wrapping across the six tiles, with its normal hold
repeat. Confirm activates on release. UI geometry derives from the renderer's
oriented viewable area and the theme's button-hint band. The SDK's linear layout
splits that area into rows and columns; no separate touch hit-testing is added.
English strings are translation sources with normal language fallback.

The launcher owns one checked activity allocation including the existing
UiAppHost interaction tables, an atomic selected mode, reusable button props,
and navigation state. Its object size in the X4C ELF is 1,536 bytes; the reader
hub's HomeActivity object is 184 bytes. They replace rather than retain each
other, but both objects briefly coexist during deferred navigation (1,720 bytes
before allocator overhead). An outgoing activity's existing caches can also
coexist with the incoming launcher until ActivityManager releases it. Rendering
adds no framebuffer or explicit heap allocation. Allocation failure logs and
leaves the current activity in place. Initial launcher heap samples are recorded
below; peak transition heap and runtime stack measurements are still pending.

This milestone does not add mode persistence or USB auto-switching. Existing
book resume behavior remains in place; full last-mode startup will be added
alongside working non-reader modes.

### Launcher verification

Build/run `LauncherLayoutTest`, `DockSleepPolicyTest` and `HomeButtonInputTest`
with CMake/CTest. Eight cases pass. Existing side-button/orientation and
button-remap-release scripts pass as well. Layout tests cover portrait and
landscape dimensions with four different oriented/inset content rectangles;
they are not pixel screenshots or hardware rotation tests.

On hardware, test all four orientations and remapped front/side buttons:

1. Home shows six readable, non-overlapping tiles in two columns, with hints
   clear of the bezel.
2. Each navigation press advances exactly once; holding repeats. Confirm acts
   once on release, including after a Home gesture or a child-screen return.
3. Reader exposes existing library/file-transfer functions. Back returns to the
   launcher, not a book. Home from an open book returns to the launcher.
4. Settings opens and returns with Settings selected. Each `(soon)` destination
   remains visibly unavailable and starts no remote action.
5. Wake from sleep clears the old sleep frame correctly. Verify normal book
   resume still works; do not mistake it for full last-mode persistence.

Final `pio run -e x4c` succeeds; log:
`/tmp/xteink-launcher-final-build.log`. Static RAM is 111,420 bytes (unchanged
from the dock-only build). Firmware flash is 5,410,727 bytes (+4,016 from the
dock-only build), leaving 1,142,873 bytes in the existing app slot. This is build
headroom, not a prediction of Ink runtime memory or eventual firmware size.
Object sizes were inspected from DWARF using the target GDB, without device
access. Formatting and diff whitespace checks pass. No SDK, partitions or
persistent formats changed by the launcher/sleep code. The first authorized
development flash is recorded below; no firmware was published.

### First authorized device installation

On 2026-11-09, the user explicitly approved backup and installation on the
connected X4 Classic. Esptool identified ESP32-S3 revision 0.2, 8 MB embedded
PSRAM and 16 MB flash, MAC `1c:29:04:27:35:1c`.

A complete 16,777,216-byte pre-install flash backup was saved privately at:
`~/.local/share/xteink/backups/20261109-144309-1c290427351c/flash-before.bin`.
Its digest was independently verified against the device before writing. The
same directory contains its SHA-256, the exact installed factory image,
source diff, restore notes, installation log and boot serial log. The backup
can contain credentials; keep it private and do not commit it.

Esptool 5.3.0 backup transfers stalled, including smaller-block retries. The
isolated esptool 4.11.0 backup completed and verified successfully. Installation
used that version, wrote the factory image at offset zero, and passed flash
hash verification before resetting. The factory image changes the OEM flash
partition layout to this repository's existing `partitions.csv`; this is why
the backup covers all flash, not just the original app. No SD flashing or
formatting was performed.

The boot log records CrossPoint `2.0.0-x4c`, UC8279 detection, successful display
initialization, entry to Launcher at 2,290 ms and completion of its refresh at
2,849 ms. Early missing NVS namespace messages occurred on first boot; they did
not prevent initialization. At 10 and 20 seconds, the launcher reported internal
heap free 172,848 bytes (minimum 170,924; largest block 118,772) and PSRAM free
8,288,472 bytes. These are initial idle samples, not worst-case mode-transition
measurements or a power budget. The low-power-mode log here is CPU idle, not
proof of deep sleep.

The human accepted the initial launcher's appearance/navigation and asked for
larger fonts and more interesting tiles later. Rotated-layout and dock sleep
testing remain pending. Independent review agents remain unavailable; launcher
handoff is incomplete.

## Authorized experimental USB installation

On 2026-11-09 the user authorized the live-page build, then instructed us not to
repeat full flash backups of our own installed builds. The redundant read was
stopped. Keep the original OEM backup and exact installed images instead.

The app from `/tmp/xteink-usb-clean-build.log` was installed at `0x10000` with
esptool 4.11.0, preserving the current partition table and NVS. Flash hash
verification passed. Exact app, SHA-256 and installation/session logs are in
`~/.local/share/xteink/backups/20261109-144309-1c290427351c/usb-prototype-update/`.
No SD flashing or formatting was performed. The dock sleep policy is included.

This build uses 114,844 bytes static RAM and 5,413,515 bytes firmware flash.
Four host C++ protocol tests pass; 27 SAI tests pass after adding paced USB
transmission coverage. Full last-mode persistence and durable device Inbox
remain unimplemented. Independent reviews remain unavailable.

The console accepts a bounded `XT1 PAGE` line with nonzero decimal 32-bit ID and
hex-encoded UTF-8 title/body (64 and 768 bytes respectively). The 1,728-byte input
buffer is loop-owned, with a 128-byte work budget and three-second inter-byte
timeout. Invalid UTF-8, controls, NUL and oversized lines are rejected. The
existing screenshot command is dispatched through the same bounded reader on
X4C; other boards keep the previous path.

An initial unpaced send did not complete delivery. The Arduino HWCDC RX queue is
256 bytes; a paced retry (32-byte chunks, 50 ms apart, after newline sync) worked.
SAI's `send_frame` and CLI now use that pacing. The device entered USBPage and
emitted correlated `XT1 RECEIVED 1794236603 1` after panel refresh. The human was
asked to press Confirm (OPEN) or Back (DISMISS). The human confirmed the page on
the X4 and dex recorded `XT1 ACTION 1794236603 OPEN`; the listener exited
successfully. This verifies one correlated hardware page/display/button round
trip. The page is pushed over the prior activity; visual return and the Back
path are still unverified. No remote command was executed by this test.

This is a trusted-direct-USB prototype, not an authenticated session or durable
saved-page protocol. It does not provide reconnect recovery or safe automatic
action retries. Text pagination/orientation, blank paragraphs, shared-console
interleaving, sleep/exit cleanup, SDK UI hosting compliance and activity/render
state synchronization require further review before calling the USB feature
ready. The current screen uses direct renderer text rather than UiAppHost and
must be migrated to the shared UI contract. No firmware was published.

## Ink story picker/player prototype

The human reprioritized playable Ink stories over Hosts/TV, with a lean
implementation and selectable files in an SD folder. The `x4c` development
profile now pins the same InkCPP runtime commit as local eenk and enables the
Stories tile. Other profiles keep the previous feature set. This is explicit
personal downstream scope, not an upstream reader feature.

`StoriesActivity` uses the existing UiListActivity for both the `/stories`
file picker and choice lists. A bounded session wrapper advances the real Ink
VM, buffers narration, and presents short UTF-8 panels through SDK text layout.
There is no additional task, framebuffer, renderer, settings schema, partition
or network service. Row providers borrow file names/choice strings rather than
allocate per-row copies. VM/presentation changes hold RenderLock.

The original example `The Lantern at Low Tide` includes three endings and an
inventory-dependent choice. Its compiled asset is embedded for first-run
installation into `/stories/low-tide.bin` only when absent. A temp file is
flushed/closed and renamed; existing stories are never overwritten. The actual
player always loads through the SD path. The source is CC0-1.0. See
[story deployment and compilation](../stories/README.md) for the pinned tools,
limits and supported format.

The activity owns up to 32 short file names, a bounded narration buffer, one
short presentation panel, and a checked PSRAM-only story buffer (max 256 KiB).
The VM borrows that buffer and is destroyed before it. InkCPP performs its own
internal allocations: heap admission checks are not an OOM proof, and trusted
compiled stories are required. The VM is not a bytecode sandbox. Save/resume
across leaving Stories or sleep is not implemented. A temporary USB page keeps
the parent activity/VM resident, so overlay peak memory must be checked.

Four integration tests execute all three endings, verify inventory and reload,
reject basic incompatible/truncated headers before VM construction, and retain
UTF-8/paragraph text across panels. Eleven existing dock/launcher/USB tests and
both input-orientation/remap scripts pass. Independent read-only review agents
are unavailable, so firmware handoff remains incomplete.

Hardware acceptance: open Stories and select low-tide.bin; read Continue panels
and choose different paths to the three endings; Back should return to the
picker and then Home. Check landscape and remapped buttons for clipping or
double activation. Deploy another compatible .bin file and reopen Stories to
verify discovery. Confirm an overlaid USB message returns to the same story
panel/choices. Sleep/reboot and Back currently discard story progress; check
this intentionally limited behavior rather than expecting saved games.

### Story test-build installation

After explicit human approval, the final X4C build was installed app-only at
`0x10000` using esptool 4.11.0. No repeated flash backup, partition rewrite or
NVS erase was performed. Flash hash verification passed. Boot logs confirm
normal Launcher entry and completion of display refresh; playing the story on
hardware is awaiting the human check.

Final build: `/tmp/xteink-stories-final-build.log` (success, no warnings reported
in that incremental final log). Static RAM 115,172 bytes (+328 from USB build);
flash 5,500,679 bytes (+87,164), leaving 1,052,921 bytes in the unchanged app slot.
DWARF reports StoriesActivity 6,132 bytes including its 1,560-byte session wrapper.
The PSRAM story bytes and VM allocations are additional, not included in that
object size. Initial launcher heap after this flash: internal free 168,832 bytes,
minimum 166,908 and largest block 114,676; PSRAM free 8,288,472. These samples do
not measure VM or USB-overlay peak usage or power.

The installed app, SHA-256, source diff, InkCPP MIT notice, build/flash logs and
ongoing device session log are saved privately under
`~/.local/share/xteink/backups/20261109-144309-1c290427351c/stories-prototype-update/`.
The previous USB app remains under `usb-prototype-update/usb-live.app.bin` for
app-only rollback. Source/deployment instructions are in `stories/README.md`.
The human was asked to select Stories → low-tide.bin and try the lighthouse and
lantern branch. No firmware commit or publication was performed; independent
review and full hardware acceptance remain incomplete.


## Story upload over USB and story orientation

The human chose USB upload over Wi-Fi/manual copying so stories can be pushed
from whichever Linux host the dock is attached to (dex now, ztx later). The host
side is in the `inkstor` repo (`npm run setup:x4c` once per host, then
`npm run deploy`); it has no native modules and no host-specific paths.
Protocol, limits and file locations: [stories/README.md](../stories/README.md#upload-over-usb-recommended).

Also in this change:

- Stories applies the reader orientation setting (all four orientations) on
  enter and restores portrait on exit, like ReaderActivity.
- The narration buffer grew from 1,536 to 8,192 bytes: inkstor stories have up
  to 4,735 bytes between choices (measured over 3,000 random playthroughs each).
  This is inside the heap-allocated StoriesActivity, so it adds about 6.6 KB
  of internal heap while Stories is open, not static RAM.
- Upload state is static: about 1 KiB (one parsed command with a 768-byte
  chunk, plus the receiver). The USB RX ring grows by 1,792 bytes of heap.

Checks: 15 host CTest cases pass (parser, CRC, existing story/USB tests).
`pio run -e x4c` succeeds: static RAM 116,196 bytes (+1,024), flash 5,504,003
bytes. The app was installed at `0x10000` with esptool 4.11.0 and hash-verified.
Artifacts are in `~/.local/share/xteink/backups/20261109-144309-1c290427351c/story-upload-update/`.
On hardware, all four inkstor stories were uploaded and acknowledged with a
verified CRC (128 KiB in 11.5 s). A re-upload replaced a file, and a deliberate
CRC mismatch was rejected with `ERROR CRC`.

Not yet verified: playing the uploaded stories and landscape layout on the
device; an upload while a USB live page or a storage-exclusive screen is open;
a cable pull mid-upload (expected: temp file removed after 10 s). The Thai
story will show missing glyphs because the built-in fonts lack Thai. The picker
does not refresh automatically; reopen Stories. Independent reviews remain
outstanding.

### Story pagination and language tag

Pages now fill the screen by measured lines instead of a 200-byte cap, and
prefer paragraph/sentence breaks in the last 30% of a page. Continue is a small
right-aligned `»`; choices appear below the last page when they fit. Stories may
declare `# lang: de` (InkCPP global tag); it only selects the end label today.
The example story is tagged `lang: en`. The 200-byte page buffer was removed: a page is a range of
the session text. 22 host tests pass, including new pagination tests. Static RAM is unchanged at 116,196 bytes; flash is 5,505,259 bytes.
The app was installed at `0x10000` and hash-verified, and the re-tagged inkstor stories were uploaded over USB.
Not yet checked on the device: page fill, `»` placement and inline choices in all four orientations.
The choice limit was raised from 8 to 16 (`InkStorySession::MAX_CHOICES`). Choice
text is borrowed from the VM, so this adds no buffers. inkstor's build now fails
when random playthroughs exceed 16 choices, 8,191 text bytes or 64 lines.

### Story screens: hints, legend, marker line, list selection

Story screens hide the header and button hints and use that space for text.
The last text line leaves room for `»`. Unused buttons on narration pages
(Left/Right/Up/Down) open a legend drawn with `GUI.drawButtonHints` and
`GUI.drawSideButtonHints`, which respect button remapping. Back re-selects the
story that was left. Measured InkCPP snapshot sizes for resume, from random
playthroughs on the host: up to about 3.3 KB (Becher), 8.6 KB (Platin-Kodex),
8.2 KB (Stille Krone) and 1 KB (Thai). Resume is not implemented yet.

### Story resume and playthrough count

Saves are written only when leaving a story or in `prepareForSleep()`.
Every deep-sleep path calls it through `enterDeepSleep` and
`deliverSleepPluginEvents`; that includes activities under an overlay. A
`saved` flag prevents duplicate writes per page. Resume restores the InkCPP
snapshot and the saved passage text; the VM does not replay output. Host tests
check that inventory and choices survive a round trip and that the VM does not
borrow the snapshot buffer. Hold Select to start over. An ending discards the
save and increments `<story>.plays`. Format and limits: stories/README.md.
25 host tests pass. Static RAM is 116,204 bytes (+8); flash is 5,509,023 bytes.
The app was installed and hash-verified.
Not yet tested on hardware: resume after Back, Home and sleep; hold-to-restart;
and the count display.

### Story controls and wake-to-story

The button legend was removed because every button now acts on story pages. Down/Right
advance, Up/Left page back (recomputed forward from the passage start), and
Back must be held. At a decision point, Up flashes the last choice for 1.5 s;
that choice is not saved, so after a resume there is nothing to flash. Boot
checks the wake marker in the existing goHome branch of `setup()`; holding
Back while waking skips it. The end label marker is wider (text + 64 px) after
"Ende" was truncated on hardware. 25 host tests pass; the app was installed
and hash-verified. Not yet tested on hardware: wake-to-story, the paging
buttons, the choice flash and the hold timings.
Fix: on hardware, paging used raw Up/Down/Left/Right, so in landscape the key
labelled Down paged back. Paging now uses `NavNext`/`NavPrevious`, the
orientation-aware keys behind list navigation and its hints.
A short Back now acts like Select on story screens; holding Back still returns to the list.

## TV remote (Bluetooth LE)

The TV remote tile (`XTEINK_TV_REMOTE`, x4c only) opens `TvRemoteActivity`. Remote
mode makes the X4 a NimBLE GATT peripheral "X4 Remote" (RX write / TX notify,
chunked JSON: sai `xteink/TV_REMOTE_PROTOCOL.md`). `x4bridge` on tix connects to it
and relays to deck_tv. The menu has TV power (with "Switching the TV on..." until
the bridge reports it), TV home screen, D-pad, Playback and volume, the clip groups
(a group lists its clips, plus Play all), and Keyboard mode. Keyboard mode is a BLE
HID keyboard "X4 Keyboard" under its own static random address, meant to be paired
with the TV directly. The choice is kept in `/.crosspoint/tv_mode`.

Remote mode admits only paired bridges: bonded LE Secure Connections (Just Works), the link encrypted by the
radio. "Pair a bridge (60 s)" opens a window in which one new bridge may pair. Paired bridges are kept in
`/.crosspoint/tv_bridges`, and "Forget paired bridges" removes them and their bonds. A link that is not secured within
10 s is dropped. Keyboard-mode bonds (the TV) never count as bridges. The x4c profile lifts the page turner's
`CONFIG_BT_NIMBLE_ROLE_PERIPHERAL/BROADCASTER_DISABLED` (the prebuilt core has every role).
The page turner uses NimBLE only while a book is open, so the two never
overlap. Static RAM 116,220 bytes, flash about 5.53 MB. Starting the radio takes
about 41 KB of internal heap.

The D-pad and Playback screens send every key to the TV. Back always leaves them.
Holding Select sends the TV's back key on the D-pad screen and mute on the Playback
screen. `CMD:CRASHLOG` on the USB console prints `/crash_report.txt`.

Hardware, 11-12 (test builds that also accept the developer laptop's adapter):
remote mode worked through a bridge on a laptop. TV on/off, the D-pad, volume,
seeking and the clip groups were tried; a clip played once the TV helper was up. The
X4's BLE address is `1c:29:04:27:35:1d`.

Fixes from these tests:
- Back trapped the user on the D-pad screen.
- A missing glyph appeared where the status line used a newline.
- The volume keys were inverted.
- Stopping the radio while connected crashed NimBLE's host task (InstrFetchProhibited
  in `host_task`). The link is now closed before `deinit`, as in `BleKeyboardHost`.

Not yet verified on hardware: keyboard mode after that fix, and the final build that
accepts only tix. While the radio started, USB serial dropped for about 2 minutes.
Tests: `test/tv_framing` (host).
