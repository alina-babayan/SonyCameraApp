#include "sonycamera.h"
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QTimer>
#include <QThreadPool>

SonyCamera::SonyCamera(QObject *parent) : QObject(parent)
{
    m_savePath  = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/SonyCaptures";
    QDir().mkpath(m_savePath);
    m_savePathW = m_savePath.toStdWString();
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
    if (m_connected) return true;
    if (!m_sdkInitialized && !initialize()) return false;

    QThreadPool::globalInstance()->start([this]() {
        SCRSDK::ICrEnumCameraObjectInfo* cameraList = nullptr;
        SCRSDK::CrError result = SCRSDK::EnumCameraObjects(&cameraList, 1);

        if (result != SCRSDK::CrError_None || !cameraList) {
            QMetaObject::invokeMethod(this, [this]() { emit errorOccurred("Camera enumeration failed."); }, Qt::QueuedConnection);
            return;
        }

        CrInt32u count = cameraList->GetCount();
        if (count == 0) {
            QMetaObject::invokeMethod(this, [this]() { emit errorOccurred("No cameras found. Plug in your Sony camera via USB."); }, Qt::QueuedConnection);
            cameraList->Release();
            return;
        }

        auto* info = const_cast<SCRSDK::ICrCameraObjectInfo*>(cameraList->GetCameraObjectInfo(0));
        SCRSDK::CrDeviceHandle handle = 0;
        result = SCRSDK::Connect(info, this, &handle, SCRSDK::CrSdkControlMode_Remote, SCRSDK::CrReconnecting_OFF);
        cameraList->Release();

        if (result != SCRSDK::CrError_None) {
            QMetaObject::invokeMethod(this, [this, result]() {
                emit errorOccurred(QString("Camera connection failed. Error: %1").arg(result));
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [this, handle]() {
            cameraHandle = handle;
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
}

void SonyCamera::takePhoto()
{
    if (!m_connected || cameraHandle == 0) { emit errorOccurred("Camera not connected."); return; }

    bool wasLvActive = m_liveViewActive;
    if (wasLvActive) m_lvTimer->stop();

    SCRSDK::CrDeviceHandle h = cameraHandle;
    QThreadPool::globalInstance()->start([h]() {
        SCRSDK::SendCommand(h, SCRSDK::CrCommandId_Release, SCRSDK::CrCommandParam_Down);
        SCRSDK::SendCommand(h, SCRSDK::CrCommandId_Release, SCRSDK::CrCommandParam_Up);
    });

    if (wasLvActive) {
        QTimer::singleShot(800, this, [this]() {
            if (m_liveViewActive) m_lvTimer->start();
        });
    }
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
    SCRSDK::CrError err = SCRSDK::GetLiveViewImageInfo(cameraHandle, &imgInfo);
    if (err != SCRSDK::CrError_None) return;

    CrInt32u bufSize = imgInfo.GetBufferSize();
    if (bufSize == 0) return;


    std::vector<CrInt8u> buf(bufSize);
    SCRSDK::CrImageDataBlock dataBlock;
    dataBlock.SetSize(bufSize);
    dataBlock.SetData(buf.data());

    err = SCRSDK::GetLiveViewImage(cameraHandle, &dataBlock);
    if (err != SCRSDK::CrError_None) return;

    const CrInt8u* imgData  = dataBlock.GetImageData();
    CrInt32u       imgBytes = dataBlock.GetImageSize();

    if (!imgData || imgBytes == 0) return;

    QByteArray jpegData(reinterpret_cast<const char*>(imgData),
                        static_cast<int>(imgBytes));

    QImage frame;
    if (frame.loadFromData(jpegData, "JPEG") && !frame.isNull()) {
        emit liveViewFrameReady(frame);
    }
}


void SonyCamera::fetchFocusRange()
{
    if (!m_connected || cameraHandle == 0) return;

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

            m_focusPosition = static_cast<quint32>(p.GetCurrentValue() & 0xFFFF);
            SCRSDK::ReleaseDeviceProperties(cameraHandle, props);
        }
    }

    {
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


void SonyCamera::fetchProperty(SCRSDK::CrDevicePropertyCode code,
                               QVariantList& outValues, quint64& outCurrent)
{
    outValues.clear();
    outCurrent = 0;
    if (!m_connected || cameraHandle == 0) return;

    CrInt32u numCodes = 1;
    CrInt32u codeArr  = static_cast<CrInt32u>(code);

    SCRSDK::CrDeviceProperty* props = nullptr;
    CrInt32 numProps = 0;

    SCRSDK::CrError err = SCRSDK::GetSelectDeviceProperties(
        cameraHandle, numCodes, &codeArr, &props, &numProps);

    if (err != SCRSDK::CrError_None || !props || numProps == 0) return;

    SCRSDK::CrDeviceProperty& p = props[0];
    outCurrent = p.GetCurrentValue();

    CrInt32u elemSize = 4;
    switch (p.GetValueType()) {
    case SCRSDK::CrDataType_UInt8:
    case SCRSDK::CrDataType_Int8:   elemSize = 1; break;
    case SCRSDK::CrDataType_UInt16:
    case SCRSDK::CrDataType_Int16:  elemSize = 2; break;
    case SCRSDK::CrDataType_UInt32:
    case SCRSDK::CrDataType_Int32:  elemSize = 4; break;
    case SCRSDK::CrDataType_UInt64:
    case SCRSDK::CrDataType_Int64:  elemSize = 8; break;
    default: elemSize = 4; break;
    }

    CrInt8u* raw  = p.GetSetValues();
    CrInt32u size = p.GetSetValueSize();

    if (raw && size > 0) {
        CrInt32u count = size / elemSize;
        for (CrInt32u i = 0; i < count; i++) {
            quint64 val = 0;
            memcpy(&val, raw + i * elemSize, elemSize);
            outValues.append(QVariant::fromValue(val));
        }
    } else if (p.IsSetEnableCurrentValue()) {
        CrInt8u* rv   = p.GetValues();
        CrInt32u rvSz = p.GetValueSize();

        qint64 mn = 0, mx = 0, step = 1;
        bool gotRange = false;

        if (rv && rvSz >= elemSize * 3) {
            auto readVal = [&](int idx) -> qint64 {
                qint64 v = 0;
                memcpy(&v, rv + idx * elemSize, elemSize);
                if      (elemSize == 1) v = static_cast<qint8>(static_cast<quint8>(v));
                else if (elemSize == 2) v = static_cast<qint16>(static_cast<quint16>(v));
                else if (elemSize == 4) v = static_cast<qint32>(static_cast<quint32>(v));
                return v;
            };
            mn   = readVal(0);
            mx   = readVal(1);
            step = readVal(2);
            if (step == 0) step = 1;
            if (mn < mx && step > 0 && (mx - mn) / step < 10000)
                gotRange = true;
        }

        if (!gotRange) {
            switch (code) {
            case SCRSDK::CrDeviceProperty_ExposureBiasCompensation:
                mn = -5000; mx = 5000; step = 333; gotRange = true;
                break;
            case SCRSDK::CrDeviceProperty_CreativeLook_Sharpness:
                mn = -3; mx = 3; step = 1; gotRange = true;
                break;
            case SCRSDK::CrDeviceProperty_MonitorBrightnessManual:
                mn = -2; mx = 2; step = 1; gotRange = true;
                break;
            default:
                break;
            }
        }

        if (gotRange) {
            for (qint64 v = mn; v <= mx; v += step) {
                quint64 bits = 0;
                memcpy(&bits, &v, elemSize);
                if (elemSize < 8)
                    bits &= (quint64(1) << (elemSize * 8)) - 1;
                outValues.append(QVariant::fromValue(bits));
            }
        }
    }

    SCRSDK::ReleaseDeviceProperties(cameraHandle, props);
}

void SonyCamera::fetchAllSettings()
{
    fetchProperty(SCRSDK::CrDeviceProperty_IsoSensitivity,              m_isoValues,        m_currentISO);
    fetchProperty(SCRSDK::CrDeviceProperty_ShutterSpeed,                m_shutterValues,    m_currentShutter);
    fetchProperty(SCRSDK::CrDeviceProperty_ExposureBiasCompensation,    m_exposureValues,   m_currentExposure);
    fetchProperty(SCRSDK::CrDeviceProperty_CreativeLook_Sharpness,      m_sharpnessValues,  m_currentSharpness);
    fetchProperty(SCRSDK::CrDeviceProperty_MonitorBrightnessManual,     m_brightnessValues, m_currentBrightness);

    qDebug() << "Settings fetched:"
             << "ISO:" << m_isoValues.size()
             << "Shutter:" << m_shutterValues.size()
             << "EV:" << m_exposureValues.size()
             << "Sharpness:" << m_sharpnessValues.size() << "(cur=" << m_currentSharpness << ")"
             << "Brightness:" << m_brightnessValues.size()
             << QString("(cur=0x%1)").arg(m_currentBrightness, 8, 16, QChar('0'));

    emit settingsChanged();
}

bool SonyCamera::setProperty(SCRSDK::CrDevicePropertyCode code, quint64 value)
{
    if (!m_connected || cameraHandle == 0) return false;

    SCRSDK::CrDataType dtype;
    switch (code) {
    case SCRSDK::CrDeviceProperty_ExposureBiasCompensation:
        dtype = SCRSDK::CrDataType_UInt16;  break;
    case SCRSDK::CrDeviceProperty_CreativeLook_Sharpness:
        dtype = SCRSDK::CrDataType_Int8;    break;
    case SCRSDK::CrDeviceProperty_MonitorBrightnessManual:
        dtype = SCRSDK::CrDataType_Int16;   break;
    case SCRSDK::CrDeviceProperty_ShutterSpeed:
        dtype = SCRSDK::CrDataType_UInt32;  break;
    case SCRSDK::CrDeviceProperty_IsoSensitivity:
        dtype = SCRSDK::CrDataType_UInt32;  break;
    default:
        dtype = SCRSDK::CrDataType_UInt32;  break;
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
    if (setProperty(SCRSDK::CrDeviceProperty_MonitorBrightnessManual, value)) {
        m_currentBrightness = value;
        emit settingsChanged();
        emit logMessage(QString("Brightness set to: %1").arg(value));
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

QString SonyCamera::formatBrightness(quint64 raw) const
{
    qint16 val = static_cast<qint16>(raw & 0xFFFF);
    quint16 hi = static_cast<quint16>((raw >> 16) & 0xFFFF);
    if (hi != 0 || val == static_cast<qint16>(0xFFFE) || val == static_cast<qint16>(0xFFFF))
        return "—";
    return QString::number(val);
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

    m_exifModel = fetchStr(SCRSDK::CrDeviceProperty_ModelName);
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
        case SCRSDK::CrExposure_M_Manual:            m_exifExposureMode = "Manual (M)";       break;
        case SCRSDK::CrExposure_P_Auto:              m_exifExposureMode = "Program (P)";      break;
        case SCRSDK::CrExposure_A_AperturePriority:  m_exifExposureMode = "Aperture (A)";     break;
        case SCRSDK::CrExposure_S_ShutterSpeedPriority: m_exifExposureMode = "Shutter (S)";   break;
        case SCRSDK::CrExposure_Auto:                m_exifExposureMode = "Intelligent Auto"; break;
        case SCRSDK::CrExposure_Auto_Plus:           m_exifExposureMode = "Superior Auto";    break;
        case SCRSDK::CrExposure_Movie_P:             m_exifExposureMode = "Movie P";          break;
        case SCRSDK::CrExposure_Movie_A:             m_exifExposureMode = "Movie A";          break;
        case SCRSDK::CrExposure_Movie_S:             m_exifExposureMode = "Movie S";          break;
        case SCRSDK::CrExposure_Movie_M:             m_exifExposureMode = "Movie M";          break;
        default:
            m_exifExposureMode = em > 0
                                     ? QString("Mode 0x%1").arg(em, 0, 16)
                                     : "—";
            break;
        }
    }

    {
        quint64 wb = fetchUint(SCRSDK::CrDeviceProperty_WhiteBalance);
        switch (static_cast<quint16>(wb & 0xFFFF)) {
        case SCRSDK::CrWhiteBalance_AWB:                   m_exifWhiteBalance = "Auto";              break;
        case SCRSDK::CrWhiteBalance_Daylight:              m_exifWhiteBalance = "Daylight";          break;
        case SCRSDK::CrWhiteBalance_Shadow:                m_exifWhiteBalance = "Shadow";            break;
        case SCRSDK::CrWhiteBalance_Cloudy:                m_exifWhiteBalance = "Cloudy";            break;
        case SCRSDK::CrWhiteBalance_Tungsten:              m_exifWhiteBalance = "Tungsten";          break;
        case SCRSDK::CrWhiteBalance_Fluorescent:           m_exifWhiteBalance = "Fluorescent";       break;
        case SCRSDK::CrWhiteBalance_Flush:                 m_exifWhiteBalance = "Flash";             break;
        case SCRSDK::CrWhiteBalance_ColorTemp:             m_exifWhiteBalance = "Color Temp";        break;
        case SCRSDK::CrWhiteBalance_Custom_1:              m_exifWhiteBalance = "Custom 1";          break;
        case SCRSDK::CrWhiteBalance_Custom_2:              m_exifWhiteBalance = "Custom 2";          break;
        case SCRSDK::CrWhiteBalance_Custom_3:              m_exifWhiteBalance = "Custom 3";          break;
        case SCRSDK::CrWhiteBalance_Custom:                m_exifWhiteBalance = "Custom";            break;
        case SCRSDK::CrWhiteBalance_Underwater_Auto:       m_exifWhiteBalance = "Underwater Auto";   break;
        default:
            m_exifWhiteBalance = wb > 0 ? QString("0x%1").arg(wb, 0, 16) : "—";
            break;
        }
    }

    {
        quint64 bat = fetchUint(SCRSDK::CrDeviceProperty_BatteryLevel);
        switch (static_cast<quint32>(bat)) {
        case SCRSDK::CrBatteryLevel_PreEndBattery:   m_exifBatteryLevel = "Critical";      break;
        case SCRSDK::CrBatteryLevel_1_4:             m_exifBatteryLevel = "25%";           break;
        case SCRSDK::CrBatteryLevel_2_4:             m_exifBatteryLevel = "50%";           break;
        case SCRSDK::CrBatteryLevel_3_4:             m_exifBatteryLevel = "75%";           break;
        case SCRSDK::CrBatteryLevel_4_4:             m_exifBatteryLevel = "100%";          break;
        case SCRSDK::CrBatteryLevel_1_3:             m_exifBatteryLevel = "33%";           break;
        case SCRSDK::CrBatteryLevel_2_3:             m_exifBatteryLevel = "66%";           break;
        case SCRSDK::CrBatteryLevel_3_3:             m_exifBatteryLevel = "100%";          break;
        case SCRSDK::CrBatteryLevel_USBPowerSupply:  m_exifBatteryLevel = "USB Power";     break;
        case SCRSDK::CrBatteryLevel_BatteryNotInstalled: m_exifBatteryLevel = "No Battery"; break;
        default:
            m_exifBatteryLevel = bat > 0 ? QString("Level 0x%1").arg(bat, 0, 16) : "—";
            break;
        }
    }

    qDebug() << "EXIF — model:" << m_exifModel << "lens:" << m_exifLens
             << "aperture:" << m_exifAperture << "mode:" << m_exifExposureMode
             << "WB:" << m_exifWhiteBalance << "battery:" << m_exifBatteryLevel;

    emit exifReady();
}


void SonyCamera::setupSaveInfo()
{
    SCRSDK::CrError err = SCRSDK::SetSaveInfo(
        cameraHandle,
        const_cast<CrChar*>(m_savePathW.c_str()),
        const_cast<CrChar*>(L""),
        SCRSDK::CrSETSAVEINFO_AUTO_NUMBER);

    if (err == SCRSDK::CrError_None) {
        emit logMessage("Save folder set: " + m_savePath);
    } else {
        emit errorOccurred(QString("SetSaveInfo failed: %1").arg(err));
    }

    SCRSDK::SetDeviceSetting(cameraHandle, SCRSDK::Setting_Key_EnablePostView,          SCRSDK::CrDeviceSetting_Enable);
    SCRSDK::SetDeviceSetting(cameraHandle, SCRSDK::Setting_Key_PostViewTransferringType, SCRSDK::CrPostViewTransferring_Legacy);
    SCRSDK::SetDeviceSetting(cameraHandle, SCRSDK::Setting_Key_PartialBuffer,            1);

    QTimer::singleShot(500, this, [this]() { fetchAllSettings(); });
    QTimer::singleShot(800, this, [this]() { fetchFocusRange(); });
}


void SonyCamera::OnConnected(SCRSDK::DeviceConnectionVersioin version)
{
    qDebug() << "Camera connected!";
    m_connected = true;
    emit connectionChanged(true);
    QTimer::singleShot(300, this, [this]() { setupSaveInfo(); });
}

void SonyCamera::OnDisconnected(CrInt32u error)
{
    qDebug() << "Camera disconnected. Code:" << error;
    m_lvTimer->stop();
    m_liveViewActive = false;
    emit liveViewActiveChanged(false);
    m_connected = false;
    cameraHandle = 0;
    emit connectionChanged(false);
}

void SonyCamera::OnPropertyChanged()
{
    if (m_connected) {
        QTimer::singleShot(100, this, [this]() { fetchAllSettings(); });
        QTimer::singleShot(150, this, [this]() {
            if (!m_connected || cameraHandle == 0 || m_focusDragging) return;
            CrInt32u codeArr = static_cast<CrInt32u>(SCRSDK::CrDeviceProperty_FocusPositionCurrentValue);
            SCRSDK::CrDeviceProperty* props = nullptr;
            CrInt32 numProps = 0;
            SCRSDK::CrError err = SCRSDK::GetSelectDeviceProperties(
                cameraHandle, 1, &codeArr, &props, &numProps);
            if (err == SCRSDK::CrError_None && props && numProps > 0) {
                quint32 live = static_cast<quint32>(props[0].GetCurrentValue() & 0xFFFF);
                if (live > 0 && live != m_focusPosition) {
                    m_focusPosition = live;
                    emit focusPositionChanged(m_focusPosition);
                }
                SCRSDK::ReleaseDeviceProperties(cameraHandle, props);
            }
        });
    }
}

void SonyCamera::OnLvPropertyChanged()
{
    if (m_liveViewActive) {
        QMetaObject::invokeMethod(this, [this]() {
            pollLiveViewFrame();
        }, Qt::QueuedConnection);
    }
}

void SonyCamera::OnCompleteDownload(CrChar* filename, CrInt32u type)
{
    if (!filename) { emit errorOccurred("Download callback: filename is null."); return; }
    QString localPath = QString::fromWCharArray(filename);
    QFileInfo fi(localPath);
    if (fi.exists()) {
        emit photoTaken(localPath);
    } else {
        QString copy = localPath;
        QTimer::singleShot(300, this, [this, copy]() {
            if (QFileInfo::exists(copy)) emit photoTaken(copy);
            else emit errorOccurred("Photo file not found: " + copy);
        });
    }
}

void SonyCamera::OnWarning(CrInt32u w)   { qDebug() << "Warning:" << QString("0x%1").arg(w, 8, 16, QChar('0')); }
void SonyCamera::OnWarningExt(CrInt32u w, CrInt32, CrInt32, CrInt32) { qDebug() << "Warning ext:" << w; }
void SonyCamera::OnError(CrInt32u error) { emit errorOccurred(QString("Camera error: 0x%1").arg(error, 8, 16, QChar('0'))); }

bool SonyCamera::isConnected() const { return m_connected; }




void SonyCamera::setWhiteBalance(quint32 value)
{
    if (!setProperty(SCRSDK::CrDeviceProperty_WhiteBalance,
                     static_cast<quint64>(value)))
    {
        emit errorOccurred("Failed to set White Balance");
        return;
    }

    QString label;
    switch (static_cast<quint16>(value & 0xFFFF)) {
    case SCRSDK::CrWhiteBalance_AWB:             label = "Auto";             break;
    case SCRSDK::CrWhiteBalance_Daylight:        label = "Daylight";         break;
    case SCRSDK::CrWhiteBalance_Shadow:          label = "Shade";            break;
    case SCRSDK::CrWhiteBalance_Cloudy:          label = "Cloudy";           break;
    case SCRSDK::CrWhiteBalance_Tungsten:        label = "Incandescent";     break;
    case SCRSDK::CrWhiteBalance_Fluorescent:     label = "Fluorescent";      break;
    case SCRSDK::CrWhiteBalance_Flush:           label = "Flash";            break;
    case SCRSDK::CrWhiteBalance_Custom:          label = "Custom";           break;
    default:
        label = QString("0x%1").arg(value, 0, 16);
        break;
    }

    emit settingsChanged();
    emit logMessage("White Balance set to: " + label);
}