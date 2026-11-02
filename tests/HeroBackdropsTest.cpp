#include <QtTest/QtTest>

#include "models/HeroBackdrops.h"
#include "models/MediaModels.h"

namespace {

QVariantMap backdrop(const QString &itemId, int index, const QString &tag,
                     Bloom::ArtworkOwnerKind ownerKind = Bloom::ArtworkOwnerKind::MediaItem)
{
    Bloom::ArtworkRef ref;
    ref.connectionId = QStringLiteral("connection-1");
    ref.itemId = itemId;
    ref.kind = Bloom::ArtworkKind::Backdrop;
    ref.ownerKind = ownerKind;
    ref.index = index;
    ref.tag = tag;
    return ref.toVariantMap();
}

QStringList tags(const QVariantList &refs)
{
    QStringList result;
    for (const QVariant &value : refs) {
        result.append(value.toMap().value(QStringLiteral("tag")).toString());
    }
    return result;
}

} // namespace

class HeroBackdropsTest : public QObject
{
    Q_OBJECT

private slots:
    void mainBackdropFirstThenTheOthersByIndex();
    void otherOwnersKindsAndRepeatsAreLeftOut();
    void limitAndMissingMainBackdrop();
};

void HeroBackdropsTest::mainBackdropFirstThenTheOthersByIndex()
{
    const QVariantMap main = backdrop(QStringLiteral("movie-1"), 0, QStringLiteral("b0"));
    const QVariantMap item{
        {QStringLiteral("backdropArtwork"), main},
        {QStringLiteral("artwork"), QVariantList{
             backdrop(QStringLiteral("movie-1"), 2, QStringLiteral("b2")),
             main,
             backdrop(QStringLiteral("movie-1"), 1, QStringLiteral("b1"))}}};
    QCOMPARE(tags(Bloom::HeroBackdrops::refs(item)),
             (QStringList{QStringLiteral("b0"), QStringLiteral("b1"), QStringLiteral("b2")}));

    // One backdrop: just the main one, as before.
    const QVariantMap single{
        {QStringLiteral("backdropArtwork"), main},
        {QStringLiteral("artwork"), QVariantList{main}}};
    QCOMPARE(tags(Bloom::HeroBackdrops::refs(single)), QStringList{QStringLiteral("b0")});
}

void HeroBackdropsTest::otherOwnersKindsAndRepeatsAreLeftOut()
{
    const QVariantMap main = backdrop(QStringLiteral("movie-1"), 0, QStringLiteral("b0"));
    Bloom::ArtworkRef thumb = Bloom::ArtworkRef::fromVariantMap(main);
    thumb.kind = Bloom::ArtworkKind::Thumb;
    thumb.tag = QStringLiteral("thumb");
    const QVariantMap item{
        {QStringLiteral("backdropArtwork"), main},
        {QStringLiteral("artwork"), QVariantList{
             main,
             // Another item's backdrop (an episode's series backdrop).
             backdrop(QStringLiteral("series-1"), 1, QStringLiteral("series")),
             // A collection owning the same item ID.
             backdrop(QStringLiteral("movie-1"), 1, QStringLiteral("owned"),
                      Bloom::ArtworkOwnerKind::Collection),
             thumb.toVariantMap(),
             // The same image at a second position is shown once.
             backdrop(QStringLiteral("movie-1"), 1, QStringLiteral("b0")),
             backdrop(QStringLiteral("movie-1"), 2, QStringLiteral("b2")),
             backdrop(QStringLiteral("movie-1"), 2, QStringLiteral("b2"))}}};
    QCOMPARE(tags(Bloom::HeroBackdrops::refs(item)),
             (QStringList{QStringLiteral("b0"), QStringLiteral("b2")}));
}

void HeroBackdropsTest::limitAndMissingMainBackdrop()
{
    QVariantList artwork;
    for (int index = 0; index < 12; ++index) {
        artwork.append(backdrop(QStringLiteral("movie-1"), index,
                                QStringLiteral("b%1").arg(index)));
    }
    const QVariantMap item{
        {QStringLiteral("backdropArtwork"), artwork.first()},
        {QStringLiteral("artwork"), artwork}};
    QCOMPARE(Bloom::HeroBackdrops::refs(item).size(), qsizetype(Bloom::HeroBackdrops::kMaxBackdrops));
    QCOMPARE(tags(Bloom::HeroBackdrops::refs(item, 3)),
             (QStringList{QStringLiteral("b0"), QStringLiteral("b1"), QStringLiteral("b2")}));

    // Without a main backdrop (a poster fallback) nothing cycles.
    QVERIFY(Bloom::HeroBackdrops::refs(QVariantMap{{QStringLiteral("artwork"), artwork}}).isEmpty());
}

QTEST_GUILESS_MAIN(HeroBackdropsTest)
#include "HeroBackdropsTest.moc"
