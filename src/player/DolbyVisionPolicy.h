#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVersionNumber>

#include "player/PlaybackPolicy.h"

// Pure Dolby Vision / mpv runtime capability policy. Lives in
// Bloom::PlayerPolicy: no I/O, no QObject. See docs/playback.md
// ("Dolby Vision processing and Profile 7 FEL").
namespace Bloom::PlaybackPolicy {

// Upstream FEL baseline (mpv PR #17932, merge commit 99b4c12). Keep in sync
// with packaging/dependencies.json `dolby_vision_fel`.
inline constexpr int kFelMinimumLibplaceboApi = 370;
inline constexpr int kFelMinimumLibavcodecMajor = 62;
inline constexpr int kFelMinimumLibavcodecMinor = 36;
inline constexpr int kFelMinimumLibavcodecMicro = 100;

// Label used for the Bloom-managed format filter. mpv list options replace a
// labelled entry in place, so appending it twice never duplicates it.
inline constexpr auto kDolbyVisionFilterLabel = "bloom-dovi";

struct DolbyVisionSourceInfo
{
    bool isDolbyVision = false;
    int profile = 0;           // 0 when the provider did not report one
    int compatibilityId = -1;  // -1 when unknown
    // Profile 7 is dual-layer by definition (BL + EL + RPU). Whether the EL is
    // MEL or FEL is only known from the RPU inside mpv/libplacebo, so Bloom
    // never asserts FEL from provider metadata; `enhancementLayerHint` carries
    // an explicit "fel"/"mel" tag only when the title/path names it.
    bool dualLayer = false;
    QString enhancementLayerHint;
};

[[nodiscard]] DolbyVisionSourceInfo describeDolbyVisionSource(const QVariantMap &mediaSource);
[[nodiscard]] QString dolbyVisionSourceSummary(const DolbyVisionSourceInfo &info);

enum class FormatFilterSupport {
    Unknown,                         // not probed yet; emit nothing
    Unsupported,                     // `format` lacks `dolbyvision` (pre-0.37)
    DolbyVisionOnly,                 // pre-PR-#17932 mpv
    DolbyVisionAndEnhancementLayer,  // post-PR-#17932 mpv
};

[[nodiscard]] QString formatFilterSupportName(FormatFilterSupport support);

// `@bloom-dovi:format=...` for the given support level, or empty.
[[nodiscard]] QString dolbyVisionFormatFilter(FormatFilterSupport support);

// Appends `--vf-append=@bloom-dovi:format=...` for Dolby Vision sources only.
// Existing filters (profile deinterlacing, user mpv.conf/extra args) are
// preserved because the entry is appended through mpv's list semantics;
// call this after every other arg so a later `--vf=` cannot drop it.
[[nodiscard]] QStringList appendDolbyVisionFilterArgs(const QStringList &args,
                                                      HdrContentKind kind,
                                                      FormatFilterSupport support);

// Args (in order) whose filter lists disable DV metadata or the EL, e.g.
// `--vf-append=format=dolbyvision=no`. Bloom does not rewrite them (they are
// explicit user choices) but reports them in diagnostics.
[[nodiscard]] QStringList dolbyVisionStrippingFilterArgs(const QStringList &args);

enum class Capability { Unknown, Supported, Unsupported };
[[nodiscard]] QString capabilityName(Capability capability);

enum class RendererKind {
    Unknown,
    GpuNext,         // vo=gpu-next: libplacebo DV reshaping + FEL composition
    LegacyGpu,       // vo=gpu: no DV reshaping
    LibmpvRenderApi, // vo=libmpv render API: legacy vo_gpu renderer, no DV
};

[[nodiscard]] QString rendererKindName(RendererKind renderer);
[[nodiscard]] RendererKind rendererKindForVo(const QString &vo);

struct MpvRuntimeInfo
{
    QString backend;
    QString mpvVersion;          // e.g. "mpv v0.41.0-1200-gdde067107"
    QString ffmpegVersion;       // e.g. "9.0.2" or "N-127602-g2d49d0e62"
    QString libplaceboVersion;   // e.g. "v7.376.0 (v7.360.0-171-g0df80ae)"
    QVersionNumber libavcodecVersion; // runtime version where reported
    QString executablePath;      // external mpv
    QString libraryPath;         // embedded libmpv
    QString clientApiVersion;    // embedded libmpv, "major.minor"
    FormatFilterSupport formatFilter = FormatFilterSupport::Unknown;
    // True when mpvVersion equals the build-time verified packaged pin, whose
    // FFmpeg dovi_split / libplacebo API were checked during packaging.
    bool matchesVerifiedPackagedStack = false;
};

// Parses `mpv --version` output (also the verbose startup banner).
[[nodiscard]] MpvRuntimeInfo parseMpvVersionOutput(const QString &text);
// "v7.376.0 ..." -> 376; -1 when unparseable.
[[nodiscard]] int libplaceboApiVersion(const QString &libplaceboVersion);
// "9.0.2"/"n9.0.2" -> 9.0.2; git snapshots ("N-...") -> null.
[[nodiscard]] QVersionNumber ffmpegReleaseVersion(const QString &ffmpegVersion);

struct FelCapability
{
    Capability overall = Capability::Unknown;
    Capability mpvEnhancementLayer = Capability::Unknown;
    Capability ffmpegDoviSplit = Capability::Unknown;
    Capability libplacebo = Capability::Unknown;
    Capability renderer = Capability::Unknown;
    QStringList reasons;
};

[[nodiscard]] FelCapability evaluateFelCapability(const MpvRuntimeInfo &info,
                                                  RendererKind renderer);
[[nodiscard]] QString felCapabilitySummary(const FelCapability &capability);

// Structured playback snapshot assembled from mpv properties. Keys are the
// mpv property names (`track-list`, `vf`, `video-params`, `vo-passes`,
// `hwdec-current`, `options/hwdec`, ...); missing keys mean "unavailable".
struct PlaybackDiagnostics
{
    QString vo;
    QString gpuContext;
    QString gpuApi;
    QString hwdecRequested;
    QString hwdecCurrent;
    bool softwareDecoding = false;
    bool hardwareFallbackToSoftware = false;
    QString targetColorspaceHint;
    QString targetColorspaceHintMode;
    QString videoFilterChain;

    QString videoCodec;
    QString videoDecoder;
    int dolbyVisionProfile = 0;
    int bitDepth = 0;
    QString pixelFormat;
    QString sourceTransfer;
    QString sourcePrimaries;
    QString sourceColormatrix;
    QString outputTransfer;
    QString outputPrimaries;
    double outputSignalPeak = 0.0;

    // Dolby Vision evidence (all derived from structured properties).
    bool dolbyVisionMetadataApplied = false;   // video-params colormatrix=dolbyvision
    bool dolbyVisionMetadataStripped = false;  // filter chain has dolbyvision=no
    bool enhancementLayerTrackPresent = false; // dependent video track in track-list
    bool enhancementLayerTrackObservable = false;
    bool enhancementLayerEnabled = true;       // no enhancement-layer=no in chain
    bool enhancementLayerRendered = false;     // vo-passes contains "enhancement layer"
    bool renderPassesObservable = false;

    qint64 droppedFrames = -1;
    qint64 decoderDroppedFrames = -1;
    qint64 delayedFrames = -1;
};

[[nodiscard]] QString describeVideoFilterChain(const QVariant &vfProperty);
[[nodiscard]] PlaybackDiagnostics evaluatePlaybackDiagnostics(const QVariantMap &properties);
[[nodiscard]] QString playbackDiagnosticsSummary(const PlaybackDiagnostics &diagnostics,
                                                 const DolbyVisionSourceInfo &source);

// Properties a backend should snapshot for evaluatePlaybackDiagnostics().
[[nodiscard]] QStringList playbackDiagnosticProperties();
[[nodiscard]] QStringList runtimeProvenanceProperties();

} // namespace Bloom::PlaybackPolicy
