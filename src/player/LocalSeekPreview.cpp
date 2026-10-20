#include "LocalSeekPreview.h"
#include "utils/BloomLogging.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>
#include <cmath>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <sys/resource.h>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#endif
#endif

struct SeekPreviewCache {
    explicit SeekPreviewCache(const QString &root)
        : directory(root + QStringLiteral("/session-XXXXXX")),
          lock(directory.path() + QStringLiteral("/active.lock"))
    {
        locked = directory.isValid() && lock.tryLock();
    }
    QTemporaryDir directory;
    QLockFile lock;
    bool locked = false;
};

namespace {
namespace Protocol = Bloom::SeekPreviewProtocol;

bool supportedStream(const Bloom::StreamRequest &stream)
{
    const auto &url = stream.url;
    return stream.isValid() && stream.method == Bloom::PlaybackMethod::DirectPlay
        && (url.isLocalFile() || ((url.scheme() == QStringLiteral("http")
            || url.scheme() == QStringLiteral("https")) && !url.host().isEmpty() && url.userInfo().isEmpty()));
}

QString defaultHelperProgram()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/bloom-seek-preview")
#ifdef Q_OS_WIN
        + QStringLiteral(".exe")
#endif
        ;
}

void removeStaleCaches(const QString &root)
{
    QThreadPool::globalInstance()->start([root] {
        const QDir directory(root);
        for (const auto &entry : directory.entryInfoList({ QStringLiteral("session-*") }, QDir::Dirs | QDir::NoDotAndDotDot)) {
            // A new instance can still be creating its lock file.
            if (entry.isSymLink() || entry.lastModified().secsTo(QDateTime::currentDateTimeUtc()) < 60) continue;
            QLockFile lock(entry.filePath() + QStringLiteral("/active.lock"));
            lock.setStaleLockTime(0);
            if (lock.tryLock()) QDir(entry.filePath()).removeRecursively();
        }
    });
}

#ifdef Q_OS_WIN
std::shared_ptr<void> createProcessJob()
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return {};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job);
        return {};
    }
    return { job, [](void *value) { CloseHandle(value); } };
}

bool assignProcessJob(QProcess *process, void *job)
{
    HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process->processId()));
    if (!child) return false;
    const bool assigned = AssignProcessToJobObject(job, child) != FALSE;
    CloseHandle(child);
    return assigned;
}
#endif
}

LocalSeekPreview::LocalSeekPreview(QObject *parent)
    : LocalSeekPreview(Options { defaultHelperProgram(), 10000, {} }, parent)
{
}

LocalSeekPreview::LocalSeekPreview(Options options, QObject *parent)
    : QObject(parent), m_options(std::move(options))
{
    if (m_options.cacheRoot.isEmpty()) {
        m_options.cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/seek-previews");
    }
    removeStaleCaches(m_options.cacheRoot);
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] { fail("deadline", 0, true); });
}

LocalSeekPreview::~LocalSeekPreview()
{
    cancelWork();
}

void LocalSeekPreview::setSource(const Bloom::PlaybackDescriptor &descriptor, bool toneMap)
{
    clear();
    if (!descriptor.isValid() || descriptor.durationMs <= 0 || descriptor.durationMs > Protocol::maxDurationMs
        || !supportedStream(descriptor.stream)) return;
    if (!QFileInfo::exists(m_options.helperProgram)) {
        qCWarning(lcPlaybackTrickplay) << "Local seek previews unavailable: helper is missing";
        return;
    }
#if defined(BLOOM_HAS_LIBMPV)
    m_descriptor = descriptor;
    m_toneMap = toneMap;
    m_available = true;
#else
    Q_UNUSED(toneMap);
#endif
}

void LocalSeekPreview::updateStream(const Bloom::StreamRequest &stream)
{
    if (!m_descriptor.isValid() || !supportedStream(stream)) {
        clear();
        return;
    }
    const qint64 count = (m_descriptor.durationMs - 1) / intervalMs + 1;
    const bool resume = m_frames.size() < count;
    m_descriptor.stream = stream;
    if (resume) {
        m_complete = false;
        m_failures = 0;
        m_skippedFrames.clear();
        m_available = true;
        cancelWork(true);
    }
}

void LocalSeekPreview::clear()
{
    const bool hadFrames = !m_frames.isEmpty();
    cancelWork();
    m_descriptor = {};
    m_available = false;
    m_frames.clear();
    m_cache.reset();
    m_totalImageBytes = 0;
    m_progress.clear();
    m_skippedFrames.clear();
    m_failures = 0;
    m_complete = false;
    if (hadFrames) emit framesChanged();
}

void LocalSeekPreview::cancelWork(bool restart)
{
    m_deadline.stop();
    m_progress.clear();
    m_runningFrame = -1;
    const quint64 generation = ++m_generation;
    QPointer<LocalSeekPreview> owner(this);
    const auto resume = [owner, generation, restart] {
        if (restart && owner && owner->m_generation == generation) owner->start();
    };
    if (QProcess *process = std::exchange(m_process, nullptr)) {
        disconnect(process, nullptr, this, nullptr);
        const auto cache = m_cache;
        const auto job = std::move(m_processJob);
        process->setParent(QCoreApplication::instance());
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                process, [process, cache, job, resume] { process->deleteLater(); resume(); });
        process->kill();
        if (process->state() == QProcess::NotRunning) {
            process->deleteLater();
            QTimer::singleShot(0, this, resume);
        }
    } else if (restart) {
        QTimer::singleShot(0, this, resume);
    }
}

qint64 LocalSeekPreview::frameIndex(double seconds) const
{
    if (!m_available || !std::isfinite(seconds) || seconds < 0.0
        || seconds * 1000.0 >= static_cast<double>(m_descriptor.durationMs)) return -1;
    return static_cast<qint64>(seconds * 1000.0) / intervalMs;
}

QString LocalSeekPreview::frameUrlAt(double seconds) const
{
    return m_frames.value(frameIndex(seconds));
}

void LocalSeekPreview::start()
{
    if (!m_available || m_process || m_complete) return;
    if (!m_cache) {
        QDir().mkpath(m_options.cacheRoot);
        QFile::setPermissions(m_options.cacheRoot, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        m_cache = { new SeekPreviewCache(m_options.cacheRoot), [](SeekPreviewCache *cache) {
            QThreadPool::globalInstance()->start([cache] { delete cache; });
        } };
        if (!m_cache->locked) {
            fail("cache");
            return;
        }
    }
    auto *process = new QProcess(this);
    m_process = process;
    process->setProgram(m_options.helperProgram);
    process->setArguments({ m_cache->directory.path() });
    process->setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    m_processJob = createProcessJob();
    if (!m_processJob) {
        fail("job-creation");
        return;
    }
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
    });
#elif defined(Q_OS_UNIX)
    process->setChildProcessModifier([parent = ::getpid()] {
        (void) ::setpriority(PRIO_PROCESS, 0, 10);
#ifdef Q_OS_LINUX
        if (::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || ::getppid() != parent) ::_exit(1);
#endif
    });
#endif
    connect(process, &QProcess::started, this, [this, process] {
#ifdef Q_OS_WIN
        if (!assignProcessJob(process, m_processJob.get())) {
            fail("job-assignment");
            return;
        }
#endif
        QJsonArray skip;
        for (auto it = m_frames.cbegin(); it != m_frames.cend(); ++it) skip.append(it.key());
        for (const auto index : m_skippedFrames) skip.append(index);
        const QJsonObject input {
            { QStringLiteral("url"), m_descriptor.stream.url.toString(QUrl::FullyEncoded) },
            { QStringLiteral("headers"), QJsonObject::fromVariantMap(m_descriptor.stream.headers) },
            { QStringLiteral("durationMs"), m_descriptor.durationMs },
            { QStringLiteral("toneMap"), m_toneMap }, { QStringLiteral("skip"), skip }
        };
        const QByteArray bytes = QJsonDocument(input).toJson(QJsonDocument::Compact);
        if (bytes.size() > Protocol::maxRequestBytes || process->write(bytes) != bytes.size()) {
            fail("request");
            return;
        }
        process->closeWriteChannel();
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        fail("process", static_cast<int>(error));
    });
    connect(process, &QProcess::readyReadStandardOutput, this, &LocalSeekPreview::readProgress);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process](int code, QProcess::ExitStatus status) {
        readProgress();
        if (m_process != process) return;
        m_deadline.stop();
        const qint64 count = (m_descriptor.durationMs - 1) / intervalMs + 1;
        if (code != Protocol::Success || status != QProcess::NormalExit
            || m_frames.size() + m_skippedFrames.size() != count) {
            fail("helper-exit", code, code == Protocol::FrameTimeout || code == Protocol::Frame);
            return;
        }
        m_process = nullptr;
        m_processJob.reset();
        m_complete = true;
        m_available = !m_frames.isEmpty();
        process->deleteLater();
        if (!m_available) emit unavailable();
        emit finished(m_frames.size() == count);
    });
    m_deadline.start(qMax(1, m_options.timeoutMs));
    process->start();
}

void LocalSeekPreview::readProgress()
{
    if (!m_process) return;
    m_progress += m_process->readAllStandardOutput();
    if (m_progress.size() > Protocol::maxRequestBytes) {
        fail("progress-size");
        return;
    }
    while (m_progress.contains('\n')) {
        const auto newline = m_progress.indexOf('\n');
        const QByteArray line = m_progress.left(newline);
        m_progress.remove(0, newline + 1);
        const auto fields = line.split(' ');
        bool ok = false;
        const qint64 index = fields.size() == 2 ? fields.at(1).toLongLong(&ok) : -1;
        const qint64 count = (m_descriptor.durationMs - 1) / intervalMs + 1;
        if (!ok || index < 0 || index >= count || m_frames.contains(index) || m_skippedFrames.contains(index)) {
            fail("progress-index");
            return;
        }
        if (fields.first() == "begin" && m_runningFrame == -1) {
            m_runningFrame = index;
            m_deadline.start(qMax(1, m_options.timeoutMs));
            continue;
        }
        if (fields.first() != "ready" || m_runningFrame != index) {
            fail("progress-stage");
            return;
        }
        const QString path = m_cache->directory.filePath(QStringLiteral("frame-%1.jpg").arg(index));
        const QFileInfo file(path);
        bool valid = false;
        {
            QImageReader reader(path, "jpeg");
            const QSize size = reader.size();
            valid = file.isFile() && !file.isSymLink() && file.size() <= Protocol::maxImageBytes
                && size.width() > 0 && size.height() > 0 && size.width() <= 320 && size.height() <= 180
                && !reader.read().isNull() && m_totalImageBytes + file.size() <= Protocol::maxTotalImageBytes;
        }
        if (!valid) {
            fail("image-validation");
            return;
        }
        m_runningFrame = -1;
        m_totalImageBytes += file.size();
        m_frames.insert(index, QUrl::fromLocalFile(path).toString());
        m_deadline.start(qMax(1, m_options.timeoutMs));
        emit framesChanged();
    }
}

void LocalSeekPreview::fail(const char *stage, int detail, bool skipFrame)
{
    qCWarning(lcPlaybackTrickplay) << "Local seek preview generation stopped at" << stage
        << "detail" << detail << "cached frames" << m_frames.size();
    const bool retry = skipFrame && m_runningFrame >= 0 && ++m_failures < 3;
    if (retry) m_skippedFrames.insert(m_runningFrame);
    m_complete = !retry;
    m_available = retry || !m_frames.isEmpty();
    cancelWork(retry);
    if (!m_available) emit unavailable();
    if (!retry) emit finished(false);
}
