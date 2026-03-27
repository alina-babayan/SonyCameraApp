📷 Sony Camera Control & Photo Editor

(Qt + Sony SDK Project Documentation)

1. 📌 Overview

This project is a desktop application built with Qt (C++ + QML) that allows users to:

Connect to a Sony camera via USB
Capture photos remotely
View live camera feed (Live View)
Adjust camera settings (ISO, shutter, exposure, etc.)
Display EXIF metadata
View and manage captured images
Perform basic image viewing operations (zoom, rotate, compare, histogram)

The application integrates the Sony Camera Remote SDK (SCRSDK) for hardware communication.

2. 🏗️ Architecture

The system follows a hybrid architecture:

Backend (C++)
Handles camera communication via Sony SDK
Processes image data
Manages device properties and EXIF info
Emits signals to UI
Frontend (QML)
Provides graphical interface
Displays images and live feed
Handles user interactions
User (QML UI)
      ↓
SonyCamera (C++ Controller)
      ↓
Sony SDK (SCRSDK)
      ↓
Sony Camera Device
3. ⚙️ Core Components
3.1 📦 SonyCamera Class (Backend)

Defined in:


Responsibilities:
SDK initialization & shutdown
Camera connection management
Live view streaming
Photo capture
Camera settings control
EXIF metadata retrieval
3.2 🎨 QML UI (Frontend)

Defined in:


Responsibilities:
User interface layout
Display images & live feed
User controls (buttons, sliders)
Histogram & comparison tools
Logging system
4. 🔌 Camera Lifecycle
4.1 Initialization
SCRSDK::Init();
Initializes Sony SDK
Must be called before any camera operation
4.2 Connection Flow
Enumerate devices
Select first camera
Connect using SDK
SCRSDK::EnumCameraObjects(...)
SCRSDK::Connect(...)
4.3 Disconnection
SCRSDK::Disconnect(...)
SCRSDK::ReleaseDevice(...)
4.4 Shutdown
SCRSDK::Release();
5. 📸 Features
5.1 Photo Capture
Triggered via:
SCRSDK::SendCommand(... CrCommandId_Release ...)
Automatically saves images to:
Pictures/SonyCaptures
5.2 Live View
Uses timer (~30 FPS):
m_lvTimer->setInterval(33);
Fetches JPEG frames:
SCRSDK::GetLiveViewImage(...)
Emits frames to UI:
emit liveViewFrameReady(frame);
5.3 Camera Settings Control

Supported settings:

Setting	Function
ISO	setISO()
Shutter Speed	setShutterSpeed()
Exposure	setExposure()
Sharpness	setSharpness()
Brightness	setBrightness()

Generic handler:

setProperty(code, value);
5.4 Manual Focus Control
Retrieves focus range
Allows slider-based adjustment
setFocusPosition(value);
5.5 EXIF Metadata

Fetched using:

fetchExifInfo();

Includes:

Camera model
Lens
Aperture
ISO
Shutter speed
White balance
Battery level
5.6 Image Viewer Features

From QML:

Zoom (mouse wheel / pinch)
Pan (drag)
Rotate
Fullscreen mode
Image comparison (before/after slider)
Histogram display
6. 🖥️ User Interface
Main Sections
🔹 Top Bar
Connection status indicator
Connect / Disconnect buttons
Error display
🔹 Left Panel
Open image
Take photo
Start/Stop live view
Rotate / fullscreen
🔹 Center Canvas
Image display
Live view stream
Compare mode
Histogram overlay
🔹 Right Panel
Camera settings controls
ISO, shutter, exposure, etc.
Manual focus slider
🔹 Bottom Panel
Toolbar (zoom, histogram, compare)
Image gallery (film strip)
🔹 Log Panel
Displays real-time logs:
Connection events
Errors
Actions
7. 🔄 Signals & Events
Backend → UI
liveViewFrameReady(frame)
photoTaken(filePath)
settingsChanged()
exifReady()
errorOccurred(message)
logMessage(message)
UI → Backend
connectCamera()
disconnectCamera()
takePhoto()
startLiveView()
setISO(value) etc.
8. 🧵 Multithreading
Uses QThreadPool for:
Camera connection
Photo capture

This prevents UI blocking.

9. 📁 File Storage

Images saved to:

~/Pictures/SonyCaptures

Auto-created if not existing.

10. ⚠️ Error Handling
SDK failures emit:
errorOccurred("message")
UI displays errors with timeout (5 seconds)
11. 🚀 Key Advantages
Real-time live view streaming
Full camera control via SDK
Modern responsive UI (QML)
Non-blocking multithreaded design
Advanced viewer tools (histogram, compare)
12. 🧩 Possible Improvements
RAW image support
Image editing tools (filters, crop)
Multi-camera support
Video recording
Cloud sync
13. 📚 Technologies Used
C++ (Qt Framework)
QML (Qt Quick UI)
Sony Camera Remote SDK (SCRSDK)
Qt Threading (QThreadPool)
Qt Multimedia / Image Handling
