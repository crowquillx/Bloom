#include "SoftwareRoundedImage.h"

#include <QPainter>
#include <QPainterPath>
#include <QQuickItemGrabResult>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>

SoftwareRoundedImage::SoftwareRoundedImage(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setOpaquePainting(false);
    setRenderTarget(QQuickPaintedItem::Image);

    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(32);
    connect(&m_retryTimer, &QTimer::timeout,
            this, &SoftwareRoundedImage::attemptGrab);
    connect(this, &QQuickItem::widthChanged,
            this, &SoftwareRoundedImage::requestRefresh);
    connect(this, &QQuickItem::heightChanged,
            this, &SoftwareRoundedImage::requestRefresh);
    connect(this, &QQuickItem::windowChanged,
            this, [this]() { requestRefresh(); });
}

void SoftwareRoundedImage::setSourceItem(QQuickItem *sourceItem)
{
    if (m_sourceItem == sourceItem) {
        return;
    }
    if (m_sourceItem) {
        disconnect(m_sourceItem, nullptr, this, nullptr);
    }
    m_sourceItem = sourceItem;
    if (m_sourceItem) {
        connect(m_sourceItem, &QObject::destroyed, this, [this]() {
            m_sourceItem = nullptr;
            requestRefresh();
            emit sourceItemChanged();
        });
    }
    emit sourceItemChanged();
    requestRefresh();
}

void SoftwareRoundedImage::setRadius(qreal radius)
{
    const qreal normalized = std::max<qreal>(0.0, radius);
    if (qFuzzyCompare(m_radius, normalized)) {
        return;
    }
    m_radius = normalized;
    emit radiusChanged();
    update();
}

void SoftwareRoundedImage::requestRefresh()
{
    ++m_generation;
    m_retryTimer.stop();
    m_image = {};
    setReady(false);
    update();
    m_retryTimer.start(0);
}

void SoftwareRoundedImage::attemptGrab()
{
    if (!m_sourceItem) {
        return;
    }
    // Size changes already schedule a refresh. Avoid polling forever for a
    // zero-sized delegate; only the transient lack of a scene window needs a
    // bounded-delay retry.
    if (width() <= 0.0 || height() <= 0.0) {
        return;
    }
    if (!m_sourceItem->window()) {
        m_retryTimer.start();
        return;
    }

    const QSize targetSize(
        std::max(1, int(std::ceil(width()))),
        std::max(1, int(std::ceil(height()))));
    const quint64 generation = m_generation;
    const QSharedPointer<QQuickItemGrabResult> result =
        m_sourceItem->grabToImage(targetSize);
    if (!result) {
        m_retryTimer.start();
        return;
    }

    connect(result.data(), &QQuickItemGrabResult::ready, this,
            [this, result, generation]() {
        if (generation != m_generation || !m_sourceItem) {
            return;
        }
        const QImage image = result->image();
        if (image.isNull()) {
            m_retryTimer.start();
            return;
        }
        m_image = image;
        setReady(true);
        update();
    });
}

void SoftwareRoundedImage::paint(QPainter *painter)
{
    painter->setCompositionMode(QPainter::CompositionMode_Source);
    painter->fillRect(boundingRect(), Qt::transparent);
    if (!m_ready || m_image.isNull()) {
        return;
    }

    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter->setRenderHint(QPainter::Antialiasing, true);
    const qreal clampedRadius = std::clamp(
        m_radius, 0.0, std::min(width(), height()) / 2.0);
    QPainterPath clipPath;
    clipPath.addRoundedRect(boundingRect(), clampedRadius, clampedRadius);
    painter->setClipPath(clipPath);
    painter->drawImage(boundingRect(), m_image);
}

void SoftwareRoundedImage::setReady(bool ready)
{
    if (m_ready == ready) {
        return;
    }
    m_ready = ready;
    emit readyChanged();
}
