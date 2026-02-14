#pragma once

#include <QQuickItem>
#include <QRectF>
#include <qqmlintegration.h>

class MpvVideoItem : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MpvVideoItem)
    Q_PROPERTY(qulonglong winId READ winId NOTIFY winIdChanged)

public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);

    qulonglong winId() const;

signals:
    void viewportChanged(qreal x, qreal y, qreal width, qreal height);
    void winIdChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void emitViewportChanged();
};
