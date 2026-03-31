#ifndef SONYCAMERA_H
#define SONYCAMERA_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QImage>
#include "CameraRemote_SDK.h"
#include "IDeviceCallback.h"

class SonyCamera : public QObject, public SCRSDK::IDeviceCallback
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool liveViewActive READ isLiveViewActive NOTIFY liveViewActiveChanged)

    Q_PROPERTY(QVariantList isoValues      READ isoValues      NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList shutterValues  READ shutterValues  NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList exposureValues READ exposureValues NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList sharpnessValues READ sharpnessValues NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList brightnessValues READ brightnessValues NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList imageSizeValues  READ imageSizeValues  NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList imageQualValues  READ imageQualValues  NOTIFY settingsChanged)

    Q_PROPERTY(quint64 currentISO       READ currentISO       NOTIFY settingsChanged)
    Q_PROPERTY(quint64 currentShutter   READ currentShutter   NOTIFY settingsChanged)
    Q_PROPERTY(quint64 currentExposure  READ currentExposure  NOTIFY settingsChanged)
    Q_PROPERTY(quint64 currentSharpness READ currentSharpness NOTIFY settingsChanged)
    Q_PROPERTY(quint64 currentBrightness READ currentBrightness NOTIFY settingsChanged)
    Q_PROPERTY(quint64 currentImageSize  READ currentImageSize  NOTIFY settingsChanged)
    Q_PROPERTY(quint64 currentImageQual  READ currentImageQual  NOTIFY settingsChanged)

    Q_PROPERTY(quint32 focusPosition    READ focusPosition    NOTIFY focusPositionChanged)
    Q_PROPERTY(quint32 focusMin         READ focusMin         NOTIFY focusRangeReady)
    Q_PROPERTY(quint32 focusMax         READ focusMax         NOTIFY focusRangeReady)
    Q_PROPERTY(bool    focusRangeValid  READ focusRangeValid  NOTIFY focusRangeReady)

public:
    explicit SonyCamera(QObject *parent = nullptr);
    ~SonyCamera();

    bool isConnected() const;
    bool isLiveViewActive() const { return m_liveViewActive; }

    QVariantList isoValues()       const { return m_isoValues; }
    QVariantList shutterValues()   const { return m_shutterValues; }
    QVariantList exposureValues()  const { return m_exposureValues; }
    QVariantList sharpnessValues() const { return m_sharpnessValues; }
    QVariantList brightnessValues()const { return m_brightnessValues; }
    QVariantList imageSizeValues() const { return m_imageSizeValues; }
    QVariantList imageQualValues() const { return m_imageQualValues; }

    quint64 currentISO()       const { return m_currentISO; }
    quint64 currentShutter()   const { return m_currentShutter; }
    quint64 currentExposure()  const { return m_currentExposure; }
    quint64 currentSharpness() const { return m_currentSharpness; }
    quint64 currentBrightness()const { return m_currentBrightness; }
    quint64 currentImageSize() const { return m_currentImageSize; }
    quint64 currentImageQual() const { return m_currentImageQual; }

    quint32 focusPosition()   const { return m_focusPosition; }
    quint32 focusMin()        const { return m_focusMin; }
    quint32 focusMax()        const { return m_focusMax; }
    bool    focusRangeValid() const { return m_focusRangeValid; }

    Q_INVOKABLE bool initialize();
    Q_INVOKABLE bool connectCamera();
    Q_INVOKABLE void takePhoto();
    Q_INVOKABLE void disconnectCamera();
    Q_INVOKABLE void shutdown();

    Q_INVOKABLE void startLiveView();
    Q_INVOKABLE void stopLiveView();

    Q_INVOKABLE void fetchFocusRange();
    Q_INVOKABLE void setFocusPosition(quint32 value);
    Q_INVOKABLE void setFocusDragging(bool dragging) { m_focusDragging = dragging; }

    Q_INVOKABLE void fetchAllSettings_() { fetchAllSettings(); }
    Q_INVOKABLE void setISO(quint64 value);
    Q_INVOKABLE void setShutterSpeed(quint64 value);
    Q_INVOKABLE void setExposure(quint64 value);
    Q_INVOKABLE void setSharpness(quint64 value);
    Q_INVOKABLE void setBrightness(quint64 value);
    Q_INVOKABLE void setWhiteBalance(quint32 value);
    Q_INVOKABLE void setImageSize(quint64 value);
    Q_INVOKABLE void setImageQual(quint64 value);

    Q_INVOKABLE QString formatISO(quint64 raw) const;
    Q_INVOKABLE QString formatShutter(quint64 raw) const;
    Q_INVOKABLE QString formatExposure(quint64 raw) const;
    Q_INVOKABLE QString formatBrightness(quint64 raw) const;
    Q_INVOKABLE QString formatImageSize(quint64 raw) const;
    Q_INVOKABLE QString formatImageQual(quint64 raw) const;

    Q_INVOKABLE void    fetchExifInfo();
    Q_INVOKABLE QString exifModel()        const { return m_exifModel; }
    Q_INVOKABLE QString exifLens()         const { return m_exifLens; }
    Q_INVOKABLE QString exifFocalLength()  const { return m_exifFocalLength; }
    Q_INVOKABLE QString exifAperture()     const { return m_exifAperture; }
    Q_INVOKABLE QString exifISO()          const { return m_exifISO; }
    Q_INVOKABLE QString exifShutter()      const { return m_exifShutter; }
    Q_INVOKABLE QString exifExposureMode() const { return m_exifExposureMode; }
    Q_INVOKABLE QString exifWhiteBalance() const { return m_exifWhiteBalance; }
    Q_INVOKABLE QString exifBatteryLevel() const { return m_exifBatteryLevel; }

signals:
    void connectionChanged(bool connected);
    void photoTaken(const QString& filePath);
    void errorOccurred(const QString& message);
    void logMessage(const QString& message);
    void settingsChanged();
    void exifReady();

    void liveViewActiveChanged(bool active);
    void liveViewFrameReady(const QImage& frame);

    // Fired with the post-view JPEG decoded in memory — arrives before the file
    // is written to disk, so the UI can show the shot with zero disk round-trip.
    void postViewFrameReady(const QImage& frame);

    void focusPositionChanged(quint32 position);
    void focusRangeReady();

public:

    void OnConnected(SCRSDK::DeviceConnectionVersioin version) override;
    void OnDisconnected(CrInt32u error) override;
    void OnPropertyChanged() override;
    void OnLvPropertyChanged() override;
    void OnCompleteDownload(CrChar* filename, CrInt32u type) override;
    void OnWarning(CrInt32u warning) override;
    void OnWarningExt(CrInt32u warning, CrInt32 p1, CrInt32 p2, CrInt32 p3) override;
    void OnError(CrInt32u error) override;

private:
    void setupSaveInfo();
    void fetchAllSettings();
    void fetchProperty(SCRSDK::CrDevicePropertyCode code,
                       QVariantList& outValues, quint64& outCurrent);
    bool setProperty(SCRSDK::CrDevicePropertyCode code, quint64 value);
    void pollLiveViewFrame();

    SCRSDK::CrDeviceHandle cameraHandle  = 0;
    bool m_connected      = false;
    bool m_sdkInitialized = false;
    bool m_liveViewActive = false;
    QString  m_savePath;
    std::wstring m_savePathW;

    QTimer* m_lvTimer = nullptr;
    bool    m_capturePending = false;  // true between shutter and post-view frame

    quint32 m_focusPosition  = 0;
    quint32 m_focusMin       = 0;
    quint32 m_focusMax       = 0;
    bool    m_focusRangeValid = false;
    bool    m_focusDragging  = false;

    QVariantList m_isoValues;
    QVariantList m_shutterValues;
    QVariantList m_exposureValues;
    QVariantList m_sharpnessValues;
    QVariantList m_brightnessValues;
    QVariantList m_imageSizeValues;
    QVariantList m_imageQualValues;

    quint64 m_currentISO        = 0;
    quint64 m_currentShutter    = 0;
    quint64 m_currentExposure   = 0;
    quint64 m_currentSharpness  = 0;
    quint64 m_currentBrightness = 0;
    quint64 m_currentImageSize  = 0;
    quint64 m_currentImageQual  = 0;

    QString m_exifModel;
    QString m_exifLens;
    QString m_exifFocalLength;
    QString m_exifAperture;
    QString m_exifISO;
    QString m_exifShutter;
    QString m_exifExposureMode;
    QString m_exifWhiteBalance;
    QString m_exifBatteryLevel;
};

#endif // SONYCAMERA_H