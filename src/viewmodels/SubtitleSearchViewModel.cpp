#include "SubtitleSearchViewModel.h"

#include "network/PlaybackService.h"
#include "player/PlayerController.h"
#include "utils/ConfigManager.h"

#include <QStringList>

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

    if (m_playbackService) {
        connect(m_playbackService, &PlaybackService::subtitleSearchStatusLoaded,
                this, &SubtitleSearchViewModel::onStatusLoaded);
        connect(m_playbackService, &PlaybackService::subtitleSearchResultsLoaded,
                this, &SubtitleSearchViewModel::onResultsLoaded);
        connect(m_playbackService, &PlaybackService::subtitleDownloaded,
                this, &SubtitleSearchViewModel::onDownloaded);
        connect(m_playbackService, &PlaybackService::subtitleSearchFailed,
                this, &SubtitleSearchViewModel::onFailed);
    }
    if (m_playerController) {
        for (const auto signal : {&PlayerController::isPlaybackActiveChanged,
                                  &PlayerController::currentItemIdChanged,
                                  &PlayerController::mediaSourceIdChanged,
                                  &PlayerController::playSessionIdChanged}) {
            connect(m_playerController, signal, this, &SubtitleSearchViewModel::onPlaybackChanged);
        }
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
    setLanguage(m_languages.at(next).toMap().value(QStringLiteral("value")).toString());
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
    for (const QVariant &track : m_playerController->availableSubtitleTracks()) {
        const int index = track.toMap().value(QStringLiteral("index"), -1).toInt();
        if (index >= 0) {
            target.knownSubtitleIndexes.append(index);
        }
    }
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
    m_playbackKey = key;
    resetForItem();
    if (key.isEmpty() || !m_playbackService) {
        setAvailable(false);
        return;
    }
    const QString connectionId = m_playbackService->subtitleSearchConnectionId();
    const auto known = m_statusByConnection.constFind(connectionId);
    if (known != m_statusByConnection.cend()) {
        setAvailable(*known);
        return;
    }
    setAvailable(false);
    if (!m_statusKey.isEmpty() && m_statusConnectionId == connectionId) {
        return; // the read in flight answers for this connection
    }
    if (!m_statusKey.isEmpty()) {
        m_playbackService->cancelSubtitleSearch(m_statusKey);
    }
    m_statusKey = nextRequestKey(QStringLiteral("status"));
    m_statusConnectionId = connectionId;
    const QString statusKey = m_statusKey;
    m_playbackService->loadSubtitleSearchStatus(statusKey);
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
    m_statusByConnection.insert(m_statusConnectionId, available);
    if (!m_playbackKey.isEmpty() && m_playbackService
        && m_playbackService->subtitleSearchConnectionId() == m_statusConnectionId) {
        setAvailable(available);
    }
}

void SubtitleSearchViewModel::onResultsLoaded(const QString &requestKey, const QVariantList &results)
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
    setResults(Bloom::sortSubtitleSearchResults(parsed), true);
}

void SubtitleSearchViewModel::onDownloaded(const QString &requestKey, const QVariantMap &subtitle)
{
    if (requestKey.isEmpty() || requestKey != m_downloadKey) {
        return;
    }
    m_downloadKey.clear();
    const QString resultId = m_downloadingId;
    setDownloadingId(QString());
    if (m_downloadPlaybackKey != m_playbackKey || !m_playerController) {
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
    if (!downloaded.isValid()
        || !m_playerController->addDownloadedSubtitleTrack(
            downloaded.url.toString(QUrl::FullyEncoded), title, downloaded.language,
            downloaded.sourceIndex)) {
        emit downloadFailed(tr("The subtitle was downloaded but could not be loaded."));
        return;
    }
    emit subtitleLoaded(title);
}

void SubtitleSearchViewModel::onFailed(const QString &requestKey, const QString &error)
{
    if (requestKey.isEmpty()) {
        return;
    }
    if (requestKey == m_statusKey) {
        // Not remembered: the next item asks again.
        m_statusKey.clear();
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
                                         bool searched)
{
    if (results == m_results && searched == m_searched) {
        return;
    }
    emitModelReset([this, &results, searched]() {
        m_results = results;
        m_searched = searched;
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
