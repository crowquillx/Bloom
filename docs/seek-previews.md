# Seek previews

Bloom uses server trickplay images first. If the server has no images, returns
invalid metadata, or fails to deliver the sheets, `PlayerController` falls back
to `LocalSeekPreview`. The fallback uses the selected file version's canonical
`PlaybackDescriptor`. It does not create a second playback session or send
playback reports.

Settings > Playback has a toggle for each source (`serverTrickplayEnabled`,
`localSeekPreviewsEnabled`; see [config](config.md)). With server trickplay
off, Bloom skips the lookup and treats the item as having no server images, so
local generation takes over if it is on. Both default to on and apply from the
next playback.

## Generation

Local generation reads the media a second time, alongside playback. Remote
mounts that fetch on demand, such as Usenet or debrid FUSE mounts, can turn each
preview seek into a large upstream fetch and fail playback under that load. The
fallback is therefore as gentle as the feature allows:

- **On demand.** Nothing is read when the fallback is chosen. Only the visible
  filmstrip requests frames, through `trickplayPreviewUrlAt`. Playback position
  updates show existing frames but never request new ones.
- **Settled batches.** Requests start a batch 400 ms after the last new frame
  is asked for, so continuous scrubbing reads nothing until it pauses. A batch
  covers only the frames asked for in the last 1.4 seconds, starting at the
  centre of the strip and working outwards. A new batch replaces the old one;
  frames that scrolled away are dropped.
- **Paced.** The helper reads one frame at a time and idles one second between
  frames, so each seek's server-side read-ahead can wind down.
- **Yields to playback.** The helper runs only while playback is Playing or
  Paused and has been for five seconds. Loading, buffering (including
  `paused-for-cache`), stream recovery, and errors stop it at once; its
  unfinished frames resume after playback settles.
- **Backs off.** A failed frame waits 30 seconds before the next read.

Each image becomes available as soon as it is ready. The existing filmstrip
updates through `trickplayFrameRevision`. An image that is still being generated
stays empty; the strip keeps its time label. The first look at a position shows
empty frames for a moment while they are generated.

## Process and cache

`Bloom::SeekPreview` owns the asynchronous helper process and its session cache.
The `bloom-seek-preview` executable uses the packaged libmpv. It has its own
decoder, no window, no audio, no subtitles, no user scripts, and no user mpv
configuration. The helper runs at lower process priority. Software decode uses
two threads and keyframe seeks avoid decoding a full GOP for each slot. Network readahead is disabled
and the demuxer cache is limited to 128 KiB.

The media URL and headers pass through standard input, with a 64 KiB limit.
They do not appear in process arguments or logs. Signed URLs remain opaque.
The helper validates HTTP header names and rejects line breaks and null bytes
in header values. TLS certificate verification remains enabled.

JPEG images fit within 320 by 180 pixels and use quality 75. HDR sources use an
SDR tone-mapping filter. Images stay in a private session directory under the
application cache's `seek-previews` directory. Bloom does not reuse thumbnails
across playback sessions and does not write stream URLs to disk.
Stopping playback, replacing the source, changing versions or playlist parts,
or destroying the controller cancels the helper and removes its images after
the helper exits. Removal runs on a worker thread. A replacement audio URL
keeps completed images and restarts any running batch with that
provider-finalized URL. Clearing the cache updates the filmstrip revision.

The helper exits if its progress pipe closes. Linux also uses a parent-death
signal and Windows uses a job with `KILL_ON_JOB_CLOSE`. A crash can leave cache
files behind. A later startup removes session directories older than a minute
when their owner lock can be acquired; it does not remove another live
instance's cache. This cleanup also runs on a worker thread.

The fallback accepts direct-play files with a known duration of at most 24
hours. Transcodes, unknown durations, and forward-only timelines are excluded.
Dolby Vision sources without a compatible HDR base layer are also excluded
because the software preview filter cannot convert their IPT colour space.
Each image must be at most 1 MiB and the total JPEG data must stay within
128 MiB. A timeout, frame decode failure, or position that yields no picture
(for example past the last frame) skips that slot. The rest of the batch resumes
after the 30-second backoff, with at most three failed attempts for the source.
Other failures and the cache limit
stop generation but keep images that are already available. If no image is
available, local previews are disabled. Playback continues in each case.
There is a ten-second parent deadline for each image, including process startup.
The helper has an eight-second frame deadline and a five-second network timeout.
Cancellation and deadlines do not wait for the child on the UI thread. Logs
record the failing stage and status code, without URLs or credentials.

## Build and validation

CMake builds and installs `bloom-seek-preview` next to Bloom. Both executables
use the same libmpv runtime. All release packaging paths run the helper's
`--self-test`, which decodes a synthetic frame through the HDR filter chain.
Portable and Flatpak FFmpeg builds enable libzimg for the `zscale` filter.
Those packaging paths also build mpv without Lua, which removes the `ytdl`
option; the helper treats its absence as expected and only fails on other
option errors. The `seek-preview-no-scripting` Nix check runs the self-test
against a scripting-free libmpv so this stays covered.
Builds without libmpv have no local fallback.
The existing embedded Linux trickplay opt-in still applies.
Linux builds with `BLOOM_BUNDLE_LIBMPV=ON` give both executables a runtime
search path to the installed `lib/bloom` directory. Installation copies the
versioned libmpv file and its symbolic links.

`LocalSeekPreviewTest` runs the real helper against generated local video and
an authenticated HTTP range server. It covers a timeline longer than 100 seconds,
centre-first order, the settle delay, suspension, pacing, the failure backoff,
image changes across the timeline, source replacement, cache cleanup, decoder
failure, a stalled server, and header validation. It also checks the helper
under an installed UTF-8 locale, cache retention after a frame failure, and
cache reuse across a stream URL refresh. A small synthetic HDR fixture checks
that tone mapping changes the output and preserves neutral colour.
`PlayerControllerAutoplayContextTest` checks automatic fallback, that nothing is
generated until the filmstrip asks, server priority, multipart offsets, and
terminal-state response rejection. It checks that both settings gate their
source and that playback health suspends generation. It also checks that
autoplay keeps the descriptor, source, session and track metadata needed to
start local previews. A 121-second H.264 fixture with
B-frames and 15-second keyframe gaps checks out-of-order preview generation
and image content across the timeline.

The H.264 fixture was generated with FFmpeg 8.1:

```sh
ffmpeg -f lavfi -i "nullsrc=s=64x36:r=2:d=121,geq=lum='40+T':cb=128:cr=128" \
  -c:v libx264 -preset veryslow -qp 18 -g 30 -keyint_min 30 -sc_threshold 0 \
  -bf 3 -pix_fmt yuv420p tests/fixtures/seek-preview-h264.mkv
```

The HDR fixture was generated with FFmpeg 9.0.1:

```sh
ffmpeg -f lavfi -i 'color=gray:size=32x18:rate=1' -t 11 -c:v ffv1 \
  -pix_fmt yuv420p10le -color_primaries bt2020 -color_trc smpte2084 \
  -colorspace bt2020nc tests/fixtures/seek-preview-hdr.mkv
```

Manual checks must cover real Jellyfin and Silo stream authorization, concurrent
range reads, expired URLs, remote 4K playback load, HDR and Dolby Vision colour,
and Windows overlay visibility and geometry. See [manual validation](manual-validation.md).
