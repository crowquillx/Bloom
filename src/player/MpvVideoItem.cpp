#include "MpvVideoItem.h"

#include <QMutexLocker>
#include <QPointF>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

MpvVideoItem::MpvVideoItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(QQuickItem::ItemHasContents, true);
}

qulonglong MpvVideoItem::winId() const
{
    if (!window()) {
        return 0;
    }

    return static_cast<qulonglong>(window()->winId());
}

void MpvVideoItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry != oldGeometry) {
        emitViewportChanged();
    }
}

void MpvVideoItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);

    if (change == QQuickItem::ItemSceneChange || change == QQuickItem::ItemVisibleHasChanged) {
        emit winIdChanged();
        emitViewportChanged();
    }
}

void MpvVideoItem::setSoftwareFrame(const QImage &frame)
{
    {
        QMutexLocker locker(&m_frameMutex);
        m_softwareFrame = frame.convertToFormat(QImage::Format_RGBX8888);
    }
    update();
}

void MpvVideoItem::clearSoftwareFrame()
{
    {
        QMutexLocker locker(&m_frameMutex);
        m_softwareFrame = QImage();
    }
    update();
}

QSGNode *MpvVideoItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *updatePaintNodeData)
{
    Q_UNUSED(updatePaintNodeData);

    QImage frameCopy;
    {
        QMutexLocker locker(&m_frameMutex);
        frameCopy = m_softwareFrame;
    }

    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
    }

    if (!window()) {
        delete node;
        return nullptr;
    }

    node->setRect(boundingRect());

    if (frameCopy.isNull()) {
        frameCopy = QImage(1, 1, QImage::Format_RGBX8888);
        frameCopy.fill(Qt::black);
    }

    QSGTexture *texture = window()->createTextureFromImage(frameCopy, QQuickWindow::TextureIsOpaque);
    if (!texture || !texture->textureSize().isValid()) {
        delete texture;
        delete node;
        return nullptr;
    }

    texture->setFiltering(QSGTexture::Linear);
    node->setTexture(texture);
    return node;
}

void MpvVideoItem::emitViewportChanged()
{
    const QPointF topLeft = mapToScene(QPointF(0.0, 0.0));
    emit viewportChanged(topLeft.x(), topLeft.y(), width(), height());
}
