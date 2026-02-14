#pragma once

#include <QQuickItem>
#include <QRectF>
#include <QImage>
#include <QMutex>
#include <qqmlintegration.h>

class MpvVideoItem : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MpvVideoItem)
    Q_PROPERTY(qulonglong winId READ winId NOTIFY winIdChanged)

public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);

    qulonglong winId() const;
    Q_INVOKABLE void setSoftwareFrame(const QImage &frame);
    Q_INVOKABLE void clearSoftwareFrame();

signals:
    void viewportChanged(qreal x, qreal y, qreal width, qreal height);
    void winIdChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *updatePaintNodeData) override;

private:
    void emitViewportChanged();
    mutable QMutex m_frameMutex;
    QImage m_softwareFrame;
};
