# Ink stories on X4 Classic

Personal downstream feature; enabled only in the `x4c` development profile.
This is the InkCPP VM used by eenk, not a second copy of eenk's firmware/UI.
The dependency is pinned to `t0mg/inkcpp` commit
`97001f2fa7d4c257b89919c17f96b7ee91b22e8b` (InkCPP binary format v2).
InkCPP's MIT notices are retained in [INKCPP-LICENSE.txt](INKCPP-LICENSE.txt).

## Play and deploy

1. Put trusted, compiled **InkCPP `.bin` story files** in `/stories` on the SD
   card. This is not a firmware `.bin`, raw `.ink` source, or Ink JSON file.
2. Open **Stories** on Home and select a file.
3. Select **Continue** to read the next short text panel. When the narration
   ends, Continue shows the available choices. Select a choice to follow it.
4. Back returns to the story picker. Back again returns Home. The current
   version does not save progress when leaving Stories or powering down.

Copy stories with the USB upload below, the existing file-transfer screen or a
card reader. Reopen Stories to rescan. The initial picker lists up to 32 lowercase `.bin` files in
this one directory, scanning up to 256 entries; it does not recurse. File names
must be shorter than 95 UTF-8 bytes. The current prototype limits stories to 256 KiB,
16 choices at a decision (more stop the story with an error; the choice
list scrolls when they do not fit), and 8,191 UTF-8 bytes or 64 lines of narration
between decisions. inkstor's `npm run build:x4c` checks these limits with random playthroughs.
These are explicit implementation limits, not Ink language restrictions.

A short original example, **The Lantern at Low Tide**, has three endings and an
inventory-dependent choice. Opening Stories creates `/stories/low-tide.bin` if
it is absent. Existing files are not overwritten. `examples/low-tide.ink` is its
source, dedicated to the public domain under CC0-1.0. The compiled example is
embedded solely for that first-run SD installation; the picker/player then
loads it through the same SD path as other stories.

The player uses CrossPoint's shared list UI, remapped buttons, fonts and
existing render task, and follows the reader orientation setting (all four
orientations; portrait is restored on exit). Reading screens have no header.

Story screens show no header and no button hints. Controls on a story page:
Select or the next key go to the next page (like `»`); the previous key goes to
the previous page. Next/previous are the logical `NavNext`/`NavPrevious` keys
that CrossPoint lists use and label as Down/Up. They follow orientation, so in
landscape they are not the raw Up/Down buttons. Paging back stops at the last decision: there, the previous key briefly
(1.5 s) shows the choice that led to this passage. On the choice list, the previous key on
the first choice returns to the text. Hold Back (0.7 s) to save and return to
the story list. A short Back acts like Select (`»` is next to it in landscape). In the story list, hold Back to
leave Stories and hold Select to start a story over. Leaving a story selects
it in the list.

Progress: leaving a story (hold Back, Home gesture) or going to sleep (power
button, auto-sleep, low battery: `Activity::prepareForSleep`) saves it; there
is no save per choice. Opening the story resumes on the same page. After sleeping inside a story, the
device boots straight back into it: a `/stories/.save/.resume` marker is written
in `prepareForSleep()` and consumed when Stories opens. Hold Back while waking to
skip this. Otherwise Stories opens in the story list. Hold Select
on a story in the list (0.7 s) to start over. Reaching an ending deletes the
save and adds one to the playthrough count, so the next open starts fresh. The
list shows the count after the name from 2 on (`3×`). Sudden power loss
without sleep (e.g. a fully drained battery) loses progress since the last save.

Files in `/stories/.save/` (hidden from the picker): `<story>.sav` holds a
20-byte header (`XTS1`, story CRC-32, page offset, text length, language), the
current passage text and the InkCPP snapshot. It is written to `.tmp`, then
renamed. A save whose CRC does not match the story bytes, e.g. after a changed
story was uploaded, is ignored and the story starts fresh. Saves are at most
about 17 KB (8 KB text plus snapshot; measured snapshots are 1 to 8.6 KB).
Loading reads the file into a transient PSRAM buffer (64 KiB limit), which is
freed after the restore. `<story>.plays` holds the decimal playthrough count.

Pagination (`src/stories/StoryTextLayout.h`): narration up to the next choice
is wrapped with the real font and width, without the SDK's 16-line cap. A page
holds as many lines as fit on the screen; the last line is shortened to leave
room (plus a small gap) for a right-aligned `»` at its end. If text
remains, the page ends at the last paragraph end, else sentence end
(`. ! ? : …`, closing quotes allowed), within the last 30% of its lines; else
at a word boundary. Page-edge newlines/spaces are trimmed; blank lines inside
the text are kept. On the last page, choices are listed left-aligned below the
text when they fit; otherwise `»` opens a separate choice screen. The end row
says "Ende" for `lang: de`, otherwise "Finished".

Metadata: put global tags at the very top of the `.ink` file, e.g.
`# lang: de`. Only a two-letter `lang` is read today.

A USB live page may be pushed over the story; dismissing it retains the story
VM and current page. There is no new task, network service, flash partition
or per-story cache.

## Upload over USB (recommended)

Any Linux host can push stories over the USB cable; no Wi-Fi, card reader or
taps on the device. The host tooling lives in `inkstor/` of the `sai` repo:

```sh
npm run setup:x4c   # once per host: builds the pinned InkCPP compiler
npm run deploy      # compile every *.ink, upload all to /stories
node x4c/deploy.js build-x4c/one-story.bin   # upload selected files only
```

The port is auto-detected as the single `/dev/serial/by-id/*Espressif*` entry;
set `XTEINK_PORT` to override. The user needs access to the serial device
(usually the `dialout` or `uucp` group). Close other serial monitors first: the
upload shares the debug console. The device must be awake, and the upload is
answered `BUSY` while a storage-exclusive screen (USB drive, web transfer) is
open. Typical speed is about 11 KB/s (a 128 KiB story takes about 12 s).

### Protocol

Text lines on the existing USB console, one command per line, each answered
before the host sends the next. `<id>` is a nonzero 32-bit decimal chosen by
the host for one file.

| Host sends | Device replies |
| --- | --- |
| `XT1 FILE BEGIN <id> <name-hex> <size> <crc32-hex8>` | `XT1 READY <id> 0` |
| `XT1 FILE DATA <id> <offset> <payload-hex>` | `XT1 ACK <id> <bytes-written>` |
| `XT1 FILE END <id>` | `XT1 DONE <id> 0` |

Errors: `XT1 ERROR <id> STORAGE|NOUPLOAD|SIZE|WRITE|CRC|RENAME`, or
`XT1 BUSY <id> 1`. Rules:

- Name: lowercase `[a-z0-9._-]`, ending `.bin`, not starting with `.`, at most
  94 bytes; saved as `/stories/<name>`. The names `sleep-overlay.png` and
  `sleep-overlay.bmp` are also accepted and saved in the SD root, where the
  Transparent sleep screen reads them (`node x4c/deploy.js sleep-overlay.png`).
  LIST and DELETE cover stories only. Size: 1 to 262,144 bytes. CRC: standard
  CRC-32 (zlib).
- Payload: at most 768 bytes per DATA line. The ACK always reports the
  committed offset; a DATA line with a different offset is not written and only
  re-ACKed, so the host can resend safely after a lost reply.
- Data goes to `/stories/.upload.tmp`. END checks size and CRC, then replaces
  `/stories/<name>`. Any failure, a new BEGIN or 10 s without a frame deletes
  the temp file; an existing story is only replaced after a verified upload.
- One upload at a time; frames refresh the inactivity timer, so the device does
  not sleep mid-upload.

Implementation: parser `src/util/UsbFileTransfer.h` (host tests in
`test/usb_live/UsbFileTransferTest.cpp`), writer
`src/stories/StoryUploadReceiver.cpp`, dispatch in `src/main.cpp` next to
`XT1 PAGE`. On X4C the USB RX queue is raised from 256 to 2,048 bytes so a whole
frame fits, and the console reads up to 512 bytes per loop.

This is a trusted-cable protocol: anyone with USB access can replace stories.
It has no authentication, and stories are still not sandboxed (see Limits).

## Compile a story on the host

Use the pinned compiler above, or an eenky/eenk build using the same compatible
binary format. The first firmware build downloads the pinned runtime to
`.pio/libdeps/x4c/inkcpp`. To build its host tools (requires CMake and a C++
compiler; inklecate is downloaded from the upstream Ink release):

```sh
cmake -S .pio/libdeps/x4c/inkcpp -B /tmp/ink-story-tools \
  -DCMAKE_BUILD_TYPE=Release -DINKCPP_INKLECATE=OS
cmake --build /tmp/ink-story-tools --target inkcpp_cl -j4
```

On Linux, compile Ink source to Ink JSON, then to the device binary:

```sh
/tmp/ink-story-tools/inklecate/linux/inklecate -o /tmp/story.json story.ink
/tmp/ink-story-tools/inkcpp_cl/inkcpp_cl -o story.bin /tmp/story.json
```

Copy `story.bin` to `/stories/story.bin` on the card. To regenerate the shipped
example, use the same commands with `examples/low-tide.ink` and output
`examples/low-tide.bin`. Do not rebuild Ink on the device.

## Limits and verification

Only load stories compiled from trusted source. Header/version/section bounds
are checked before VM construction, but the third-party VM is not a sandbox or
a full bytecode validator. Its internal allocations and instruction execution
are not fully bounded or recoverable on OOM/malicious input. The firmware checks
available internal/default heap before starting the VM; that is not an OOM
proof. Imported assets, images, sound, external functions and persistent saves
are not supported in this first player.

The story bytes use checked PSRAM-only allocation, capped at 256 KiB. The
activity owns the VM, fixed-size directory names, narration buffer and one text
panel. Render uses borrowed list row providers rather than a second row array.
No SD handle is retained after loading. VM state is freed before its backing
bytes when exiting the story or destroying the activity. Device heap/stack and
story-to-USB-overlay peak usage still need measurement.

Optional host integration tests:

```sh
cmake -S test -B /tmp/story-tests \
  -DINKCPP_SOURCE_DIR="$PWD/.pio/libdeps/x4c/inkcpp"
cmake --build /tmp/story-tests --target InkStorySessionTest -j4
ctest --test-dir /tmp/story-tests -R 'InkStorySession|StoryTextPage' --output-on-failure
```

Tests execute all three example endings, check inventory-dependent choices and
reload, reject incompatible/truncated section headers, and preserve UTF-8 and
paragraphs across text panels. Text-layout regressions verify that newline
characters never reach the glyph renderer, blank paragraph gaps remain, and
portrait/landscape test bands start at the top and stay within bounds. They do not replace hardware/orientation/input
checks or independent firmware review.
