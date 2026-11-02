#pragma once

#include <QByteArray>
#include <QByteArrayList>
#include <QHostAddress>
#include <QList>
#include <QMap>

#include <optional>

/**
 * @brief The DNS wire format as multicast DNS service discovery uses it
 * (RFC 1035, RFC 6762, RFC 6763), Qt Core only.
 *
 * Only the record types DNS-SD browsing reads are decoded (PTR, SRV, TXT, A,
 * AAAA); other records are skipped. Parsing is bounded and rejects any
 * malformed packet whole: truncated sections, label or name length overruns,
 * and compression pointers that do not point strictly backwards.
 */
namespace Bloom::Dns {

/// A domain name as its labels, so a dot inside a DNS-SD instance label
/// ("Den.v2") stays part of that label.
using Name = QByteArrayList;

enum RecordType : quint16 {
    TypeA = 1,
    TypePTR = 12,
    TypeTXT = 16,
    TypeAAAA = 28,
    TypeSRV = 33,
    TypeANY = 255,
};

inline constexpr quint16 ClassIN = 1;
/// Top bit of a question's class: the querier asks for a unicast reply
/// (RFC 6762 §5.4). Top bit of a record's class: cache flush (§10.2).
inline constexpr quint16 ClassTopBit = 0x8000;
inline constexpr quint16 FlagResponse = 0x8000;
inline constexpr quint16 FlagAuthoritative = 0x0400;

inline constexpr int MaxLabelBytes = 63;
inline constexpr int MaxNameBytes = 255;

/// "_silo._tcp.local" as labels. For fixed names only: no escapes.
Name nameFromDotted(QByteArrayView dotted);
/// ASCII case-insensitive name equality (RFC 6762 §16).
bool namesEqual(const Name &a, const Name &b);
/// True when @p name is one label under @p parent.
bool isDirectChild(const Name &name, const Name &parent);
/// A map key that compares names case-insensitively.
QByteArray nameKey(const Name &name);

struct Question {
    Name name;
    quint16 type = TypePTR;
    quint16 qclass = ClassIN;
    bool unicastResponse = false;
};

struct Record {
    Name name;
    quint16 type = 0;
    quint16 rrclass = ClassIN; // without the cache-flush bit
    bool cacheFlush = false;
    quint32 ttl = 0;
    // Decoded data for the types browsing reads.
    Name target;                 // PTR and SRV
    quint16 priority = 0;        // SRV
    quint16 weight = 0;          // SRV
    quint16 port = 0;            // SRV
    QByteArrayList txt;          // TXT character-strings
    QHostAddress address;        // A and AAAA
};

struct Message {
    quint16 id = 0;
    quint16 flags = 0;
    QList<Question> questions;
    QList<Record> answers;
    QList<Record> authorities;
    QList<Record> additionals;

    bool isResponse() const { return (flags & FlagResponse) != 0; }
};

/// Encodes @p message, compressing repeated name suffixes in owner names and
/// PTR/SRV targets. Returns an empty array when a name or label is too long
/// or a record type cannot be encoded.
QByteArray encode(const Message &message);

/// A standard mDNS query (id 0, no flags) for @p questions, with
/// @p knownAnswers in the answer section (RFC 6762 §7.1).
QByteArray encodeQuery(const QList<Question> &questions,
                       const QList<Record> &knownAnswers = {});

std::optional<Message> decode(const QByteArray &packet);

/// DNS-SD TXT attributes (RFC 6763 §6.3, §6.4): keys are lower-cased, the
/// first occurrence of a key wins, empty strings and strings starting with
/// '=' are ignored, and a key without '=' maps to an empty value.
QMap<QByteArray, QByteArray> txtAttributes(const QByteArrayList &strings);

} // namespace Bloom::Dns
