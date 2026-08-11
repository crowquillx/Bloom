#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>

class SoftwareRoundedImage : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(QQuickItem *sourceItem READ sourceItem WRITE setSourceItem
                   NOTIFY sourceItemChanged)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)

public:
    explicit SoftwareRoundedImage(QQuickItem *parent = nullptr);

    QQuickItem *sourceItem() const { return m_sourceItem.data(); }
    void setSourceItem(QQuickItem *sourceItem);

    qreal radius() const { return m_radius; }
    void setRadius(qreal radius);

    bool isReady() const { return m_ready; }

    Q_INVOKABLE void requestRefresh();
    void paint(QPainter *painter) override;

protected:
    void itemChange(ItemChange change, const ItemChangeData &value) override;

signals:
    void sourceItemChanged();
    void radiusChanged();
    void readyChanged();

private:
    void attemptGrab();
    void scheduleRetry();
    void setReady(bool ready);

    QPointer<QQuickItem> m_sourceItem;
    QTimer m_retryTimer;
    QImage m_image;
    qreal m_radius = 0.0;
    quint64 m_generation = 0;
    int m_retryCount = 0;
    bool m_ready = false;

    static constexpr int MaximumGrabRetries = 4;
};
