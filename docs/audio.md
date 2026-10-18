# Audio output

Bloom keeps the original audio stream when mpv can decode it. Encoded transport
support does not control provider decode claims. Bloom uses PCM when encoded
transport is unavailable. PCM decode of TrueHD and DTS-HD MA keeps the channel
audio lossless. It does not keep Atmos or DTS:X object metadata.

## Architecture and audit

The original path was provider playback info, `MediaStreamInfo`, track selection
in `PlayerController`, provider stream preparation in `PlaybackService`, then mpv
startup arguments. The stream model held codec, profile, channels, layout and
bitrate. The descriptor kept only codec and track labels. Silo declared a fixed
local decode list and an empty passthrough list. Jellyfin sent no client audio
codec profile. Bloom did not manage `audio-spdif`. User profiles could set it.
Device reload handled explicit devices. Automatic device changes relied on mpv.
Runtime snapshots covered video, not audio output.

The current path is provider metadata, canonical source metadata, endpoint
capabilities, pure output policy, provider stream preparation, descriptor,
shared mpv arguments, then runtime verification.

`Bloom::AudioSourceMetadata` holds codec, profile, channel count, channel layout,
sample rate, bit depth, bitrate, fidelity, and object metadata indications.
`PlaybackTrack::audio` keeps these values in each descriptor.
`PlaybackDescriptor::audioOutputPlan` carries the output decision.

Jellyfin maps `SampleRate`, `BitDepth` and `AudioSpatialFormat`, as well as the
existing stream fields. Silo maps `sample_rate`, `bit_depth`, `profile`, `title`
and the existing stream fields. Both adapters use the same canonical keys.
Structured profile and stream title values can indicate Atmos, JOC or DTS:X.
File names do not provide that evidence. A false indication means the provider
did not identify the extension. It does not prove that the extension is absent.
Fidelity is a codec/profile classification, not a measurement of the source.

`Bloom::AudioPolicy` is part of the Qt-Core-only `Bloom::PlayerPolicy` target.
It has no QObject, provider API, network or OS calls. Its inputs are
`AudioSourceMetadata`, `AudioOutputCapabilities`, and `AudioPreferences`.
Its `AudioOutputPlan` separates local decode support, encoded transport,
expected PCM layout, object metadata retention and provider conversion need.
Unknown source metadata does not by itself require provider conversion.

`AudioOutputService` owns endpoint discovery and the capability snapshot.
`PlaybackService::requestPlaybackDescriptor` waits for the bounded endpoint
query, evaluates and logs the selected source plan, then starts provider stream
preparation. This is before Silo's start request and before Jellyfin descriptor
creation. The service attaches the final selected track plan to the descriptor.
`PlayerController` uses descriptor audio metadata where available and revalidates
the plan against the latest endpoint snapshot before mpv starts. Track changes
and playlist parts use the same policy.

The provider transport request uses local decode support. Silo sends that list
as `audio_decode_codecs` and keeps its transport passthrough claims empty.
Jellyfin sends the shared list in `DeviceProfile.DirectPlayProfiles.AudioCodec`.
The declared list includes WMA, MLP, common legacy audio and PCM variants
supported by the pinned mpv build.
It leaves container and video codec restrictions unset. An explicit server
fallback uses progressive Matroska. Its audio profile permits the same source
codecs and selects FLAC first if audio conversion is required. Compatible audio
and video remain eligible for stream copy. The fallback has no channel limit.
Embedded subtitle profiles keep supported text and bitmap subtitles available
to mpv without requiring video burn-in. External subtitle delivery is not
advertised because Bloom does not yet map Jellyfin's delivery URLs.
The fallback remains subject to server permissions and encoder availability.
An explicit server `SupportsDirectPlay` value permits original playback even when a conversion URL
is also present. An unknown value preserves the prior URL selection.
Neither request removes TrueHD or DTS because the local endpoint lacks encoded
transport. Server permissions, source access, video constraints and the server's
own transport decision can still require a different delivery.

## Endpoint detection

On Windows, the probe resolves the selected WASAPI endpoint. Automatic output
uses the multimedia default endpoint. The worker calls `IAudioClient::IsFormatSupported`
in exclusive mode for AC-3, E-AC-3, TrueHD, DTS and DTS-HD carrier formats.
It also checks PCM layouts using 16-bit and 32-bit containers at common rates.
The query does not open a playback stream or change Windows Spatial Audio.
The device ID and accepted formats provide the evidence. The probe does not
inspect manufacturer, GPU, TV, soundbar or provider names.

The result proves driver format acceptance only. It cannot prove that every
device downstream of a TV can decode the carrier. Runtime checks prove that
mpv opened an encoded carrier. Soundbar/AVR confirmation remains a manual test.
The encoded probes cover common carrier rates. Other rates need runtime checks.

Linux currently has no reliable mapping from the selected mpv endpoint to encoded
sink capabilities in this implementation. PipeWire, PulseAudio and ALSA device
names do not provide that evidence. Auto therefore uses PCM on Linux. Custom
can enable known working formats. Unknown Windows query results also use PCM.
Capabilities are not persisted across app starts.

The query runs outside the UI thread. Playback preparation has a 1500 ms deadline.
A timeout supplies unknown capabilities. One active worker prevents an unresponsive
driver from creating more probe workers. The OS call itself cannot be cancelled;
the deadline bounds playback preparation, not a stalled driver call's lifetime.
Native queries use a serialized process-lifetime pool separate from Qt's global
pool. A blocked query cannot make application shutdown wait. Workers capture
only the probe function and device value, so late completion does not access a
destroyed service.

## Settings and mpv

Settings > Playback has Audio Passthrough modes Auto, Disabled and Custom.
Custom exposes Dolby Digital, Dolby Digital Plus, Dolby TrueHD, DTS and DTS-HD.
Atmos and DTS:X are metadata in those transports, not independent codec toggles.
Existing installations use Auto with no custom formats. Mode and Custom format
changes use the same saved-settings confirmation as other playback settings.

All three backends receive the same generated audio options after profile options:

- `audio-spdif` is the selected carrier, or empty for local PCM decode.
- `audio-device` is the resolved device (see [Output device selection](#output-device-selection)).
  It is `auto` while a saved device is missing.
- `audio-exclusive` is `yes` only for a WASAPI endpoint with Exclusive Mode on.
- `audio-fallback-to-null=yes` keeps playback alive when the endpoint is missing.
- Mono and stereo sources request their own layout. They do not request upmix.
- In exclusive mode, a multichannel layout that the format query accepted
  requests that layout with stereo as a fallback. An unverified layout uses
  `audio-channels=auto`. mpv then queries the driver for each layout and
  downmixes only to a layout the driver accepts. `auto-safe` would force stereo
  here, because no system mixer is involved.
- In shared mode and on Linux, multichannel uses `audio-channels=auto-safe`.
  The system mixer's layout (Windows speaker setup, PipeWire or PulseAudio sink)
  decides the output, and mpv downmixes to it when it has fewer channels.

Bloom does not force a sample rate, sample format or lossy intermediate codec.
mpv negotiates the output format. The source classification does not prove that
the endpoint kept the original rate or precision. Check `audio-out-params`.
Managed plan arguments follow profile arguments, including explicit empty
`audio-spdif`. Scripts or conditional user profiles can still change mpv state.
Runtime verification reports a disagreement with the plan.

These changes do not change renderer, HDR, Dolby Vision, refresh matching or
window composition. Windows Spatial Audio is independent of movie bitstream
transport. Bloom does not require or change its setting.

## Runtime checks and recovery

`IPlayerBackend::requestAudioDiagnostics` uses bounded IPC property snapshots for
external mpv and asynchronous property snapshots for embedded libmpv. While the
backend runs, the controller requests an audio snapshot every three seconds.
Plan generations reject results from an old track or output decision.

`PlayerController.audioDiagnostics` and playback logs expose source, plan,
prepared plan, endpoint evidence and runtime result. The result includes
`current-ao`, configured `audio-device`, decoder, `audio-params`,
`audio-out-params`, active encoded carrier, and object metadata retention.
`audio-device` is a configured preference; mpv does not report the exact device
actually opened through that property. The OS resolved endpoint is separate.

A source codec never proves passthrough. An active non-null AO and a matching
`spdif-*` output format provide carrier evidence. Missing properties remain
unverified. A null AO or an unexpected PCM/carrier result for a passthrough plan
causes local PCM fallback. Bloom clears `audio-spdif` and reopens audio on the
same source and provider session. The prepared plan remains visible for comparison.
Fallback changes the active plan and records its reason. A failed exclusive
open is handled as a separate step (below), so PCM fallback keeps the mode.

mpv device lists and Qt audio output notifications refresh the endpoint state.
Both explicit and automatic outputs can reload after a device change. The
controller debounces device list changes by 500 ms. A five-second endpoint poll
also detects default endpoint and capability changes. A capability refresh
reopens audio only if the plan or the resolved endpoint changed. A long
wall-clock gap invalidates capabilities after resume. A null output triggers
rate-limited reopen attempts, so a delayed HDMI endpoint can recover while the
media session stays open. The saved explicit device is retained while
disconnected; playback uses the system default until it returns.

Each verification takes one fallback step, and a check runs about 1.2 seconds
after every reopen:

1. A rejected encoded carrier falls back to local PCM decode.
2. A null output with exclusive PCM requested falls back to shared mode for the
   session. The device may be in use by another exclusive client or reject
   every exclusive format.
3. A null output on an explicit endpoint in shared mode marks that endpoint
   failed. Playback moves to the system default for the session.
4. A null output on the system default uses the rate-limited reopen attempts.

Steps 2 and 3 show a short notice. Session fallbacks clear when the device list
changes, when the user picks an output or changes Exclusive Mode, and when the
next playback starts. Picking the current output again retries it immediately.

While exclusive output is verified active, a probe that rejects every format
for the same endpoint keeps the previous format lists. A held exclusive stream
can make a driver reject format queries. Without this rule each probe would
reopen audio.

## Output device selection

Each backend reports mpv's `audio-device-list`. During playback the list is the
observed live property. While idle, `IPlayerBackend::refreshAudioDevices`
enumerates without a media session:

- External mpv runs `mpv --no-config --audio-device=help`. The process is
  event-driven, has a 4-second deadline and 256 KiB of output. One query runs
  at a time.
- Embedded libmpv reads the property from a temporary core with no file, VO,
  scripts or config. It runs on a serialized worker that is never joined, so a
  blocked driver cannot delay shutdown.

Bloom enumerates once at startup, when Settings or the playback output list
opens, and when QtMultimedia reports an OS output change.

mpv lists every compiled driver. A Linux host can list `pipewire/...`,
`pulse/...` and many raw `alsa/...` devices for one card. The selector shows
System Default and the endpoints of the active driver only. The active driver
is mpv's `current-ao` after playback starts. Before that it is the first driver
in mpv's autoprobe order that lists a device. This prevents a selection from
bypassing PipeWire or PulseAudio. Bare driver defaults such as `pipewire` are
hidden because System Default covers them.

Ids are stable mpv device ids, for example `wasapi/{endpoint id}` or
`pipewire/<node name>`. Bloom stores the id and its label per platform under
`settings.playback.audio_output_devices.<windows|linux|macos>`. `AudioPolicy::resolveDevice`
matches the saved id against the live list:

- A listed id is used.
- A missing id is kept in config, shown as not connected, and resolves to `auto`.
  It does not change the OS default device.
- If the id is missing but exactly one listed endpoint under the same driver
  has the identical saved label, that endpoint is used. Two endpoints with the
  same label are not guessed.
- An id from another driver or platform resolves to `auto`.

`PlayerController.audioOutputOptions` and `audioOutputStatus` expose the
selector rows and the resolved state to QML. `selectAudioOutputDevice(id)` saves
the choice. It then sets mpv's `audio-device`, `audio-channels`,
`audio-exclusive` and `audio-spdif`, cycles `aid` and runs `ao-reload`.
The pinned mpv marks these options as audio-output updates. A runtime change
reopens only the AO; the demuxer, video, provider session, position, pause
state and selected tracks remain. A short audio gap is expected.

## Exclusive mode (Windows)

Settings > Playback > Exclusive Mode is shown only when the active driver is
WASAPI. It is on by default, including for existing installations. It is stored
as `settings.playback.audio_exclusive_mode`.

With the setting on, Bloom passes `audio-exclusive=yes`. mpv's WASAPI output
then tries the source rate, sample format and layout as-is. If the driver
rejects them it searches the formats the driver accepts. It never falls back to
shared mode itself; it fails, and `audio-fallback-to-null` opens the null
output. Bloom reports exclusive mode as active only when `current-ao` is
`wasapi`, `audio-exclusive` reads back as set and `audio-out-params` shows an
open output. Encoded carriers on WASAPI are always exclusive, independent of
this setting.

Exclusive mode bypasses the Windows mixer and Windows volume. Other apps cannot
play sound on that endpoint during playback. Bloom's own interface sounds use
QtMultimedia in shared mode, so they are also silent on that endpoint while
exclusive playback runs. If the endpoint's Windows "Allow applications to take
exclusive control of this device" option is off, the open fails and Bloom
continues in shared mode. Bloom does not change that or any other Windows
sound setting. Bloom's volume control still
applies, because it scales samples in mpv. At 100% and unmuted no gain is
applied. Changing the setting or the device during playback reopens audio,
which releases the previous endpoint first.

Exclusive mode is not a bit-perfect guarantee. Lossy codecs are decoded, mpv
converts to a sample format the driver accepts, and the driver may still
resample or remap channels. Check `audio-out-params` in the diagnostics for the
negotiated rate, format and layout.

On Linux, Bloom always passes `audio-exclusive=no`. mpv's PipeWire output does
implement the flag (`PW_STREAM_FLAG_EXCLUSIVE`). On PipeWire 1.6.9 with the
pinned mpv, setting it during playback produced `no target node available`,
repeated reloads and the end of playback. Bloom does not lock ALSA devices or
bypass the sound server.

## Windows HTPC validation

Use the Windows HTPC, TV and eARC soundbar. The LG C2 and onn. system are one
validation setup, not policy inputs. Use media you are permitted to play. Keep
all media outside the repository.

1. Connect the PC to the TV by HDMI. Connect the soundbar to the TV by eARC.
   Select the intended endpoint in Bloom, or use the Windows default output.
   Record Windows endpoint format settings and TV audio output settings.
2. Start Bloom with playback logging enabled. Start with Auto passthrough.
   Confirm video mode, refresh rate, HDR and renderer settings match the baseline.
3. For every sample below, save the provider's received audio fields with
   credentials removed. Save the normalized metadata, pre-provider plan,
   descriptor plan, generated mpv arguments, runtime result and soundbar display.
   Record the actual channel count, layout, sample rate and output format.
4. Repeat each encoded sample with Disabled. Expect original provider audio and
   local PCM decode. Repeat with the matching Custom format if Auto has unknown
   endpoint evidence. A custom override is an instruction to try, not proof.

| Sample | Plan when carrier support is known | Runtime and soundbar checks | Metadata result |
| --- | --- | --- | --- |
| Stereo PCM | PCM stereo | PCM, two channels, no upmix | No object indication |
| Multichannel PCM | PCM source layout if supported | PCM surround; check every speaker | No object indication |
| AC-3 | `audio-spdif=ac3` | `spdif-ac3`, Dolby Digital indication | Channel audio retained |
| E-AC-3 | `audio-spdif=eac3` | `spdif-eac3`, Dolby Digital Plus indication | Channel audio retained |
| E-AC-3 Atmos/JOC | `audio-spdif=eac3` | `spdif-eac3`, Atmos indication at soundbar | Atmos retained in carrier |
| TrueHD | `audio-spdif=truehd` | `spdif-truehd`, TrueHD indication if display supports it | Lossless channel audio retained |
| TrueHD Atmos | `audio-spdif=truehd` | `spdif-truehd`, Atmos indication at soundbar | Atmos retained in carrier |
| DTS | `audio-spdif=dts` | `spdif-dts`, DTS indication if chain accepts it | Channel audio retained |
| DTS-HD MA | `audio-spdif=dts-hd` | `spdif-dtshd`, DTS-HD indication if accepted | Lossless channel audio retained |
| DTS:X | `audio-spdif=dts-hd` | `spdif-dtshd`, DTS:X indication if accepted | DTS:X retained in carrier |

For every encoded row, absent support must select PCM with empty `audio-spdif`.
Expect PCM indication and usable channel audio. TrueHD and DTS-HD MA local decode
must not request a lossy provider conversion. Atmos/DTS:X diagnostics must report
object metadata loss with PCM. A TV chain that does not pass DTS must still play
the original DTS-HD MA through local decode. Check the provider session to prove
that its audio did not change to AAC or another converted track.

Run these recovery checks during PCM and encoded playback, then while paused:

1. Turn the TV/soundbar off. Playback must remain alive with fallback/null output.
   Turn them on after a delay. Check device restoration and fresh runtime output.
2. Disconnect and reconnect HDMI. Check both an explicit saved device and Auto.
3. Change the Windows default output to headphones, then back to HDMI in Auto.
   Stereo must stay stereo. Encoded output must stop on an unsupported endpoint.
4. Suspend the PC. Wake it before the TV and soundbar. Wait, then power them on.
   Check capability invalidation, delayed recovery, playback position and session ID.
5. Trigger the normal HDR and refresh changes. Check audio recovery and unchanged
   4K, 10-bit, HDR/Dolby Vision and renderer behavior.
6. Check Windows controls above video. Show/hide controls, resize, minimize/restore,
   maximize and enter/leave fullscreen. Record video geometry and clipping.

A test passes only with the saved evidence. A missing Windows host, receiver
indicator or sample is an unavailable manual gate, not an automated pass.

## Output selection validation

Use any two outputs the host has, for example onboard audio and a USB DAC,
headset or HDMI endpoint. Use stereo and multichannel PCM samples. Record the
playback log lines `reopening audio output on`, `Audio runtime verification`
and any `Audio output notice`.

On every platform:

1. Open Settings > Playback. Check that the selector lists System Default and
   only the active driver's endpoints with readable names.
2. During playback, open the audio panel, select Output, and switch between the
   two outputs. Check position, pause state and audio/subtitle tracks before and
   after. Expect a short audio gap only.
3. Restart Bloom. Check that the chosen output is still selected and used.
4. Disconnect the chosen output during playback. Expect the notice, System
   Default playback and an unchanged saved choice. Reconnect it and check that
   audio returns to it.
5. Play stereo and 5.1/7.1 sources. Check `audio-out-params` channel count and
   rate against the source and the output's capability.

Windows only:

1. With Exclusive Mode on, play stereo and multichannel PCM. Expect
   "Playing in exclusive mode" and other apps silent on that endpoint.
2. Turn Exclusive Mode off during playback. Expect shared mode and other apps
   audible again.
3. Start another app that holds the endpoint exclusively, then start playback.
   Expect the shared-mode notice. If shared mode also fails, expect the
   System Default notice.
4. Switch devices with Exclusive Mode on. The first device must be released;
   check that another app can use it afterwards.
5. Play a rate the endpoint does not accept exclusively, such as 44.1 kHz on a
   48 kHz-only device. Record the negotiated `audio-out-params`.

Linux only: repeat the general steps on PipeWire and on PulseAudio, and check
that `current-ao` stays the sound server's driver. Check that the system's own
routing, per-application volume and sink settings still apply.

Validation performed for this change on 2026-10-18, Linux, PipeWire 1.6.9, the
pinned mpv `v0.41.0-1200-gdde067107` over IPC, output muted:

| Check | Result |
| --- | --- |
| `audio-device` set to another PipeWire sink while paused (video + 5.1 FLAC) | `current-ao` stayed `pipewire`; `time-pos` 10.000 before and after; pause and `aid` unchanged |
| Same switch during playback | `time-pos` 12.042 to 12.083 across the switch, then normal progress |
| 5.1 FLAC with `audio-channels=auto-safe` | 6 channels, 48 kHz, `s32` before and after |
| Mono 96 kHz FLAC with `audio-channels=mono` | 1 channel at 96 kHz kept |
| `audio-channels=stereo` on the 5.1 source | 2 channels (mpv downmix) |
| `audio-device` set to a nonexistent PipeWire node | Playback continued on `pipewire` |
| `audio-exclusive=yes` on PipeWire | Stream error and end of playback; Bloom never sets it on Linux |
| Audio-only file, switch while paused | About 0.27 s of buffered audio skipped; mpv seeks back only while audio plays |

Windows WASAPI shared and exclusive playback, device contention, unsupported
exclusive formats, PulseAudio-only hosts, HDMI and Bluetooth endpoints, and the
Bloom UI on a real display were not validated for this change. They remain
manual gates.

## Upstream references

The implementation uses the current [mpv audio options](https://mpv.io/manual/master/#audio),
[structured output properties](https://mpv.io/manual/master/#properties),
[WASAPI format mapping](https://github.com/mpv-player/mpv/blob/master/audio/out/ao_wasapi_utils.c),
and Microsoft's [IEC 61937 format description](https://learn.microsoft.com/en-us/windows/win32/coreaudio/representing-formats-for-iec-61937-transmissions).
Jellyfin's [profile matching code](https://github.com/jellyfin/jellyfin/blob/master/MediaBrowser.Model/Extensions/ContainerHelper.cs)
defines empty container and codec restrictions. API fields remain checked against
Bloom's pinned provider contract manifests.
Its [stream builder](https://github.com/jellyfin/jellyfin/blob/v10.11.10/MediaBrowser.Model/Dlna/StreamBuilder.cs)
prefers matching source codecs in the fallback profile.

## Change inventory

The starting commit was `5667efd` on `main`. The branch was pulled before edits.
The following files implement this change and its verification:

- `AGENTS.md`
- `docs/audio.md`
- `docs/canonical-models.md`
- `docs/config.md`
- `docs/playback.md`
- `docs/provider-compatibility.md`
- `nix/tests.nix`
- `src/audio/AudioEndpointProbe.cpp`
- `src/audio/AudioEndpointProbe.h`
- `src/audio/AudioOutputService.cpp`
- `src/audio/AudioOutputService.h`
- `src/cmake/BloomLibraries.cmake`
- `src/models/AudioTypes.h`
- `src/models/MediaModels.cpp`
- `src/models/MediaModels.h`
- `src/network/PlaybackService.cpp`
- `src/network/PlaybackService.h`
- `src/network/Types.cpp`
- `src/network/Types.h`
- `src/player/AudioPolicy.cpp`
- `src/player/AudioPolicy.h`
- `src/player/PlayerController.cpp`
- `src/player/PlayerController.h`
- `src/player/backend/ExternalMpvBackend.cpp`
- `src/player/backend/ExternalMpvBackend.h`
- `src/player/backend/IPlayerBackend.h`
- `src/player/backend/LinuxMpvBackend.cpp`
- `src/player/backend/LinuxMpvBackend.h`
- `src/player/backend/MpvLibmpvRuntime.cpp`
- `src/player/backend/MpvLibmpvRuntime.h`
- `src/player/backend/WindowsMpvBackend.cpp`
- `src/player/backend/WindowsMpvBackend.h`
- `src/providers/jellyfin/JellyfinModelMapper.cpp`
- `src/providers/jellyfin/JellyfinPlaybackProvider.cpp`
- `src/providers/silo/SiloModelMapper.cpp`
- `src/providers/silo/SiloPlaybackProvider.cpp`
- `src/ui/SettingsScreen.qml`
- `src/ui/settings/PlaybackSettings.qml`
- `src/utils/ConfigManager.cpp`
- `src/utils/ConfigManager.h`
- `tests/AudioOutputServiceTest.cpp`
- `tests/AudioPolicyTest.cpp`
- `tests/CMakeLists.txt`
- `tests/CanonicalModelsTest.cpp`
- `tests/ConfigManagerThemeTest.cpp`
- `tests/PlayerControllerAutoplayContextTest.cpp`
- `tests/ProviderCatalogTest.cpp`
- `tests/SiloPlaybackProviderTest.cpp`

Automated results and unavailable platform gates are reported with the handoff.
The codec and recovery tests use synthetic metadata and fake endpoints. They
do not certify physical HDMI/eARC output.

### Output selection and exclusive mode

The starting commit was `0f5a00d` on `main`. These files implement output
selection, exclusive mode and their verification:

- `AGENTS.md`
- `docs/audio.md`
- `docs/config.md`
- `docs/playback.md`
- `src/audio/AudioOutputService.cpp`
- `src/audio/AudioOutputService.h`
- `src/models/AudioTypes.h`
- `src/player/AudioPolicy.cpp`
- `src/player/AudioPolicy.h`
- `src/player/PlayerController.cpp`
- `src/player/PlayerController.h`
- `src/player/backend/ExternalMpvBackend.cpp`
- `src/player/backend/ExternalMpvBackend.h`
- `src/player/backend/IPlayerBackend.h`
- `src/player/backend/LinuxMpvBackend.cpp`
- `src/player/backend/LinuxMpvBackend.h`
- `src/player/backend/MpvLibmpvRuntime.cpp`
- `src/player/backend/MpvLibmpvRuntime.h`
- `src/player/backend/WindowsMpvBackend.cpp`
- `src/player/backend/WindowsMpvBackend.h`
- `src/ui/EmbeddedPlaybackOverlay.qml`
- `src/ui/Icons.qml`
- `src/ui/SettingsScreen.qml`
- `src/ui/settings/PlaybackSettings.qml`
- `src/utils/ConfigManager.cpp`
- `src/utils/ConfigManager.h`
- `tests/AudioOutputServiceTest.cpp`
- `tests/AudioPolicyTest.cpp`
- `tests/ConfigManagerThemeTest.cpp`
- `tests/PlayerBackendFactoryTest.cpp`
- `tests/PlayerControllerAutoplayContextTest.cpp`
