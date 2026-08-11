#include "UpdateManifestVerifier.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>

#include "optional/monocypher-ed25519.h"

#include <algorithm>
#include <optional>

namespace
{

constexpr auto activeKeyId = "bloom-update-2026-08";
constexpr auto activePublicKeyHex = "debd4cc08648f5f53490d8e0bf285bd7aa9285f69aa405317b9aa6ef97c11696";

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage)
    {
        *errorMessage = message;
    }
}

std::optional<QByteArray> strictBase64(const QString &text)
{
    const auto result = QByteArray::fromBase64Encoding(text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!result)
    {
        return std::nullopt;
    }
    return result.decoded;
}

} // namespace

QList<TrustedUpdateManifestKey> UpdateManifestVerifier::embeddedKeys()
{
    return {{QString::fromLatin1(activeKeyId), QByteArray::fromHex(QByteArrayLiteral(activePublicKeyHex))}};
}

bool UpdateManifestVerifier::verify(const QByteArray &envelope, QByteArray *payload, QString *errorMessage)
{
    return verify(envelope, embeddedKeys(), payload, errorMessage);
}

bool UpdateManifestVerifier::verify(const QByteArray &envelope, const QList<TrustedUpdateManifestKey> &trustedKeys,
                                    QByteArray *payload, QString *errorMessage)
{
    if (!payload)
    {
        setError(errorMessage, QObject::tr("Update manifest verification failed."));
        return false;
    }
    payload->clear();

    if (envelope.isEmpty() || envelope.size() > MaximumEnvelopeBytes)
    {
        setError(errorMessage, QObject::tr("Update manifest exceeds the allowed size."));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(envelope, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        setError(errorMessage, QObject::tr("Update manifest envelope is not valid JSON."));
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toInt() != 2)
    {
        setError(errorMessage, QObject::tr("Update manifest schema is not supported."));
        return false;
    }

    const QString keyId = root.value(QStringLiteral("key_id")).toString().trimmed();
    const auto trustedKey =
        std::find_if(trustedKeys.cbegin(), trustedKeys.cend(), [&keyId](const TrustedUpdateManifestKey &key)
                     { return key.keyId == keyId && key.publicKey.size() == 32; });
    if (trustedKey == trustedKeys.cend())
    {
        setError(errorMessage, QObject::tr("Update manifest was signed by an unknown key."));
        return false;
    }

    const auto decodedPayload = strictBase64(root.value(QStringLiteral("payload")).toString());
    const auto decodedSignature = strictBase64(root.value(QStringLiteral("signature")).toString());
    if (!decodedPayload.has_value() || decodedPayload->isEmpty() || decodedPayload->size() > MaximumPayloadBytes ||
        !decodedSignature.has_value() || decodedSignature->size() != 64)
    {
        setError(errorMessage, QObject::tr("Update manifest signature envelope is invalid."));
        return false;
    }

    const int verificationResult = crypto_ed25519_check(
        reinterpret_cast<const uint8_t *>(decodedSignature->constData()),
        reinterpret_cast<const uint8_t *>(trustedKey->publicKey.constData()),
        reinterpret_cast<const uint8_t *>(decodedPayload->constData()), static_cast<size_t>(decodedPayload->size()));
    if (verificationResult != 0)
    {
        setError(errorMessage, QObject::tr("Update manifest signature verification failed."));
        return false;
    }

    *payload = *decodedPayload;
    return true;
}
