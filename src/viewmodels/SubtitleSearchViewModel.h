#pragma once

#include "BaseViewModel.h"
#include "models/SubtitleSearch.h"
#include "providers/IPlaybackProvider.h"

#include <QHash>
#include <QPointer>
#include <QVariantList>

class ConfigManager;
class PlaybackService;
class PlayerController;

/**
 * @brief Online subtitle search for the playing file (docs/viewmodels.md).
 *
 * The playback settings pop-out's "Search subtitles…" page binds to it. It
 * reads whether the live connection offers the search, searches in one
 * language at a time, and loads a chosen result into mpv through
 * PlayerController, which selects it and saves the pick. Rows are the
 * results, best first (SubtitleSearchResult maps, role `result`).
 *
 * Every request carries a generation: a new search, a language change, a
 * new item, a stopped playback or cancel() starts a new one, and answers of
 * older ones are dropped.
 */
class SubtitleSearchViewModel : public BaseViewModel
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(int count READ count NOTIFY resultsChanged)
    Q_PROPERTY(bool searched READ searched NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(QString languageLabel READ languageLabel NOTIFY languageChanged)
    Q_PROPERTY(QString downloadingId READ downloadingId NOTIFY downloadingChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged)

public:
    enum Roles { ResultRole = Qt::UserRole + 1 };

    explicit SubtitleSearchViewModel(PlaybackService *playbackService,
                                     PlayerController *playerController,
                                     ConfigManager *configManager,
                                     QObject *parent = nullptr);
    ~SubtitleSearchViewModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const { return m_available; }
    QVariantList results() const;
    int count() const { return static_cast<int>(m_results.size()); }
    bool searched() const { return m_searched; }
    QVariantList languages() const { return m_languages; }
    QString language() const { return m_language; }
    QString languageLabel() const;
    QString downloadingId() const { return m_downloadingId; }
    bool downloading() const { return !m_downloadingId.isEmpty(); }

    /// The page opened: searches the playing file in the current language
    /// unless results for it are already shown.
    Q_INVOKABLE void open();
    /// Searches again in the current language.
    Q_INVOKABLE void search();
    void reload() override { search(); }
    Q_INVOKABLE void setLanguage(const QString &language);
    /// Moves through the language list by step (Left/Right) and searches.
    Q_INVOKABLE void cycleLanguage(int step);
    /// Downloads the result, loads it into mpv, selects it and saves the pick.
    Q_INVOKABLE void download(const QString &resultId);
    /// Drops every request in flight.
    Q_INVOKABLE void cancel();

    /// The language the search starts in: the subtitle language setting,
    /// else the audio language setting, else English.
    static QString defaultLanguage(const QString &subtitleSelection,
                                   const QString &audioSelection,
                                   const QVariantList &languages);

signals:
    void availableChanged();
    void resultsChanged();
    void languageChanged();
    void downloadingChanged();
    /// The chosen subtitle is playing.
    void subtitleLoaded(const QString &title);
    void downloadFailed(const QString &message);

private:
    void onPlaybackChanged();
    void onStatusLoaded(const QString &requestKey, bool available);
    void onResultsLoaded(const QString &requestKey, const QVariantList &results);
    void onDownloaded(const QString &requestKey, const QVariantMap &subtitle);
    void onFailed(const QString &requestKey, const QString &error);

    bool playbackActive() const;
    QString playbackKey() const;
    SubtitleSearchTarget currentTarget() const;
    QString nextRequestKey(const QString &kind);
    void resetForItem();
    void setAvailable(bool available);
    void setResults(const QList<Bloom::SubtitleSearchResult> &results, bool searched);
    void setDownloadingId(const QString &resultId);
    void cancelRequests();

    QPointer<PlaybackService> m_playbackService;
    QPointer<PlayerController> m_playerController;
    QPointer<ConfigManager> m_configManager;

    QVariantList m_languages;
    QString m_language;
    bool m_languageChosen = false;

    // Generation shared by every request; each request key embeds it.
    quint64 m_generation = 0;
    quint64 m_requestSerial = 0;
    QString m_statusKey;
    QString m_searchKey;
    QString m_downloadKey;

    // The playing item the state belongs to (connection, item, version,
    // session); results and downloads of another are dropped.
    QString m_playbackKey;
    // Status answers per connection: the document is read once.
    QHash<QString, bool> m_statusByConnection;
    QString m_statusConnectionId;
    bool m_available = false;

    QList<Bloom::SubtitleSearchResult> m_results;
    bool m_searched = false;
    QString m_resultsPlaybackKey;
    QString m_resultsLanguage;
    QString m_downloadingId;
    QString m_downloadPlaybackKey;
};
