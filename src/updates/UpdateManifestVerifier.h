#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

struct TrustedUpdateManifestKey
{
    QString keyId;
    QByteArray publicKey;
};

class UpdateManifestVerifier
{
  public:
    static constexpr qsizetype MaximumEnvelopeBytes = 1024 * 1024;
    static constexpr qsizetype MaximumPayloadBytes = 768 * 1024;

    static QList<TrustedUpdateManifestKey> embeddedKeys();

    static bool verify(const QByteArray &envelope, QByteArray *payload, QString *errorMessage = nullptr);
    static bool verify(const QByteArray &envelope, const QList<TrustedUpdateManifestKey> &trustedKeys,
                       QByteArray *payload, QString *errorMessage = nullptr);
};
