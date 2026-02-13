#pragma once

#include <QQuickItem>
#include <QRectF>
#include <qqmlintegration.h>

class MpvVideoItem : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MpvVideoItem)

public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);

signals:
    void viewportChanged(qreal x, qreal y, qreal width, qreal height);

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void emitViewportChanged();
};
