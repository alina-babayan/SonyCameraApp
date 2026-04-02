#include "sonycamera.h"
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QTimer>
#include <QThreadPool>
#include <QThread>
#include <QDateTime>

SonyCamera::SonyCamera(QObject *parent) : QObject(parent)
{
    m_savePath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/SonyCaptures";
    QDir().mkpath(m_savePath);
    m_savePathLinux = m_savePath.toStdString();
    qDebug() << "Photos will be saved to:" << m_savePath;

    m_lvTimer = new QTimer(this);
    m_lvTimer->setInterval(33);
    connect(m_lvTimer, &QTimer::timeout, this, &SonyCamera::pollLiveViewFrame);
}

SonyCamera::~SonyCamera() { shutdown(); }

bool SonyCamera::initialize()
{
    if (m_sdkInitialized) return true;
    qDebug() << "Initializing Sony SDK...";
    if (!SCRSDK::Init()) { emit errorOccurred("SDK initialization failed!"); return false; }
    m_sdkInitialized = true;
    qDebug() << "Sony SDK initialized.";
    return true;
}

bool SonyCamera::connectCamera()
{
    if (m_connected || m_connecting) return true;
    if (!m_sdkInitialized && !initialize()) return false;

    m_connecting = true;
    emit connectingChanged(true);
    emit logMessage("Scanning for camera...");

    QThreadPool::globalInstance()->start([this]() {
        QThread::msleep(300);

        SCRSDK::ICrEnumCameraObjectInfo* cameraList = nullptr;
        SCRSDK::CrError result = SCRSDK::EnumCameraObjects(&cameraList, 5);

        if (result != SCRSDK::CrError_None || !cameraList) {
            QMetaObject::invokeMethod(this, [this]() {
                m_connecting = false;
                emit connectingChanged(false);
                emit errorOccurred("Camera enumeration failed.");
            }, Qt::QueuedConnection);
            return;
        }

        CrInt32u count = cameraList->GetCount();
        if (count == 0) {
            cameraList->Release();
            QMetaObject::invokeMethod(this, [this]() {
                m_connecting = false;
                emit connectingChanged(false);
                emit errorOccurred("No cameras found. Check USB and set camera to PC Remote mode.");
            }, Qt::QueuedConnection);
            return;
        }

        auto* info = const_cast<SCRSDK::ICrCameraObjectInfo*>(cameraList->GetCameraObjectInfo(0));
        SCRSDK::CrDeviceHandle handle = 0;

        result = SCRSDK::Connect(info, this, &handle,
                                 SCRSDK::CrSdkControlMode_Remote,
                                 SCRSDK::CrReconnecting_OFF);

        cameraList->Release();

        if (result != SCRSDK::CrError_None) {
            QMetaObject::invokeMethod(this, [this, result]() {
                m_connecting = false;
                emit connectingChanged(false);
                emit errorOccurred(QString("Camera connection failed. Error: 0x%1").arg(result, 0, 16));
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [this, handle]() {
            cameraHandle = handle;
            m_connecting = false;
            emit connectingChanged(false);
        }, Qt::QueuedConnection);
    });

    return true;
}

void SonyCamera::disconnectCamera()
{
    stopLiveView();
    if (cameraHandle != 0) {
        SCRSDK::Disconnect(cameraHandle);
        SCRSDK::ReleaseDevice(cameraHandle);
        cameraHandle = 0;
    }
    m_connected  = false;
    m_connecting = false;
    emit connectingChanged(false);
}

void SonyCamera::takePhoto()
{
    if (!m_connected || cameraHandle == 0) {
        emit errorOccurred("Camera not connected.");
        return;
    }

    // FIX: Pause live view during capture to avoid USB bandwidth contention
    // which causes disconnection especially with Extra Fine / large files.
    m_lvWasActiveBeforeCapture = m_liveViewActive;
    if (m_liveViewActive) {
        m_lvTimer->stop();
        qDebug() << "Live view paused for capture.";
    }

    m_capturePending = true;
    emit logMessage("Taking photo... Waiting for high-res file from camera.");

    // FIX: Use longer shutter-button hold for heavy quality modes (Extra Fine)
    // to avoid triggering a disconnect from a too-short command burst.
    bool isHeavyQuality = (m_currentImageQual == 4 ||
                           m_currentImageQual == static_cast<quint64>(SCRSDK::CrImageQuality_ExFine));
    int holdMs = isHeavyQuality ? 300 : 120;

    SCRSDK::CrDeviceHandle h = cameraHandle;
    QThreadPool::globalInstance()->start([h, holdMs]() {
        SCRSDK::SendCommand(h, SCRSDK::CrCommandId_Release, SCRSDK::CrCommandParam_Down);
        QThread::msleep(holdMs);
        SCRSDK::SendCommand(h, SCRSDK::CrCommandId_Release, SCRSDK::CrCommandParam_Up);
    });

    QTimer::singleShot(20000, this, [this]() {
        if (m_capturePending) {
            m_capturePending = false;
            // Resume live view on timeout too
            if (m_lvWasActiveBeforeCapture && m_liveViewActive == false && m_connected) {
                m_liveViewActive = true;
                emit liveViewActiveChanged(true);
                m_lvTimer->start();
                qDebug() << "Live view resumed after capture timeout.";
            }
            emit errorOccurred("Capture timeout. Please check 'PC Remote Settings' on camera.");
        }
    });
}

void SonyCamera::shutdown()
{
    stopLiveView();
    if (cameraHandle != 0) {
        SCRSDK::Disconnect(cameraHandle);
        SCRSDK::ReleaseDevice(cameraHandle);
        cameraHandle = 0;
    }
    if (m_sdkInitialized) {
        SCRSDK::Release();
        m_sdkInitialized = false;
    }
}

void SonyCamera::startLiveView()
{
    if (!m_connected || cameraHandle == 0) {
        emit errorOccurred("Connect camera before starting live view.");
        return;
    }
    if (m_liveViewActive) return;
    m_liveViewActive = true;
    emit liveViewActiveChanged(true);
    emit logMessage("Live view started.");
    m_lvTimer->start();
}

void SonyCamera::stopLiveView()
{
    if (!m_liveViewActive) return;
    m_lvTimer->stop();
    m_liveViewActive = false;
    emit liveViewActiveChanged(false);
    emit logMessage("Live view stopped.");
}

void SonyCamera::pollLiveViewFrame()
{
    if (!m_connected || cameraHandle == 0 || !m_liveViewActive) return;

    SCRSDK::CrImageInfo imgInfo;
    if (SCRSDK::GetLiveViewImageInfo(cameraHandle, &imgInfo) != SCRSDK::CrError_None) return;

    CrInt32u bufSize = imgInfo.GetBufferSize();
    if (bufSize == 0) return;

    std::vector<CrInt8u> buf(bufSize);
    SCRSDK::CrImageDataBlock dataBlock;
    dataBlock.SetSize(bufSize);
    dataBlock.SetData(buf.data());

    if (SCRSDK::GetLiveViewImage(cameraHandle, &dataBlock) == SCRSDK::CrError_None) {
        QImage frame;
        const uchar* imageData = reinterpret_cast<const uchar*>(dataBlock.GetImageData());
        if (frame.loadFromData(imageData, static_cast<int>(dataBlock.GetImageSize()), "JPEG")) {
            emit liveViewFrameReady(frame);
        }
    }
}

void SonyCamera::fetchFocusRange()
{
    if (!m_connected || cameraHandle == 0) return;

    // FIX: First check if FocusPositionSetting is actually supported by the camera
    // in its current mode (MF required). Querying it in AF/Auto mode triggers
    // warning 0x00020011 and causes the camera to disconnect (error 33287 / 0x8207).
    bool focusSettingSupported = false;
    bool focusCurrentSupported = false;
    {
        SCRSDK::CrDeviceProperty* all = nullptr;
        CrInt32 cnt = 0;
        if (SCRSDK::GetDeviceProperties(cameraHandle, &all, &cnt) == SCRSDK::CrError_None && all) {
            for (CrInt32 i = 0; i < cnt; i++) {
                CrInt32u c = all[i].GetCode();
                if (c == static_cast<CrInt32u>(SCRSDK::CrDeviceProperty_FocusPositionSetting))
                    focusSettingSupported = true;
                if (c == static_cast<CrInt32u>(SCRSDK::CrDeviceProperty_FocusPositionCurrentValue))
                    focusCurrentSupported = true;
            }
            SCRSDK::ReleaseDeviceProperties(cameraHandle, all);
        }
    }

    if (!focusSettingSupported) {
        qDebug() << "FocusPositionSetting not available in current camera mode (MF required). Skipping fetchFocusRange.";
        m_focusRangeValid = false;
        emit focusRangeReady();
        return;
    }

    // Query FocusPositionSetting for range + current position
    {
        CrInt32u codeArr = static_cast<CrInt32u>(SCRSDK::CrDeviceProperty_FocusPositionSetting);
        SCRSDK::CrDeviceProperty* props = nullptr;
        CrInt32 numProps = 0;
        SCRSDK::CrError err = SCRSDK::GetSelectDeviceProperties(
            cameraHandle, 1, &codeArr, &props, &numProps);

        if (err == SCRSDK::CrError_None && props && numProps > 0) {
            SCRSDK::CrDeviceProperty& p = props[0];
            CrInt8u* rv   = p.GetValues();
            CrInt32u rvSz = p.GetValueSize();

            if (rv && rvSz >= 6) {
                quint16 mn, mx, step;
                memcpy(&mn,   rv + 0, 2);
                memcpy(&mx,   rv + 2, 2);
                memcpy(&step, rv + 4, 2);
                if (mx > mn) {
                    m_focusMin        = mn;
                    m_focusMax        = mx;
                    m_focusRangeValid = true;
                    qDebug() << "Focus range:" << mn << "-" << mx << "step" << step;
                }
            }
            // Current position comes from the same property — no need for a second query
            m_focusPosition = static_cast<quint32>(p.GetCurrentValue() & 0xFFFF);
            SCRSDK::ReleaseDeviceProperties(cameraHandle, props);
        }
    }

    // FIX: Only query FocusPositionCurrentValue if the camera reports it as supported.
    // Removed unconditional second query that also triggered warning 0x00020011.
    if (focusCurrentSupported) {
        CrInt32u codeArr = static_cast<CrInt32u>(SCRSDK::CrDeviceProperty_FocusPositionCurrentValue);
        SCRSDK::CrDeviceProperty* props = nullptr;
        CrInt32 numProps = 0;
        SCRSDK::CrError err = SCRSDK::GetSelectDeviceProperties(
            cameraHandle, 1, &codeArr, &props, &numProps);

        if (err == SCRSDK::CrError_None && props && numProps > 0) {
            quint32 live = static_cast<quint32>(props[0].GetCurrentValue() & 0xFFFF);
            if (live > 0) m_focusPosition = live;
            SCRSDK::ReleaseDeviceProperties(cameraHandle, props);
        }
    }

    emit focusRangeReady();
    emit focusPositionChanged(m_focusPosition);
}

void SonyCamera::setFocusPosition(quint32 value)
{
    if (!m_connected || cameraHandle == 0) return;

    if (m_focusRangeValid) {
        if (value < m_focusMin) value = m_focusMin;
        if (value > m_focusMax) value = m_focusMax;
    }

    SCRSDK::CrDeviceProperty prop;
    prop.SetCode(static_cast<CrInt32u>(SCRSDK::CrDeviceProperty_FocusPositionSetting));
    prop.SetCurrentValue(static_cast<CrInt64u>(value));
    prop.SetValueType(SCRSDK::CrDataType_UInt16);

    SCRSDK::CrError err = SCRSDK::SetDeviceProperty(cameraHandle, &prop);
    if (err == SCRSDK::CrError_None) {
        m_focusPosition = value;
        emit focusPositionChanged(m_focusPosition);
        emit logMessage(QString("Focus position set to %1").arg(value));
    } else {
        qDebug() << "setFocusPosition failed:" << err;
        emit errorOccurred(QString("Focus set failed: 0x%1").arg(err, 0, 16));
    }
}

static quint64 readCurrentValue(SCRSDK::CrDeviceHandle handle,
                                SCRSDK::CrDevicePropertyCode code)
{
    CrInt32u codeArr = static_cast<CrInt32u>(code);
    SCRSDK::CrDeviceProperty* props = nullptr;
    CrInt32 numProps = 0;
    quint64 val = 0;
    if (SCRSDK::GetSelectDeviceProperties(handle, 1, &codeArr, &props, &numProps)
            == SCRSDK::CrError_None && props && numProps > 0) {
        val = props[0].GetCurrentValue();
        SCRSDK::ReleaseDeviceProperties(handle, props);
    }
    return val;
}
void SonyCamera::fetchProperty(SCRSDK::CrDevicePropertyCode code,
                               QVariantList& outValues, quint64& outCurrent)
{
    outValues.clear();
    outCurrent = 0;
    if (!m_connected || cameraHandle == 0) return;

    CrInt32u elemSize = 4;
    switch (code) {
    case SCRSDK::CrDeviceProperty_ExposureBiasCompensation:
    case SCRSDK::CrDeviceProperty_ImageSize:
    case SCRSDK::CrDeviceProperty_WhiteBalance:           elemSize = 2; break;
    case SCRSDK::CrDeviceProperty_CreativeLook_Sharpness: elemSize = 1; break;
    default: elemSize = 4; break;
    }

    CrInt32u codeArr = static_cast<CrInt32u>(code);
    SCRSDK::CrDeviceProperty* props = nullptr;
    CrInt32 numProps = 0;

    if (SCRSDK::GetSelectDeviceProperties(cameraHandle, 1, &codeArr, &props, &numProps)
            == SCRSDK::CrError_None && props && numProps > 0)
    {
        outCurrent = props[0].GetCurrentValue();

        auto processBuffer = [&](CrInt8u* buf, CrInt32u sz) {
            if (!buf || sz == 0) return;
            for (CrInt32u i = 0; i < (sz / elemSize); i++) {
                quint64 v = 0;
                // Use a temporary buffer to ensure no out-of-bounds read
                memcpy(&v, buf + (i * elemSize), elemSize);
                outValues.append(v);
            }
        };

        processBuffer(props[0].GetSetValues(), props[0].GetSetValueSize());
        if (outValues.isEmpty()) {
            processBuffer(props[0].GetValues(), props[0].GetValueSize());
        }

        SCRSDK::ReleaseDeviceProperties(cameraHandle, props);
    }

    // Hard fallback for Quality (0x101) if values are still 0
    if (code == SCRSDK::CrDeviceProperty_StillImageQuality && outValues.isEmpty()) {
        outValues << (quint64)0x01 << (quint64)0x02 << (quint64)0x03
                  << (quint64)SCRSDK::CrFileType_Raw
                  << (quint64)SCRSDK::CrFileType_RawJpeg;
    }
}

void SonyCamera::fetchAllSettings()
{
    m_currentISO = readCurrentValue(cameraHandle, SCRSDK::CrDeviceProperty_IsoSensitivity);
    m_isoValues.clear();
    m_isoValues.append(QVariant::fromValue(quint64(0x00FFFFFF)));
    static const quint32 isos[] = {
        50,64,80,100,125,160,200,250,320,400,500,640,800,1000,1250,1600,
        2000,2500,3200,4000,5000,6400,8000,10000,12800,16000,20000,
        25600,32000,40000,51200,64000,80000,102400
    };
    for (auto iso : isos)
        m_isoValues.append(QVariant::fromValue(quint64(iso)));

    // Shutter
    m_currentShutter = readCurrentValue(cameraHandle, SCRSDK::CrDeviceProperty_ShutterSpeed);
    m_shutterValues.clear();
    m_shutterValues.append(QVariant::fromValue(quint64(0x00000000)));
    static const quint32 dens[] = {
        8000,6400,5000,4000,3200,2500,2000,1600,1250,1000,800,640,500,
        400,320,250,200,160,125,100,80,60,50,40,30,25,20,15,13,10,8,6,5,4,3,2
    };
    for (auto d : dens)
        m_shutterValues.append(QVariant::fromValue(quint64((quint32(1) << 16) | d)));
    struct { quint32 n, d; } longs[] = {
        {1,1},{13,10},{16,10},{2,1},{25,10},{3,1},{4,1},{5,1},
        {6,1},{8,1},{10,1},{13,1},{15,1},{20,1},{25,1},{30,1}
    };
    for (auto& s : longs)
        m_shutterValues.append(QVariant::fromValue(quint64((s.n << 16) | s.d)));

    fetchProperty(SCRSDK::CrDeviceProperty_ExposureBiasCompensation, m_exposureValues,   m_currentExposure);
    fetchProperty(SCRSDK::CrDeviceProperty_CreativeLook_Sharpness,   m_sharpnessValues,  m_currentSharpness);

    m_brightnessValues.clear();
    m_currentBrightness = 0;

    fetchProperty(SCRSDK::CrDeviceProperty_ImageSize, m_imageSizeValues, m_currentImageSize);
    if (m_imageSizeValues.size() < 2) {
        m_imageSizeValues.clear();
        m_imageSizeValues.append(QVariant::fromValue(quint64(SCRSDK::CrImageSize_L)));
        m_imageSizeValues.append(QVariant::fromValue(quint64(SCRSDK::CrImageSize_M)));
        m_imageSizeValues.append(QVariant::fromValue(quint64(SCRSDK::CrImageSize_S)));
    }
    fetchProperty(SCRSDK::CrDeviceProperty_StillImageQuality, m_imageQualValues, m_currentImageQual);
    if (m_imageQualValues.isEmpty()) {
        m_imageQualValues.append(QVariant::fromValue(quint64(4)));
        m_imageQualValues.append(QVariant::fromValue(quint64(2)));
        m_imageQualValues.append(QVariant::fromValue(quint64(3)));
        m_imageQualValues.append(QVariant::fromValue(quint64(1)));
    }

    qDebug() << "Settings fetched:"
             << "ISO:" << m_isoValues.size()
             << "Shutter:" << m_shutterValues.size()
             << "EV:" << m_exposureValues.size()
             << "Sharpness:" << m_sharpnessValues.size()
             << "ImgSize:" << m_imageSizeValues.size()
             << "ImgQual:" << m_imageQualValues.size();

    emit settingsChanged();
}

bool SonyCamera::setProperty(SCRSDK::CrDevicePropertyCode code, quint64 value)
{
    if (!m_connected || cameraHandle == 0) return false;

    SCRSDK::CrDataType dtype;
    switch (code) {
    case SCRSDK::CrDeviceProperty_ExposureBiasCompensation: dtype = SCRSDK::CrDataType_UInt16; break;
    case SCRSDK::CrDeviceProperty_CreativeLook_Sharpness:   dtype = SCRSDK::CrDataType_Int8;   break;
    case SCRSDK::CrDeviceProperty_ShutterSpeed:             dtype = SCRSDK::CrDataType_UInt32; break;
    case SCRSDK::CrDeviceProperty_IsoSensitivity:           dtype = SCRSDK::CrDataType_UInt32; break;
    case SCRSDK::CrDeviceProperty_ImageSize:                dtype = SCRSDK::CrDataType_UInt16; break;
    case SCRSDK::CrDeviceProperty_StillImageQuality:        dtype = SCRSDK::CrDataType_UInt32; break;
    default:                                                dtype = SCRSDK::CrDataType_UInt32; break;
    }

    SCRSDK::CrDeviceProperty prop;
    prop.SetCode(static_cast<CrInt32u>(code));
    prop.SetCurrentValue(value);
    prop.SetValueType(dtype);

    SCRSDK::CrError err = SCRSDK::SetDeviceProperty(cameraHandle, &prop);
    if (err != SCRSDK::CrError_None) {
        qDebug() << "SetDeviceProperty failed for code" << code << "error:" << err;
        return false;
    }
    return true;
}

void SonyCamera::setISO(quint64 value)
{
    if (setProperty(SCRSDK::CrDeviceProperty_IsoSensitivity, value)) {
        m_currentISO = value;
        emit settingsChanged();
        emit logMessage("ISO set to: " + formatISO(value));
    }
}

void SonyCamera::setShutterSpeed(quint64 value)
{
    if (setProperty(SCRSDK::CrDeviceProperty_ShutterSpeed, value)) {
        m_currentShutter = value;
        emit settingsChanged();
        emit logMessage("Shutter speed set to: " + formatShutter(value));
    }
}

void SonyCamera::setExposure(quint64 value)
{
    if (setProperty(SCRSDK::CrDeviceProperty_ExposureBiasCompensation, value)) {
        m_currentExposure = value;
        emit settingsChanged();
        emit logMessage("Exposure set to: " + formatExposure(value));
    }
}

void SonyCamera::setSharpness(quint64 value)
{
    if (setProperty(SCRSDK::CrDeviceProperty_CreativeLook_Sharpness, value)) {
        m_currentSharpness = value;
        emit settingsChanged();
        emit logMessage(QString("Sharpness set to: %1").arg(value));
    }
}

void SonyCamera::setBrightness(quint64 value)
{
    Q_UNUSED(value);
    emit errorOccurred("Brightness control not supported in this SDK version.");
}

QString SonyCamera::formatISO(quint64 raw) const
{
    quint32 isoVal = static_cast<qint32>(raw & 0x00FFFFFF);
    quint32 mode   = (raw >> 24) & 0x0F;
    if ((raw & 0x00FFFFFF) == 0x00FFFFFF) return "ISO AUTO";
    if (mode == 0x01) return QString("ISO %1 (MFNR)").arg(isoVal);
    return QString("ISO %1").arg(isoVal);
}

QString SonyCamera::formatShutter(quint64 raw) const
{
    if (raw == 0x00000000) return "BULB";
    if (raw == 0xFFFFFFFF) return "--";
    quint32 num = (raw >> 16) & 0xFFFF;
    quint32 den = raw & 0xFFFF;
    if (den == 0) return "--";
    if (num >= den) {
        double secs = static_cast<double>(num) / den;
        if (secs == static_cast<int>(secs))
            return QString("%1\"").arg(static_cast<int>(secs));
        return QString("%1\"").arg(secs, 0, 'f', 1);
    }
    return QString("1/%1").arg(den / num);
}

QString SonyCamera::formatExposure(quint64 raw) const
{
    qint16 ev1000 = static_cast<qint16>(static_cast<quint16>(raw & 0xFFFF));
    double ev = ev1000 / 1000.0;
    if (ev >= 0) return QString("+%1 EV").arg(ev, 0, 'f', 1);
    return QString("%1 EV").arg(ev, 0, 'f', 1);
}

QString SonyCamera::formatBrightness(quint64 raw) const
{
    Q_UNUSED(raw);
    return "—";
}

void SonyCamera::fetchExifInfo()
{
    if (!m_connected || cameraHandle == 0) return;

    auto fetchStr = [&](SCRSDK::CrDevicePropertyCode code) -> QString {
        CrInt32u codeArr = static_cast<CrInt32u>(code);
        SCRSDK::CrDeviceProperty* props = nullptr;
        CrInt32 numProps = 0;
        SCRSDK::CrError err = SCRSDK::GetSelectDeviceProperties(
            cameraHandle, 1, &codeArr, &props, &numProps);
        if (err != SCRSDK::CrError_None || !props || numProps == 0) return QString();
        CrInt16u* str = props[0].GetCurrentStr();
        QString result;
        if (str) {
            result = QString::fromUtf16(reinterpret_cast<const char16_t*>(str));
            result = result.trimmed().remove(QChar('\0'));
        }
        SCRSDK::ReleaseDeviceProperties(cameraHandle, props);
        return result;
    };

    auto fetchUint = [&](SCRSDK::CrDevicePropertyCode code) -> quint64 {
        CrInt32u codeArr = static_cast<CrInt32u>(code);
        SCRSDK::CrDeviceProperty* props = nullptr;
        CrInt32 numProps = 0;
        SCRSDK::CrError err = SCRSDK::GetSelectDeviceProperties(
            cameraHandle, 1, &codeArr, &props, &numProps);
        if (err != SCRSDK::CrError_None || !props || numProps == 0) return 0;
        quint64 val = props[0].GetCurrentValue();
        SCRSDK::ReleaseDeviceProperties(cameraHandle, props);
        return val;
    };

    m_exifModel = fetchStr(SCRSDK::CrDeviceProperty_LensModelName);
    if (m_exifModel.isEmpty()) m_exifModel = "Unknown";

    m_exifLens = fetchStr(SCRSDK::CrDeviceProperty_LensModelName);
    if (m_exifLens.isEmpty()) m_exifLens = "—";

    {
        quint64 fd = fetchUint(SCRSDK::CrDeviceProperty_FocalDistanceInMeter);
        if (fd == 0 || fd == 0xFFFFFFFF)
            m_exifFocalLength = "∞ / —";
        else
            m_exifFocalLength = QString("%1 m").arg(fd / 100.0, 0, 'f', 2);
    }

    {
        quint64 fn = fetchUint(SCRSDK::CrDeviceProperty_FNumber);
        quint16 fnVal = static_cast<quint16>(fn & 0xFFFF);
        if (fnVal == 0xFFFD || fnVal == 0xFFFE || fnVal == 0xFFFF || fnVal == 0)
            m_exifAperture = "—";
        else
            m_exifAperture = QString("f/%1").arg(fnVal / 100.0, 0, 'f', fnVal % 100 == 0 ? 0 : 1);
    }

    m_exifISO     = formatISO(m_currentISO);
    m_exifShutter = formatShutter(m_currentShutter);

    {
        quint64 em = fetchUint(SCRSDK::CrDeviceProperty_ExposureProgramMode);
        switch (static_cast<quint32>(em)) {
        case SCRSDK::CrExposure_M_Manual:               m_exifExposureMode = "Manual (M)";       break;
        case SCRSDK::CrExposure_P_Auto:                 m_exifExposureMode = "Program (P)";      break;
        case SCRSDK::CrExposure_A_AperturePriority:     m_exifExposureMode = "Aperture (A)";     break;
        case SCRSDK::CrExposure_S_ShutterSpeedPriority: m_exifExposureMode = "Shutter (S)";      break;
        case SCRSDK::CrExposure_Auto:                   m_exifExposureMode = "Intelligent Auto"; break;
        case SCRSDK::CrExposure_Auto_Plus:              m_exifExposureMode = "Superior Auto";    break;
        case SCRSDK::CrExposure_Movie_P:                m_exifExposureMode = "Movie P";          break;
        case SCRSDK::CrExposure_Movie_A:                m_exifExposureMode = "Movie A";          break;
        case SCRSDK::CrExposure_Movie_S:                m_exifExposureMode = "Movie S";          break;
        case SCRSDK::CrExposure_Movie_M:                m_exifExposureMode = "Movie M";          break;
        default:
            m_exifExposureMode = em > 0 ? QString("Mode 0x%1").arg(em, 0, 16) : "—";
            break;
        }
    }

    {
        quint64 wb = fetchUint(SCRSDK::CrDeviceProperty_WhiteBalance);
        switch (static_cast<quint16>(wb & 0xFFFF)) {
        case SCRSDK::CrWhiteBalance_AWB:             m_exifWhiteBalance = "Auto";            break;
        case SCRSDK::CrWhiteBalance_Daylight:        m_exifWhiteBalance = "Daylight";        break;
        case SCRSDK::CrWhiteBalance_Shadow:          m_exifWhiteBalance = "Shadow";          break;
        case SCRSDK::CrWhiteBalance_Cloudy:          m_exifWhiteBalance = "Cloudy";          break;
        case SCRSDK::CrWhiteBalance_Tungsten:        m_exifWhiteBalance = "Tungsten";        break;
        case SCRSDK::CrWhiteBalance_Fluorescent:     m_exifWhiteBalance = "Fluorescent";     break;
        case SCRSDK::CrWhiteBalance_Flush:           m_exifWhiteBalance = "Flash";           break;
        case SCRSDK::CrWhiteBalance_ColorTemp:       m_exifWhiteBalance = "Color Temp";      break;
        case SCRSDK::CrWhiteBalance_Custom_1:        m_exifWhiteBalance = "Custom 1";        break;
        case SCRSDK::CrWhiteBalance_Custom_2:        m_exifWhiteBalance = "Custom 2";        break;
        case SCRSDK::CrWhiteBalance_Custom_3:        m_exifWhiteBalance = "Custom 3";        break;
        case SCRSDK::CrWhiteBalance_Custom:          m_exifWhiteBalance = "Custom";          break;
        case SCRSDK::CrWhiteBalance_Underwater_Auto: m_exifWhiteBalance = "Underwater Auto"; break;
        default:
            m_exifWhiteBalance = wb > 0 ? QString("0x%1").arg(wb, 0, 16) : "—";
            break;
        }
    }

    {
        quint64 bat = fetchUint(SCRSDK::CrDeviceProperty_BatteryLevel);
        switch (static_cast<quint32>(bat)) {
        case SCRSDK::CrBatteryLevel_PreEndBattery:  m_exifBatteryLevel = "Critical";   break;
        case SCRSDK::CrBatteryLevel_1_4:            m_exifBatteryLevel = "25%";        break;
        case SCRSDK::CrBatteryLevel_2_4:            m_exifBatteryLevel = "50%";        break;
        case SCRSDK::CrBatteryLevel_3_4:            m_exifBatteryLevel = "75%";        break;
        case SCRSDK::CrBatteryLevel_4_4:            m_exifBatteryLevel = "100%";       break;
        case SCRSDK::CrBatteryLevel_1_3:            m_exifBatteryLevel = "33%";        break;
        case SCRSDK::CrBatteryLevel_2_3:            m_exifBatteryLevel = "66%";        break;
        case SCRSDK::CrBatteryLevel_3_3:            m_exifBatteryLevel = "100%";       break;
        case SCRSDK::CrBatteryLevel_USBPowerSupply: m_exifBatteryLevel = "USB Power";  break;
        default:
            m_exifBatteryLevel = bat > 0 ? QString("Level 0x%1").arg(bat, 0, 16) : "—";
            break;
        }
    }

    qDebug() << "EXIF — lens:" << m_exifLens
             << "aperture:" << m_exifAperture
             << "mode:" << m_exifExposureMode
             << "WB:" << m_exifWhiteBalance
             << "battery:" << m_exifBatteryLevel;

    emit exifReady();
}

void SonyCamera::setupSaveInfo()
{
    SCRSDK::CrError err = SCRSDK::SetSaveInfo(
        cameraHandle,
        const_cast<CrChar*>(m_savePathLinux.c_str()),
        const_cast<CrChar*>("IMG"), 0);

    if (err != SCRSDK::CrError_None)
        err = SCRSDK::SetSaveInfo(
            cameraHandle,
            const_cast<CrChar*>(m_savePathLinux.c_str()),
            const_cast<CrChar*>(""), 1);

    if (err != SCRSDK::CrError_None)
        err = SCRSDK::SetSaveInfo(
            cameraHandle,
            const_cast<CrChar*>(m_savePathLinux.c_str()),
            nullptr, 0);

    if (err == SCRSDK::CrError_None) {
        m_saveSupported = true;
        emit logMessage("Full-res download enabled → " + m_savePath);
    } else {
        m_saveSupported = false;
        qDebug() << "SetSaveInfo ALL variants failed. Last error: 0x" << Qt::hex << err;
        emit logMessage(QString(
                            "⚠ SetSaveInfo failed (0x%1). "
                            "On camera: Menu → PC Remote Settings → Still Img. Save Dest. → 'PC Only'. "
                            "Until then, photos are captured from live-view (low resolution).")
                            .arg(static_cast<uint>(err), 0, 16));

    }

    SCRSDK::SetDeviceSetting(cameraHandle, SCRSDK::Setting_Key_EnableLiveView, SCRSDK::CrDeviceSetting_Enable);

    QTimer::singleShot(1500, this, [this]() {
        if(m_connected) fetchAllSettings();
    });

    QTimer::singleShot(2500, this, [this]() {
        if(m_connected) fetchFocusRange();
    });
    QTimer::singleShot(500,  this, [this]() { fetchAllSettings(); });
    QTimer::singleShot(2000, this, [this]() { fetchFocusRange(); });
}

void SonyCamera::OnConnected(SCRSDK::DeviceConnectionVersioin version)
{
    Q_UNUSED(version);
    qDebug() << "Camera connected!";
    QMetaObject::invokeMethod(this, [this]() {
        m_connected  = true;
        m_connecting = false;
        emit connectingChanged(false);
        emit connectionChanged(true);
        QTimer::singleShot(300, this, [this]() { setupSaveInfo(); });
    }, Qt::QueuedConnection);
}

void SonyCamera::OnDisconnected(CrInt32u error)
{
    qDebug() << "Camera disconnected. Code:" << error;
    QMetaObject::invokeMethod(this, [this]() {
        m_lvTimer->stop();
        m_liveViewActive = false;
        emit liveViewActiveChanged(false);
        m_connected  = false;
        m_connecting = false;
        cameraHandle = 0;
        m_capturePending = false;
        m_lvWasActiveBeforeCapture = false;
        emit connectingChanged(false);
        emit connectionChanged(false);
    }, Qt::QueuedConnection);
}

void SonyCamera::OnPropertyChanged()
{
    QMetaObject::invokeMethod(this, [this]() {
        if (!m_connected) return;
        if (m_capturePending) return;
        m_settingsFetchPending = true;
        if (m_propertyDebounceTimer) {
            m_propertyDebounceTimer->stop();
            m_propertyDebounceTimer->deleteLater();
        }
        m_propertyDebounceTimer = new QTimer(this);
        m_propertyDebounceTimer->setSingleShot(true);
        m_propertyDebounceTimer->setInterval(2000);
        connect(m_propertyDebounceTimer, &QTimer::timeout, this, [this]() {
            m_propertyDebounceTimer = nullptr;
            m_settingsFetchPending  = false;
            if (!m_connected || m_capturePending) return;
            fetchAllSettings();
        });
        m_propertyDebounceTimer->start();
    }, Qt::QueuedConnection);
}

void SonyCamera::OnLvPropertyChanged()
{
}

void SonyCamera::OnCompleteDownload(CrChar* filename, CrInt32u type)
{
    qDebug() << "OnCompleteDownload called, type:" << type
             << "filename:" << (filename ? QString::fromLocal8Bit(filename) : "null");

    if (!filename) {
        QMetaObject::invokeMethod(this, [this]() {
            emit errorOccurred("Download callback: filename is null.");
        }, Qt::QueuedConnection);
        return;
    }

    QString localPath = QString::fromLocal8Bit(filename);
    bool isPreview = (type == 0x0001);
    qDebug() << (isPreview ? "Preview" : "Full-res") << "downloaded type=0x"
             << Qt::hex << type << localPath;

    if (isPreview) return;

    QMetaObject::invokeMethod(this, [this]() {
        m_capturePending = false;

        // FIX: Resume live view now that the full-res file has been transferred.
        if (m_lvWasActiveBeforeCapture && !m_liveViewActive && m_connected) {
            m_liveViewActive = true;
            emit liveViewActiveChanged(true);
            m_lvTimer->start();
            qDebug() << "Live view resumed after capture download.";
        }
        m_lvWasActiveBeforeCapture = false;
    }, Qt::QueuedConnection);

    struct PollState { QString path; int attempts; };
    auto* state = new PollState{ localPath, 0 };

    QMetaObject::invokeMethod(this, [this, state]() {
        auto* timer = new QTimer(this);
        timer->setInterval(100);
        connect(timer, &QTimer::timeout, this, [this, state, timer]() {
            state->attempts++;
            if (QFileInfo::exists(state->path)) {
                qint64 sz = QFileInfo(state->path).size();
                if (sz > 0) {
                    emit logMessage(QString("Photo saved (%1 MB): ").arg(sz/1024.0/1024.0, 0,'f',1) + state->path);
                    emit photoTaken(state->path);
                    timer->stop(); timer->deleteLater(); delete state;
                }
            } else if (state->attempts >= 60) {
                emit errorOccurred("Photo file not found after download: " + state->path);
                timer->stop(); timer->deleteLater(); delete state;
            }
        });
        timer->start();
    }, Qt::QueuedConnection);
}

void SonyCamera::OnWarning(CrInt32u w)
{ qDebug() << "Warning:" << QString("0x%1").arg(w, 8, 16, QChar('0')); }

void SonyCamera::OnWarningExt(CrInt32u w, CrInt32, CrInt32, CrInt32)
{ qDebug() << "Warning ext:" << w; }

void SonyCamera::OnError(CrInt32u error)
{ emit errorOccurred(QString("Camera error: 0x%1").arg(error, 8, 16, QChar('0'))); }

bool SonyCamera::isConnected() const { return m_connected; }

void SonyCamera::setWhiteBalance(quint32 value)
{
    if (!setProperty(SCRSDK::CrDeviceProperty_WhiteBalance, static_cast<quint64>(value))) {
        emit errorOccurred("Failed to set White Balance");
        return;
    }

    QString label;
    switch (static_cast<quint16>(value & 0xFFFF)) {
    case SCRSDK::CrWhiteBalance_AWB:         label = "Auto";         break;
    case SCRSDK::CrWhiteBalance_Daylight:    label = "Daylight";     break;
    case SCRSDK::CrWhiteBalance_Shadow:      label = "Shade";        break;
    case SCRSDK::CrWhiteBalance_Cloudy:      label = "Cloudy";       break;
    case SCRSDK::CrWhiteBalance_Tungsten:    label = "Incandescent"; break;
    case SCRSDK::CrWhiteBalance_Fluorescent: label = "Fluorescent";  break;
    case SCRSDK::CrWhiteBalance_Flush:       label = "Flash";        break;
    case SCRSDK::CrWhiteBalance_Custom:      label = "Custom";       break;
    default: label = QString("0x%1").arg(value, 0, 16); break;
    }

    emit settingsChanged();
    emit logMessage("White Balance set to: " + label);
}

void SonyCamera::setImageSize(quint64 value)
{
    if (!setProperty(SCRSDK::CrDeviceProperty_ImageSize, value)) {
        emit errorOccurred("Failed to set Image Size");
        return;
    }
    m_currentImageSize = value;
    emit settingsChanged();
    emit logMessage("Image Size → " + formatImageSize(value));
}

void SonyCamera::setImageQual(quint64 value)
{
    if (!m_connected || cameraHandle == 0) return;

    qDebug() << "Setting Quality to:" << formatImageQual(value) << " (Value:" << value << ")";
    if (!setProperty(SCRSDK::CrDeviceProperty_StillImageQuality, value)) {
        emit errorOccurred("Failed to set Quality. Camera might not support this mode in current Dial position.");
        return;
    }

    m_currentImageQual = value;
    emit settingsChanged();
    emit logMessage("Quality changed to → " + formatImageQual(value));
}

QString SonyCamera::formatImageSize(quint64 raw) const
{
    switch (static_cast<quint16>(raw)) {
    case SCRSDK::CrImageSize_L:   return "L";
    case SCRSDK::CrImageSize_M:   return "M";
    case SCRSDK::CrImageSize_S:   return "S";
    case SCRSDK::CrImageSize_VGA: return "VGA";
    default: return raw > 0 ? QString("0x%1").arg(raw, 0, 16) : "—";
    }
}

QString SonyCamera::formatImageQual(quint64 raw) const
{
    uint32_t val = static_cast<uint32_t>(raw);

    // Sony standard mapping for Quality
    if (val == 0x01) return "Extra Fine";
    if (val == 0x02) return "Fine";
    if (val == 0x03) return "Standard";

    if (val == SCRSDK::CrFileType_Raw)     return "RAW";
    if (val == SCRSDK::CrFileType_RawJpeg) return "RAW+JPEG";

    return raw > 0 ? QString("0x%1").arg(raw, 0, 16) : "—";
}
