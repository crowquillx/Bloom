#include "network/discovery/DnsMessage.h"

#include <QHash>

namespace Bloom::Dns {

namespace {

QByteArray asciiLower(QByteArrayView bytes)
{
    QByteArray lowered(bytes.data(), bytes.size());
    for (char &character : lowered) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return lowered;
}

// Length-prefixed labels from @p first on, exact case: a compression key.
QByteArray suffixKey(const Name &name, qsizetype first)
{
    QByteArray key;
    for (qsizetype index = first; index < name.size(); ++index) {
        key.append(static_cast<char>(name.at(index).size()));
        key.append(name.at(index));
    }
    return key;
}

class Writer
{
public:
    QByteArray out;
    bool ok = true;

    void u8(quint8 value) { out.append(static_cast<char>(value)); }
    void u16(quint16 value)
    {
        u8(static_cast<quint8>(value >> 8));
        u8(static_cast<quint8>(value & 0xff));
    }
    void u32(quint32 value)
    {
        u16(static_cast<quint16>(value >> 16));
        u16(static_cast<quint16>(value & 0xffff));
    }

    void name(const Name &labels)
    {
        int wireBytes = 1;
        for (const QByteArray &label : labels) {
            wireBytes += 1 + static_cast<int>(label.size());
            if (label.isEmpty() || label.size() > MaxLabelBytes) {
                ok = false;
                return;
            }
        }
        if (wireBytes > MaxNameBytes) {
            ok = false;
            return;
        }
        for (qsizetype index = 0; index < labels.size(); ++index) {
            const QByteArray key = suffixKey(labels, index);
            const auto known = m_suffixes.constFind(key);
            if (known != m_suffixes.constEnd()) {
                u16(static_cast<quint16>(0xc000 | known.value()));
                return;
            }
            // Pointers carry 14 bits of offset.
            if (out.size() < 0x3fff) {
                m_suffixes.insert(key, static_cast<int>(out.size()));
            }
            u8(static_cast<quint8>(labels.at(index).size()));
            out.append(labels.at(index));
        }
        u8(0);
    }

    void question(const Question &question)
    {
        name(question.name);
        u16(question.type);
        u16(static_cast<quint16>(question.qclass
                                 | (question.unicastResponse ? ClassTopBit : 0)));
    }

    void record(const Record &record)
    {
        name(record.name);
        u16(record.type);
        u16(static_cast<quint16>(record.rrclass | (record.cacheFlush ? ClassTopBit : 0)));
        u32(record.ttl);
        const qsizetype lengthAt = out.size();
        u16(0);
        const qsizetype dataStart = out.size();
        switch (record.type) {
        case TypePTR:
            name(record.target);
            break;
        case TypeSRV:
            u16(record.priority);
            u16(record.weight);
            u16(record.port);
            name(record.target);
            break;
        case TypeTXT:
            if (record.txt.isEmpty()) {
                // An empty TXT record holds one empty string (RFC 6763 §6.1).
                u8(0);
            }
            for (const QByteArray &string : record.txt) {
                if (string.size() > 255) {
                    ok = false;
                    return;
                }
                u8(static_cast<quint8>(string.size()));
                out.append(string);
            }
            break;
        case TypeA: {
            bool isIPv4 = false;
            const quint32 address = record.address.toIPv4Address(&isIPv4);
            if (!isIPv4) {
                ok = false;
                return;
            }
            u32(address);
            break;
        }
        case TypeAAAA: {
            if (record.address.protocol() != QAbstractSocket::IPv6Protocol) {
                ok = false;
                return;
            }
            const Q_IPV6ADDR address = record.address.toIPv6Address();
            out.append(reinterpret_cast<const char *>(address.c), 16);
            break;
        }
        default:
            ok = false;
            return;
        }
        const qsizetype length = out.size() - dataStart;
        if (length > 0xffff) {
            ok = false;
            return;
        }
        out[lengthAt] = static_cast<char>((length >> 8) & 0xff);
        out[lengthAt + 1] = static_cast<char>(length & 0xff);
    }

private:
    QHash<QByteArray, int> m_suffixes;
};

class Reader
{
public:
    explicit Reader(const QByteArray &packet)
        : m_data(reinterpret_cast<const quint8 *>(packet.constData()))
        , m_size(static_cast<int>(packet.size()))
    {
    }

    bool u16(int &position, quint16 &value) const
    {
        if (position + 2 > m_size) {
            return false;
        }
        value = static_cast<quint16>((m_data[position] << 8) | m_data[position + 1]);
        position += 2;
        return true;
    }

    bool u32(int &position, quint32 &value) const
    {
        quint16 high = 0;
        quint16 low = 0;
        if (!u16(position, high) || !u16(position, low)) {
            return false;
        }
        value = (static_cast<quint32>(high) << 16) | low;
        return true;
    }

    // Reads a possibly compressed name at @p position and moves it past the
    // name's bytes at that position. Every pointer must point before the
    // sequence it was found in, so targets strictly decrease and a pointer
    // loop cannot form; at most MaxPointerHops pointers are followed.
    std::optional<Name> name(int &position) const
    {
        Name labels;
        int cursor = position;
        int sequenceStart = position;
        bool jumped = false;
        int hops = 0;
        int wireBytes = 1;
        while (true) {
            if (cursor >= m_size) {
                return std::nullopt;
            }
            const quint8 length = m_data[cursor];
            if (length == 0) {
                if (!jumped) {
                    position = cursor + 1;
                }
                return labels;
            }
            if ((length & 0xc0) == 0xc0) {
                if (cursor + 1 >= m_size) {
                    return std::nullopt;
                }
                const int target = ((length & 0x3f) << 8) | m_data[cursor + 1];
                if (target >= sequenceStart || ++hops > MaxPointerHops) {
                    return std::nullopt;
                }
                if (!jumped) {
                    position = cursor + 2;
                    jumped = true;
                }
                sequenceStart = target;
                cursor = target;
                continue;
            }
            if ((length & 0xc0) != 0) {
                return std::nullopt; // reserved label types
            }
            wireBytes += 1 + length;
            if (wireBytes > MaxNameBytes || cursor + 1 + length > m_size) {
                return std::nullopt;
            }
            labels.append(QByteArray(reinterpret_cast<const char *>(m_data + cursor + 1),
                                     length));
            cursor += 1 + length;
        }
    }

    std::optional<Question> question(int &position) const
    {
        Question question;
        auto name = this->name(position);
        quint16 qclass = 0;
        if (!name || !u16(position, question.type) || !u16(position, qclass)) {
            return std::nullopt;
        }
        question.name = *name;
        question.unicastResponse = (qclass & ClassTopBit) != 0;
        question.qclass = qclass & ~ClassTopBit;
        return question;
    }

    std::optional<Record> record(int &position) const
    {
        Record record;
        auto name = this->name(position);
        quint16 rrclass = 0;
        quint16 dataLength = 0;
        if (!name || !u16(position, record.type) || !u16(position, rrclass)
            || !u32(position, record.ttl) || !u16(position, dataLength)) {
            return std::nullopt;
        }
        record.name = *name;
        record.cacheFlush = (rrclass & ClassTopBit) != 0;
        record.rrclass = rrclass & ~ClassTopBit;
        const int dataEnd = position + dataLength;
        if (dataEnd > m_size) {
            return std::nullopt;
        }
        int cursor = position;
        switch (record.type) {
        case TypePTR: {
            auto target = this->name(cursor);
            if (!target || cursor != dataEnd) {
                return std::nullopt;
            }
            record.target = *target;
            break;
        }
        case TypeSRV: {
            if (!u16(cursor, record.priority) || !u16(cursor, record.weight)
                || !u16(cursor, record.port)) {
                return std::nullopt;
            }
            auto target = this->name(cursor);
            if (!target || cursor != dataEnd) {
                return std::nullopt;
            }
            record.target = *target;
            break;
        }
        case TypeTXT:
            while (cursor < dataEnd) {
                const quint8 length = m_data[cursor];
                if (cursor + 1 + length > dataEnd) {
                    return std::nullopt;
                }
                record.txt.append(QByteArray(
                    reinterpret_cast<const char *>(m_data + cursor + 1), length));
                cursor += 1 + length;
            }
            break;
        case TypeA: {
            quint32 address = 0;
            if (dataLength != 4 || !u32(cursor, address)) {
                return std::nullopt;
            }
            record.address = QHostAddress(address);
            break;
        }
        case TypeAAAA: {
            if (dataLength != 16) {
                return std::nullopt;
            }
            record.address = QHostAddress(m_data + cursor);
            break;
        }
        default:
            break; // not read; skipped whole
        }
        position = dataEnd;
        return record;
    }

private:
    const quint8 *m_data;
    int m_size;
};

} // namespace

Name nameFromDotted(QByteArrayView dotted)
{
    Name labels;
    for (const QByteArray &label : QByteArray(dotted.data(), dotted.size()).split('.')) {
        if (!label.isEmpty()) {
            labels.append(label);
        }
    }
    return labels;
}

bool namesEqual(const Name &a, const Name &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (qsizetype index = 0; index < a.size(); ++index) {
        if (asciiLower(a.at(index)) != asciiLower(b.at(index))) {
            return false;
        }
    }
    return true;
}

bool isDirectChild(const Name &name, const Name &parent)
{
    return name.size() == parent.size() + 1 && namesEqual(name.mid(1), parent);
}

QByteArray nameKey(const Name &name)
{
    QByteArray key;
    for (const QByteArray &label : name) {
        key.append(static_cast<char>(label.size()));
        key.append(asciiLower(label));
    }
    return key;
}

QByteArray encode(const Message &message)
{
    Writer writer;
    writer.u16(message.id);
    writer.u16(message.flags);
    const qsizetype counts[] = {message.questions.size(), message.answers.size(),
                                message.authorities.size(), message.additionals.size()};
    for (const qsizetype count : counts) {
        if (count > 0xffff) {
            return {};
        }
        writer.u16(static_cast<quint16>(count));
    }
    for (const Question &question : message.questions) {
        writer.question(question);
    }
    for (const QList<Record> *section :
         {&message.answers, &message.authorities, &message.additionals}) {
        for (const Record &record : *section) {
            writer.record(record);
        }
    }
    return writer.ok ? writer.out : QByteArray();
}

QByteArray encodeQuery(const QList<Question> &questions, const QList<Record> &knownAnswers)
{
    Message message;
    message.questions = questions;
    message.answers = knownAnswers;
    return encode(message);
}

std::optional<Message> decode(const QByteArray &packet)
{
    const Reader reader(packet);
    int position = 0;
    Message message;
    quint16 counts[4] = {};
    if (!reader.u16(position, message.id) || !reader.u16(position, message.flags)) {
        return std::nullopt;
    }
    int entries = 0;
    for (quint16 &count : counts) {
        if (!reader.u16(position, count)) {
            return std::nullopt;
        }
        entries += count;
    }
    if (entries > MaxEntriesPerMessage) {
        return std::nullopt;
    }
    for (quint16 index = 0; index < counts[0]; ++index) {
        auto question = reader.question(position);
        if (!question) {
            return std::nullopt;
        }
        message.questions.append(*question);
    }
    QList<Record> *sections[] = {&message.answers, &message.authorities, &message.additionals};
    for (int section = 0; section < 3; ++section) {
        for (quint16 index = 0; index < counts[section + 1]; ++index) {
            auto record = reader.record(position);
            if (!record) {
                return std::nullopt;
            }
            sections[section]->append(*record);
        }
    }
    return message;
}

QMap<QByteArray, QByteArray> txtAttributes(const QByteArrayList &strings)
{
    QMap<QByteArray, QByteArray> attributes;
    for (const QByteArray &string : strings) {
        if (string.isEmpty() || string.startsWith('=')) {
            continue;
        }
        const qsizetype equals = string.indexOf('=');
        const QByteArray key = asciiLower(equals < 0 ? string : string.left(equals));
        if (!attributes.contains(key)) {
            attributes.insert(key, equals < 0 ? QByteArray() : string.mid(equals + 1));
        }
    }
    return attributes;
}

} // namespace Bloom::Dns
