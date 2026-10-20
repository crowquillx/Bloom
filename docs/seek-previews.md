# Seek previews

Bloom uses server trickplay images first. If the server has no images, returns
invalid metadata, or fails to deliver the sheets, `PlayerController` starts
`LocalSeekPreview`. The fallback uses the selected file version's canonical
`PlaybackDescriptor`. It does not create a second playback session or send
playback reports.

## Generation

Generation starts when server preview lookup fails or reports no previews. It
does not wait for a seek or for the controls to open. One helper process opens
the media and generates keyframe images for the full file at ten-second intervals. It
samples the start and end first, then divides the remaining gaps. This gives
distant positions previews early while it fills the full timeline.

Each image becomes available as soon as it is ready. The existing filmstrip
updates through `trickplayFrameRevision`. There is no full-file readiness gate.
An image that is still being generated stays empty; the strip keeps its time
label. Generation takes time and adds media reads and CPU use. It cannot make
all previews available at playback start.

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
keeps completed images and resumes only the missing slots with that
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
128 MiB. A timeout skips that slot and resumes the remaining timeline, with at
most three failed attempts for the source. Other failures and the cache limit
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
Builds without libmpv have no local fallback.
The existing embedded Linux trickplay opt-in still applies.

`LocalSeekPreviewTest` runs the real helper against generated local video and
an authenticated HTTP range server. It covers a timeline longer than 100 seconds
without seek requests, generation order, image changes across the timeline,
source replacement, cache cleanup, decoder failure, a stalled server, and header
validation. It also checks the helper under an installed UTF-8 locale, cache
retention after a frame failure, and cache reuse across a stream URL refresh. A small
synthetic HDR fixture checks that tone mapping changes the output and preserves
neutral colour. `PlayerControllerAutoplayContextTest` checks automatic fallback,
server priority, multipart offsets, and terminal-state response rejection.

The HDR fixture was generated with FFmpeg 9.0.1:

```sh
ffmpeg -f lavfi -i 'color=gray:size=32x18:rate=1' -t 11 -c:v ffv1 \
  -pix_fmt yuv420p10le -color_primaries bt2020 -color_trc smpte2084 \
  -colorspace bt2020nc tests/fixtures/seek-preview-hdr.mkv
```

Manual checks must cover real Jellyfin and Silo stream authorization, concurrent
range reads, expired URLs, remote 4K playback load, HDR and Dolby Vision colour,
and Windows overlay visibility and geometry. See [manual validation](manual-validation.md).
