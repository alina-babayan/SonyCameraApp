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

    if (m_propertyDebounceTimer) {
        m_propertyDebounceTimer->stop();
        m_propertyDebounceTimer->deleteLater();
        m_propertyDebounceTimer = nullptr;
        m_settingsFetchPending = false;
    }

    m_lvWasActiveBeforeCapture = m_liveViewActive;
    if (m_liveViewActive) {
        m_lvTimer->stop();
        m_liveViewActive = false;
        emit liveViewActiveChanged(false);
    }

    m_lvPolling = false;
    m_capturePending = true;
    emit logMessage("Taking photo… waiting for full-res file (may take 10–30 s for RAW).");

    int holdMs = 800;

    SCRSDK::CrDeviceHandle h = cameraHandle;
    QThreadPool::globalInstance()->start([h, holdMs]() {
        SCRSDK::SendCommand(h, SCRSDK::CrCommandId_Release, SCRSDK::CrCommandParam_Down);
        QThread::msleep(holdMs);
        SCRSDK::SendCommand(h, SCRSDK::CrCommandId_Release, SCRSDK::CrCommandParam_Up);
    });

    QTimer::singleShot(9000, this, [this]() {
        if (m_capturePending) {
            m_capturePending = false;
            if (m_lvWasActiveBeforeCapture && !m_liveViewActive && m_connected) {
                QTimer::singleShot(2000, this, [this]() {
                    if (m_connected && !m_liveViewActive && !m_capturePending) {
                        m_liveViewActive = true;
                        emit liveViewActiveChanged(true);
                        m_lvTimer->start();
                    }
                    m_lvWasActiveBeforeCapture = false;
                });
            } else {
                m_lvWasActiveBeforeCapture = false;
            }
            emit errorOccurred(
                "Capture timeout (90 s). "
                "Check camera: Menu → Network → PC Remote Settings → "
                "Still Img. Save Dest. → 'PC+Memory Card'.");
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
    if (!m_connected || cameraHandle == 0 || !m_liveViewActive || m_capturePending)
        return;

    if (m_lvPolling) return;
    m_lvPolling = true;

    SCRSDK::CrImageInfo imgInfo;
    if (SCRSDK::GetLiveViewImageInfo(cameraHandle, &imgInfo) != SCRSDK::CrError_None) {
        m_lvPolling = false; return;
    }

    auto bufSize = imgInfo.GetBufferSize();
    if (bufSize == 0) { m_lvPolling = false; return; }

    std::vector<CrInt8u> buf(bufSize);
    SCRSDK::CrImageDataBlock dataBlock;
    dataBlock.SetSize(bufSize);
    dataBlock.SetData(buf.data());

    if (SCRSDK::GetLiveViewImage(cameraHandle, &dataBlock) == SCRSDK::CrError_None) {
        QImage frame;
        const uchar* imageData = reinterpret_cast<const uchar*>(dataBlock.GetImageData());
        if (frame.loadFromData(imageData, (int)dataBlock.GetImageSize(), "JPEG")) {
            emit liveViewFrameReady(frame);
        }
    }

    m_lvPolling = false;
}

void SonyCamera::fetchFocusRange()
{
    if (!m_connected || cameraHandle == 0) return;

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
        qDebug() << "FocusPositionSetting not in property list — MF mode required. Skipping.";
        m_focusRangeValid = false;
        emit focusRangeReady();
        return;
    }

    {
        CrInt32u codeArr = static_cast<CrInt32u>(SCRSDK::CrDeviceProperty_FocusPositionSetting);
        SCRSDK::CrDeviceProperty* props = nullptr;
        CrInt32 numProps = 0;
        SCRSDK::CrError err = SCRSDK::GetSelectDeviceProperties(
            cameraHandle, 1, &codeArr, &props, &numProps);

        if (err != SCRSDK::CrError_None || !props || numProps == 0) {
            qDebug() << "FocusPositionSetting query rejected (err=0x"
                     << Qt::hex << static_cast<uint>(err) << ") — unavailable in this mode.";
            if (props) SCRSDK::ReleaseDeviceProperties(cameraHandle, props);
            m_focusRangeValid = false;
            emit focusRangeReady();
            return;
        }

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
        m_focusPosition = static_cast<quint32>(p.GetCurrentValue() & 0xFFFF);
        SCRSDK::ReleaseDeviceProperties(cameraHandle, props);
    }

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
        } else {
            if (props) SCRSDK::ReleaseDeviceProperties(cameraHandle, props);
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
    case SCRSDK::CrDeviceProperty_WhiteBalance:
    case SCRSDK::CrDeviceProperty_JpegQuality:             elemSize = 2; break;
    case SCRSDK::CrDeviceProperty_CreativeLook_Sharpness: elemSize = 1; break;
    case SCRSDK::CrDeviceProperty_IsoSensitivity:
    case SCRSDK::CrDeviceProperty_ShutterSpeed:           elemSize = 4; break;
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

}

void SonyCamera::fetchAllSettings()
{
    if (!m_connected || cameraHandle == 0 || m_capturePending) return;

    fetchProperty(SCRSDK::CrDeviceProperty_IsoSensitivity, m_isoValues, m_currentISO);
    if (m_isoValues.isEmpty())
        m_currentISO = readCurrentValue(cameraHandle, SCRSDK::CrDeviceProperty_IsoSensitivity);

    fetchProperty(SCRSDK::CrDeviceProperty_ShutterSpeed, m_shutterValues, m_currentShutter);
    if (m_shutterValues.isEmpty())
        m_currentShutter = readCurrentValue(cameraHandle, SCRSDK::CrDeviceProperty_ShutterSpeed);

    fetchProperty(SCRSDK::CrDeviceProperty_ExposureBiasCompensation, m_exposureValues,   m_currentExposure);
    fetchProperty(SCRSDK::CrDeviceProperty_CreativeLook_Sharpness,   m_sharpnessValues,  m_currentSharpness);

    fetchProperty(SCRSDK::CrDeviceProperty_ImageSize, m_imageSizeValues, m_currentImageSize);
    if (m_imageSizeValues.size() < 2) {
        m_imageSizeValues.clear();
        m_imageSizeValues.append(QVariant::fromValue(quint64(SCRSDK::CrImageSize_L)));
        m_imageSizeValues.append(QVariant::fromValue(quint64(SCRSDK::CrImageSize_M)));
        m_imageSizeValues.append(QVariant::fromValue(quint64(SCRSDK::CrImageSize_S)));
    }

    fetchProperty(SCRSDK::CrDeviceProperty_JpegQuality, m_jpegQualValues, m_currentJpegQual);
    if (m_jpegQualValues.isEmpty()) {
        m_jpegQualValues.append(QVariant::fromValue(quint64(1)));
        m_jpegQualValues.append(QVariant::fromValue(quint64(2)));
        m_jpegQualValues.append(QVariant::fromValue(quint64(3)));
    }

    qDebug() << "Settings fetched:"
             << "ISO:" << m_isoValues.size()
             << "Shutter:" << m_shutterValues.size()
             << "EV:" << m_exposureValues.size()
             << "Sharpness:" << m_sharpnessValues.size()
             << "ImgSize:" << m_imageSizeValues.size()
             << "JpegQual:" << m_jpegQualValues.size();

    emit settingsChanged();
}

bool SonyCamera::setProperty(SCRSDK::CrDevicePropertyCode code, quint64 value)
{
    if (!m_connected || cameraHandle == 0) return false;

    SCRSDK::CrDataType dtype;
    switch (code) {
    case SCRSDK::CrDeviceProperty_JpegQuality:
    case SCRSDK::CrDeviceProperty_ImageSize:
    case SCRSDK::CrDeviceProperty_ExposureBiasCompensation: dtype = SCRSDK::CrDataType_UInt16; break;
    case SCRSDK::CrDeviceProperty_CreativeLook_Sharpness:   dtype = SCRSDK::CrDataType_Int8;   break;
    default:                                                dtype = SCRSDK::CrDataType_UInt32; break;
    }

    SCRSDK::CrDeviceProperty prop;
    prop.SetCode(static_cast<CrInt32u>(code));
    prop.SetCurrentValue(value);
    prop.SetValueType(dtype);

    SCRSDK::CrError err = SCRSDK::SetDeviceProperty(cameraHandle, &prop);
    return (err == SCRSDK::CrError_None);
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
    struct SaveVariant { const char* prefix; CrInt32u mode; };
    static const SaveVariant variants[] = {
                                           { "IMG", 2 },
                                           { "IMG", 1 },
                                           { "",    2 },
                                           { "",    1 },
                                           { nullptr, 2 },
                                           { nullptr, 1 },
                                           };

    SCRSDK::CrError err = SCRSDK::CrError_Generic;
    for (const auto& v : variants) {
        err = SCRSDK::SetSaveInfo(
            cameraHandle,
            const_cast<CrChar*>(m_savePathLinux.c_str()),
            const_cast<CrChar*>(v.prefix),
            v.mode);
        if (err == SCRSDK::CrError_None) break;
        qDebug() << "SetSaveInfo prefix=" << (v.prefix ? v.prefix : "null")
                 << "mode=" << v.mode << "failed: 0x" << Qt::hex << static_cast<uint>(err);
    }

    if (err == SCRSDK::CrError_None) {
        emit logMessage("✓ Full-res PC download enabled → " + m_savePath);
    } else {
        qDebug() << "SetSaveInfo ALL variants failed. Last error: 0x"
                 << Qt::hex << static_cast<uint>(err);
        emit logMessage(QString(
                            "⚠ SetSaveInfo failed (0x%1). "
                            "On camera: Menu → Network → PC Remote Settings → "
                            "Still Img. Save Dest. → set to 'PC+Memory Card' or 'PC Only'.")
                            .arg(static_cast<uint>(err), 0, 16));
    }

    SCRSDK::SetDeviceSetting(cameraHandle,
                             SCRSDK::Setting_Key_EnableLiveView,
                             SCRSDK::CrDeviceSetting_Enable);

    QTimer::singleShot(800, this, [this]() {
        if (!m_connected) return;
        fetchAllSettings();
        QTimer::singleShot(600, this, [this]() {
            if (m_connected) fetchFocusRange();
        });
    });
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
        m_warnedContentsTransfer = false;
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
        m_propertyDebounceTimer->setInterval(4000);
        connect(m_propertyDebounceTimer, &QTimer::timeout, this, [this]() {
            m_propertyDebounceTimer = nullptr;
            m_settingsFetchPending  = false;
            if (!m_connected || m_capturePending) return;
            fetchAllSettings();
        });
        m_propertyDebounceTimer->start();
    }, Qt::QueuedConnection);
}

void SonyCamera::OnLvPropertyChanged() {}

void SonyCamera::OnCompleteDownload(CrChar* filename, CrInt32u type)
{
    if (!filename) return;
    if (type == 0x0001) {
        qDebug() << "OnCompleteDownload: preview received, waiting for full-res...";
        return;
    }

    QString path = QString::fromLocal8Bit(filename);
    qDebug() << "OnCompleteDownload: full-res type=0x"
             << Qt::hex << type << path;

    QMetaObject::invokeMethod(this, [this, path]() {
        m_capturePending = false;

        struct PollState {
            QString  path;
            int      attempts = 0;
            qint64   lastSize = -1;
            int      stable   = 0;
        };
        auto* state = new PollState{ path };

        auto* timer = new QTimer(this);
        timer->setInterval(500);

        connect(timer, &QTimer::timeout, this, [this, state, timer]() {
            state->attempts++;
            QFileInfo fi(state->path);

            if (fi.exists()) {
                qint64 sz = fi.size();
                if (sz > 0 && sz == state->lastSize) {
                    if (++state->stable >= 3) {
                        qDebug() << "File stable at"
                                 << sz / 1024.0 / 1024.0 << "MB:" << state->path;
                        emit logMessage(
                            QString("✓ Photo saved (%1 MB): ")
                                .arg(sz / 1024.0 / 1024.0, 0, 'f', 1)
                            + state->path);
                        emit photoTaken(state->path);

                        if (m_lvWasActiveBeforeCapture && m_connected) {
                            QTimer::singleShot(1500, this, [this]() {
                                if (m_connected && !m_capturePending)
                                    startLiveView();
                            });
                        }
                        m_lvWasActiveBeforeCapture = false;

                        timer->stop(); timer->deleteLater(); delete state;
                        return;
                    }
                } else {
                    state->stable = 0;
                }
                state->lastSize = sz;
            }

            if (state->attempts >= 120) {
                if (state->lastSize > 0) {
                    emit logMessage(
                        QString("⚠ Photo may be incomplete (%1 MB): ")
                            .arg(state->lastSize / 1024.0 / 1024.0, 0, 'f', 1)
                        + state->path);
                    emit photoTaken(state->path);
                } else {
                    emit errorOccurred("File not found after download: " + state->path);
                }
                if (m_lvWasActiveBeforeCapture && m_connected) {
                    QTimer::singleShot(1500, this, [this]() {
                        if (m_connected && !m_capturePending) startLiveView();
                    });
                }
                m_lvWasActiveBeforeCapture = false;
                timer->stop(); timer->deleteLater(); delete state;
            }
        });

        timer->start();
    }, Qt::QueuedConnection);
}

void SonyCamera::OnWarning(CrInt32u w)
{
    qDebug() << "Warning:" << QString("0x%1").arg(w, 8, 16, QChar('0'));
    if (w == 0x00020011 && !m_warnedContentsTransfer) {
        m_warnedContentsTransfer = true;
        QMetaObject::invokeMethod(this, [this]() {
            emit logMessage(
                "⚠ Warning 0x00020011: Camera cannot transfer full-res file. "
                "On camera: Menu → Network → PC Remote Settings → "
                "Still Img. Save Dest. → set to 'PC+Memory Card'.");
        }, Qt::QueuedConnection);
    }
}

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
QString SonyCamera::formatJpegQual(quint64 raw) const
{
    switch (static_cast<quint16>(raw & 0xFFFF)) {
    case 1: return "light";
    case 2: return "Fine";
    case 3: return "Standard";
    default: return raw > 0 ? QString("Extra fine").arg(raw, 0, 16) : "—";
    }
}

void SonyCamera::setJpegQual(quint64 value)
{
    if (!m_connected || cameraHandle == 0) return;

    if (!setProperty(SCRSDK::CrDeviceProperty_JpegQuality, value & 0xFFFF)) {
        emit errorOccurred("Failed to set JPEG Quality.");
        return;
    }

    m_currentJpegQual = value;
    emit settingsChanged();
    emit logMessage("JPEG Quality → " + formatJpegQual(value));
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

