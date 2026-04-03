#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "sonycamera.h"
#include "liveviewprovider.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    LiveViewProvider* lvProvider = new LiveViewProvider();

    SonyCamera camera;
    camera.initialize();

    QObject::connect(&camera, &SonyCamera::liveViewFrameReady,
                     [lvProvider](const QImage& frame) {
                         lvProvider->updateFrame(frame);
                     });

    QQmlApplicationEngine engine;

    engine.addImageProvider("liveview", lvProvider);

    engine.rootContext()->setContextProperty("sonyCamera", &camera);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("SonyPhotoEditor", "Main");

    return app.exec();
}
