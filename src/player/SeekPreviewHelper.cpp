#include "SeekPreviewProtocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <clocale>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <utility>

#if defined(BLOOM_HAS_LIBMPV)
#include <mpv/client.h>
#endif

namespace {
namespace Protocol = Bloom::SeekPreviewProtocol;

bool progress(const char *stage, qint64 index)
{
    return std::printf("%s %lld\n", stage, static_cast<long long>(index)) >= 0
        && std::fflush(stdout) == 0;
}

#if defined(BLOOM_HAS_LIBMPV)
bool configureMpv(mpv_handle *handle, const QJsonObject &request, const QString &rawDirectory, bool selfTest)
{
    const auto option = [handle](const char *name, const QByteArray &value) {
        return mpv_set_option_string(handle, name, value.constData()) >= 0;
    };
    // mpv only defines `ytdl` when it is built with Lua. The Linux packaging
    // builds mpv without scripting, so its absence is expected, not a failure.
    const int ytdlStatus = mpv_set_option_string(handle, "ytdl", "no");
    if (ytdlStatus < 0 && ytdlStatus != MPV_ERROR_OPTION_NOT_FOUND) return false;
    for (const auto &[name, value] : {
             std::pair { "config", "no" }, { "load-scripts", "no" },
             { "terminal", "no" }, { "msg-level", "all=no" },
             { "audio", "no" }, { "sub", "no" }, { "secondary-sid", "no" },
             { "autoload-files", "no" }, { "cover-art-auto", "no" },
             { "hwdec", "no" }, { "vd-lavc-threads", "2" },
             { "vo", "image" }, { "vo-image-format", "jpg" }, { "vo-image-jpeg-quality", "75" },
             { "pause", "yes" }, { "idle", "yes" }, { "keep-open", "yes" }, { "cache", "no" },
             { "demuxer-readahead-secs", "0" }, { "demuxer-max-bytes", "131072" },
             { "network-timeout", "5" }, { "tls-verify", "yes" },
             { "hr-seek", "no" }, { "audio-display", "no" } }) {
        if (!option(name, value)) return false;
    }
    if (!option("vo-image-outdir", rawDirectory.toUtf8())) return false;
    QString filters;
    if (selfTest) {
        filters = QStringLiteral("setparams=color_primaries=bt2020:color_trc=smpte2084:colorspace=bt2020nc,");
    }
    filters += QStringLiteral("scale=w=320:h=180:force_original_aspect_ratio=decrease:reset_sar=1");
    if (request.value(QStringLiteral("toneMap")).toBool()) {
        filters += QStringLiteral(",zscale=transfer=linear:npl=100,format=gbrpf32le,tonemap=hable,"
                                  "zscale=primaries=bt709:transfer=bt709:matrix=bt709:range=limited");
    }
    filters += QStringLiteral(",format=yuv420p");
    if (!option("vf", (QStringLiteral("lavfi=[") + filters + QLatin1Char(']')).toUtf8())) return false;

    const QJsonObject headers = request.value(QStringLiteral("headers")).toObject();
    QList<QByteArray> headerBytes;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const QByteArray name = it.key().toLatin1();
        const QByteArray value = it.value().toString().toUtf8();
        if (name.isEmpty() || !it.value().isString()) return false;
        for (const char ch : name) {
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                  || (ch >= '0' && ch <= '9') || QByteArray("!#$%&'*+-.^_`|~").contains(ch))) return false;
        }
        if (value.contains('\r') || value.contains('\n') || value.contains('\0')) return false;
        headerBytes.append(name + ": " + value);
    }
    QList<mpv_node> nodes;
    for (QByteArray &header : headerBytes) {
        mpv_node node {};
        node.format = MPV_FORMAT_STRING;
        node.u.string = header.data();
        nodes.append(node);
    }
    mpv_node_list list {};
    list.num = static_cast<int>(nodes.size());
    list.values = nodes.data();
    mpv_node optionNode {};
    optionNode.format = MPV_FORMAT_NODE_ARRAY;
    optionNode.u.list = &list;
    return headers.isEmpty()
        || mpv_set_option(handle, "http-header-fields", MPV_FORMAT_NODE, &optionNode) >= 0;
}

int waitForFrame(mpv_handle *handle)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 8000) {
        const mpv_event *event = mpv_wait_event(handle, 0.05);
        if (event->event_id == MPV_EVENT_PLAYBACK_RESTART) return Protocol::Success;
        if (event->event_id == MPV_EVENT_END_FILE || event->event_id == MPV_EVENT_SHUTDOWN
            || (event->event_id == MPV_EVENT_COMMAND_REPLY && event->error < 0)) return Protocol::Frame;
    }
    return Protocol::FrameTimeout;
}

int generate(const QJsonObject &request, const QDir &output, bool selfTest)
{
    const QUrl url(request.value(QStringLiteral("url")).toString());
    const qint64 durationMs = request.value(QStringLiteral("durationMs")).toInteger();
    if (!url.isValid() || durationMs <= 0 || durationMs > Protocol::maxDurationMs
        || !(url.isLocalFile() || ((url.scheme() == QStringLiteral("http")
              || url.scheme() == QStringLiteral("https")) && !url.host().isEmpty()
              && url.userInfo().isEmpty()))) return Protocol::InvalidRequest;
    const qint64 count = (durationMs - 1) / Protocol::intervalMs + 1;
    QList<qint64> order;
    QSet<qint64> ordered;
    for (const auto value : request.value(QStringLiteral("order")).toArray()) {
        const qint64 index = value.toInteger(-1);
        if (index < 0 || index >= count || ordered.contains(index)) return Protocol::InvalidRequest;
        order.append(index);
        ordered.insert(index);
    }
    const qint64 paceMs = request.value(QStringLiteral("paceMs")).toInteger(-1);
    if (order.isEmpty() || paceMs < 0 || paceMs > Protocol::maxFramePaceMs) return Protocol::InvalidRequest;
    QTemporaryDir raw(output.filePath(QStringLiteral("raw-XXXXXX")));
    if (!raw.isValid()) return Protocol::Image;
    // QCoreApplication resets the Unix locale during construction.
    if (!std::setlocale(LC_NUMERIC, "C")) return Protocol::Initialization;
    const std::unique_ptr<mpv_handle, decltype(&mpv_terminate_destroy)> handle(
        mpv_create(), &mpv_terminate_destroy);
    if (!handle || !configureMpv(handle.get(), request, raw.path(), selfTest)
        || mpv_initialize(handle.get()) < 0) return Protocol::Initialization;
    const QByteArray media = url.isLocalFile() ? url.toLocalFile().toUtf8() : url.toEncoded();
    bool loaded = false;
    qint64 totalImageBytes = 0;
    for (const auto &frame : output.entryInfoList({ QStringLiteral("frame-*.jpg") }, QDir::Files)) {
        totalImageBytes += frame.size();
    }
    const QDir rawOutput(raw.path());
    for (qsizetype slot = 0; slot < order.size(); ++slot) {
        const qint64 index = order.at(slot);
        const QString destination = output.filePath(QStringLiteral("frame-%1.jpg").arg(index));
        if (slot > 0 && paceMs > 0) QThread::msleep(static_cast<unsigned long>(paceMs));
        if (QFileInfo::exists(destination) && !QFile::remove(destination)) return Protocol::Image;
        if (!progress("begin", index)) return Protocol::ParentClosed;
        const QByteArray position = QByteArray::number(index * Protocol::intervalMs / 1000);
        if (!loaded) {
            if (mpv_set_property_string(handle.get(), "start", position.constData()) < 0) return Protocol::Source;
            const char *load[] = { "loadfile", media.constData(), nullptr };
            if (mpv_command_async(handle.get(), 0, load) < 0) return Protocol::Source;
            loaded = true;
        } else {
            const char *seek[] = { "seek", position.constData(), "absolute+keyframes", nullptr };
            if (mpv_command_async(handle.get(), 0, seek) < 0) return Protocol::Frame;
        }
        const int result = waitForFrame(handle.get());
        if (result != Protocol::Success) return result;
        int seekable = 0;
        if (mpv_get_property(handle.get(), "seekable", MPV_FORMAT_FLAG, &seekable) < 0
            || !seekable) return Protocol::Source;
        std::int64_t width = 0;
        std::int64_t height = 0;
        const QStringList images = rawOutput.entryList({ QStringLiteral("????????.jpg") }, QDir::Files, QDir::Name);
        // No picture for this position (for example past the last frame) fails only this frame.
        if (mpv_get_property(handle.get(), "video-out-params/w", MPV_FORMAT_INT64, &width) < 0
            || mpv_get_property(handle.get(), "video-out-params/h", MPV_FORMAT_INT64, &height) < 0
            || width <= 0 || height <= 0 || images.isEmpty()) return Protocol::Frame;
        if (width > 320 || height > 180 || (selfTest && (width != 320 || height != 180))) return Protocol::Image;
        if (!QFile::rename(rawOutput.filePath(images.last()), destination)) return Protocol::Image;
        totalImageBytes += QFileInfo(destination).size();
        if (totalImageBytes > Protocol::maxTotalImageBytes) {
            QFile::remove(destination);
            return Protocol::CacheLimit;
        }
        for (qsizetype i = 0; i + 1 < images.size(); ++i) QFile::remove(rawOutput.filePath(images.at(i)));
        if (!progress("ready", index)) return Protocol::ParentClosed;
    }
    return Protocol::Success;
}
#endif
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
#if defined(BLOOM_HAS_LIBMPV)
    if (app.arguments() == QStringList { app.arguments().first(), QStringLiteral("--self-test") }) {
        QTemporaryDir output;
        QFile video(output.filePath(QStringLiteral("sample.y4m")));
        if (!output.isValid() || !video.open(QIODevice::WriteOnly)) return Protocol::Image;
        video.write("YUV4MPEG2 W640 H360 F1:1 Ip A1:1 C420jpeg\nFRAME\n");
        video.write(QByteArray(640 * 360 * 3 / 2, static_cast<char>(128)));
        video.close();
        return generate({ { QStringLiteral("url"), QUrl::fromLocalFile(video.fileName()).toString() },
                          { QStringLiteral("durationMs"), 1000 }, { QStringLiteral("toneMap"), true },
                          { QStringLiteral("order"), QJsonArray { 0 } }, { QStringLiteral("paceMs"), 0 } },
                        QDir(output.path()), true);
    }
    if (app.arguments().size() != 2 || !QDir(app.arguments().at(1)).exists()) return Protocol::InvalidRequest;
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return Protocol::InvalidRequest;
    const QByteArray bytes = input.read(Protocol::maxRequestBytes + 1);
    if (bytes.isEmpty() || bytes.size() > Protocol::maxRequestBytes) return Protocol::InvalidRequest;
    return generate(QJsonDocument::fromJson(bytes).object(), QDir(app.arguments().at(1)), false);
#else
    return Protocol::Initialization;
#endif
}
