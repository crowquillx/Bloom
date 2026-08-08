#include <QCoreApplication>
#include <QEventLoop>
#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <cstring>

#include "network/AuthenticationService.h"
#include "network/HttpTransport.h"
#include "network/LibraryService.h"
#include "providers/ICatalogProvider.h"
#include "providers/silo/SiloCatalogProvider.h"
#include "providers/silo/SiloProviderAdapter.h"
#include "utils/ConfigManager.h"

#include "TestConfigIsolation.h"

namespace {

class ControlledReply final : public QNetworkReply
{
public:
    ControlledReply(const QNetworkRequest &request,
                    int statusCode,
                    QByteArray payload,
                    bool delayed,
                    QObject *parent)
        : QNetworkReply(parent)
        , m_statusCode(statusCode)
        , m_payload(std::move(payload))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        if (statusCode >= 400) {
            setError(statusCode == 401
                         ? QNetworkReply::AuthenticationRequiredError
                         : QNetworkReply::UnknownServerError,
                     QStringLiteral("test network error"));
        }
        open(QIODevice::ReadOnly);
        if (!delayed) {
            QTimer::singleShot(0, this, [this]() { complete(); });
        }
    }

    void abort() override
    {
        m_aborted = true;
    }

    qint64 bytesAvailable() const override
    {
        return (m_payload.size() - m_offset) + QNetworkReply::bytesAvailable();
    }

    void complete()
    {
        if (isFinished()) {
            return;
        }
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_statusCode);
        setFinished(true);
        if (!m_payload.isEmpty()) {
            emit readyRead();
        }
        emit finished();
    }

    bool wasAborted() const { return m_aborted; }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 remaining = m_payload.size() - m_offset;
        const qint64 count = qMin(maxSize, remaining);
        if (count <= 0) {
            return -1;
        }
        memcpy(data, m_payload.constData() + m_offset,
               static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

private:
    int m_statusCode;
    QByteArray m_payload;
    qint64 m_offset = 0;
    bool m_aborted = false;
};

struct ResponsePlan
{
    int statusCode;
    QByteArray payload;
    bool delayed = false;
};

struct RecordedRequest
{
    QNetworkAccessManager::Operation operation;
    QNetworkRequest request;
    QByteArray body;
};

class ControlledNetworkAccessManager final : public QNetworkAccessManager
{
public:
    QList<ResponsePlan> responses;
    QList<RecordedRequest> requests;
    QList<QPointer<ControlledReply>> replies;

protected:
    QNetworkReply *createRequest(Operation operation,
                                 const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        requests.append(
            {operation, request, outgoingData ? outgoingData->readAll() : QByteArray()});
        const ResponsePlan plan = responses.isEmpty()
            ? ResponsePlan{500, QByteArrayLiteral(R"({"error":"queue_empty"})"), false}
            : responses.takeFirst();
        auto *reply = new ControlledReply(
            request, plan.statusCode, plan.payload, plan.delayed, this);
        replies.append(reply);
        return reply;
    }
};

class ExposedAuthenticationService final : public AuthenticationService
{
public:
    ExposedAuthenticationService(HttpTransport *transport,
                                 IProviderAdapter *adapter)
        : AuthenticationService(nullptr, transport, adapter)
    {
    }

    void replaceAccount(const QString &accountId, const QString &accessToken)
    {
        seedSession(QStringLiteral("https://silo.example.test"),
                    accountId,
                    accessToken,
                    accountId);
    }
};

} // namespace

class SiloCatalogServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void nativeRoutesUseContentIdentityAndCorrectMethods();
    void structuredFiltersUseNativeQueryBody();
    void unsupportedFiltersAreExplicit();
    void responsePreservesSnapshotAndPaginationTruth();
    void nativeResponseShapesPreserveHomeLibrarySearchAndDetailData();
    void sparseNativeMappingsPreserveFallbacksAndState();
    void requestHeadersAndGenerationSuppressStaleReplies();
};

void SiloCatalogServiceTest::nativeRoutesUseContentIdentityAndCorrectMethods()
{
    SiloCatalogProvider provider;
    ProviderCatalogQuery query;

    ProviderCatalogRequest request = provider.createRequest(
        ProviderCatalogOperation::Views, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v1/user/libraries"));
    QVERIFY(request.body.isEmpty());

    request = provider.createRequest(ProviderCatalogOperation::Items, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v1/catalog?source=query&include_technical=true"));

    ProviderCatalogQuery paged;
    paged.snapshot = QStringLiteral("opaque/snapshot + token");
    request = provider.createRequest(ProviderCatalogOperation::Items, paged);
    QVERIFY(request.supported);
    const QUrlQuery pagedParameters(QUrl(request.relativeEndpoint));
    QCOMPARE(pagedParameters.queryItemValue(QStringLiteral("snapshot")),
             QStringLiteral("opaque/snapshot + token"));

    query.itemId = QStringLiteral("content/with space");
    request = provider.createRequest(ProviderCatalogOperation::Item, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v1/catalog/items/content%2Fwith%20space"));

    request = provider.createRequest(ProviderCatalogOperation::Versions, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Get);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v1/catalog/items/content%2Fwith%20space/versions"));

    ProviderCatalogQuery hierarchy;
    hierarchy.seriesId = QStringLiteral("series/one");
    request = provider.createRequest(ProviderCatalogOperation::Items, hierarchy);
    QVERIFY(request.supported);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v1/catalog/series/series%2Fone/seasons"));
    hierarchy.parentId = QStringLiteral("season/one");
    request = provider.createRequest(ProviderCatalogOperation::Items, hierarchy);
    QVERIFY(request.supported);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v1/catalog/items/season%2Fone/episodes"));

    ProviderCatalogQuery state;
    state.itemId = QStringLiteral("content-42");
    state.stateValue = true;
    ProviderCatalogQuery season;
    season.parentId = QStringLiteral("series/one");
    season.includeItemTypes = {QStringLiteral("Season")};
    request = provider.createRequest(ProviderCatalogOperation::Items, season);
    QVERIFY(request.supported);
    QCOMPARE(request.relativeEndpoint,
             QStringLiteral("/api/v1/catalog/series/series%2Fone/seasons"));
    request = provider.createRequest(ProviderCatalogOperation::SetWatched, state);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Post);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v1/watched/content-42"));
    request = provider.createRequest(ProviderCatalogOperation::SetFavorite, state);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Put);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v1/favorites/content-42"));

    state.stateValue = false;
    request = provider.createRequest(ProviderCatalogOperation::SetWatched, state);
    QCOMPARE(request.method, ProviderHttpMethod::Delete);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v1/watched/content-42"));
    request = provider.createRequest(ProviderCatalogOperation::SetFavorite, state);
    QCOMPARE(request.method, ProviderHttpMethod::Delete);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v1/favorites/content-42"));
    QVERIFY(provider.parseResponse(ProviderCatalogOperation::SetWatched, {}).valid);
    QVERIFY(provider.parseResponse(ProviderCatalogOperation::SetFavorite, {}).valid);
}

void SiloCatalogServiceTest::structuredFiltersUseNativeQueryBody()
{
    SiloCatalogProvider provider;
    ProviderCatalogQuery query;
    query.parentId = QStringLiteral("17");
    query.startIndex = 40;
    query.limit = 250;
    query.genres = {QStringLiteral("Science Fiction")};
    query.studios = {QStringLiteral("Example Studio")};
    query.years = {1982, 2026};
    query.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series")};
    query.watched = ProviderCatalogTriState::No;
    query.favorite = ProviderCatalogTriState::Yes;
    query.sortBy = QStringLiteral("PremiereDate");
    query.sortOrder = QStringLiteral("Descending");

    const ProviderCatalogRequest request = provider.createRequest(
        ProviderCatalogOperation::Items, query);
    QVERIFY(request.supported);
    QCOMPARE(request.method, ProviderHttpMethod::Post);
    QCOMPARE(request.relativeEndpoint, QStringLiteral("/api/v1/catalog/query"));
    QCOMPARE(request.extraHeaders.value(QByteArrayLiteral("Content-Type")),
             QByteArrayLiteral("application/json"));

    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(body.value(QStringLiteral("library_id")).toInt(), 17);
    QCOMPARE(body.value(QStringLiteral("offset")).toInt(), 40);
    QCOMPARE(body.value(QStringLiteral("limit")).toInt(), 100);
    QCOMPARE(body.value(QStringLiteral("sort")).toString(),
             QStringLiteral("release_date"));
    QCOMPARE(body.value(QStringLiteral("order")).toString(), QStringLiteral("desc"));
    QCOMPARE(body.value(QStringLiteral("match")).toString(), QStringLiteral("all"));

    const QJsonArray groups = body.value(QStringLiteral("groups")).toArray();
    QCOMPARE(groups.size(), 3);
    const QJsonObject allGroup = groups.at(0).toObject();
    QCOMPARE(allGroup.value(QStringLiteral("match")).toString(), QStringLiteral("all"));
    const QJsonArray allRules = allGroup.value(QStringLiteral("rules")).toArray();
    QCOMPARE(allRules.size(), 4);
    QCOMPARE(allRules.at(0).toObject().value(QStringLiteral("field")).toString(),
             QStringLiteral("genre"));
    QCOMPARE(allRules.at(0).toObject().value(QStringLiteral("op")).toString(),
             QStringLiteral("contains"));
    QCOMPARE(allRules.at(1).toObject().value(QStringLiteral("field")).toString(),
             QStringLiteral("studio"));
    QCOMPARE(allRules.at(2).toObject().value(QStringLiteral("field")).toString(),
             QStringLiteral("watched"));
    QCOMPARE(allRules.at(2).toObject().value(QStringLiteral("value")).toBool(), false);
    QCOMPARE(allRules.at(3).toObject().value(QStringLiteral("field")).toString(),
             QStringLiteral("favorited"));
    QCOMPARE(allRules.at(3).toObject().value(QStringLiteral("value")).toBool(), true);

    QCOMPARE(groups.at(1).toObject().value(QStringLiteral("match")).toString(),
             QStringLiteral("any"));
    QCOMPARE(groups.at(1).toObject().value(QStringLiteral("rules")).toArray().size(), 2);
    QCOMPARE(groups.at(2).toObject().value(QStringLiteral("match")).toString(),
             QStringLiteral("any"));
    QCOMPARE(groups.at(2).toObject().value(QStringLiteral("rules")).toArray().size(), 2);
    query.parentId.clear();
    const ProviderCatalogRequest unscopedRequest = provider.createRequest(
        ProviderCatalogOperation::Items, query);
    QVERIFY(unscopedRequest.supported);
    const QJsonObject unscopedBody = QJsonDocument::fromJson(unscopedRequest.body).object();
    QVERIFY(!unscopedBody.contains(QStringLiteral("library_id")));

    ProviderCatalogQuery search;
    search.parentId = QStringLiteral("17");
    search.searchTerm = QStringLiteral("Alien & Aliens");
    search.limit = 30;
    const ProviderCatalogRequest searchRequest = provider.createRequest(
        ProviderCatalogOperation::Search, search);
    QVERIFY(searchRequest.supported);
    QCOMPARE(searchRequest.method, ProviderHttpMethod::Get);
    QVERIFY(searchRequest.body.isEmpty());
    const QUrl searchUrl(searchRequest.relativeEndpoint);
    const QUrlQuery parameters(searchUrl);
    QCOMPARE(searchUrl.path(), QStringLiteral("/api/v1/catalog"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("source")), QStringLiteral("query"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("library_id")), QStringLiteral("17"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("q")), QStringLiteral("Alien & Aliens"));
    QCOMPARE(parameters.queryItemValue(QStringLiteral("limit")), QStringLiteral("30"));
}

void SiloCatalogServiceTest::unsupportedFiltersAreExplicit()
{
    SiloCatalogProvider provider;
    const auto verifyUnsupported = [&provider](ProviderCatalogOperation operation,
                                               const ProviderCatalogQuery &query) {
        const ProviderCatalogRequest request = provider.createRequest(operation, query);
        QVERIFY(!request.supported);
        QVERIFY2(!request.unsupportedReason.trimmed().isEmpty(),
                 "Unsupported provider semantics must include a reason");
        QVERIFY(request.relativeEndpoint.isEmpty());
        QVERIFY(request.body.isEmpty());
    };

    ProviderCatalogQuery query;
    query.tags = {QStringLiteral("award-winner")};
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    query.includeItemTypes = {QStringLiteral("Audio")};
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    query.minPremiereDate = QDate(2020, 1, 1);
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    query.sortBy = QStringLiteral("unsupported-sort");
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    query.itemIds = {QStringLiteral("content-1")};
    verifyUnsupported(ProviderCatalogOperation::Items, query);

    query = {};
    const ProviderCatalogRequest nextUp = provider.createRequest(
        ProviderCatalogOperation::NextUp, query);
    QVERIFY(nextUp.supported);
    QCOMPARE(nextUp.method, ProviderHttpMethod::Get);
    QCOMPARE(nextUp.relativeEndpoint,
             QStringLiteral("/api/v1/home/sections/system-next-up/items"));
    verifyUnsupported(ProviderCatalogOperation::RandomItems, query);
    verifyUnsupported(ProviderCatalogOperation::ResolveLibrary, query);
}

void SiloCatalogServiceTest::sparseNativeMappingsPreserveFallbacksAndState()
{
    const QJsonObject item{
        {QStringLiteral("content_id"), QStringLiteral("content-1")},
        {QStringLiteral("title"), QStringLiteral("Sparse")},
        {QStringLiteral("poster_url"), QString()},
        {QStringLiteral("still_url"), QStringLiteral("still-url")},
        {QStringLiteral("poster_thumbhash"), QString()},
        {QStringLiteral("still_thumbhash"), QStringLiteral("still-hash")},
        {QStringLiteral("user_state"), QJsonObject{
             {QStringLiteral("position_seconds"), 12.5},
             {QStringLiteral("is_in_progress"), false}}},
        {QStringLiteral("versions"), QJsonArray{
             QJsonObject{{QStringLiteral("file_id"), QStringLiteral("file-1")},
                         {QStringLiteral("chapters"), QJsonArray{
                              QJsonObject{{QStringLiteral("start_seconds"), 0.0}}}}}}}};
    const QVariantMap mapped = SiloModelMapper::mediaItem(item, QStringLiteral("silo"));
    QCOMPARE(mapped.value(QStringLiteral("primaryArtworkUrl")).toString(),
             QStringLiteral("still-url"));
    QCOMPARE(mapped.value(QStringLiteral("artworkThumbhashes")).toMap()
                 .value(QStringLiteral("poster")).toString(),
             QStringLiteral("still-hash"));
    QVERIFY(mapped.value(QStringLiteral("isInProgress")).toBool());
    const QVariantList versions = mapped.value(QStringLiteral("versions")).toList();
    QCOMPARE(versions.size(), 1);
    const QVariantList chapters = versions.constFirst().toMap()
                                      .value(QStringLiteral("chapters")).toList();
    QCOMPARE(chapters.size(), 1);
    QCOMPARE(chapters.constFirst().toMap().value(QStringLiteral("endMs")).toLongLong(), -1);

    const QVariantMap native = SiloModelMapper::nativeState(
        QJsonObject{
            {QStringLiteral("content_id"), QStringLiteral("content-1")},
            {QStringLiteral("state"), QJsonObject{
                 {QStringLiteral("position_seconds"), 4.0},
                 {QStringLiteral("played"), false}}}},
        QStringLiteral("silo"));
    QCOMPARE(native.value(QStringLiteral("itemId")).toString(), QStringLiteral("content-1"));
    QVERIFY(native.value(QStringLiteral("isInProgress")).toBool());
}

void SiloCatalogServiceTest::responsePreservesSnapshotAndPaginationTruth()
{
    SiloCatalogProvider provider;
    const ProviderCatalogResponse page = provider.parseResponse(
        ProviderCatalogOperation::Items,
        QByteArrayLiteral(
            R"({"items":[{"content_id":"content-1"},{"content_id":"content-2"}],"total":912,"total_exact":false,"has_more":true,"snapshot":"2026-08-07T11:22:33.123456789Z"})"),
        {{QByteArrayLiteral("ETAG"), QByteArrayLiteral("\"silo-page-v1\"")},
         {QByteArrayLiteral("last-modified"),
          QByteArrayLiteral("Fri, 07 Aug 2026 11:22:33 GMT")}});
    QVERIFY(page.valid);
    QCOMPARE(page.rawItems.size(), 2);
    const QString firstId = page.rawItems.at(0).toObject()
                                .value(QStringLiteral("content_id")).toString();
    const QString secondId = page.rawItems.at(1).toObject()
                                 .value(QStringLiteral("content_id")).toString();
    QVERIFY(!firstId.isEmpty());
    QVERIFY(!secondId.isEmpty());
    QVERIFY(firstId != secondId);
    QCOMPARE(page.total, 912);
    QVERIFY(page.hasMore);
    QCOMPARE(page.capabilityMetadata.value(QStringLiteral("totalPresent")).toBool(), true);
    QCOMPARE(page.capabilityMetadata.value(QStringLiteral("totalExact")).toBool(), false);
    QCOMPARE(page.capabilityMetadata.value(QStringLiteral("hasMorePresent")).toBool(), true);
    QCOMPARE(page.snapshot.value(QStringLiteral("snapshot")).toString(),
             QStringLiteral("2026-08-07T11:22:33.123456789Z"));
    QCOMPARE(page.snapshot.value(QStringLiteral("etag")).toByteArray(),
             QByteArrayLiteral("\"silo-page-v1\""));
    QCOMPARE(page.snapshot.value(QStringLiteral("lastModified")).toByteArray(),
             QByteArrayLiteral("Fri, 07 Aug 2026 11:22:33 GMT"));

    const ProviderCatalogResponse queryWindow = provider.parseResponse(
        ProviderCatalogOperation::Items,
        QByteArrayLiteral(
            R"({"items":[{"content_id":"content-3"}],"total":1,"total_exact":false,"has_more":false})"));
    QVERIFY(queryWindow.valid);
    QCOMPARE(queryWindow.rawItems.size(), 1);
    const QString nextId = queryWindow.rawItems.first().toObject()
                               .value(QStringLiteral("content_id")).toString();
    QVERIFY(!nextId.isEmpty());
    QVERIFY(nextId != firstId);
    QVERIFY(nextId != secondId);
    QCOMPARE(queryWindow.total, 1);
    QVERIFY(!queryWindow.hasMore);
    QCOMPARE(queryWindow.capabilityMetadata.value(QStringLiteral("totalExact")).toBool(),
             false);
    QVERIFY(!queryWindow.snapshot.contains(QStringLiteral("snapshot")));

    const ProviderCatalogResponse detail = provider.parseResponse(
        ProviderCatalogOperation::Item,
        QByteArrayLiteral(
            R"({"content_id":"content-42","versions":[{"file_id":"file-99"}]})"));
    QVERIFY(detail.valid);
    QCOMPARE(detail.rawItem.value(QStringLiteral("content_id")).toString(),
             QStringLiteral("content-42"));
    QCOMPARE(detail.rawItem.value(QStringLiteral("versions")).toArray()
                 .at(0).toObject().value(QStringLiteral("file_id")).toString(),
             QStringLiteral("file-99"));
}

void SiloCatalogServiceTest::nativeResponseShapesPreserveHomeLibrarySearchAndDetailData()
{
    SiloCatalogProvider provider;

    const ProviderCatalogResponse home = provider.parseResponse(
        ProviderCatalogOperation::Views,
        QByteArrayLiteral(
            R"({"sections":[{"id":"system-next-up","title":"Next Up","type":"next_up"},{"id":"library-17","title":"Movies","type":"library"}]})"));
    QVERIFY(home.valid);
    QCOMPARE(home.rawItems.size(), 2);
    QCOMPARE(home.rawItems.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("system-next-up"));
    QCOMPARE(home.rawItems.at(1).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("library"));

    const ProviderCatalogResponse library = provider.parseResponse(
        ProviderCatalogOperation::Items,
        QByteArrayLiteral(
            R"({"items":[{"content_id":"content-1","type":"movie","title":"One"},{"content_id":"content-2","type":"movie","title":"Two"}],"total":2,"total_exact":true,"has_more":false})"));
    QVERIFY(library.valid);
    QCOMPARE(library.rawItems.size(), 2);
    QCOMPARE(library.rawItems.at(1).toObject().value(QStringLiteral("content_id")).toString(),
             QStringLiteral("content-2"));
    QCOMPARE(library.total, 2);
    QCOMPARE(library.capabilityMetadata.value(QStringLiteral("totalExact")).toBool(), true);
    QVERIFY(!library.hasMore);

    const ProviderCatalogResponse search = provider.parseResponse(
        ProviderCatalogOperation::Search,
        QByteArrayLiteral("{\"items\":[{\"content_id\":\"content-9\",\"type\":\"series\",\"title\":\"Alien\"}],\"total\":1,\"total_exact\":true,\"has_more\":false,\"search_diagnostics\":{\"term\":\"Alien\"}}"));
    QVERIFY(search.valid);
    QCOMPARE(search.rawItems.size(), 1);
    QCOMPARE(search.rawItems.first().toObject()
                 .value(QStringLiteral("content_id")).toString(),
             QStringLiteral("content-9"));
    QCOMPARE(search.capabilityMetadata.value(QStringLiteral("searchDiagnostics")).toMap()
                 .value(QStringLiteral("term")).toString(),
             QStringLiteral("Alien"));

    const ProviderCatalogResponse detailResponse = provider.parseResponse(
        ProviderCatalogOperation::Item,
        QByteArrayLiteral(
            R"JSON({"content_id":"content-42","type":"movie","title":"Example","provider_ids":{"imdb_id":"tt123"},"user_state":{"played":true,"is_favorite":true},"versions":[{"file_id":"file-99","duration":123.456}],"playback_variants":[{"variant_id":"variant-1","part_count":2}],"subtitles":[{"language":"en"}]})JSON"));
    QVERIFY(detailResponse.valid);
    QCOMPARE(detailResponse.rawItem.value(QStringLiteral("content_id")).toString(),
             QStringLiteral("content-42"));
    QCOMPARE(detailResponse.rawItem.value(QStringLiteral("provider_ids")).toObject()
                 .value(QStringLiteral("imdb_id")).toString(),
             QStringLiteral("tt123"));
    QCOMPARE(detailResponse.rawItem.value(QStringLiteral("versions")).toArray().size(), 1);
    QCOMPARE(detailResponse.rawItem.value(QStringLiteral("playback_variants")).toArray()
                 .first().toObject().value(QStringLiteral("part_count")).toInt(),
             2);
    QCOMPARE(detailResponse.rawItem.value(QStringLiteral("subtitles")).toArray().size(), 1);
}

void SiloCatalogServiceTest::requestHeadersAndGenerationSuppressStaleReplies()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ScopedConfigIsolation isolation(temporaryDirectory.path());
    ConfigManager config;

    ControlledNetworkAccessManager manager;
    manager.responses = {
        {200,
         QByteArrayLiteral(
             R"({"access_token":"access-1","refresh_token":"refresh-1","expires_in":900,"user":{"id":42,"username":"Alice"}})"),
         false},
        {200,
         QByteArrayLiteral(
             R"({"profiles":[{"id":"profile-1","name":"Alice","has_pin":false,"is_child":false,"is_primary":true},{"id":"profile-2","name":"Cinema","has_pin":false,"is_child":false,"is_primary":false}]})"),
         false}
    };
    HttpTransport transport(&manager);
    SiloProviderAdapter adapter;
    ExposedAuthenticationService auth(&transport, &adapter);
    auth.initialize(&config);
    // Wait out async restore so later auth state is deterministic.
    QTRY_VERIFY_WITH_TIMEOUT(!auth.isRestoringSession(), 1000);
    auth.setProviderSelection(QStringLiteral("silo"));
    auth.authenticate(QStringLiteral("https://silo.example.test"),
                      QStringLiteral("Alice"),
                      QStringLiteral("password"));
    QTRY_COMPARE_WITH_TIMEOUT(auth.authenticationStep(), QStringLiteral("profiles"), 1000);
    auth.selectProfile(QStringLiteral("profile-1"));
    QTRY_VERIFY_WITH_TIMEOUT(auth.isAuthenticated(), 1000);
    QCOMPARE(config.getActiveConnection().value_or(ServerConnection{}).profileId,
             QStringLiteral("profile-1"));

    LibraryService service(&auth);
    QSignalSpy viewsSpy(&service, &LibraryService::canonicalViewsLoadedForConnection);
    QVERIFY(viewsSpy.isValid());

    manager.responses.append(
        {200, QByteArrayLiteral(R"([{"id":1,"name":"Old account"}])"), true});
    const qsizetype oldRequestIndex = manager.requests.size();
    service.getViews();
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), oldRequestIndex + 1, 1000);
    const RecordedRequest oldRequest = manager.requests.at(oldRequestIndex);
    QCOMPARE(oldRequest.operation, QNetworkAccessManager::GetOperation);
    QCOMPARE(oldRequest.request.url().path(),
             QStringLiteral("/api/v1/user/libraries"));
    QCOMPARE(oldRequest.request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-1"));
    QCOMPARE(oldRequest.request.rawHeader("X-Profile-Id"),
             QByteArrayLiteral("profile-1"));
    QVERIFY(oldRequest.body.isEmpty());
    QPointer<ControlledReply> staleAccountReply = manager.replies.constLast();
    QVERIFY(staleAccountReply);

    auth.replaceAccount(QStringLiteral("account-2"), QStringLiteral("access-2"));
    QTRY_VERIFY_WITH_TIMEOUT(staleAccountReply->wasAborted(), 1000);
    staleAccountReply->complete();
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(viewsSpy.count(), 0);

    manager.responses.append(
        {200, QByteArrayLiteral(R"([{"id":2,"name":"New account"}])"), true});
    const qsizetype newRequestIndex = manager.requests.size();
    service.getViews();
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), newRequestIndex + 1, 1000);
    const RecordedRequest newRequest = manager.requests.at(newRequestIndex);
    QCOMPARE(newRequest.operation, QNetworkAccessManager::GetOperation);
    QCOMPARE(newRequest.request.url().path(),
             QStringLiteral("/api/v1/user/libraries"));
    QCOMPARE(newRequest.request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-2"));
    QCOMPARE(newRequest.request.rawHeader("X-Profile-Id"),
             QByteArrayLiteral("profile-1"));
    QPointer<ControlledReply> currentReply = manager.replies.constLast();
    QVERIFY(currentReply);
    currentReply->complete();
    QTRY_COMPARE_WITH_TIMEOUT(viewsSpy.count(), 1, 1000);
    const QVariantList currentViews = viewsSpy.first().at(1).toList();
    QCOMPARE(currentViews.size(), 1);
    QCOMPARE(currentViews.constFirst().toMap()
                 .value(QStringLiteral("itemId")).toString(),
             QStringLiteral("2"));

    manager.responses.append(
        {200, QByteArrayLiteral(R"([{"id":3,"name":"Stale profile"}])"), true});
    service.getViews();
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), newRequestIndex + 2, 1000);
    QPointer<ControlledReply> staleProfileReply = manager.replies.constLast();
    QVERIFY(staleProfileReply);
    auth.selectProfile(QStringLiteral("profile-2"));
    QTRY_VERIFY_WITH_TIMEOUT(auth.isAuthenticated(), 1000);
    QCOMPARE(config.getActiveConnection().value_or(ServerConnection{}).profileId,
             QStringLiteral("profile-2"));
    QTRY_VERIFY_WITH_TIMEOUT(staleProfileReply->wasAborted(), 1000);
    staleProfileReply->complete();
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCOMPARE(viewsSpy.count(), 1);

    manager.responses.append(
        {200, QByteArrayLiteral(R"([{"id":4,"name":"Current profile"}])"), true});
    const qsizetype profileRequestIndex = manager.requests.size();
    service.getViews();
    QTRY_COMPARE_WITH_TIMEOUT(manager.requests.size(), profileRequestIndex + 1, 1000);
    const RecordedRequest profileRequest = manager.requests.at(profileRequestIndex);
    QCOMPARE(profileRequest.operation, QNetworkAccessManager::GetOperation);
    QCOMPARE(profileRequest.request.url().path(),
             QStringLiteral("/api/v1/user/libraries"));
    QCOMPARE(profileRequest.request.rawHeader("Authorization"),
             QByteArrayLiteral("Bearer access-2"));
    QCOMPARE(profileRequest.request.rawHeader("X-Profile-Id"),
             QByteArrayLiteral("profile-2"));
    QVERIFY(profileRequest.body.isEmpty());
    QPointer<ControlledReply> profileReply = manager.replies.constLast();
    QVERIFY(profileReply);
    profileReply->complete();
    QTRY_COMPARE_WITH_TIMEOUT(viewsSpy.count(), 2, 1000);
}

QTEST_MAIN(SiloCatalogServiceTest)
#include "SiloCatalogServiceTest.moc"
