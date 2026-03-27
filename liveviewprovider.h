#ifndef LIVEVIEWPROVIDER_H
#define LIVEVIEWPROVIDER_H

#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>

class LiveViewProvider : public QQuickImageProvider
{
public:
    explicit LiveViewProvider()
        : QQuickImageProvider(QQuickImageProvider::Image) {}

    void updateFrame(const QImage& frame)
    {
        QMutexLocker lock(&m_mutex);
        m_frame = frame;
    }


    QImage requestImage(const QString&, QSize* size, const QSize& ) override
    {
        QMutexLocker lock(&m_mutex);
        if (size) *size = m_frame.size();
        return m_frame;
    }

private:
    QMutex m_mutex;
    QImage m_frame;
};

#endif // LIVEVIEWPROVIDER_H