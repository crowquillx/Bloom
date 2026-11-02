#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "network/discovery/DnsMessage.h"
#include "network/discovery/DnsSdResolver.h"
#include "network/discovery/JellyfinServerBrowser.h"
#include "network/discovery/MdnsServerBrowser.h"
#include "network/discovery/ServerCandidateValidator.h"
#include "network/discovery/ServerDiscoveryService.h"
#include "providers/jellyfin/JellyfinLanDiscovery.h"
#include "providers/jellyfin/JellyfinProviderAdapter.h"
#include "providers/silo/SiloLanDiscovery.h"
#include "providers/silo/SiloProviderAdapter.h"

using namespace Bloom::Dns;

namespace {

const QString kSiloId = QStringLiteral("3f2a9d5e-6b1c-4c7e-9a0d-2f4b8c1e7a35");

Name silo() { return nameFromDotted("_silo._tcp.local"); }
Name instanceName(const QByteArray &label)
{
    Name name{label};
    name.append(silo());
    return name;
}
Name hostName() { return nameFromDotted("silo-3f2a9d5e-a1b2c3.local"); }

Record ptrRecord(const Name &instance, quint32 ttl = 4500)
{
    Record record;
    record.name = silo();
    record.type = TypePTR;
    record.ttl = ttl;
    record.target = instance;
    return record;
}

Record srvRecord(const Name &instance, quint16 port = 8080)
{
    Record record;
    record.name = instance;
    record.type = TypeSRV;
    record.cacheFlush = true;
    record.ttl = 4500;
    record.port = port;
    record.target = hostName();
    return record;
}

Record txtRecord(const Name &instance, const QByteArrayList &strings)
{
    Record record;
    record.name = instance;
    record.type = TypeTXT;
    record.cacheFlush = true;
    record.ttl = 4500;
    record.txt = strings;
    return record;
}

Record addressRecord(const QString &address, quint32 ttl = 120)
{
    Record record;
    record.name = hostName();
    record.address = QHostAddress(address);
    record.type = record.address.protocol() == QAbstractSocket::IPv4Protocol ? TypeA : TypeAAAA;
    record.cacheFlush = true;
    record.ttl = ttl;
    return record;
}

Message siloResponse(const QByteArray &label = QByteArrayLiteral("Den Silo"))
{
    Message message;
    message.flags = FlagResponse | FlagAuthoritative;
    const Name instance = instanceName(label);
    message.answers = {ptrRecord(instance)};
    message.additionals = {
        srvRecord(instance),
        txtRecord(instance, {QByteArrayLiteral("v=1"), "id=" + kSiloId.toUtf8()}),
        addressRecord(QStringLiteral("fe80::1")),
        addressRecord(QStringLiteral("2001:db8::20")),
        addressRecord(QStringLiteral("192.168.1.20")),
    };
    return message;
}

// Builds wire bytes by hand, independent of Dns::encode().
class Bytes
{
public:
    QByteArray data;
    int at() const { return static_cast<int>(data.size()); }
    Bytes &u8(int value) { data.append(static_cast<char>(value)); return *this; }
    Bytes &u16(int value) { return u8((value >> 8) & 0xff).u8(value & 0xff); }
    Bytes &u32(quint32 value) { return u16(static_cast<int>(value >> 16)).u16(value & 0xffff); }
    Bytes &label(const QByteArray &text) { u8(static_cast<int>(text.size())); data.append(text); return *this; }
    Bytes &pointer(int offset) { return u16(0xc000 | offset); }
    Bytes &raw(const QByteArray &bytes) { data.append(bytes); return *this; }
    void patchU16(int offset, int value)
    {
        data[offset] = static_cast<char>((value >> 8) & 0xff);
        data[offset + 1] = static_cast<char>(value & 0xff);
    }
};

class FakeBrowser final : public LanServerBrowser
{
    Q_OBJECT

public:
    explicit FakeBrowser(bool available = true) : m_available(available) {}

    bool start() override
    {
        ++starts;
        running = m_available;
        return m_available;
    }
    void query() override { ++queries; }
    void stop() override
    {
        ++stops;
        running = false;
    }
    void announce(const LanServerAdvert &advert) { emit advertFound(advert); }

    int starts = 0;
    int queries = 0;
    int stops = 0;
    bool running = false;

private:
    bool m_available;
};

class FakeValidator final : public ServerCandidateValidator
{
public:
    struct Pending {
        ProviderKind kind;
        QUrl url;
        QString expectedServerId;
        Completion done;
    };

    void validate(ProviderKind kind, const QUrl &baseUrl, const QString &expectedServerId,
                  Completion done) override
    {
        pending.append(Pending{kind, baseUrl, expectedServerId, std::move(done)});
    }
    void cancelAll() override
    {
        ++cancels;
        canceled.append(pending);
        pending.clear();
    }

    // Completes the pending check for @p url.
    bool complete(const QUrl &url, std::optional<ProviderServerIdentity> identity)
    {
        for (qsizetype index = 0; index < pending.size(); ++index) {
            if (pending.at(index).url == url) {
                const Pending check = pending.takeAt(index);
                check.done(identity);
                return true;
            }
        }
        return false;
    }

    QList<Pending> pending;
    QList<Pending> canceled;
    int cancels = 0;
};

LanServerAdvert advert(ProviderKind kind, const QString &serverId, const QString &name,
                       const QStringList &urls, const QString &origin = {})
{
    LanServerAdvert result;
    result.providerKind = kind;
    result.serverId = serverId;
    result.name = name;
    result.origin = origin;
    result.sender = QHostAddress(origin);
    for (const QString &url : urls) {
        result.urls.append(QUrl(url));
    }
    return result;
}

struct ServiceFixture {
    FakeBrowser *mdns = new FakeBrowser();
    FakeBrowser *jellyfin = new FakeBrowser();
    FakeValidator *validator = new FakeValidator();
    ServerDiscoveryService service{{mdns, jellyfin},
                                   std::unique_ptr<ServerCandidateValidator>(validator)};

    ServiceFixture()
    {
        ServerDiscoveryService::Timing timing;
        timing.searchWindowMs = 60000;
        timing.queryOffsetsMs = {0};
        service.setTiming(timing);
    }
};

// A one-shot HTTP/1.1 server answering fixed bodies per path.
class StubHttpServer final : public QObject
{
    Q_OBJECT

public:
    // Status 0 accepts the request and never answers.
    struct Response {
        int status = 200;
        QByteArray body;
        QByteArray extraHeaders;
    };

    StubHttpServer()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    m_buffers[socket] += socket->readAll();
                    const QByteArray &buffer = m_buffers[socket];
                    if (!buffer.contains("\r\n\r\n")) {
                        return;
                    }
                    const QList<QByteArray> requestLine = buffer.left(buffer.indexOf("\r\n")).split(' ');
                    const QByteArray path = requestLine.value(1);
                    requests.append(QString::fromLatin1(path));
                    const QByteArray lowered = buffer.toLower();
                    if (lowered.contains("\nauthorization:") || lowered.contains("\nx-silo-device-id:")) {
                        credentialHeaders = true;
                    }
                    const Response response = responses.value(QString::fromLatin1(path),
                                                              Response{404, "{}", {}});
                    if (response.status == 0) {
                        return;
                    }
                    QByteArray reply = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n"
                        + "Content-Type: application/json\r\nConnection: close\r\n"
                        + response.extraHeaders
                        + "Content-Length: " + QByteArray::number(response.body.size())
                        + "\r\n\r\n" + response.body;
                    socket->write(reply);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        m_server.listen(QHostAddress::LocalHost);
    }

    QUrl baseUrl() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()));
    }

    QHash<QString, Response> responses;
    QStringList requests;
    bool credentialHeaders = false;

private:
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

std::optional<ProviderServerIdentity> runValidation(ProviderServerValidator &validator,
                                                    ProviderKind kind, const QUrl &url,
                                                    const QString &expectedId)
{
    bool finished = false;
    std::optional<ProviderServerIdentity> result;
    validator.validate(kind, url, expectedId, [&](std::optional<ProviderServerIdentity> identity) {
        finished = true;
        result = identity;
    });
    if (!QTest::qWaitFor([&]() { return finished; }, 10000)) {
        qWarning() << "validation did not finish";
    }
    return result;
}

} // namespace

class ServerDiscoveryTest : public QObject
{
    Q_OBJECT

private slots:
    // ---------- DNS wire format ----------
    void encodeQueryWritesOnePtrQuestion()
    {
        const QByteArray packet = encodeQuery({Question{silo(), TypePTR}});
        Bytes expected;
        expected.u16(0).u16(0).u16(1).u16(0).u16(0).u16(0)
            .label("_silo").label("_tcp").label("local").u8(0)
            .u16(TypePTR).u16(ClassIN);
        QCOMPARE(packet, expected.data);

        const QByteArray unicast = encodeQuery({Question{silo(), TypePTR, ClassIN, true}});
        QCOMPARE(static_cast<quint8>(unicast.at(unicast.size() - 2)), quint8(0x80));
    }

    void encodeCompressesAndDecodeRoundTrips()
    {
        const Message original = siloResponse();
        const QByteArray packet = encode(original);
        QVERIFY(!packet.isEmpty());
        // "_silo._tcp.local" is written once; every later use is a pointer.
        QCOMPARE(packet.count("_silo"), 1);
        QCOMPARE(packet.count("silo-3f2a9d5e-a1b2c3"), 1);

        const auto decoded = decode(packet);
        QVERIFY(decoded);
        QVERIFY(decoded->isResponse());
        QCOMPARE(decoded->answers.size(), 1);
        QCOMPARE(decoded->additionals.size(), 5);
        QVERIFY(namesEqual(decoded->answers.at(0).target, instanceName("Den Silo")));
        const Record &srv = decoded->additionals.at(0);
        QCOMPARE(srv.type, quint16(TypeSRV));
        QCOMPARE(srv.port, quint16(8080));
        QVERIFY(srv.cacheFlush);
        QCOMPARE(srv.rrclass, ClassIN);
        QVERIFY(namesEqual(srv.target, hostName()));
        QCOMPARE(decoded->additionals.at(1).txt,
                 QByteArrayList({"v=1", "id=" + kSiloId.toUtf8()}));
        QCOMPARE(decoded->additionals.at(4).address, QHostAddress(QStringLiteral("192.168.1.20")));
        QCOMPARE(decoded->additionals.at(3).address, QHostAddress(QStringLiteral("2001:db8::20")));
    }

    void decodesHandWrittenCompressedResponse()
    {
        // As miekg/dns (Silo's responder) packs it: names compressed against
        // earlier occurrences, including inside SRV and PTR data.
        Bytes packet;
        packet.u16(0).u16(0x8400).u16(0).u16(1).u16(0).u16(3);
        const int serviceAt = packet.at();
        packet.label("_silo").label("_tcp");
        const int localAt = packet.at();
        packet.label("local").u8(0);
        packet.u16(TypePTR).u16(ClassIN).u32(4500);
        const int ptrLength = packet.at();
        packet.u16(0);
        const int instanceAt = packet.at();
        packet.label("Living.Room").pointer(serviceAt);
        packet.patchU16(ptrLength, packet.at() - ptrLength - 2);

        packet.pointer(instanceAt).u16(TypeSRV).u16(0x8001).u32(4500);
        const int srvLength = packet.at();
        packet.u16(0).u16(0).u16(0).u16(8096);
        const int hostAt = packet.at();
        packet.label("silo-host").pointer(localAt);
        packet.patchU16(srvLength, packet.at() - srvLength - 2);

        packet.pointer(instanceAt).u16(TypeTXT).u16(0x8001).u32(4500);
        const QByteArray id = "id=" + kSiloId.toUtf8();
        packet.u16(4 + 1 + static_cast<int>(id.size())).label("v=1").label(id);

        packet.pointer(hostAt).u16(TypeA).u16(0x8001).u32(120).u16(4)
            .u8(10).u8(0).u8(0).u8(7);

        const auto message = decode(packet.data);
        QVERIFY(message);
        QCOMPARE(message->answers.at(0).target,
                 Name({"Living.Room", "_silo", "_tcp", "local"}));
        QCOMPARE(message->additionals.at(0).target, Name({"silo-host", "local"}));
        QCOMPARE(message->additionals.at(2).name, Name({"silo-host", "local"}));
        QCOMPARE(message->additionals.at(2).address, QHostAddress(QStringLiteral("10.0.0.7")));

        DnsSdResolver resolver(silo());
        resolver.absorb(*message);
        const auto services = resolver.resolved();
        QCOMPARE(services.size(), 1);
        // A dot inside the instance label stays in the label.
        QCOMPARE(services.at(0).instanceLabel, QStringLiteral("Living.Room"));
        QCOMPARE(services.at(0).port, quint16(8096));
        QCOMPARE(services.at(0).txt.value("id"), kSiloId.toUtf8());
    }

    void decodeRejectsMalformedPackets()
    {
        // Too short for a header.
        QVERIFY(!decode(QByteArray("\x00\x00\x84", 3)));

        // A pointer to itself.
        Bytes loop;
        loop.u16(0).u16(0x8400).u16(0).u16(1).u16(0).u16(0);
        loop.pointer(loop.at()).u16(TypeA).u16(1).u32(120).u16(4).u32(0x0a000001);
        QVERIFY(!decode(loop.data));

        // A forward pointer.
        Bytes forward;
        forward.u16(0).u16(0x8400).u16(0).u16(1).u16(0).u16(0);
        forward.pointer(forward.at() + 2).label("x").u8(0);
        QVERIFY(!decode(forward.data));

        // Two names pointing at each other.
        Bytes cycle;
        cycle.u16(0).u16(0).u16(2).u16(0).u16(0).u16(0);
        const int first = cycle.at();
        cycle.label("a").pointer(first + 8).u16(TypePTR).u16(1);
        cycle.label("b").pointer(first).u16(TypePTR).u16(1);
        QVERIFY(!decode(cycle.data));

        // Reserved label type 0x40.
        Bytes reserved;
        reserved.u16(0).u16(0).u16(1).u16(0).u16(0).u16(0).u8(0x41).raw("a").u8(0)
            .u16(TypePTR).u16(1);
        QVERIFY(!decode(reserved.data));

        // An answer whose data runs past the packet.
        Bytes truncated;
        truncated.u16(0).u16(0x8400).u16(0).u16(1).u16(0).u16(0).label("h").u8(0)
            .u16(TypeA).u16(1).u32(120).u16(4).u8(10);
        QVERIFY(!decode(truncated.data));

        // An A record with the wrong length.
        Bytes badA;
        badA.u16(0).u16(0x8400).u16(0).u16(1).u16(0).u16(0).label("h").u8(0)
            .u16(TypeA).u16(1).u32(120).u16(5).u32(0x0a000001).u8(0);
        QVERIFY(!decode(badA.data));

        // Counts promising more records than the packet holds.
        Bytes counts;
        counts.u16(0).u16(0x8400).u16(0).u16(200).u16(0).u16(0);
        QVERIFY(!decode(counts.data));

        // More entries than one message may declare, whatever follows.
        Bytes tooMany;
        tooMany.u16(0).u16(0x8400).u16(0).u16(MaxEntriesPerMessage).u16(1).u16(0);
        QVERIFY(!decode(tooMany.data));

        // A name longer than 255 bytes.
        Bytes longName;
        longName.u16(0).u16(0).u16(1).u16(0).u16(0).u16(0);
        for (int i = 0; i < 5; ++i) {
            longName.label(QByteArray(60, 'a'));
        }
        longName.u8(0).u16(TypePTR).u16(1);
        QVERIFY(!decode(longName.data));

        // Encoding refuses an over-long label.
        QVERIFY(encodeQuery({Question{Name{QByteArray(64, 'a')}, TypePTR}}).isEmpty());
    }

    void decodeRejectsLongPointerChains()
    {
        // An unread record type holds a name and a chain of pointers, each
        // to the one before; an A record's name enters at the chain's end.
        auto packet = [](int chainLength) {
            Bytes bytes;
            bytes.u16(0).u16(0x8400).u16(0).u16(2).u16(0).u16(0);
            bytes.label("h").u8(0).u16(99).u16(1).u32(120);
            const int lengthAt = bytes.at();
            bytes.u16(0);
            const int dataStart = bytes.at();
            int previous = bytes.at();
            bytes.label("a").u8(0);
            for (int i = 0; i < chainLength; ++i) {
                const int here = bytes.at();
                bytes.pointer(previous);
                previous = here;
            }
            bytes.patchU16(lengthAt, bytes.at() - dataStart);
            bytes.pointer(previous).u16(TypeA).u16(1).u32(120).u16(4).u32(0x0a000001);
            return bytes.data;
        };
        // The A record's own pointer is the first hop.
        const auto atLimit = decode(packet(MaxPointerHops - 1));
        QVERIFY(atLimit);
        QCOMPARE(atLimit->answers.size(), 2);
        QCOMPARE(atLimit->answers.at(1).name, Name{QByteArrayLiteral("a")});
        QVERIFY(!decode(packet(MaxPointerHops)));
        QVERIFY(!decode(packet(2000)));
    }

    void txtAttributesFollowDnsSdRules()
    {
        const auto attributes = txtAttributes({"V=1", "id=abc", "id=ignored", "", "=x", "flag",
                                               "empty="});
        QCOMPARE(attributes.value("v"), QByteArray("1"));
        QCOMPARE(attributes.value("id"), QByteArray("abc"));
        QVERIFY(attributes.contains("flag"));
        QVERIFY(attributes.value("flag").isEmpty());
        QVERIFY(attributes.contains("empty"));
        QCOMPARE(attributes.size(), 4);
    }

    // ---------- DNS-SD resolution ----------
    void resolverAssemblesInstancesAndDropsLinkLocal()
    {
        DnsSdResolver resolver(silo());
        resolver.absorb(siloResponse());
        const auto services = resolver.resolved();
        QCOMPARE(services.size(), 1);
        const ServiceInstance &service = services.at(0);
        QCOMPARE(service.instanceLabel, QStringLiteral("Den Silo"));
        QCOMPARE(service.port, quint16(8080));
        QCOMPARE(service.addresses,
                 QList<QHostAddress>({QHostAddress(QStringLiteral("192.168.1.20")),
                                      QHostAddress(QStringLiteral("2001:db8::20"))}));
    }

    void resolverAsksForWhatIsMissingAndHonoursGoodbyes()
    {
        DnsSdResolver resolver(silo());
        Message message;
        message.flags = FlagResponse;
        const Name instance = instanceName("Den Silo");
        message.answers = {ptrRecord(instance)};
        resolver.absorb(message);
        QVERIFY(resolver.resolved().isEmpty());
        auto questions = resolver.missingQuestions();
        QCOMPARE(questions.size(), 2);
        QCOMPARE(questions.at(0).type, quint16(TypeSRV));
        QCOMPARE(questions.at(1).type, quint16(TypeTXT));

        message.answers = {srvRecord(instance),
                           txtRecord(instance, {"v=1", "id=" + kSiloId.toUtf8()})};
        resolver.absorb(message);
        questions = resolver.missingQuestions();
        QCOMPARE(questions.size(), 2);
        QCOMPARE(questions.at(0).type, quint16(TypeA));
        QVERIFY(namesEqual(questions.at(0).name, hostName()));

        message.answers = {addressRecord(QStringLiteral("192.168.1.20"))};
        resolver.absorb(message);
        QCOMPARE(resolver.resolved().size(), 1);
        QVERIFY(resolver.missingQuestions().isEmpty());

        // A cache-flush set replaces the old address of that family.
        message.answers = {addressRecord(QStringLiteral("192.168.1.21"))};
        resolver.absorb(message);
        QCOMPARE(resolver.resolved().at(0).addresses,
                 QList<QHostAddress>({QHostAddress(QStringLiteral("192.168.1.21"))}));

        // Queries and other service types are ignored.
        Message query;
        query.answers = {ptrRecord(instanceName("Other"))};
        resolver.absorb(query);
        Message other;
        other.flags = FlagResponse;
        Record foreign = ptrRecord(instanceName("Printer"));
        foreign.name = nameFromDotted("_ipp._tcp.local");
        foreign.target = Name{"Printer", "_ipp", "_tcp", "local"};
        other.answers = {foreign};
        resolver.absorb(other);
        QCOMPARE(resolver.resolved().size(), 1);

        // Goodbye.
        message.answers = {ptrRecord(instance, 0)};
        resolver.absorb(message);
        QVERIFY(resolver.resolved().isEmpty());
    }

    void resolverFlushesEachAddressFamilySeparately()
    {
        DnsSdResolver resolver(silo());
        resolver.absorb(siloResponse());
        QCOMPARE(resolver.resolved().at(0).addresses,
                 QList<QHostAddress>({QHostAddress(QStringLiteral("192.168.1.20")),
                                      QHostAddress(QStringLiteral("2001:db8::20"))}));

        // A refresh of both families replaces both old sets.
        Message refresh;
        refresh.flags = FlagResponse;
        refresh.answers = {addressRecord(QStringLiteral("192.168.1.30")),
                           addressRecord(QStringLiteral("2001:db8::30"))};
        resolver.absorb(refresh);
        QCOMPARE(resolver.resolved().at(0).addresses,
                 QList<QHostAddress>({QHostAddress(QStringLiteral("192.168.1.30")),
                                      QHostAddress(QStringLiteral("2001:db8::30"))}));

        // A flush of one family leaves the other.
        Message ipv4Only;
        ipv4Only.flags = FlagResponse;
        ipv4Only.answers = {addressRecord(QStringLiteral("192.168.1.40")),
                            addressRecord(QStringLiteral("192.168.1.41"))};
        resolver.absorb(ipv4Only);
        QCOMPARE(resolver.resolved().at(0).addresses,
                 QList<QHostAddress>({QHostAddress(QStringLiteral("192.168.1.40")),
                                      QHostAddress(QStringLiteral("192.168.1.41")),
                                      QHostAddress(QStringLiteral("2001:db8::30"))}));
    }

    void resolverDropsHostsNoInstanceUses()
    {
        DnsSdResolver resolver(silo());
        resolver.absorb(siloResponse());
        QCOMPARE(resolver.cachedHostCount(), 1);
        const Name instance = instanceName("Den Silo");
        // One instance whose SRV target keeps moving, each with an address.
        for (int i = 0; i < 100; ++i) {
            const Name host = nameFromDotted("moving-" + QByteArray::number(i) + ".local");
            Record srv = srvRecord(instance);
            srv.target = host;
            Record address = addressRecord(QStringLiteral("192.168.2.%1").arg(1 + i % 200));
            address.name = host;
            Message message;
            message.flags = FlagResponse;
            message.answers = {srv, address};
            resolver.absorb(message);
            QCOMPARE(resolver.cachedHostCount(), 1);
        }
        QCOMPARE(resolver.resolved().at(0).addresses,
                 QList<QHostAddress>({QHostAddress(QStringLiteral("192.168.2.100"))}));
    }

    void resolverBoundsInstances()
    {
        DnsSdResolver resolver(silo());
        Message message;
        message.flags = FlagResponse;
        for (int i = 0; i < DnsSdResolver::MaxInstances + 10; ++i) {
            message.answers.append(ptrRecord(instanceName("S" + QByteArray::number(i))));
        }
        resolver.absorb(message);
        QCOMPARE(resolver.missingQuestions().size(), DnsSdResolver::MaxInstances * 2);
    }

    // ---------- Silo adverts ----------
    void siloAdvertNeedsVersionOneAndAnId()
    {
        const QList<QHostAddress> addresses{QHostAddress(QStringLiteral("192.168.1.20")),
                                            QHostAddress(QStringLiteral("fe80::1")),
                                            QHostAddress(QStringLiteral("2001:db8::20"))};
        const auto found = SiloLanDiscovery::advert(QStringLiteral("Den"),
                                                    {{"v", "1"}, {"id", kSiloId.toUtf8()}},
                                                    8080, addresses);
        QVERIFY(found);
        QCOMPARE(found->providerKind, ProviderKind::Silo);
        QCOMPARE(found->serverId, kSiloId);
        QCOMPARE(found->name, QStringLiteral("Den"));
        QCOMPARE(found->urls, QList<QUrl>({QUrl(QStringLiteral("http://192.168.1.20:8080")),
                                           QUrl(QStringLiteral("http://[2001:db8::20]:8080"))}));

        QVERIFY(!SiloLanDiscovery::advert(QStringLiteral("Den"), {{"v", "2"}, {"id", "x"}}, 8080,
                                          addresses));
        QVERIFY(!SiloLanDiscovery::advert(QStringLiteral("Den"), {{"v", "1"}}, 8080, addresses));
        QVERIFY(!SiloLanDiscovery::advert(QStringLiteral("Den"), {{"v", "1"}, {"id", "x"}}, 8080,
                                          {QHostAddress(QStringLiteral("fe80::1"))}));
    }

    void mdnsBrowserEmitsEachSiloServerOnce()
    {
        MdnsServerBrowser browser(SiloLanDiscovery::serviceType(), [](const ServiceInstance &service) {
            return SiloLanDiscovery::advert(service.instanceLabel, service.txt, service.port,
                                            service.addresses);
        });
        QList<LanServerAdvert> found;
        connect(&browser, &LanServerBrowser::advertFound, this,
                [&found](const LanServerAdvert &advert) { found.append(advert); });
        const QByteArray packet = encode(siloResponse());
        browser.handlePacket(packet);
        browser.handlePacket(packet);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.at(0).serverId, kSiloId);
        QCOMPARE(found.at(0).urls.constFirst(), QUrl(QStringLiteral("http://192.168.1.20:8080")));
        QCOMPARE(found.at(0).origin, QStringLiteral("silo-3f2a9d5e-a1b2c3.local"));

        // A query, garbage and another service do not report anything.
        browser.handlePacket(encodeQuery({Question{silo(), TypePTR}}));
        browser.handlePacket(QByteArray("junk"));
        QCOMPARE(found.size(), 1);

        // The browse query asks for the service type.
        const auto query = decode(browser.buildQuery());
        QVERIFY(query);
        QVERIFY(!query->isResponse());
        QVERIFY(namesEqual(query->questions.at(0).name, silo()));
        QCOMPARE(query->questions.at(0).type, quint16(TypePTR));
    }

    void mdnsAcceptsOnlySendersOnThisLink()
    {
        QNetworkAddressEntry lan;
        lan.setIp(QHostAddress(QStringLiteral("192.168.1.10")));
        lan.setPrefixLength(24);
        QNetworkAddressEntry lan6;
        lan6.setIp(QHostAddress(QStringLiteral("2001:db8:1::10")));
        lan6.setPrefixLength(64);
        const QList<QNetworkAddressEntry> local{lan, lan6};
        auto accepts = [&local](const QString &sender, int hopLimit) {
            return MdnsServerBrowser::acceptsSender(QHostAddress(sender), hopLimit, local);
        };
        // On a local subnet, whatever the hop limit (Silo sends with 1).
        QVERIFY(accepts(QStringLiteral("192.168.1.20"), 1));
        QVERIFY(accepts(QStringLiteral("192.168.1.20"), -1));
        QVERIFY(accepts(QStringLiteral("::ffff:192.168.1.20"), -1));
        QVERIFY(accepts(QStringLiteral("2001:db8:1::20"), 64));
        QVERIFY(accepts(QStringLiteral("fe80::1"), -1));
        // Off the local subnets only an unrouted packet (hop limit 255).
        QVERIFY(!accepts(QStringLiteral("10.9.8.7"), 1));
        QVERIFY(!accepts(QStringLiteral("10.9.8.7"), -1));
        QVERIFY(!accepts(QStringLiteral("2001:db8:2::20"), 254));
        QVERIFY(accepts(QStringLiteral("10.9.8.7"), 255));
        QVERIFY(!accepts(QString(), -1));
    }

    // ---------- Jellyfin replies ----------
    void jellyfinReplyAddsTheSenderAddress()
    {
        // A real reply: the published address carries no scheme or port and
        // is unreachable from the LAN; the sender address is.
        const QByteArray reply =
            "{\"Address\":\"192.168.0.85\",\"Id\":\"7e607e18f5c94adbbb4dd290e67afe43\",\"Name\":\"tanime\",\"EndpointAddress\":null}";
        const auto found = JellyfinLanDiscovery::advert(reply, QHostAddress(QStringLiteral("192.168.40.181")));
        QVERIFY(found);
        QCOMPARE(found->providerKind, ProviderKind::Jellyfin);
        QCOMPARE(found->serverId, QStringLiteral("7e607e18f5c94adbbb4dd290e67afe43"));
        QCOMPARE(found->name, QStringLiteral("tanime"));
        QCOMPARE(found->urls, QList<QUrl>({QUrl(QStringLiteral("http://192.168.0.85:8096")),
                                           QUrl(QStringLiteral("http://192.168.40.181:8096"))}));
    }

    void jellyfinReplyKeepsSchemePortAndBasePath()
    {
        auto found = JellyfinLanDiscovery::advert(
            "{\"Address\":\"http://192.168.1.5:8920/jellyfin/\",\"Id\":\"abc\",\"Name\":\"Den\"}",
            QHostAddress(QStringLiteral("::ffff:192.168.1.5")));
        QVERIFY(found);
        QCOMPARE(found->urls, QList<QUrl>({QUrl(QStringLiteral("http://192.168.1.5:8920/jellyfin"))}));

        found = JellyfinLanDiscovery::advert(
            "{\"Address\":\"https://media.example.com\",\"Id\":\"abc\",\"Name\":\"Den\"}",
            QHostAddress(QStringLiteral("192.168.1.5")));
        QVERIFY(found);
        QCOMPARE(found->urls, QList<QUrl>({QUrl(QStringLiteral("https://media.example.com")),
                                           QUrl(QStringLiteral("http://192.168.1.5:8096"))}));

        // The sender fallback keeps an HTTPS address's base path; only the
        // port falls back to Jellyfin's plain HTTP one.
        found = JellyfinLanDiscovery::advert(
            "{\"Address\":\"https://media.example.com:8920/jellyfin/\",\"Id\":\"abc\",\"Name\":\"Den\"}",
            QHostAddress(QStringLiteral("192.168.1.5")));
        QVERIFY(found);
        QCOMPARE(found->urls,
                 QList<QUrl>({QUrl(QStringLiteral("https://media.example.com:8920/jellyfin")),
                              QUrl(QStringLiteral("http://192.168.1.5:8096/jellyfin"))}));
    }

    void jellyfinReplyRejectsMalformedReplies()
    {
        const QHostAddress sender(QStringLiteral("192.168.1.5"));
        QVERIFY(!JellyfinLanDiscovery::advert("not json", sender));
        QVERIFY(!JellyfinLanDiscovery::advert("[]", sender));
        QVERIFY(!JellyfinLanDiscovery::advert("{\"Address\":\"http://a:1\",\"Name\":\"x\"}", sender));
        QVERIFY(!JellyfinLanDiscovery::advert("{\"Id\":\"  \",\"Address\":\"http://a:1\"}", sender));
        QVERIFY(!JellyfinLanDiscovery::advert(QByteArray(5000, ' ') + "{\"Id\":\"a\"}", sender));
        // No usable address at all.
        QVERIFY(!JellyfinLanDiscovery::advert("{\"Id\":\"a\",\"Address\":\"ftp://x\"}",
                                              QHostAddress(QStringLiteral("127.0.0.1"))));
        QCOMPARE(JellyfinLanDiscovery::probe(), QByteArray("Who is JellyfinServer?"));
    }

    void jellyfinBrowserEmitsEachReplyOnce()
    {
        JellyfinServerBrowser browser;
        QList<LanServerAdvert> found;
        connect(&browser, &LanServerBrowser::advertFound, this,
                [&found](const LanServerAdvert &advert) { found.append(advert); });
        const QByteArray reply = "{\"Address\":\"http://192.168.1.5:8096\",\"Id\":\"abc\",\"Name\":\"Den\"}";
        browser.handleReply(reply, QHostAddress(QStringLiteral("192.168.1.5")));
        browser.handleReply(reply, QHostAddress(QStringLiteral("::ffff:192.168.1.5")));
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.at(0).origin, QStringLiteral("192.168.1.5"));
        QCOMPARE(found.at(0).sender, QHostAddress(QStringLiteral("192.168.1.5")));
    }

    // ---------- Provider identity reads ----------
    void providersMapTheirIdentityReads()
    {
        SiloProviderAdapter silo;
        QCOMPARE(silo.endpointFor(ProviderRoute::ServerIdentity).value_or(QString()),
                 QStringLiteral("/api/v2/system/identity"));
        QCOMPARE(silo.endpointFor(ProviderRoute::ServerBranding).value_or(QString()),
                 QStringLiteral("/api/v2/theme/branding"));
        QCOMPARE(silo.mapServerIdentity({{"server_id", kSiloId}})->serverId, kSiloId);
        QVERIFY(!silo.mapServerIdentity({{"server_id", ""}}));
        QCOMPARE(silo.mapServerBrandingName({{"server_name", " Den "}}), QStringLiteral("Den"));

        JellyfinProviderAdapter jellyfin;
        QCOMPARE(jellyfin.endpointFor(ProviderRoute::ServerIdentity).value_or(QString()),
                 QStringLiteral("/System/Info/Public"));
        QVERIFY(!jellyfin.endpointFor(ProviderRoute::ServerBranding));
        const auto identity = jellyfin.mapServerIdentity({{"Id", "abc"}, {"ServerName", "Den"}});
        QVERIFY(identity);
        QCOMPARE(identity->serverId, QStringLiteral("abc"));
        QCOMPARE(identity->serverName, QStringLiteral("Den"));
    }

    void validatorConfirmsSiloIdentityAndReadsTheName()
    {
        StubHttpServer server;
        server.responses.insert(QStringLiteral("/api/v2/system/identity"),
                                {200, "{\"server_id\":\"" + kSiloId.toUtf8() + "\"}", {}});
        server.responses.insert(QStringLiteral("/api/v2/theme/branding"),
                                {200, "{\"server_name\":\"Den Silo\",\"login_subtitle\":\"\",\"storage_available\":true}", {}});
        SiloProviderAdapter silo;
        ProviderServerValidator validator({&silo});

        const auto identity = runValidation(validator, ProviderKind::Silo, server.baseUrl(), kSiloId);
        QVERIFY(identity);
        QCOMPARE(identity->serverId, kSiloId);
        QCOMPARE(identity->serverName, QStringLiteral("Den Silo"));
        QCOMPARE(server.requests, QStringList({QStringLiteral("/api/v2/system/identity"),
                                               QStringLiteral("/api/v2/theme/branding")}));
        QVERIFY(!server.credentialHeaders);

        // Another server's id is not this server.
        QVERIFY(!runValidation(validator, ProviderKind::Silo, server.baseUrl(),
                               QStringLiteral("someone-else")));
    }

    void validatorKeepsIdentityWhenBrandingFails()
    {
        StubHttpServer server;
        server.responses.insert(QStringLiteral("/api/v2/system/identity"),
                                {200, "{\"server_id\":\"" + kSiloId.toUtf8() + "\"}", {}});
        SiloProviderAdapter silo;
        ProviderServerValidator validator({&silo});
        const auto identity = runValidation(validator, ProviderKind::Silo, server.baseUrl(), kSiloId);
        QVERIFY(identity);
        QVERIFY(identity->serverName.isEmpty());
    }

    void validatorRefusesRedirectsAndErrors()
    {
        StubHttpServer server;
        server.responses.insert(QStringLiteral("/System/Info/Public"),
                                {302, "{}", "Location: http://127.0.0.1:1/System/Info/Public\r\n"});
        JellyfinProviderAdapter jellyfin;
        ProviderServerValidator validator({&jellyfin});
        QVERIFY(!runValidation(validator, ProviderKind::Jellyfin, server.baseUrl(), QStringLiteral("abc")));

        server.responses.insert(QStringLiteral("/System/Info/Public"), {500, "{}", {}});
        QVERIFY(!runValidation(validator, ProviderKind::Jellyfin, server.baseUrl(), QStringLiteral("abc")));

        server.responses.insert(QStringLiteral("/System/Info/Public"),
                                {200, "{\"Id\":\"abc\",\"ServerName\":\"Den\"}", {}});
        const auto identity = runValidation(validator, ProviderKind::Jellyfin, server.baseUrl(),
                                            QStringLiteral("abc"));
        QVERIFY(identity);
        QCOMPARE(identity->serverName, QStringLiteral("Den"));
        // Not even Jellyfin's MediaBrowser client header.
        QVERIFY(!server.credentialHeaders);

        // No adapter for the kind.
        QVERIFY(!runValidation(validator, ProviderKind::Silo, server.baseUrl(), kSiloId));
    }

    void validatorBuffersAtMostItsLimit()
    {
        auto body = [](qsizetype bytes) {
            const QByteArray head = "{\"Id\":\"abc\",\"ServerName\":\"Den\",\"pad\":\"";
            const QByteArray tail = "\"}";
            return head + QByteArray(bytes - head.size() - tail.size(), 'x') + tail;
        };
        StubHttpServer server;
        JellyfinProviderAdapter jellyfin;
        ProviderServerValidator validator({&jellyfin});
        server.responses.insert(QStringLiteral("/System/Info/Public"),
                                {200, body(ProviderServerValidator::MaxResponseBytes - 1024), {}});
        QVERIFY(runValidation(validator, ProviderKind::Jellyfin, server.baseUrl(),
                              QStringLiteral("abc")));

        // A larger body is never read whole: the check fails.
        server.responses.insert(QStringLiteral("/System/Info/Public"),
                                {200, body(ProviderServerValidator::MaxResponseBytes + 4096), {}});
        QVERIFY(!runValidation(validator, ProviderKind::Jellyfin, server.baseUrl(),
                               QStringLiteral("abc")));
    }

    void validatorGivesUpOnASilentServer()
    {
        StubHttpServer server;
        server.responses.insert(QStringLiteral("/System/Info/Public"), {0, {}, {}});
        JellyfinProviderAdapter jellyfin;
        ProviderServerValidator validator({&jellyfin});
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(!runValidation(validator, ProviderKind::Jellyfin, server.baseUrl(),
                               QStringLiteral("abc")));
        // The deadline is a coarse timer, which may fire up to 5% early.
        QVERIFY(elapsed.elapsed() >= ProviderServerValidator::AttemptTimeoutMs * 9 / 10);
        QVERIFY(elapsed.elapsed() < ProviderServerValidator::AttemptTimeoutMs + 3000);
        QCOMPARE(server.requests, QStringList({QStringLiteral("/System/Info/Public")}));
    }

    // ---------- Discovery service ----------
    void searchRunsOnlyWhileActive()
    {
        ServiceFixture fixture;
        ServerDiscoveryService &service = fixture.service;
        QVERIFY(!service.isSearching());

        service.setActive(true);
        QVERIFY(service.isAvailable());
        QVERIFY(service.isSearching());
        QCOMPARE(fixture.mdns->starts, 1);
        QCOMPARE(fixture.mdns->queries, 1);
        QCOMPARE(fixture.jellyfin->queries, 1);

        fixture.mdns->announce(advert(ProviderKind::Silo, kSiloId, QStringLiteral("Den"),
                                      {QStringLiteral("http://192.168.1.20:8080")}));
        QCOMPARE(fixture.validator->pending.size(), 1);

        // The screen hides: sockets close and checks are canceled.
        service.setActive(false);
        QVERIFY(!service.isSearching());
        QVERIFY(!fixture.mdns->running);
        QVERIFY(!fixture.jellyfin->running);
        QCOMPARE(fixture.validator->cancels, 1);
        QVERIFY(fixture.validator->pending.isEmpty());

        // A late completion of the canceled check changes nothing.
        fixture.validator->canceled.at(0).done(ProviderServerIdentity{kSiloId, {}});
        QVERIFY(service.servers().isEmpty());

        // Adverts while inactive are ignored.
        fixture.mdns->announce(advert(ProviderKind::Silo, kSiloId, QStringLiteral("Den"),
                                      {QStringLiteral("http://192.168.1.20:8080")}));
        QVERIFY(fixture.validator->pending.isEmpty());
    }

    void searchWindowAndRequeriesAreBounded()
    {
        ServiceFixture fixture;
        ServerDiscoveryService::Timing timing;
        timing.searchWindowMs = 300;
        timing.queryOffsetsMs = {0, 50, 100};
        fixture.service.setTiming(timing);
        fixture.service.setActive(true);
        QTRY_COMPARE_WITH_TIMEOUT(fixture.mdns->queries, 3, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.service.isSearching(), 2000);
        QVERIFY(!fixture.mdns->running);
        QVERIFY(fixture.service.isActive());
        QTest::qWait(150);
        QCOMPARE(fixture.mdns->queries, 3);

        // refresh() starts another bounded search.
        fixture.service.refresh();
        QVERIFY(fixture.service.isSearching());
        QCOMPARE(fixture.mdns->starts, 2);
    }

    void serversAreDedupedByIdAndFirstValidAddressWins()
    {
        ServiceFixture fixture;
        ServerDiscoveryService &service = fixture.service;
        service.setActive(true);
        const QUrl lan(QStringLiteral("http://192.168.1.20:8080"));
        const QUrl v6(QStringLiteral("http://[2001:db8::20]:8080"));
        fixture.mdns->announce(advert(ProviderKind::Silo, kSiloId, QStringLiteral("Den"),
                                      {lan.toString(), v6.toString()}));
        QCOMPARE(fixture.validator->pending.size(), 2);

        // The second address answers first and is the one listed.
        QVERIFY(fixture.validator->complete(v6, ProviderServerIdentity{kSiloId, QStringLiteral("Den Silo")}));
        QCOMPARE(service.servers().size(), 1);
        QVERIFY(fixture.validator->complete(lan, ProviderServerIdentity{kSiloId, QStringLiteral("Den Silo")}));
        QCOMPARE(service.servers().size(), 1);
        const QVariantMap entry = service.servers().at(0).toMap();
        QCOMPARE(entry.value("url").toString(), QStringLiteral("http://[2001:db8::20]:8080"));
        QCOMPARE(entry.value("address").toString(), QStringLiteral("[2001:db8::20]:8080"));
        QCOMPARE(entry.value("name").toString(), QStringLiteral("Den Silo"));
        QCOMPARE(entry.value("provider").toString(), QStringLiteral("silo"));

        // The same server advertised again, at another address, adds nothing.
        fixture.mdns->announce(advert(ProviderKind::Silo, kSiloId, QStringLiteral("Den"),
                                      {QStringLiteral("http://10.0.0.5:8080")}));
        QVERIFY(fixture.validator->pending.isEmpty());

        // The same id from another provider is another server.
        fixture.jellyfin->announce(advert(ProviderKind::Jellyfin, kSiloId, QStringLiteral("JF"),
                                          {QStringLiteral("http://192.168.1.9:8096")}));
        QCOMPARE(fixture.validator->pending.size(), 1);
    }

    void sameIdFromAnotherOriginIsListedSeparately()
    {
        ServiceFixture fixture;
        ServerDiscoveryService &service = fixture.service;
        service.setActive(true);
        const QUrl real(QStringLiteral("http://192.168.1.5:8096"));
        const QUrl other(QStringLiteral("http://192.168.1.66:8096"));
        // Another host claims the same id first and confirms first.
        fixture.jellyfin->announce(advert(ProviderKind::Jellyfin, QStringLiteral("abc"),
                                          QStringLiteral("Spoof"), {other.toString()},
                                          QStringLiteral("192.168.1.66")));
        fixture.jellyfin->announce(advert(ProviderKind::Jellyfin, QStringLiteral("abc"),
                                          QStringLiteral("Den"), {real.toString()},
                                          QStringLiteral("192.168.1.5")));
        QVERIFY(fixture.validator->complete(other, ProviderServerIdentity{QStringLiteral("abc"), {}}));
        QVERIFY(fixture.validator->complete(real, ProviderServerIdentity{QStringLiteral("abc"), {}}));
        // Both hosts are listed; the first did not take the second's card.
        QCOMPARE(service.servers().size(), 2);
        QCOMPARE(service.servers().at(0).toMap().value("url").toString(), other.toString());
        QCOMPARE(service.servers().at(1).toMap().value("url").toString(), real.toString());
        QVERIFY(service.servers().at(0).toMap().value("key")
                != service.servers().at(1).toMap().value("key"));

        // A third origin pointing at an address already checked for this id
        // gets no second check, so no second card for it.
        fixture.jellyfin->announce(advert(ProviderKind::Jellyfin, QStringLiteral("abc"),
                                          QStringLiteral("Echo"),
                                          {real.toString(), QStringLiteral("http://192.168.1.77:8096")},
                                          QStringLiteral("192.168.1.77")));
        QCOMPARE(fixture.validator->pending.size(), 1);
        QVERIFY(fixture.validator->complete(QUrl(QStringLiteral("http://192.168.1.77:8096")),
                                            std::nullopt));
        QCOMPARE(service.servers().size(), 2);
    }

    void candidatesPerSenderAreCapped()
    {
        ServiceFixture fixture;
        fixture.service.setActive(true);
        // One sender announcing many servers queues at most its share.
        for (int i = 0; i < 20; ++i) {
            fixture.jellyfin->announce(advert(
                ProviderKind::Jellyfin, QStringLiteral("id-%1").arg(i), QStringLiteral("x"),
                {QStringLiteral("http://10.0.%1.1:8096").arg(i)}, QStringLiteral("192.168.1.66")));
        }
        int checked = 0;
        while (!fixture.validator->pending.isEmpty()) {
            fixture.validator->complete(fixture.validator->pending.constFirst().url, std::nullopt);
            ++checked;
        }
        QCOMPARE(checked, ServerDiscoveryService::MaxCandidatesPerSender);

        // Another sender still gets its candidates in.
        fixture.mdns->announce(advert(ProviderKind::Silo, kSiloId, QStringLiteral("Den"),
                                      {QStringLiteral("http://192.168.1.20:8080")},
                                      QStringLiteral("192.168.1.20")));
        QCOMPARE(fixture.validator->pending.size(), 1);
    }

    void savedServersAreMarked()
    {
        ServiceFixture fixture;
        ServerConnection saved;
        saved.providerKind = ProviderKind::Silo;
        saved.serverId = kSiloId;
        saved.baseUrl = QStringLiteral("http://192.168.1.20:8080");
        fixture.service.setSavedConnections([saved]() { return QList<ServerConnection>{saved}; });
        fixture.service.setActive(true);
        const QUrl home(QStringLiteral("http://192.168.1.20:8080"));
        const QUrl elsewhere(QStringLiteral("http://192.168.1.66:8080"));
        fixture.mdns->announce(advert(ProviderKind::Silo, kSiloId, QStringLiteral("Den"),
                                      {home.toString()}, QStringLiteral("192.168.1.20")));
        fixture.mdns->announce(advert(ProviderKind::Silo, kSiloId, QStringLiteral("Den"),
                                      {elsewhere.toString()}, QStringLiteral("192.168.1.66")));
        fixture.validator->complete(home, ProviderServerIdentity{kSiloId, {}});
        fixture.validator->complete(elsewhere, ProviderServerIdentity{kSiloId, {}});
        QCOMPARE(fixture.service.servers().size(), 2);
        QVERIFY(fixture.service.servers().at(0).toMap().value("saved").toBool());
        // The same id at another host is not the saved server.
        QVERIFY(!fixture.service.servers().at(1).toMap().value("saved").toBool());
    }

    void failedCandidatesAreDropped()
    {
        ServiceFixture fixture;
        ServerDiscoveryService &service = fixture.service;
        service.setActive(true);
        const QUrl published(QStringLiteral("http://192.168.0.85:8096"));
        const QUrl sender(QStringLiteral("http://192.168.40.181:8096"));
        fixture.jellyfin->announce(advert(ProviderKind::Jellyfin, QStringLiteral("abc"),
                                          QStringLiteral("tanime"),
                                          {published.toString(), sender.toString()}));
        QVERIFY(fixture.validator->complete(published, std::nullopt));
        QVERIFY(service.servers().isEmpty());
        QVERIFY(fixture.validator->complete(sender, ProviderServerIdentity{QStringLiteral("abc"), {}}));
        QCOMPARE(service.servers().size(), 1);
        // Without a name from the server the advertised one labels it.
        QCOMPARE(service.servers().at(0).toMap().value("name").toString(), QStringLiteral("tanime"));

        // An address that failed is not retried within the search.
        fixture.jellyfin->announce(advert(ProviderKind::Jellyfin, QStringLiteral("other"),
                                          QStringLiteral("x"), {published.toString()}));
        QCOMPARE(fixture.validator->pending.size(), 1);
        QVERIFY(fixture.validator->complete(published, std::nullopt));
        fixture.jellyfin->announce(advert(ProviderKind::Jellyfin, QStringLiteral("other"),
                                          QStringLiteral("x"), {published.toString()}));
        QVERIFY(fixture.validator->pending.isEmpty());
        QCOMPARE(service.servers().size(), 1);
    }

    void validationsInFlightAreBounded()
    {
        ServiceFixture fixture;
        fixture.service.setActive(true);
        QStringList urls;
        for (int i = 0; i < 6; ++i) {
            urls.append(QStringLiteral("http://192.168.1.%1:8096").arg(10 + i));
        }
        fixture.jellyfin->announce(advert(ProviderKind::Jellyfin, QStringLiteral("abc"),
                                          QStringLiteral("Den"), urls));
        QCOMPARE(fixture.validator->pending.size(), ServerDiscoveryService::MaxValidationsInFlight);
        QVERIFY(fixture.validator->complete(QUrl(urls.at(0)), std::nullopt));
        QCOMPARE(fixture.validator->pending.size(), ServerDiscoveryService::MaxValidationsInFlight);
        // Once listed, the server's queued addresses are dropped.
        QVERIFY(fixture.validator->complete(QUrl(urls.at(1)), ProviderServerIdentity{QStringLiteral("abc"), {}}));
        QCOMPARE(fixture.service.servers().size(), 1);
        QCOMPARE(fixture.validator->pending.size(), 3);
        while (!fixture.validator->pending.isEmpty()) {
            fixture.validator->complete(fixture.validator->pending.constFirst().url, std::nullopt);
        }
        QVERIFY(fixture.service.isSearching()); // the window is still open
    }

    void unavailableWithoutAnyBrowser()
    {
        auto *browser = new FakeBrowser(false);
        ServerDiscoveryService service({browser}, std::make_unique<FakeValidator>());
        service.setActive(true);
        QVERIFY(!service.isAvailable());
        QVERIFY(!service.isSearching());

        ServerDiscoveryService inert({}, nullptr);
        inert.setActive(true);
        QVERIFY(!inert.isAvailable());
        QVERIFY(!inert.isSearching());
        QVERIFY(inert.servers().isEmpty());
    }

    void reactivationStartsAFreshList()
    {
        ServiceFixture fixture;
        fixture.service.setActive(true);
        const QUrl url(QStringLiteral("http://192.168.1.20:8080"));
        fixture.mdns->announce(advert(ProviderKind::Silo, kSiloId, QStringLiteral("Den"), {url.toString()}));
        fixture.validator->complete(url, ProviderServerIdentity{kSiloId, {}});
        QCOMPARE(fixture.service.servers().size(), 1);
        QSignalSpy changed(&fixture.service, &ServerDiscoveryService::serversChanged);
        fixture.service.setActive(false);
        QVERIFY(fixture.service.servers().isEmpty());
        QCOMPARE(changed.size(), 1);
        fixture.service.setActive(true);
        QCOMPARE(fixture.mdns->starts, 2);
        // The same address is checked again in the new search.
        fixture.mdns->announce(advert(ProviderKind::Silo, kSiloId, QStringLiteral("Den"), {url.toString()}));
        QCOMPARE(fixture.validator->pending.size(), 1);
    }
};

QTEST_GUILESS_MAIN(ServerDiscoveryTest)
#include "ServerDiscoveryTest.moc"
