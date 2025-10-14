# Android Camera2 Plugin for Unreal Engine

English | [日本語](README_JP.md)

## Motivation
Unreal Engine lacks official Camera2 API support, creating a significant barrier for XR developers who want to access device cameras on Android and Meta Quest platforms. This plugin bridges that gap by handling the complex Java/JNI integration that most Unreal developers aren't familiar with.

The goal is to **accelerate XR development in Unreal Engine** by providing easy camera access, enabling developers to focus on creating innovative XR experiences rather than wrestling with platform-specific implementations.

## Overview
This plugin provides Camera2 API access for Unreal Engine projects, specifically designed for Android and Meta Quest devices. It enables real-time camera feed capture and display as a texture within your Unreal Engine application.

## Features
- Android Camera2 API integration
- Real-time camera preview to UE texture
- Support for Meta Quest 3 passthrough cameras
- Full color YUV to RGBA conversion (with color calibration in progress)
- Simple Blueprint interface

## Supported Platforms
- Android (including Meta Quest 2/3/Pro)
- Windows (Editor only - returns null texture)

## Requirements
- Unreal Engine 5.0 or later
- Android SDK Level 21+ (Android 5.0 Lollipop)
- Camera permissions in Android Manifest

## Tested Environment
- **Unreal Engine**: 5.3
- **Device**: Meta Quest 3
- **Camera ID**: 50 (Quest 3 passthrough camera)
- **OS**: Quest system software (Android-based)

## Installation

1. Copy the `AndroidCamera2Plugin` folder to your project's `Plugins` directory
2. Regenerate project files
3. Enable the plugin in your project settings or .uproject file

## Usage

### Blueprint Setup

1. **Start Camera Preview:**
   ```
   SimpleCamera2Test::StartCameraPreview() -> bool
   ```
   Returns true if camera started successfully

2. **Get Camera Texture:**
   ```
   SimpleCamera2Test::GetCameraTexture() -> Texture2D
   ```
   Returns the camera feed texture (can be null if not started)

3. **Stop Camera Preview:**
   ```
   SimpleCamera2Test::StopCameraPreview()
   ```
   Stops the camera and releases resources

### Quick Start with Sample Blueprint

1. **Using the provided sample:**
   - Place `BP_CameraImage` actor (found in the plugin content) into your level
   - This actor contains a Widget with complete Blueprint implementation
   - Refer to the Widget Blueprint for implementation details

2. **Manual Blueprint Implementation:**
   - Create a new Actor Blueprint
   - Add a Plane or UI Image component
   - In BeginPlay:
     - Call `StartCameraPreview`
     - Get the camera texture using `GetCameraTexture`
     - Create a Dynamic Material Instance
     - Set the texture parameter to the camera texture
     - Apply the material to your plane/image

### C++ Usage

```cpp
#include "SimpleCamera2Test.h"

// Start camera
bool bSuccess = USimpleCamera2Test::StartCameraPreview();

// Get texture
UTexture2D* CameraTexture = USimpleCamera2Test::GetCameraTexture();

// Stop camera
USimpleCamera2Test::StopCameraPreview();
```

## Permissions

### Important Note: First Run Permission Dialog

**On first app launch**, Android will display a permission request dialog for camera access. Please grant all camera permissions and **restart the application** to enable camera functionality.

These permissions should normally be auto-granted via UE's `ExtraPermissions`, but this is currently not working properly. Sorry for the inconvenience - this is the current specification.

The plugin automatically adds the following permissions to your Android manifest:

- `android.permission.CAMERA`
- `horizonos.permission.HEADSET_CAMERA` (Meta Quest)
- `horizonos.permission.AVATAR_CAMERA` (Meta Quest)

## Technical Details

### Architecture
- **C++ Layer**: UObject-based interface for Blueprint/C++ access
- **JNI Bridge**: Native C++ to Java communication
- **Java Layer**: Camera2 API implementation with ImageReader
- **Frame Processing**: YUV_420_888 to RGBA conversion

### Camera Selection Priority
1. Meta Quest special cameras (ID 50, 51) - for passthrough
2. Front-facing camera
3. Back-facing camera
4. Any available camera as fallback

### Performance
- Default resolution: 320x240 (configurable in code)
- Frame format: YUV_420_888 -> BGRA8
- Processing: Background thread for camera operations
- Texture update: Game thread synchronized

### Changing Resolution

The default resolution is 800x600, but you can change it to other supported resolutions. **Important**: You must update ALL THREE locations consistently to avoid memory corruption.

**Supported Resolutions (Quest 3):**
- 320x240
- 640x480
- 800x600 (default)
- 1280x960

**Required Changes:**

1. **Java Side** - Edit `Source/AndroidCamera2Plugin/Android/src/com/epicgames/ue4/Camera2Helper.java`:
```java
private int frameWidth = 800;   // Change this
private int frameHeight = 600;  // Change this
```

2. **C++ Side (Texture Creation)** - Edit `Source/AndroidCamera2Plugin/Private/SimpleCamera2Test.cpp`:
```cpp
CameraTexture = UTexture2D::CreateTransient(800, 600, PF_B8G8R8A8);
```

3. **C++ Side (Memory Initialization)** - Same file, a few lines below:
```cpp
FMemory::Memset(TextureData, 64, 800 * 600 * 4); // width * height * 4(RGBA)
```

**After Changes:**
1. Clean build folders: Delete `Intermediate` and `Binaries` folders in both plugin and project
2. Regenerate project files
3. Rebuild the project

⚠️ **Warning**: If these three values don't match, you'll get memory access violations and texture corruption.

### Current Limitations (v1.0)
- **Color accuracy issues** - Full color YUV to RGB conversion implemented but may show warm/orange tint
- **Color space calibration needed** - Quest 3 cameras may use specific color space requiring fine-tuning
- These are temporary limitations for the initial release

## Known Issues
- Initial frames may appear dark until camera auto-exposure adjusts
- Quest 3 passthrough cameras require special permissions
- Camera preview may not work in editor (Android only)

## Troubleshooting

### Camera not starting
- Check Android logcat for permission errors
- Verify camera permissions are granted
- Ensure no other app is using the camera

### Black/Dark texture
- Camera auto-exposure may need time to adjust
- Check if YUV data is being received (see logs)
- Verify texture format compatibility

### Quest specific issues
- Ensure Quest-specific permissions are granted
- Try camera IDs 50 or 51 for passthrough cameras
- Check if passthrough is enabled in system settings

## License
MIT License - See LICENSE file for details

## Development Notes
This plugin was developed through extensive trial and error with AI assistance. While AI helped with code generation and problem-solving, the actual implementation required countless iterations, debugging sessions, and real device testing to get the Camera2 API working properly with Unreal Engine. The journey from "camera not found" to "real-time texture streaming" was filled with challenges, but that's exactly why this plugin needed to exist - so others don't have to go through the same struggle.

## Author
TARK (Olachat)

## Support
For issues and questions, please create an issue in the GitHub repository.

## Changelog

### Version 1.0
- Initial release
- Basic Camera2 API integration
- Meta Quest support
- Grayscale preview (YUV Y-channel only)
- 320x240 fixed resolution
- Note: Color support and configurable resolution planned for future versions