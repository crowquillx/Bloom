#include "MpvVideoItem.h"

#include <QPointF>
#include <QQuickWindow>

MpvVideoItem::MpvVideoItem(QQuickItem *parent)
    : QQuickItem(parent)
{
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

void MpvVideoItem::emitViewportChanged()
{
    const QPointF topLeft = mapToScene(QPointF(0.0, 0.0));
    emit viewportChanged(topLeft.x(), topLeft.y(), width(), height());
}
