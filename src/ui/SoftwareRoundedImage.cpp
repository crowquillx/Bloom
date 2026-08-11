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
        connect(m_sourceItem, &QQuickItem::windowChanged, this,
                [this](QQuickWindow *window) {
            if (window) {
                requestRefresh();
            }
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
    m_retryCount = 0;
    m_retryTimer.stop();
    m_image = {};
    setReady(false);
    update();
    m_retryTimer.start(0);
}

void SoftwareRoundedImage::itemChange(ItemChange change,
                                      const ItemChangeData &value)
{
    QQuickPaintedItem::itemChange(change, value);
    if (change == ItemDevicePixelRatioHasChanged) {
        requestRefresh();
    }
}

void SoftwareRoundedImage::attemptGrab()
{
    if (!m_sourceItem) {
        return;
    }
    // Size and window changes schedule a refresh. A detached source therefore
    // waits for windowChanged instead of polling the UI thread indefinitely.
    if (width() <= 0.0 || height() <= 0.0) {
        return;
    }
    if (!m_sourceItem->window()) {
        return;
    }

    const qreal devicePixelRatio = std::max<qreal>(
        1.0, m_sourceItem->window()->effectiveDevicePixelRatio());
    const QSize targetSize(
        std::max(1, int(std::ceil(width() * devicePixelRatio))),
        std::max(1, int(std::ceil(height() * devicePixelRatio))));
    const quint64 generation = m_generation;
    const QSharedPointer<QQuickItemGrabResult> result =
        m_sourceItem->grabToImage(targetSize);
    if (!result) {
        scheduleRetry();
        return;
    }

    connect(result.data(), &QQuickItemGrabResult::ready, this,
            [this, result, generation]() {
        if (generation != m_generation || !m_sourceItem) {
            return;
        }
        const QImage image = result->image();
        if (image.isNull()) {
            scheduleRetry();
            return;
        }
        m_image = image;
        setReady(true);
        update();
    });
}

void SoftwareRoundedImage::scheduleRetry()
{
    if (!m_sourceItem || !m_sourceItem->window()
        || m_retryCount >= MaximumGrabRetries) {
        return;
    }
    ++m_retryCount;
    m_retryTimer.start();
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
