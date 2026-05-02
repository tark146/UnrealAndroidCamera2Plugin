## meta quest camera2 plugin for unreal engine

simple, fast camera2 access for unreal engine projects on android and meta quest. streams camera frames into a UE texture for realtime use in games and XR apps. this is a fork of the original by @tark146 but since they are not seeming to be merging pull requests - this is now where I will be maintaining and pushing changes.

### what it does
- allows you to quickly access your quest passthrough cameras
- camera2 frame path wired to a UE `Texture2D` with BGRA8 updates on the render thread
- **deterministic camera selection** (left camera ID 50 by default, or explicitly select left/right)
- camera intrinsics exposed (fx, fy, cx, cy, skew) - **automatically adjusted for stream resolution**
- **camera pose (CamInHmd)** extracted from device calibration for accurate spatial tracking
- lens distortion coefficients retrieved and mapped for UE usage
- camera characteristics JSON dump available for diagnostics
- **quest 3 hardcoded calibration fallback** when runtime data isn't available
- blueprint getters for texture, intrinsics, distortion, pose, and resolutions

---

## quick start

1. enable the plugin in your project
2. call `StartCameraPreview` (blueprint) or `StartCameraPreviewWithSelection(true/false)` for explicit L/R
3. get the camera texture and apply it to a material/mesh or UI image
4. for pose estimation, use `GetCurrentQuest3Calibration()` to get properly adjusted intrinsics and CamInHmd
5. call `StopCameraPreview` when done

---

## requirements
- unreal engine 5.0+
- android SDK 21+
- camera permissions enabled on device
- works on standalone android (including meta quest 2/3/pro)
- windows editor: returns null texture (for workflow only)

---

## installation

1. copy the `AndroidCamera2Plugin` folder to your project's `Plugins` directory
2. regenerate project files
3. enable the plugin in your project settings or .uproject file

---

## API reference

### core camera functions

| function | description |
|----------|-------------|
| `StartCameraPreview()` | start camera (defaults to LEFT camera) |
| `StartCameraPreviewWithSelection(bool bUseLeftCamera)` | start with explicit L/R selection |
| `StopCameraPreview()` | stop camera and release resources |
| `GetCameraTexture()` | get the camera feed texture (null if not started) |

### camera selection (quest 3: ID 50 = left, ID 51 = right)

| function | description |
|----------|-------------|
| `SetPreferredCamera(bool bUseLeftCamera)` | set preference before starting |
| `GetPreferredCamera()` | get current preference |
| `GetSelectedCameraId()` | get the active camera ID (e.g., "50") |
| `IsLeftCamera()` | check if left camera is active |

### intrinsics (stream-adjusted)

| function | description |
|----------|-------------|
| `GetCameraFx()` | focal length X (pixels, for stream resolution) |
| `GetCameraFy()` | focal length Y (pixels, for stream resolution) |
| `GetPrincipalPoint()` | principal point (cx, cy) |
| `GetCameraSkew()` | skew coefficient |
| `GetCalibrationResolution()` | resolution these intrinsics are for |
| `GetOriginalResolution()` | native sensor resolution (1280x1280) |

### camera pose (CamInHmd)

| function | description |
|----------|-------------|
| `IsCameraPoseAvailable()` | check if device provided pose data |
| `GetCameraPoseTranslation()` | camera position in HMD space (cm, UE coords) |
| `GetCameraPoseRotation()` | camera rotation in HMD space (UE coords) |
| `GetCamInHmdTransform()` | full transform for use in pose estimation |

### quest 3 calibration (hardcoded fallback)

| function | description |
|----------|-------------|
| `GetQuest3Calibration(bool bLeftCamera, int32 StreamWidth, int32 StreamHeight)` | get hardcoded calibration for L/R camera |
| `GetCurrentQuest3Calibration(int32 StreamWidth, int32 StreamHeight)` | get calibration for active camera |

the `FQuest3CameraCalibration` struct contains:
- `CameraId`, `bIsLeftCamera` - camera identification
- `NativeFx/Fy/Cx/Cy` - intrinsics for native 1280x1280 sensor
- `StreamFx/Fy/Cx/Cy` - intrinsics adjusted for your stream resolution
- `NativeWidth/Height`, `StreamWidth/Height` - resolutions
- `PoseTranslationCm`, `PoseRotation` - CamInHmd pose (UE coordinates)
- `GetCamInHmdTransform()` - helper to get FTransform

### lens distortion

| function | description |
|----------|-------------|
| `GetLensDistortion()` | raw distortion coefficients from device |
| `GetLensDistortionUE()` | mapped to UE order [K1,K2,P1,P2,K3,K4,K5,K6] |

### diagnostics

| function | description |
|----------|-------------|
| `GetCameraCharacteristics(bool bRedump, FString& OutJson, FString& OutFilePath)` | get full characteristics JSON |

---

## quest 3 camera specifications

### hardware
- **left camera**: ID 50
- **right camera**: ID 51
- **native resolution**: 1280x1280 (square sensor)
- **stream resolution**: 1280x960 (center-cropped to 4:3)
- **FOV**: ~110° (wide angle, some barrel distortion)

### intrinsics (native 1280x1280)

| camera | fx | fy | cx | cy |
|--------|-----|-----|-------|-------|
| left (50) | 870.60 | 870.60 | 640.25 | 641.24 |
| right (51) | 869.41 | 869.41 | 635.98 | 636.24 |

### intrinsics adjustment for stream resolution

when streaming at 1280x960 (center crop from 1280x1280):
- **focal lengths unchanged** (pixels aren't scaled, just cropped)
- **principal point shifted**: cy_stream = cy_native - 160

for left camera at 1280x960:
- fx = 870.60, fy = 870.60, cx = 640.25, **cy = 481.24**

### camera pose (CamInHmd) in UE coordinates

**translation:**

| camera | X (forward) | Y (right) | Z (up) |
|--------|-------------|-----------|--------|
| Left (50) | 6.29 cm | -3.19 cm | -1.72 cm |
| Right (51) | 6.28 cm | 3.17 cm | -1.71 cm |

**rotation:** Both cameras are tilted approximately **11° downward** (negative pitch in UE).

the cameras are positioned ~6.3cm forward of the HMD center, ~3.2cm to the side, and ~1.7cm below.
---


## coordinate System Conversion

the plugin automatically converts Android/OpenGL coordinates to UE coordinates.

**validated against Meta's official Unity sample** which produces:
- Lens Offset Position: `(-0.03, -0.02, 0.06)` meters
- Lens Offset Rotation: `(11.24°, 0.26°, 359.50°)` Euler angles


### translation

**Android/OpenGL**: X-right, Y-up, Z-backward (toward user)
**Unreal Engine**: X-forward, Y-right, Z-up

Formula: `UE = (-Android_Z * 100, Android_X * 100, Android_Y * 100)` (meters → cm)

### rotation

the rotation conversion follows Meta's Unity approach:
1. Negate X and Y components of the Android quaternion
2. Compute the inverse (conjugate)
3. Apply 180° rotation around the X axis (accounts for optical axis direction)
4. Transform from Unity to UE coordinate axes

this produces approximately **-11° pitch** in UE (looking down), matching the physical camera orientation.

---

## permissions

android will display a permission request dialog for camera access. grant all camera permissions and **restart the application** to enable camera functionality. restart isn't strictly needed but encouraged especially for first bootup.

the plugin automatically adds:
- `android.permission.CAMERA`
- `horizonos.permission.HEADSET_CAMERA` (Meta Quest)
- `horizonos.permission.AVATAR_CAMERA` (Meta Quest)

---

## architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        blueprint                            │
├─────────────────────────────────────────────────────────────┤
│                    SimpleCamera2Test.cpp                    │
│  - JNI callbacks receive frame/intrinsics/pose data         │
│  - blueprint accessors expose data to game logic            │
│  - Quest 3 hardcoded calibration as fallback                │
├─────────────────────────────────────────────────────────────┤
│                    Camera2Helper.java                       │
│  - Camera2 API session management                           │
│  - intrinsics extraction & stream-adjustment                │
│  - camera pose extraction (LENS_POSE_*)                     │
│  - YUV→RGBA conversion                                      │
│  - deterministic camera selection (prefers left=50)         │
└─────────────────────────────────────────────────────────────┘
```

---

## current limitations
- Quest 3 does not report lens distortion via Camera2 API (use approximate values)
- resolution selection is fixed in code (1280x960)

---

## troubleshooting

| issue | solution |
|-------|----------|
| black texture | Wait for auto-exposure, check logcat for errors |
| camera not starting | Grant permissions and restart app |
| intrinsics are 0 | Camera not started yet, use hardcoded fallback |
---

## contribution
make changes, PR and contribute! :)
