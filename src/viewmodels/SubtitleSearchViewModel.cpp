#include "SubtitleSearchViewModel.h"

#include "network/PlaybackService.h"
#include "player/PlayerController.h"
#include "utils/ConfigManager.h"

#include <QStringList>
#include <QTimer>

#include <algorithm>

SubtitleSearchViewModel::SubtitleSearchViewModel(PlaybackService *playbackService,
                                                 PlayerController *playerController,
                                                 ConfigManager *configManager,
                                                 QObject *parent)
    : BaseViewModel(parent)
    , m_playbackService(playbackService)
    , m_playerController(playerController)
    , m_configManager(configManager)
{
    if (m_configManager) {
        m_languages = m_configManager->getSupportedTrackLanguages();
    }
    m_language = defaultLanguage(QString(), QString(), m_languages);

    m_languageSearchTimer = new QTimer(this);
    m_languageSearchTimer->setSingleShot(true);
    m_languageSearchTimer->setInterval(kLanguageSearchDelayMs);
    connect(m_languageSearchTimer, &QTimer::timeout, this, &SubtitleSearchViewModel::search);

    if (m_playbackService) {
        connect(m_playbackService, &PlaybackService::subtitleSearchStatusLoaded,
                this, &SubtitleSearchViewModel::onStatusLoaded);
        connect(m_playbackService, &PlaybackService::subtitleSearchResultsLoaded,
                this, &SubtitleSearchViewModel::onResultsLoaded);
        connect(m_playbackService, &PlaybackService::subtitleDownloaded,
                this, &SubtitleSearchViewModel::onDownloaded);
        connect(m_playbackService, &PlaybackService::subtitleSearchFailed,
                this, &SubtitleSearchViewModel::onFailed);
        connect(m_playbackService, &PlaybackService::subtitleSearchScopeChanged,
                this, &SubtitleSearchViewModel::onScopeChanged);
    }
    if (m_playerController) {
        for (const auto signal : {&PlayerController::isPlaybackActiveChanged,
                                  &PlayerController::currentItemIdChanged,
                                  &PlayerController::mediaSourceIdChanged,
                                  &PlayerController::playSessionIdChanged}) {
            connect(m_playerController, signal, this, &SubtitleSearchViewModel::onPlaybackChanged);
        }
        connect(m_playerController, &PlayerController::downloadedSubtitleLoaded,
                this, &SubtitleSearchViewModel::onTrackLoaded);
        connect(m_playerController, &PlayerController::downloadedSubtitleLoadFailed,
                this, &SubtitleSearchViewModel::onTrackLoadFailed);
    }
}

SubtitleSearchViewModel::~SubtitleSearchViewModel()
{
    cancelRequests();
}

int SubtitleSearchViewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_results.size());
}

QVariant SubtitleSearchViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()
        || role != ResultRole) {
        return {};
    }
    return m_results.at(index.row()).toVariantMap();
}

QHash<int, QByteArray> SubtitleSearchViewModel::roleNames() const
{
    return {{ResultRole, QByteArrayLiteral("result")}};
}

QVariantList SubtitleSearchViewModel::results() const
{
    QVariantList list;
    list.reserve(m_results.size());
    for (const Bloom::SubtitleSearchResult &result : m_results) {
        list.append(result.toVariantMap());
    }
    return list;
}

QString SubtitleSearchViewModel::languageLabel() const
{
    for (const QVariant &entry : m_languages) {
        const QVariantMap option = entry.toMap();
        if (option.value(QStringLiteral("value")).toString() == m_language) {
            return option.value(QStringLiteral("label")).toString();
        }
    }
    return m_language;
}

QString SubtitleSearchViewModel::defaultLanguage(const QString &subtitleSelection,
                                                 const QString &audioSelection,
                                                 const QVariantList &languages)
{
    const auto listed = [&languages](const QString &code) {
        for (const QVariant &entry : languages) {
            if (entry.toMap().value(QStringLiteral("value")).toString() == code) {
                return true;
            }
        }
        return false;
    };
    for (const QString &selection : {subtitleSelection, audioSelection}) {
        const QString code = selection.trimmed().toLower();
        if (!code.isEmpty() && listed(code)) {
            return code;
        }
    }
    return QStringLiteral("eng");
}

void SubtitleSearchViewModel::open()
{
    if (!m_available) {
        return;
    }
    if (!m_languageChosen && m_configManager) {
        const QString preferred = defaultLanguage(m_configManager->getDefaultSubtitleTrackSelection(),
                                                  m_configManager->getDefaultAudioTrackSelection(),
                                                  m_languages);
        if (preferred != m_language) {
            m_language = preferred;
            emit languageChanged();
        }
    }
    const bool current = m_resultsPlaybackKey == m_playbackKey && m_resultsLanguage == m_language;
    if (current && (m_searched || isLoading()) && !hasError()) {
        return;
    }
    search();
}

void SubtitleSearchViewModel::search()
{
    if (!m_available || !m_playbackService || m_playbackKey.isEmpty()) {
        return;
    }
    m_languageSearchTimer->stop();
    if (!m_searchKey.isEmpty()) {
        m_playbackService->cancelSubtitleSearch(m_searchKey);
    }
    ++m_generation;
    m_searchKey = nextRequestKey(QStringLiteral("search"));
    m_resultsPlaybackKey = m_playbackKey;
    m_resultsLanguage = m_language;
    clearError();
    setResults({}, false);
    setLoading(true);
    const QString key = m_searchKey;
    m_playbackService->searchSubtitles(key, currentTarget(), m_language);
}

void SubtitleSearchViewModel::setLanguage(const QString &language)
{
    const QString code = language.trimmed().toLower();
    if (code.isEmpty()) {
        return;
    }
    m_languageChosen = true;
    if (code == m_language) {
        return;
    }
    m_language = code;
    emit languageChanged();
    search();
}

void SubtitleSearchViewModel::cycleLanguage(int step)
{
    if (m_languages.isEmpty() || step == 0) {
        return;
    }
    int position = 0;
    for (int i = 0; i < m_languages.size(); ++i) {
        if (m_languages.at(i).toMap().value(QStringLiteral("value")).toString() == m_language) {
            position = i;
            break;
        }
    }
    const int size = static_cast<int>(m_languages.size());
    const int next = ((position + step) % size + size) % size;
    m_languageChosen = true;
    m_language = m_languages.at(next).toMap().value(QStringLiteral("value")).toString();
    emit languageChanged();
    if (!m_available || !m_playbackService || m_playbackKey.isEmpty()) {
        return;
    }
    // Stepping through several languages searches only the one it stops
    // on; the page shows it loading meanwhile.
    if (!m_searchKey.isEmpty()) {
        m_playbackService->cancelSubtitleSearch(m_searchKey);
        m_searchKey.clear();
    }
    m_resultsPlaybackKey = m_playbackKey;
    m_resultsLanguage = m_language;
    clearError();
    setResults({}, false);
    setLoading(true);
    m_languageSearchTimer->start();
}

void SubtitleSearchViewModel::download(const QString &resultId)
{
    if (!m_available || !m_playbackService || downloading() || m_playbackKey.isEmpty()) {
        return;
    }
    const auto found = std::find_if(m_results.cbegin(), m_results.cend(),
                                    [&resultId](const Bloom::SubtitleSearchResult &result) {
                                        return result.id == resultId;
                                    });
    if (found == m_results.cend() || m_resultsPlaybackKey != m_playbackKey) {
        return;
    }
    ++m_generation;
    m_downloadKey = nextRequestKey(QStringLiteral("download"));
    m_downloadPlaybackKey = m_playbackKey;
    m_selectionAtDownload = m_playerController ? m_playerController->selectedSubtitleTrack() : -1;
    m_loadingTrack = false;
    setDownloadingId(resultId);
    const QString key = m_downloadKey;
    m_playbackService->downloadSubtitle(key, currentTarget(), *found);
}

void SubtitleSearchViewModel::cancel()
{
    cancelRequests();
    setLoading(false);
    setDownloadingId(QString());
}

void SubtitleSearchViewModel::cancelRequests()
{
    ++m_generation;
    m_languageSearchTimer->stop();
    m_loadingTrack = false;
    if (m_playbackService) {
        if (!m_searchKey.isEmpty()) {
            m_playbackService->cancelSubtitleSearch(m_searchKey);
        }
        if (!m_downloadKey.isEmpty()) {
            m_playbackService->cancelSubtitleSearch(m_downloadKey);
        }
    }
    m_searchKey.clear();
    m_downloadKey.clear();
}

bool SubtitleSearchViewModel::playbackActive() const
{
    return m_playerController && m_playerController->isPlaybackActive()
        && !m_playerController->currentItemId().isEmpty();
}

QString SubtitleSearchViewModel::playbackKey() const
{
    if (!playbackActive() || !m_playbackService) {
        return {};
    }
    return QStringList{m_playbackService->subtitleSearchConnectionId(),
                       m_playerController->currentItemId(),
                       m_playerController->mediaSourceId(),
                       m_playerController->playSessionId()}
        .join(QLatin1Char('\n'));
}

SubtitleSearchTarget SubtitleSearchViewModel::currentTarget() const
{
    SubtitleSearchTarget target;
    if (!m_playerController || !m_playbackService) {
        return target;
    }
    target.media.connectionId = m_playbackService->subtitleSearchConnectionId();
    target.media.itemId = m_playerController->currentItemId();
    target.mediaVersionId = m_playerController->mediaSourceId();
    target.playbackSessionId = m_playerController->playSessionId();
    target.knownSubtitleStreams = m_playerController->currentSubtitleStreams();
    return target;
}

QString SubtitleSearchViewModel::nextRequestKey(const QString &kind)
{
    // Unique across instances, so two models never share an answer.
    return QStringLiteral("subtitle-search:%1:%2:%3:%4")
        .arg(QString::number(reinterpret_cast<quintptr>(this), 16), kind,
             QString::number(m_generation), QString::number(++m_requestSerial));
}

void SubtitleSearchViewModel::onPlaybackChanged()
{
    const QString key = playbackKey();
    if (key == m_playbackKey) {
        return;
    }
    // The same item on a new stream or version: a download in flight is
    // dropped (its URL belongs to the old session), so say so.
    const bool sameItem = !key.isEmpty() && !m_playbackKey.isEmpty()
        && key.section(QLatin1Char('\n'), 0, 1) == m_playbackKey.section(QLatin1Char('\n'), 0, 1);
    const bool interruptedDownload = sameItem && downloading();
    m_playbackKey = key;
    resetForItem();
    if (interruptedDownload) {
        emit downloadFailed(tr("The stream changed before the subtitle was loaded. Choose it again."));
    }
    refreshStatus();
}

void SubtitleSearchViewModel::refreshStatus()
{
    if (m_playbackKey.isEmpty() || !m_playbackService) {
        m_availableScope.clear();
        setAvailable(false);
        return;
    }
    const QString scope = m_playbackService->subtitleSearchScope();
    const auto known = m_statusByScope.constFind(scope);
    if (known != m_statusByScope.cend()) {
        m_availableScope = scope;
        setAvailable(*known);
        return;
    }
    if (m_availableScope != scope) {
        // Another profile or connection: hidden until its own answer. The
        // same one read again keeps its entry meanwhile.
        m_availableScope.clear();
        setAvailable(false);
    }
    if (!m_statusKey.isEmpty() && m_statusScope == scope) {
        return; // the read in flight answers for this connection and profile
    }
    if (!m_statusKey.isEmpty()) {
        m_playbackService->cancelSubtitleSearch(m_statusKey);
    }
    m_statusKey = nextRequestKey(QStringLiteral("status"));
    m_statusScope = scope;
    const QString statusKey = m_statusKey;
    m_playbackService->loadSubtitleSearchStatus(statusKey);
}

void SubtitleSearchViewModel::onScopeChanged()
{
    m_statusByScope.clear();
    if (!m_statusKey.isEmpty() && m_playbackService) {
        m_playbackService->cancelSubtitleSearch(m_statusKey);
    }
    m_statusKey.clear();
    m_playbackKey = playbackKey();
    refreshStatus();
}

void SubtitleSearchViewModel::resetForItem()
{
    cancelRequests();
    setLoading(false);
    clearError();
    m_resultsPlaybackKey.clear();
    m_resultsLanguage.clear();
    m_downloadPlaybackKey.clear();
    setDownloadingId(QString());
    setResults({}, false);
}

void SubtitleSearchViewModel::onStatusLoaded(const QString &requestKey, bool available)
{
    if (requestKey != m_statusKey) {
        return;
    }
    m_statusKey.clear();
    m_statusByScope.insert(m_statusScope, available);
    if (!m_playbackKey.isEmpty() && m_playbackService
        && m_playbackService->subtitleSearchScope() == m_statusScope) {
        m_availableScope = m_statusScope;
        setAvailable(available);
    }
}

void SubtitleSearchViewModel::onResultsLoaded(const QString &requestKey, const QVariantList &results,
                                              const QStringList &warnings)
{
    if (requestKey.isEmpty() || requestKey != m_searchKey) {
        return; // an older search, language or item
    }
    m_searchKey.clear();
    QList<Bloom::SubtitleSearchResult> parsed;
    parsed.reserve(results.size());
    for (const QVariant &entry : results) {
        const Bloom::SubtitleSearchResult result =
            Bloom::SubtitleSearchResult::fromVariantMap(entry.toMap());
        if (result.isValid()) {
            parsed.append(result);
        }
    }
    setLoading(false);
    setResults(Bloom::sortSubtitleSearchResults(parsed), true, warnings);
}

void SubtitleSearchViewModel::onDownloaded(const QString &requestKey, const QVariantMap &subtitle)
{
    if (requestKey.isEmpty() || requestKey != m_downloadKey) {
        return;
    }
    m_downloadKey.clear();
    const QString resultId = m_downloadingId;
    if (m_downloadPlaybackKey != m_playbackKey || !m_playerController) {
        setDownloadingId(QString());
        return; // playback moved on while the server downloaded it
    }
    const Bloom::DownloadedSubtitle downloaded = Bloom::DownloadedSubtitle::fromVariantMap(subtitle);
    QString title = downloaded.title.trimmed();
    if (title.isEmpty()) {
        for (const Bloom::SubtitleSearchResult &result : std::as_const(m_results)) {
            if (result.id == resultId) {
                title = result.title.isEmpty() ? result.provider : result.title;
                break;
            }
        }
    }
    if (title.isEmpty()) {
        title = tr("Downloaded subtitle");
    }
    // A subtitle the viewer chose while this downloaded stays: the download
    // is only listed, not selected or saved.
    const bool select = m_playerController->selectedSubtitleTrack() == m_selectionAtDownload;
    Bloom::DownloadedSubtitle track = downloaded;
    track.title = title;
    const int index = downloaded.isValid()
        ? m_playerController->addDownloadedSubtitleTrack(track.toVariantMap(), select)
        : PlayerController::kNoSubtitleTrackIndex;
    if (index == PlayerController::kNoSubtitleTrackIndex) {
        setDownloadingId(QString());
        emit downloadFailed(tr("The subtitle was downloaded but could not be loaded."));
        return;
    }
    if (!select) {
        setDownloadingId(QString());
        emit subtitleListed(title);
        return;
    }
    // Announced once mpv has the track; the row shows it loading until then.
    m_loadingTrack = true;
    m_loadingTrackIndex = index;
    m_loadingTitle = title;
}

void SubtitleSearchViewModel::onTrackLoaded(int index)
{
    if (!m_loadingTrack || index != m_loadingTrackIndex) {
        return;
    }
    m_loadingTrack = false;
    setDownloadingId(QString());
    emit subtitleLoaded(m_loadingTitle);
}

void SubtitleSearchViewModel::onTrackLoadFailed(int index, const QString &message)
{
    if (!m_loadingTrack || index != m_loadingTrackIndex) {
        return;
    }
    m_loadingTrack = false;
    setDownloadingId(QString());
    if (!message.isEmpty()) {
        emit downloadFailed(message); // no message: the viewer picked another subtitle
    }
}

void SubtitleSearchViewModel::onFailed(const QString &requestKey, const QString &error)
{
    if (requestKey.isEmpty()) {
        return;
    }
    if (requestKey == m_statusKey) {
        // Not remembered: the next item asks again.
        m_statusKey.clear();
        if (m_availableScope == m_statusScope) {
            m_availableScope.clear();
            setAvailable(false);
        }
        return;
    }
    if (requestKey == m_searchKey) {
        m_searchKey.clear();
        setLoading(false);
        m_searched = true;
        setError(error);
        emit resultsChanged();
        return;
    }
    if (requestKey == m_downloadKey) {
        m_downloadKey.clear();
        setDownloadingId(QString());
        emit downloadFailed(error);
    }
}

void SubtitleSearchViewModel::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    emit availableChanged();
}

void SubtitleSearchViewModel::setResults(const QList<Bloom::SubtitleSearchResult> &results,
                                         bool searched, const QStringList &warnings)
{
    if (results == m_results && searched == m_searched && warnings == m_warnings) {
        return;
    }
    emitModelReset([this, &results, searched, &warnings]() {
        m_results = results;
        m_searched = searched;
        m_warnings = warnings;
    });
    emit resultsChanged();
}

void SubtitleSearchViewModel::setDownloadingId(const QString &resultId)
{
    if (m_downloadingId == resultId) {
        return;
    }
    m_downloadingId = resultId;
    emit downloadingChanged();
}
