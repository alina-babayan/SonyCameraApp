#ifndef LIVEVIEWPROVIDER_H
#define LIVEVIEWPROVIDER_H

#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>

class LiveViewProvider : public QQuickImageProvider
{
public:
    explicit LiveViewProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
        // A valid placeholder so QML never sees a null image and enters a failed state.
        // Once the first real frame arrives this is replaced immediately.
        m_frame = QImage(1280, 720, QImage::Format_RGB32);
        m_frame.fill(Qt::black);
    }

    void updateFrame(const QImage& frame)
    {
        QMutexLocker lock(&m_mutex);
        m_frame = frame;
    }

    // QML calls this every time the source URL changes (i.e. on every lvFrameSeq bump).
    // Returning a valid image here is what keeps live view updating — an empty/null
    // image causes QML to mark the provider as failed and stop requesting frames.
    QImage requestImage(const QString&, QSize* size, const QSize&) override
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