#pragma once

#include "providers/ICatalogProvider.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

/**
 * @brief Wire boundary for Silo's server-side media requests (`/api/v2/requests`).
 *
 * Titles become Bloom discovery maps, the shape Seerr results share:
 * `source: "request"`, `requestBackend: "silo"`, `requestMediaType` ("movie"
 * or "tv"), `requestTmdbId`, `requestStatus`, `requestable`, the
 * `libraryItemId` of a title already in the library, and the camelCase
 * presentation keys (`name`, `mediaType`, `productionYear`, `imageUrl`,
 * `backdropUrl`, `overview`) that cards read. See docs/seerr.md.
 */
class SiloMediaRequestMapper
{
public:
    /// The six built-in row keys `GET /requests/discover/{section}` accepts.
    static bool isBuiltInDiscoverRowKey(const QString &key);
    /// A title row key `GET /requests/discover/rows/{row_id}` accepts: any
    /// row of an admin's layout, not a brand row.
    static bool isDiscoverRowKey(const QString &key);
    /// "movie" stays "movie"; Bloom's "tv" is Silo's "series". Empty when
    /// the type is neither.
    static QString wireMediaType(const QString &requestMediaType);
    /// The list route for a browse kind: "genre" reads "genres", "studio"
    /// "studios", "network" "networks". Empty for anything else.
    static QString browseListName(const QString &browseKind);
    /// Sort orders the browse routes accept.
    static bool isBrowseSort(const QString &sort);

    /// `POST /requests` body for a title, or an empty object when the type,
    /// TMDB id, or title is missing. Empty @p seasons lets Silo request every
    /// aired season the library lacks.
    static QJsonObject createBody(const QString &requestMediaType,
                                  qint64 tmdbId,
                                  const QVariantMap &media,
                                  const QList<int> &seasons);

    /// Parses a request operation's body into `capabilityMetadata`:
    /// - MediaRequestStatus: `available`, `requestsEnabled`, `allowed`,
    ///   `state`, `seasonRequestsSupported`, `followSupported`,
    ///   `watchlistTitlesSupported`, `watchlistRequests`,
    ///   `discoverRowsSupported`
    /// - MediaRequestSearch: `items`, `page`, `totalPages`
    /// - MediaRequestDetail: `detail` (a discovery map with `seasons`,
    ///   `seasonCount`, `notRequestableReason`, `requestState`,
    ///   `requestedByViewer`, and `recommendations`)
    /// - CreateMediaRequest: `request` (`requestId`, `requestState`)
    /// - MediaRequestDiscover: `rows` (`key`, `title`, `items`, `page`,
    ///   `nextPage`, `hasMore`) in response order, and, when the response
    ///   has `row_order`, `layoutRows` (see layoutRows())
    /// - MediaRequestDiscoverSection: `row`
    /// - MediaRequestList: `items` (request maps), `hasMore`, `nextCursor`
    /// - CancelMediaRequest: `request` (a request map)
    /// - SetMediaRequestFollow, SetWatchlistTitle: nothing; success only
    /// - MediaRequestBrowseCategories: `items` (browse categories)
    /// - MediaRequestBrowse: `row` (`items`, `page`, `nextPage`, `hasMore`)
    static ProviderCatalogResponse parseResponse(ProviderCatalogOperation operation,
                                                 const QByteArray &body);

    static QVariantMap features(const QJsonObject &wireStatus);
    static QVariantMap result(const QJsonObject &wireResult);
    static QVariantList results(const QJsonArray &wireResults);
    static QVariantMap detail(const QJsonObject &wireDetail);
    static QVariantMap createdRequest(const QJsonObject &wireRequest);
    /// One title row. @p fromLayout marks a row read from an admin layout's
    /// Discover page, where a row trimmed to its limit continues at page 1.
    static QVariantMap discoverRow(const QJsonObject &wireSection, bool fromLayout = false);
    /// The Discover page as an admin laid it out: @p wireRowOrder's rows in
    /// order, under their layout titles. A title row is a discoverRow() from
    /// @p wireSections, left out when it has no titles. A brand row is
    /// `{key, title, browseKind}`, where `browseKind` ("genre", "studio",
    /// or "network") names the browse categories that fill it. Row types
    /// Bloom does not know are skipped.
    static QVariantList layoutRows(const QJsonArray &wireRowOrder, const QJsonArray &wireSections);
    /// One of the viewer's requests: the discovery map for its title plus
    /// `requestId`, `requestState`, `requestedSeasons`, `createdAt`, and
    /// `canCancel` (an active request not yet being downloaded).
    static QVariantMap request(const QJsonObject &wireRequest);
    /// A genre, studio, or network: `id` (its slug), `name`, `logoUrl`, and
    /// `seriesSupported`. The list it came from gives its kind.
    static QVariantMap browseCategory(const QJsonObject &wireBrand);
};
