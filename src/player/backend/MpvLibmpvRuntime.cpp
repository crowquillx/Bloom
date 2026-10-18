#include "MpvLibmpvRuntime.h"

#include "MpvNodeUtils.h"
#include "utils/BloomLogging.h"

#include <QPromise>
#include <QThreadPool>

#include <memory>
#include <utility>

#if defined(BLOOM_HAS_LIBMPV)
#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <dlfcn.h>
#endif
#endif

namespace PlayerPolicy = Bloom::PlaybackPolicy;

namespace BloomBackend {
namespace {

QString listOperationFor(const QString &optionName)
{
    if (optionName == QStringLiteral("vf") || optionName == QStringLiteral("vf-set")) {
        return QStringLiteral("set");
    }
    static const QStringList kOperations{
        QStringLiteral("append"), QStringLiteral("add"), QStringLiteral("pre"),
        QStringLiteral("remove"), QStringLiteral("toggle"), QStringLiteral("clr"),
    };
    if (!optionName.startsWith(QStringLiteral("vf-"))) {
        return {};
    }
    const QString suffix = optionName.mid(3);
    return kOperations.contains(suffix) ? suffix : QString();
}

#if defined(BLOOM_HAS_LIBMPV)
// High tag keeps snapshot replies distinct from the backends' own userdata.
constexpr quint64 kSnapshotUserdataTag = 0xB100'0000'0000'0000ULL;
constexpr quint64 kSnapshotIndexBits = 16;

int runVfCommand(mpv_handle *handle, const QString &operation, const QString &value)
{
    const QByteArray op = operation.toUtf8();
    const QByteArray arg = value.toUtf8();
    const char *command[] = {"vf", op.constData(), arg.constData(), nullptr};
    return mpv_command(handle, command);
}
#endif

} // namespace

QList<VideoFilterListOperation> takeVideoFilterListOperations(QStringList *args)
{
    QList<VideoFilterListOperation> operations;
    if (!args) {
        return operations;
    }
    QStringList remaining;
    remaining.reserve(args->size());
    for (const QString &arg : std::as_const(*args)) {
        if (!arg.startsWith(QStringLiteral("--"))) {
            remaining << arg;
            continue;
        }
        const QString option = arg.mid(2);
        const int equalsIndex = option.indexOf(QLatin1Char('='));
        const QString name = (equalsIndex >= 0 ? option.left(equalsIndex) : option).toLower();
        const QString operation = listOperationFor(name);
        if (operation.isEmpty()) {
            remaining << arg;
            continue;
        }
        operations.append({operation, equalsIndex >= 0 ? option.mid(equalsIndex + 1) : QString()});
    }
    *args = remaining;
    return operations;
}

#if defined(BLOOM_HAS_LIBMPV)

PlayerPolicy::FormatFilterSupport probeLinkedFormatFilterSupport()
{
    static const PlayerPolicy::FormatFilterSupport cached = []() {
        mpv_handle *handle = mpv_create();
        if (!handle) {
            return PlayerPolicy::FormatFilterSupport::Unknown;
        }
        PlayerPolicy::FormatFilterSupport support = PlayerPolicy::FormatFilterSupport::Unsupported;
        if (mpv_set_option_string(handle, "vf", "format=dolbyvision=yes:enhancement-layer=yes") >= 0) {
            support = PlayerPolicy::FormatFilterSupport::DolbyVisionAndEnhancementLayer;
        } else if (mpv_set_option_string(handle, "vf", "format=dolbyvision=yes") >= 0) {
            support = PlayerPolicy::FormatFilterSupport::DolbyVisionOnly;
        }
        mpv_terminate_destroy(handle);
        return support;
    }();
    return cached;
}

QString loadedLibmpvPath()
{
#if defined(Q_OS_WIN)
    for (const wchar_t *name : {L"libmpv-2.dll", L"mpv-2.dll", L"mpv.dll"}) {
        HMODULE module = GetModuleHandleW(name);
        if (!module) {
            continue;
        }
        wchar_t buffer[MAX_PATH * 4] = {};
        const DWORD length = GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
        if (length > 0) {
            return QString::fromWCharArray(buffer, static_cast<int>(length));
        }
    }
    return {};
#elif defined(Q_OS_UNIX)
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(&mpv_create), &info) != 0 && info.dli_fname) {
        return QString::fromLocal8Bit(info.dli_fname);
    }
    return {};
#else
    return {};
#endif
}

PlayerPolicy::MpvRuntimeInfo readRuntimeInfo(mpv_handle *handle, const QString &backendName)
{
    PlayerPolicy::MpvRuntimeInfo info;
    info.backend = backendName;
    info.libraryPath = loadedLibmpvPath();
    const unsigned long api = mpv_client_api_version();
    info.clientApiVersion = QStringLiteral("%1.%2").arg(api >> 16).arg(api & 0xffff);
    info.formatFilter = probeLinkedFormatFilterSupport();
    if (!handle) {
        return info;
    }
    auto stringProperty = [handle](const char *name) {
        char *value = mpv_get_property_string(handle, name);
        const QString result = value ? QString::fromUtf8(value) : QString();
        mpv_free(value);
        return result;
    };
    info.mpvVersion = stringProperty("mpv-version");
    info.ffmpegVersion = stringProperty("ffmpeg-version");
    info.libplaceboVersion = stringProperty("libplacebo-version");
#if defined(BLOOM_PINNED_MPV_DESCRIBE)
    info.matchesVerifiedPackagedStack =
        info.mpvVersion == QStringLiteral("mpv ") + QStringLiteral(BLOOM_PINNED_MPV_DESCRIBE);
#endif
    return info;
}

QFuture<QVariantList> enumerateAudioDevicesAsync()
{
    // Leaked on purpose: a blocked driver call must never make shutdown join it.
    static QThreadPool *pool = [] {
        auto *created = new QThreadPool;
        created->setMaxThreadCount(1);
        created->setExpiryTimeout(-1);
        return created;
    }();
    static QFuture<QVariantList> inFlight;
    if (inFlight.isRunning()) {
        return inFlight;
    }
    auto promise = std::make_shared<QPromise<QVariantList>>();
    inFlight = promise->future();
    promise->start();
    pool->start([promise]() {
        QVariantList devices;
        if (mpv_handle *handle = mpv_create()) {
            static constexpr std::pair<const char *, const char *> kOptions[] = {
                {"config", "no"}, {"terminal", "no"}, {"idle", "yes"},
                {"load-scripts", "no"}, {"ytdl", "no"}, {"input-default-bindings", "no"}};
            for (const auto &[name, value] : kOptions) {
                mpv_set_option_string(handle, name, value);
            }
            mpv_node node{};
            if (mpv_initialize(handle) >= 0
                && mpv_get_property(handle, "audio-device-list", MPV_FORMAT_NODE, &node) >= 0) {
                devices = parseMpvAudioDeviceList(&node);
                mpv_free_node_contents(&node);
            }
            mpv_terminate_destroy(handle);
        }
        promise->addResult(devices);
        promise->finish();
    });
    return inFlight;
}

QStringList applyVideoFilterListOperations(mpv_handle *handle,
                                           const QList<VideoFilterListOperation> &operations)
{
    QStringList failures;
    if (!handle) {
        return failures;
    }
    const QString bloomLabel =
        QStringLiteral("@%1:").arg(QLatin1StringView(PlayerPolicy::kDolbyVisionFilterLabel));
    for (const VideoFilterListOperation &operation : operations) {
        int status = runVfCommand(handle, operation.operation, operation.value);
        if (status < 0 && operation.value.startsWith(bloomLabel)
            && operation.value.contains(QStringLiteral("enhancement-layer"))) {
            const QString fallback =
                PlayerPolicy::dolbyVisionFormatFilter(PlayerPolicy::FormatFilterSupport::DolbyVisionOnly);
            qCWarning(lcPlayback) << "libmpv rejected" << operation.value
                                  << "- retrying without enhancement-layer:" << fallback;
            status = runVfCommand(handle, operation.operation, fallback);
        }
        if (status < 0) {
            failures << QStringLiteral("vf %1 %2: %3")
                            .arg(operation.operation, operation.value,
                                 QString::fromUtf8(mpv_error_string(status)));
        }
    }
    return failures;
}

void LibmpvPropertySnapshots::request(mpv_handle *handle,
                                      const QString &stage,
                                      const QStringList &properties)
{
    if (!handle || properties.isEmpty()
        || properties.size() >= (1 << kSnapshotIndexBits)) {
        return;
    }
    for (auto it = m_snapshots.begin(); it != m_snapshots.end();) {
        if (it->deadline.hasExpired()) it = m_snapshots.erase(it);
        else ++it;
    }
    if (m_snapshots.size() >= 4) return;
    const quint64 snapshotId = m_nextSnapshot++;
    Snapshot snapshot;
    snapshot.stage = stage;
    snapshot.names = properties;
    for (int index = 0; index < properties.size(); ++index) {
        const quint64 userdata = kSnapshotUserdataTag
            | ((snapshotId & 0xffff'ffffULL) << kSnapshotIndexBits)
            | static_cast<quint64>(index);
        const QByteArray name = properties.at(index).toUtf8();
        if (mpv_get_property_async(handle, userdata, name.constData(), MPV_FORMAT_NODE) >= 0) {
            ++snapshot.outstanding;
        }
    }
    if (snapshot.outstanding > 0) {
        m_snapshots.insert(snapshotId, snapshot);
    }
}

bool LibmpvPropertySnapshots::handleEvent(const mpv_event *event, const Callback &callback)
{
    if (!event || event->event_id != MPV_EVENT_GET_PROPERTY_REPLY
        || (event->reply_userdata & kSnapshotUserdataTag) != kSnapshotUserdataTag) {
        return false;
    }
    const quint64 userdata = event->reply_userdata & ~kSnapshotUserdataTag;
    const quint64 snapshotId = userdata >> kSnapshotIndexBits;
    const int index = static_cast<int>(userdata & ((1ULL << kSnapshotIndexBits) - 1));
    const auto snapshot = m_snapshots.find(snapshotId);
    if (snapshot == m_snapshots.end()) {
        return true;
    }
    const auto *property = static_cast<const mpv_event_property *>(event->data);
    if (event->error >= 0 && property && property->format == MPV_FORMAT_NODE && property->data
        && index < snapshot->names.size()) {
        snapshot->values.insert(snapshot->names.at(index),
                                mpvNodeToVariant(static_cast<const mpv_node *>(property->data)));
    }
    if (--snapshot->outstanding <= 0) {
        const QString stage = snapshot->stage;
        const QVariantMap values = snapshot->values;
        m_snapshots.erase(snapshot);
        if (callback) {
            callback(stage, values);
        }
    }
    return true;
}

void LibmpvPropertySnapshots::reset()
{
    m_snapshots.clear();
}

#endif // BLOOM_HAS_LIBMPV

} // namespace BloomBackend
