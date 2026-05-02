#include "SimpleCamera2Test.h"
#include "Engine/Engine.h"
#include "Async/AsyncWork.h"
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "Rendering/Texture2DResource.h"
#include "RenderingThread.h"

DEFINE_LOG_CATEGORY(LogSimpleCamera2);

#if PLATFORM_ANDROID
#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"
#endif





// Static variables for camera preview
static UTexture2D* CameraTexture = nullptr;
static bool bCameraPreviewActive = false;

// Intrinsics storage (pixels)
static float GCameraFx = 0.0f;
static float GCameraFy = 0.0f;
static float GCameraCx = 0.0f;
static float GCameraCy = 0.0f;
static float GCameraSkew = 0.0f;
static int32 GCameraCalibWidth = 0;
static int32 GCameraCalibHeight = 0;

// Lens distortion storage
static TArray<float> GLensDistortionCoeffs;
static int32 GLensDistortionLength = 0;

// Original resolution storage
static int32 GOriginalResolutionWidth = 0;
static int32 GOriginalResolutionHeight = 0;

// JSON dump of full CameraCharacteristics
static FString GCameraCharacteristicsJson;
static FString GCameraCharacteristicsJsonPath;

// Camera selection info (Quest 3: 50=left, 51=right)
static FString GSelectedCameraId;
static bool GIsLeftCamera = true;

// Camera pose in HMD space (CamInHmd)
// Translation in meters, rotation as quaternion (x,y,z,w)
static FVector GCameraPoseTranslation = FVector::ZeroVector;
static FQuat GCameraPoseRotation = FQuat::Identity;
static bool GCameraPoseAvailable = false;

// Camera preference (for next StartCameraPreview call)
static bool GPreferLeftCamera = true;

// =============================================================================
// QUEST 3 HARDCODED CALIBRATION DATA
// Extracted from actual Quest 3 device dumps - these are the reference values
// =============================================================================
namespace Quest3Calibration
{
    // Native sensor resolution (both cameras)
    constexpr int32 NativeWidth = 1280;
    constexpr int32 NativeHeight = 1280;
    
    // LEFT CAMERA (ID 50) - Native 1280x1280 intrinsics (from JSON dump)
    constexpr float LeftFx = 870.6005249023438f;
    constexpr float LeftFy = 870.6005249023438f;
    constexpr float LeftCx = 640.2453002929688f;
    constexpr float LeftCy = 641.2428588867188f;
    
    // LEFT CAMERA pose in HMD space (meters, gyroscope reference)
    // Translation: [-0.03187, -0.01716, -0.06286] meters
    // Rotation: quaternion [x,y,z,w] from JSON (Android Camera2 order)
    // JSON: "rotation":[-0.9951009154319763,-0.0002342800289625302,-0.005589410196989775,0.09870576858520508]
    // Note: This is in Android/OpenGL convention
    constexpr float LeftTx = -0.03187057375907898f;
    constexpr float LeftTy = -0.01715778559446335f;
    constexpr float LeftTz = -0.06285717338323593f;
    constexpr float LeftQx = -0.9951009154319763f;
    constexpr float LeftQy = -0.0002342800289625302f;
    constexpr float LeftQz = -0.005589410196989775f;
    constexpr float LeftQw = 0.09870576858520508f;
    
    // RIGHT CAMERA (ID 51) - Native 1280x1280 intrinsics
    constexpr float RightFx = 869.4124755859375f;
    constexpr float RightFy = 869.4124755859375f;
    constexpr float RightCx = 635.97998046875f;
    constexpr float RightCy = 636.2386474609375f;
    
    // RIGHT CAMERA pose in HMD space (meters, gyroscope reference)
    // Translation: [0.03175, -0.01712, -0.06281] meters
    // Rotation: quaternion [x,y,z,w] from JSON (Android Camera2 order)
    // JSON: "rotation":[-0.9954029321670532,-0.00033292744774371386,0.00344613054767251,0.09571301192045212]
    constexpr float RightTx = 0.031745150685310367f;
    constexpr float RightTy = -0.017119500786066057f;
    constexpr float RightTz = -0.06280999630689621f;
    constexpr float RightQx = -0.9954029321670532f;
    constexpr float RightQy = -0.00033292744774371386f;
    constexpr float RightQz = 0.00344613054767251f;
    constexpr float RightQw = 0.09571301192045212f;
    
    // Convert Android/OpenGL pose to UE coordinate system
    // Android Camera2: X-right, Y-up, Z-backward (toward user), right-handed
    // UE: X-forward, Y-right, Z-up, left-handed
    //
    // VALIDATED AGAINST META'S OFFICIAL UNITY SAMPLE:
    // Unity uses: MRUK.FlipZ(translation) and complex quaternion transform
    // Result: ~11° downward pitch for Quest 3 passthrough cameras
    
    inline FVector ConvertTranslationToUE(float tx, float ty, float tz)
    {
        // Meta Unity approach: FlipZ (negate Z)
        // Then coordinate system transform: Android → UE
        // Android: X-right, Y-up, Z-backward → After FlipZ: X-right, Y-up, Z-forward
        // UE: X-forward, Y-right, Z-up
        //
        // So: UE_X = Android_Z (after flip = -original_Z)
        //     UE_Y = Android_X
        //     UE_Z = Android_Y
        // Convert meters to cm
        return FVector(-tz * 100.0f, tx * 100.0f, ty * 100.0f);
    }
    
    inline FQuat ConvertRotationToUE(float qx, float qy, float qz, float qw)
    {
        // =========================================================================
        // QUATERNION CONVERSION - VALIDATED AGAINST META'S UNITY SAMPLE
        // =========================================================================
        // Meta's Unity sample produces Euler angles: (11.24°, 0.26°, 359.50°)
        // This represents approximately 11° downward tilt for Quest 3 cameras.
        //
        // The Quest 3 cameras physically point ~11° downward to better capture
        // hand interactions. In UE coordinates, this should be ~-11° pitch
        // (negative pitch = looking down).
        //
        // Meta Unity transform:
        //   Quaternion.Inverse(new Quaternion(-x, -y, z, w)) * Quaternion.Euler(180, 0, 0)
        // =========================================================================
        
        // Combined steps 1-2: Start with (-qx,-qy,qz,qw), then conjugate gives (qx,qy,-qz,qw)
        float ax = qx;
        float ay = qy;
        float az = -qz;
        float aw = qw;
        
        // Normalize
        float mag = FMath::Sqrt(ax*ax + ay*ay + az*az + aw*aw);
        if (mag > SMALL_NUMBER)
        {
            ax /= mag; ay /= mag; az /= mag; aw /= mag;
        }
        
        // Step 3: Multiply by 180° rotation around X: R = (1, 0, 0, 0)
        // result.x = qw, result.y = qz, result.z = -qy, result.w = -qx
        float bx = aw;
        float by = az;
        float bz = -ay;
        float bw = -ax;
        
        // Step 4: Convert Unity (X-right, Y-up, Z-forward) to UE (X-forward, Y-right, Z-up)
        // Axis mapping: Unity_Z → UE_X, Unity_X → UE_Y, Unity_Y → UE_Z
        //
        // Unity X (right) is the pitch axis; UE Y (right) is the pitch axis.
        // Both engines are left-handed, so positive rotation around the right
        // axis tilts the forward vector DOWNWARD in both systems.
        // Therefore the pitch component maps 1:1 -- NO sign flip.
        //
        // Previous code had "-bx" which INVERTED the camera's 11° downward
        // tilt to 11° upward, causing every tag's world position to have a
        // ~38% vertical error that rotated with HMD orientation -- the root
        // cause of catastrophic Z-component disagreement between players.
        FQuat UEQuat(bz, bx, by, bw);
        UEQuat.Normalize();
        
        return UEQuat;
    }
}

#if PLATFORM_ANDROID
static jobject Camera2HelperInstance = nullptr;

static bool EnsureCamera2HelperInstance(JNIEnv* Env)
{
	if (!Env)
	{
		return false;
	}

	if (Camera2HelperInstance)
	{
		return true;
	}

	jobject Activity = FAndroidApplication::GetGameActivityThis();
	if (!Activity)
	{
		UE_LOG(LogSimpleCamera2, Error, TEXT("Game Activity is null; cannot acquire Camera2Helper instance"));
		return false;
	}

	jclass ActivityClass = Env->GetObjectClass(Activity);
	if (!ActivityClass)
	{
		UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to get Activity class while acquiring Camera2Helper"));
		return false;
	}

	jmethodID GetClassLoaderMethod = Env->GetMethodID(ActivityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
	if (!GetClassLoaderMethod)
	{
		UE_LOG(LogSimpleCamera2, Error, TEXT("getClassLoader not found on Activity"));
		Env->DeleteLocalRef(ActivityClass);
		return false;
	}

	jobject ClassLoader = Env->CallObjectMethod(Activity, GetClassLoaderMethod);
	if (Env->ExceptionCheck())
	{
		Env->ExceptionDescribe();
		Env->ExceptionClear();
		Env->DeleteLocalRef(ActivityClass);
		return false;
	}

	jclass ClassLoaderClass = Env->GetObjectClass(ClassLoader);
	jmethodID LoadClassMethod = nullptr;
	if (ClassLoaderClass)
	{
		LoadClassMethod = Env->GetMethodID(ClassLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
	}

	if (!LoadClassMethod)
	{
		UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to get loadClass method while acquiring Camera2Helper"));
		if (ClassLoaderClass) { Env->DeleteLocalRef(ClassLoaderClass); }
		Env->DeleteLocalRef(ClassLoader);
		Env->DeleteLocalRef(ActivityClass);
		return false;
	}

	jstring ClassName = Env->NewStringUTF("com.epicgames.ue4.Camera2Helper");
	jclass Camera2Class = (jclass)Env->CallObjectMethod(ClassLoader, LoadClassMethod, ClassName);
	Env->DeleteLocalRef(ClassName);

	if (Env->ExceptionCheck())
	{
		Env->ExceptionDescribe();
		Env->ExceptionClear();
		if (Camera2Class) { Env->DeleteLocalRef(Camera2Class); }
		if (ClassLoaderClass) { Env->DeleteLocalRef(ClassLoaderClass); }
		Env->DeleteLocalRef(ClassLoader);
		Env->DeleteLocalRef(ActivityClass);
		return false;
	}

	if (!Camera2Class)
	{
		UE_LOG(LogSimpleCamera2, Error, TEXT("Camera2Helper class not found while acquiring instance"));
		if (ClassLoaderClass) { Env->DeleteLocalRef(ClassLoaderClass); }
		Env->DeleteLocalRef(ClassLoader);
		Env->DeleteLocalRef(ActivityClass);
		return false;
	}

	jmethodID GetInstanceMethod = Env->GetStaticMethodID(Camera2Class, "getInstance", "(Landroid/content/Context;)Lcom/epicgames/ue4/Camera2Helper;");
	if (!GetInstanceMethod)
	{
		UE_LOG(LogSimpleCamera2, Error, TEXT("getInstance method not found on Camera2Helper"));
		Env->DeleteLocalRef(Camera2Class);
		if (ClassLoaderClass) { Env->DeleteLocalRef(ClassLoaderClass); }
		Env->DeleteLocalRef(ClassLoader);
		Env->DeleteLocalRef(ActivityClass);
		return false;
	}

	jobject LocalCamera = Env->CallStaticObjectMethod(Camera2Class, GetInstanceMethod, Activity);
	bool bSuccess = false;
	if (Env->ExceptionCheck())
	{
		Env->ExceptionDescribe();
		Env->ExceptionClear();
	}
	else if (LocalCamera)
	{
		Camera2HelperInstance = Env->NewGlobalRef(LocalCamera);
		Env->DeleteLocalRef(LocalCamera);
		bSuccess = (Camera2HelperInstance != nullptr);
		if (!bSuccess)
		{
			UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to create global ref for Camera2Helper"));
		}
	}
	else
	{
		UE_LOG(LogSimpleCamera2, Error, TEXT("getInstance returned null for Camera2Helper"));
	}

	Env->DeleteLocalRef(Camera2Class);
	if (ClassLoaderClass) { Env->DeleteLocalRef(ClassLoaderClass); }
	Env->DeleteLocalRef(ClassLoader);
	Env->DeleteLocalRef(ActivityClass);

	return bSuccess;
}
#endif



// JNI callback for real Camera2 frames
#if PLATFORM_ANDROID
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onFrameAvailable(
    JNIEnv* env, jclass clazz, jbyteArray data, jint width, jint height)
{
    // Early exit if camera is being stopped
    if (!bCameraPreviewActive)
    {
        return;
    }

    static bool bCamera2LogsOnce = false;
    if (!bCamera2LogsOnce)
    {
        UE_LOG(LogSimpleCamera2, Log,
            TEXT("Camera2 frame received: %dx%d"), width, height);
    }

    // Check texture validity
    if (!CameraTexture || !data)
    {
        if (!bCamera2LogsOnce)
        {
            UE_LOG(LogSimpleCamera2, Warning,
                TEXT("CameraTexture or data is null"));
        }
        bCamera2LogsOnce = true;
        return;
    }

    // Get frame data from Java
    jbyte* frameData = env->GetByteArrayElements(data, nullptr);
    if (!frameData)
    {
        if (!bCamera2LogsOnce)
        {
            UE_LOG(LogSimpleCamera2, Error,
                TEXT("Failed to get frame data from Java"));
        }
        bCamera2LogsOnce = true;
        return;
    }

    // Copy frame data
    int32 DataSize = width * height * 4;
    uint8* FrameDataCopy = new uint8[DataSize];
    FMemory::Memcpy(FrameDataCopy, frameData, DataSize);

    // Release Java array immediately
    env->ReleaseByteArrayElements(data, frameData, JNI_ABORT);

    // Update texture safely with validity check
    AsyncTask(ENamedThreads::GameThread,
        [FrameDataCopy, width, height]()
        {
            // Double-check camera is still active and texture exists
            if (bCameraPreviewActive && CameraTexture &&
                CameraTexture->GetResource())
            {
                FTexture2DResource* TextureResource =
                    static_cast<FTexture2DResource*>(CameraTexture->GetResource());
                const uint32 SrcPitch = static_cast<uint32>(width) * 4u;
                FUpdateTextureRegion2D Region(0, 0, 0, 0,
                    static_cast<uint32>(width), static_cast<uint32>(height));

                ENQUEUE_RENDER_COMMAND(UpdateCameraTexture2D)(
                    [TextureResource, Region, FrameDataCopy, SrcPitch]
                    (FRHICommandListImmediate& RHICmdList)
                    {
                        RHICmdList.UpdateTexture2D(
                            TextureResource->GetTexture2DRHI(),
                            0, Region, SrcPitch, FrameDataCopy);
                        delete[] FrameDataCopy;
                    });
            }
            else
            {
                // Clean up if camera was stopped
                delete[] FrameDataCopy;
            }
        });

    bCamera2LogsOnce = true;
}
#endif

#if PLATFORM_ANDROID
// JNI callback for full CameraCharacteristics JSON dump
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onCharacteristicsDumpAvailable(JNIEnv* env, jclass clazz,
    jstring jsonStr)
{
    const char* UTFChars = (jsonStr != nullptr) ? env->GetStringUTFChars(jsonStr, nullptr) : nullptr;
    if (UTFChars)
    {
        GCameraCharacteristicsJson = UTF8_TO_TCHAR(UTFChars);
        env->ReleaseStringUTFChars(jsonStr, UTFChars);
        UE_LOG(LogSimpleCamera2, Warning, TEXT("Received CameraCharacteristics JSON dump (%d chars)"), GCameraCharacteristicsJson.Len());
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Silver, TEXT("CameraCharacteristics dump received"));
        }
    }
    else
    {
        UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to receive CameraCharacteristics JSON dump"));
    }

	if (Camera2HelperInstance)
	{
		jclass HelperClass = env->GetObjectClass(Camera2HelperInstance);
		if (HelperClass)
		{
			jmethodID GetPathMethod = env->GetMethodID(HelperClass, "getLastCharacteristicsDumpPath", "()Ljava/lang/String;");
			jmethodID GetJsonMethod = env->GetMethodID(HelperClass, "getLastCharacteristicsDumpJson", "()Ljava/lang/String;");

			auto UpdateFromString = [&env](jstring StringObj, FString& OutValue)
			{
				if (!StringObj)
				{
					OutValue.Reset();
					return;
				}
				const char* Chars = env->GetStringUTFChars(StringObj, nullptr);
				if (Chars)
				{
					OutValue = UTF8_TO_TCHAR(Chars);
					env->ReleaseStringUTFChars(StringObj, Chars);
				}
				env->DeleteLocalRef(StringObj);
			};

			if (GetPathMethod)
			{
				jstring PathString = (jstring)env->CallObjectMethod(Camera2HelperInstance, GetPathMethod);
				if (env->ExceptionCheck())
				{
					env->ExceptionDescribe();
					env->ExceptionClear();
				}
				else
				{
					UpdateFromString(PathString, GCameraCharacteristicsJsonPath);
				}
			}

			if (GetJsonMethod)
			{
				jstring JsonString = (jstring)env->CallObjectMethod(Camera2HelperInstance, GetJsonMethod);
				if (env->ExceptionCheck())
				{
					env->ExceptionDescribe();
					env->ExceptionClear();
				}
				else
				{
					UpdateFromString(JsonString, GCameraCharacteristicsJson);
				}
			}

			env->DeleteLocalRef(HelperClass);
		}
	}
}

// JNI callback for intrinsics
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onIntrinsicsAvailable(JNIEnv* env, jclass clazz,
    jfloat fx, jfloat fy, jfloat cx, jfloat cy, jfloat skew, jint width, jint height)
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2 intrinsics received: fx=%.2f fy=%.2f cx=%.2f cy=%.2f skew=%.3f %dx%d"),
        fx, fy, cx, cy, skew, width, height);

    GCameraFx = fx;
    GCameraFy = fy;
    GCameraCx = cx;
    GCameraCy = cy;
    GCameraSkew = skew;
    GCameraCalibWidth = width;
    GCameraCalibHeight = height;

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Cyan,
            FString::Printf(TEXT("Intrinsics fx=%.0f fy=%.0f cx=%.0f cy=%.0f"), fx, fy, cx, cy));
    }
}

// JNI callback for SENSOR_INFO_PIXEL_ARRAY_SIZE
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onPixelArraySizeAvailable(JNIEnv* env, jclass clazz,
    jint width, jint height)
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2 pixel array size: %dx%d"), width, height);
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Silver,
            FString::Printf(TEXT("Pixel Array: %dx%d"), width, height));
    }
}

// JNI callback for SENSOR_INFO_ACTIVE_ARRAY_SIZE
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onActiveArraySizeAvailable(JNIEnv* env, jclass clazz,
    jint width, jint height)
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2 active array size: %dx%d"), width, height);
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Silver,
            FString::Printf(TEXT("Active Array: %dx%d"), width, height));
    }
}

// JNI callback for lens distortion coefficients
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onDistortionAvailable(JNIEnv* env, jclass clazz,
    jfloatArray coeffs, jint length)
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2 lens distortion received: length=%d"), length);

    if (coeffs && length > 0)
    {
        // Get distortion coefficients from Java
        jfloat* distortionData = env->GetFloatArrayElements(coeffs, nullptr);
        if (distortionData)
        {
            // Store distortion coefficients
            GLensDistortionCoeffs.SetNum(length);
            GLensDistortionLength = length;
            
            for (int32 i = 0; i < length; i++)
            {
                GLensDistortionCoeffs[i] = distortionData[i];
            }
            
            // Log first few coefficients for debugging
            FString CoeffStr = TEXT("Distortion coeffs: ");
            for (int32 i = 0; i < FMath::Min(length, 5); i++)
            {
                CoeffStr += FString::Printf(TEXT("%.4f "), GLensDistortionCoeffs[i]);
            }
            UE_LOG(LogSimpleCamera2, Warning, TEXT("%s"), *CoeffStr);
            
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Magenta,
                    FString::Printf(TEXT("Lens Distortion: %d coeffs"), length));
            }
            
            // Release Java array
            env->ReleaseFloatArrayElements(coeffs, distortionData, JNI_ABORT);
        }
        else
        {
            UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to get distortion data from Java"));
        }
    }
    else
    {
        UE_LOG(LogSimpleCamera2, Warning, TEXT("No lens distortion data available"));
        GLensDistortionCoeffs.Empty();
        GLensDistortionLength = 0;
    }
}

// JNI callback for original resolution
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onOriginalResolutionAvailable(JNIEnv* env, jclass clazz,
    jint width, jint height)
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2 original resolution received: %dx%d"), width, height);

    GOriginalResolutionWidth = width;
    GOriginalResolutionHeight = height;

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Orange,
            FString::Printf(TEXT("Original Resolution: %dx%d"), width, height));
    }
}

// JNI callback for camera selection (Quest 3: 50=left, 51=right)
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onCameraSelected(JNIEnv* env, jclass clazz,
    jstring cameraId, jboolean isLeftCamera)
{
    const char* IdChars = (cameraId != nullptr) ? env->GetStringUTFChars(cameraId, nullptr) : nullptr;
    if (IdChars)
    {
        GSelectedCameraId = UTF8_TO_TCHAR(IdChars);
        env->ReleaseStringUTFChars(cameraId, IdChars);
    }
    else
    {
        GSelectedCameraId = TEXT("unknown");
    }
    
    GIsLeftCamera = (isLeftCamera == JNI_TRUE);
    
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera selected: ID=%s, isLeft=%s"), 
        *GSelectedCameraId, GIsLeftCamera ? TEXT("true") : TEXT("false"));
    
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Cyan,
            FString::Printf(TEXT("Camera: %s (%s)"), *GSelectedCameraId, 
                GIsLeftCamera ? TEXT("LEFT") : TEXT("RIGHT")));
    }
}

// JNI callback for camera pose (CamInHmd transform)
// Translation is in meters, rotation is quaternion (x,y,z,w) in Android/OpenGL convention
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onCameraPoseAvailable(JNIEnv* env, jclass clazz,
    jfloat tx, jfloat ty, jfloat tz, jfloat qx, jfloat qy, jfloat qz, jfloat qw)
{
    // =========================================================================
    // COORDINATE CONVERSION - MATCHING META'S OFFICIAL UNITY SAMPLE
    // =========================================================================
    // Translation: Use same conversion as hardcoded calibration
    GCameraPoseTranslation = Quest3Calibration::ConvertTranslationToUE(tx, ty, tz);
    
    // Rotation: Use same conversion as hardcoded calibration
    GCameraPoseRotation = Quest3Calibration::ConvertRotationToUE(qx, qy, qz, qw);
    
    GCameraPoseAvailable = true;
    
    UE_LOG(LogSimpleCamera2, Warning, 
        TEXT("Camera pose received - Translation(cm): [%.2f, %.2f, %.2f], Rotation(xyzw): [%.4f, %.4f, %.4f, %.4f]"),
        GCameraPoseTranslation.X, GCameraPoseTranslation.Y, GCameraPoseTranslation.Z,
        GCameraPoseRotation.X, GCameraPoseRotation.Y, GCameraPoseRotation.Z, GCameraPoseRotation.W);
    
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green,
            FString::Printf(TEXT("CamInHmd: [%.1f, %.1f, %.1f] cm"), 
                GCameraPoseTranslation.X, GCameraPoseTranslation.Y, GCameraPoseTranslation.Z));
    }
}
#endif

bool USimpleCamera2Test::StartCameraPreview()
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("=== StartCameraPreview CALLED FROM BLUEPRINT ==="));
    
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, TEXT("StartCameraPreview CALLED"));
    }
    
#if PLATFORM_ANDROID
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Starting real Camera2 preview on Android"));
    
    // Auto-request camera permissions if not granted
    JNIEnv* Env = FAndroidApplication::GetJavaEnv();
    if (Env)
    {
        // Check and request permissions
        jobject Activity = FAndroidApplication::GetGameActivityThis();
        if (Activity)
        {
            UE_LOG(LogSimpleCamera2, Warning, TEXT("Checking camera permissions..."));
            
            // Get the Activity class and checkSelfPermission method
            jclass ActivityClass = Env->GetObjectClass(Activity);
            jmethodID CheckPermMethod = Env->GetMethodID(ActivityClass, 
                "checkSelfPermission", "(Ljava/lang/String;)I");
            jmethodID RequestPermMethod = Env->GetMethodID(ActivityClass,
                "requestPermissions", "([Ljava/lang/String;I)V");
            
            if (CheckPermMethod && RequestPermMethod)
            {
                // Check CAMERA permission
                jstring CameraPermStr = Env->NewStringUTF("android.permission.CAMERA");
                jint CameraPermResult = Env->CallIntMethod(Activity, CheckPermMethod, CameraPermStr);
                
                // PackageManager.PERMISSION_GRANTED = 0
                if (CameraPermResult != 0)
                {
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera permission not granted, requesting..."));
                    
                    // Create permission array
                    jobjectArray PermArray = Env->NewObjectArray(3, 
                        Env->FindClass("java/lang/String"), nullptr);
                    
                    jstring Perm1 = Env->NewStringUTF("android.permission.CAMERA");
                    jstring Perm2 = Env->NewStringUTF("horizonos.permission.HEADSET_CAMERA");
                    jstring Perm3 = Env->NewStringUTF("horizonos.permission.AVATAR_CAMERA");
                    
                    Env->SetObjectArrayElement(PermArray, 0, Perm1);
                    Env->SetObjectArrayElement(PermArray, 1, Perm2);
                    Env->SetObjectArrayElement(PermArray, 2, Perm3);
                    
                    // Request permissions (request code = 1001)
                    Env->CallVoidMethod(Activity, RequestPermMethod, PermArray, 1001);
                    
                    // Clean up
                    Env->DeleteLocalRef(Perm1);
                    Env->DeleteLocalRef(Perm2);
                    Env->DeleteLocalRef(Perm3);
                    Env->DeleteLocalRef(PermArray);
                    
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("Permission request sent. User must grant permission and retry."));
                    if (GEngine)
                    {
                        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow, 
                            TEXT("Please grant camera permission and try again"));
                    }
                    
                    Env->DeleteLocalRef(CameraPermStr);
                    return false; // Return false, user needs to grant permission first
                }
                else
                {
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera permission already granted"));
                }
                
                Env->DeleteLocalRef(CameraPermStr);
            }
        }
    }
    
    if (bCameraPreviewActive)
    {
        UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera preview already active"));
        return true;
    }
    
    // Create texture for camera feed if not already created
    UE_LOG(LogSimpleCamera2, Warning, TEXT("=== CHECKING CAMERA TEXTURE ==="));
    if (!CameraTexture)
    {
        UE_LOG(LogSimpleCamera2, Warning, TEXT("Creating new camera texture 1280x960"));
        CameraTexture = UTexture2D::CreateTransient(1280, 960, PF_B8G8R8A8);
        if (CameraTexture)
        {
            UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera texture created successfully"));
            CameraTexture->AddToRoot(); // Prevent garbage collection

            // Initialize with dark pattern asynchronously
            const int32 InitW = 1280;
            const int32 InitH = 960;
            const int32 InitSize = InitW * InitH * 4;
            uint8* InitData = new uint8[InitSize];
            FMemory::Memset(InitData, 64, InitSize); // Dark gray

            // Ensure resource is created before update
            CameraTexture->UpdateResource();
            AsyncTask(ENamedThreads::Type::GameThread, [InitData, InitW, InitH]()
            {
                if (CameraTexture)
                {
                    FTexture2DResource* TextureResource = static_cast<FTexture2DResource*>(CameraTexture->GetResource());
                    if (TextureResource)
                    {
                        const uint32 Pitch = static_cast<uint32>(InitW) * 4u;
                        FUpdateTextureRegion2D Region(0, 0, 0, 0, static_cast<uint32>(InitW), static_cast<uint32>(InitH));
                        ENQUEUE_RENDER_COMMAND(InitCameraTexture2D)(
                            [TextureResource, Region, InitData, Pitch](FRHICommandListImmediate& RHICmdList)
                            {
                                RHICmdList.UpdateTexture2D(TextureResource->GetTexture2DRHI(), 0, Region, Pitch, InitData);
                                delete[] InitData;
                            });
                    }
                    else
                    {
                        delete[] InitData;
                    }
                }
                else
                {
                    delete[] InitData;
                }
            });
        }
    }
    
    // Start real Camera2 using Camera2Helper
    UE_LOG(LogSimpleCamera2, Warning, TEXT("=== STARTING JNI CAMERA2HELPER ACCESS ==="));
    // Env already defined above, reuse it
    if (Env)
    {
        UE_LOG(LogSimpleCamera2, Warning, TEXT("JNI Environment obtained"));
        jobject Activity = FAndroidApplication::GetGameActivityThis();
        
        if (Activity)
        {
            UE_LOG(LogSimpleCamera2, Warning, TEXT("Game Activity obtained"));
        }
        else
        {
            UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to get Game Activity"));
        }
        
        // Get Camera2Helper class - Use Activity's class loader
        UE_LOG(LogSimpleCamera2, Warning, TEXT("Finding Camera2Helper class..."));
        
        // Try using the Activity's class loader
        jclass ActivityClass = Env->GetObjectClass(Activity);
        jmethodID GetClassLoaderMethod = Env->GetMethodID(ActivityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
        jobject ClassLoader = Env->CallObjectMethod(Activity, GetClassLoaderMethod);
        
        jclass ClassLoaderClass = Env->GetObjectClass(ClassLoader);
        jmethodID LoadClassMethod = Env->GetMethodID(ClassLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        
        jstring ClassName = Env->NewStringUTF("com.epicgames.ue4.Camera2Helper");
        jclass Camera2Class = (jclass)Env->CallObjectMethod(ClassLoader, LoadClassMethod, ClassName);
        Env->DeleteLocalRef(ClassName);
        
        if (Camera2Class)
        {
            UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ Camera2Helper class found successfully"));
            
            // Get getInstance method
            UE_LOG(LogSimpleCamera2, Warning, TEXT("Getting getInstance method..."));
            jmethodID GetInstanceMethod = Env->GetStaticMethodID(Camera2Class, 
                "getInstance", "(Landroid/content/Context;)Lcom/epicgames/ue4/Camera2Helper;");
                
            if (GetInstanceMethod)
            {
                UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ getInstance method found"));
                
                // Get Camera2Helper instance
                UE_LOG(LogSimpleCamera2, Warning, TEXT("Calling getInstance method with Activity..."));
                jobject LocalCamera = Env->CallStaticObjectMethod(Camera2Class, 
                    GetInstanceMethod, Activity);
                    
                if (LocalCamera)
                {
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ Camera2Helper instance obtained"));
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("Creating global reference..."));
                    Camera2HelperInstance = Env->NewGlobalRef(LocalCamera);
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ Global reference created"));
                    
                    // Start camera
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("Getting startCamera method..."));
                    jmethodID StartMethod = Env->GetMethodID(Camera2Class, 
                        "startCamera", "()Z");
                        
                    if (StartMethod)
                    {
                        UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ startCamera method found"));
                        UE_LOG(LogSimpleCamera2, Warning, TEXT("Calling startCamera method..."));
                        jboolean result = Env->CallBooleanMethod(Camera2HelperInstance, StartMethod);
                        UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ startCamera method call completed"));
                        
                        bCameraPreviewActive = (result == JNI_TRUE);
                        
                        if (bCameraPreviewActive)
                        {
                            UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ Real Camera2 started successfully"));
                            if (GEngine)
                            {
                                GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, 
                                    TEXT("Camera2: Real Camera Started!"));
                            }
                        }
                        else
                        {
                            UE_LOG(LogSimpleCamera2, Error, TEXT("✗ Failed to start real Camera2 - Java method returned false"));
                            if (GEngine)
                            {
                                GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, 
                                    TEXT("Camera2: Failed to start"));
                            }
                        }
                    }
                    else
                    {
                        UE_LOG(LogSimpleCamera2, Error, TEXT("✗ startCamera method not found"));
                    }
                    
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("Cleaning up local reference..."));
                    Env->DeleteLocalRef(LocalCamera);
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ Local reference cleaned up"));
                }
                else
                {
                    UE_LOG(LogSimpleCamera2, Error, TEXT("✗ Failed to get Camera2Helper instance from getInstance call"));
                }
            }
            else
            {
                UE_LOG(LogSimpleCamera2, Error, TEXT("✗ getInstance method not found"));
            }
        }
        else
        {
            UE_LOG(LogSimpleCamera2, Error, TEXT("✗ Camera2Helper class not found"));
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, 
                    TEXT("Camera2Helper class not found"));
            }
        }
        
        // Check for JNI exceptions
        if (Env->ExceptionCheck())
        {
            UE_LOG(LogSimpleCamera2, Error, TEXT("✗ JNI Exception detected"));
            Env->ExceptionDescribe();  // Print to logcat
            Env->ExceptionClear();
        }
        else
        {
            UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ No JNI exceptions detected"));
        }
    }
    else
    {
        UE_LOG(LogSimpleCamera2, Error, TEXT("✗ Failed to get JNI Environment"));
    }
    
    UE_LOG(LogSimpleCamera2, Warning, TEXT("=== StartCameraPreview FUNCTION COMPLETED ==="));
    return bCameraPreviewActive;
#else
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera preview: Not on Android platform"));
    return false;
#endif
}

void USimpleCamera2Test::StopCameraPreview()
{
    UE_LOG(LogSimpleCamera2, Log, TEXT("Stopping real Camera2 preview"));

    // Set flag immediately to prevent new frame processing
    bCameraPreviewActive = false;

#if PLATFORM_ANDROID
    if (Camera2HelperInstance)
    {
        JNIEnv* Env = FAndroidApplication::GetJavaEnv();
        if (Env)
        {
            // Get the class from the existing instance (not FindClass!)
            jclass Camera2Class = Env->GetObjectClass(Camera2HelperInstance);
            if (Camera2Class)
            {
                jmethodID StopMethod = Env->GetMethodID(Camera2Class,
                    "stopCamera", "()V");
                if (StopMethod)
                {
                    UE_LOG(LogSimpleCamera2, Log,
                        TEXT("Calling stopCamera method..."));
                    Env->CallVoidMethod(Camera2HelperInstance, StopMethod);

                    // Check for exceptions
                    if (Env->ExceptionCheck())
                    {
                        UE_LOG(LogSimpleCamera2, Error,
                            TEXT("Exception calling stopCamera"));
                        Env->ExceptionDescribe();
                        Env->ExceptionClear();
                    }
                    else
                    {
                        UE_LOG(LogSimpleCamera2, Log,
                            TEXT("stopCamera completed successfully"));
                    }
                }
                else
                {
                    UE_LOG(LogSimpleCamera2, Error,
                        TEXT("stopCamera method not found"));
                }

                // Clean up local reference
                Env->DeleteLocalRef(Camera2Class);
            }

            // Delete global reference
            Env->DeleteGlobalRef(Camera2HelperInstance);
            Camera2HelperInstance = nullptr;
            UE_LOG(LogSimpleCamera2, Log,
                TEXT("Camera2Helper instance cleaned up"));
        }
    }

    // Small delay to let pending frame callbacks complete
    FPlatformProcess::Sleep(0.1f);
#endif

    // Clean up texture
    if (CameraTexture)
    {
        FlushRenderingCommands();
        CameraTexture->RemoveFromRoot();
        CameraTexture = nullptr;
    }

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow,
            TEXT("Real Camera2: Stopped"));
    }
}


UTexture2D* USimpleCamera2Test::GetCameraTexture()
{
    return CameraTexture;
}

// Blueprint accessors for intrinsics
float USimpleCamera2Test::GetCameraFx()
{
    return GCameraFx;
}

float USimpleCamera2Test::GetCameraFy()
{
    return GCameraFy;
}

FVector2D USimpleCamera2Test::GetPrincipalPoint()
{
    return FVector2D(GCameraCx, GCameraCy);
}

float USimpleCamera2Test::GetCameraSkew()
{
    return GCameraSkew;
}

FIntPoint USimpleCamera2Test::GetCalibrationResolution()
{
    return FIntPoint(GCameraCalibWidth, GCameraCalibHeight);
}

TArray<float> USimpleCamera2Test::GetLensDistortion()
{
    return GLensDistortionCoeffs;
}

FIntPoint USimpleCamera2Test::GetOriginalResolution()
{
    return FIntPoint(GOriginalResolutionWidth, GOriginalResolutionHeight);
}

TArray<float> USimpleCamera2Test::GetLensDistortionUE()
{
    // Always return 8 coefficients in the order: [K1,K2,P1,P2,K3,K4,K5,K6]
    TArray<float> Mapped;
    Mapped.SetNumZeroed(8);

    // Nothing recorded
    if (GLensDistortionLength <= 0 || GLensDistortionCoeffs.Num() <= 0)
    {
        return Mapped;
    }

    const int32 N = GLensDistortionLength;

    // Case 1: Android Brown model (5 floats): [k1, k2, k3, p1, p2]
	if (N == 5)
	{
		// Most devices provide 5 radial coefficients via LENS_DISTORTION (no tangential).
		// Map them as K1..K5, leaving P1/P2 at zero.
		Mapped[0] = GLensDistortionCoeffs[0]; // K1
		Mapped[1] = GLensDistortionCoeffs[1]; // K2
		// P1,P2 remain 0
		Mapped[4] = GLensDistortionCoeffs[2]; // K3
		Mapped[5] = GLensDistortionCoeffs[3]; // K4
		Mapped[6] = GLensDistortionCoeffs[4]; // K5
		// K6 remains 0
		return Mapped;
	}

    // Case 2: Radial-only model (>=6 floats): [k1,k2,k3,k4,k5,k6,...]
    if (N >= 6)
    {
        Mapped[0] = GLensDistortionCoeffs[0]; // K1
        Mapped[1] = GLensDistortionCoeffs[1]; // K2
        // P1,P2 = 0
        Mapped[4] = GLensDistortionCoeffs[2]; // K3
        Mapped[5] = GLensDistortionCoeffs[3]; // K4
        Mapped[6] = GLensDistortionCoeffs[4]; // K5
        Mapped[7] = GLensDistortionCoeffs[5]; // K6
        return Mapped;
    }

    // Fallback: copy what we can for first two as K1,K2
    Mapped[0] = GLensDistortionCoeffs[0];
    if (N > 1) { Mapped[1] = GLensDistortionCoeffs[1]; }
    return Mapped;
}

FString USimpleCamera2Test::GetSelectedCameraId()
{
    return GSelectedCameraId;
}

bool USimpleCamera2Test::IsLeftCamera()
{
    return GIsLeftCamera;
}

bool USimpleCamera2Test::IsCameraPoseAvailable()
{
    return GCameraPoseAvailable;
}

FVector USimpleCamera2Test::GetCameraPoseTranslation()
{
    return GCameraPoseTranslation;
}

FQuat USimpleCamera2Test::GetCameraPoseRotation()
{
    return GCameraPoseRotation;
}

FTransform USimpleCamera2Test::GetCamInHmdTransform()
{
    if (GCameraPoseAvailable)
    {
        return FTransform(GCameraPoseRotation, GCameraPoseTranslation, FVector::OneVector);
    }
    
    // Fallback: use hardcoded Quest 3 left camera calibration (validated against Meta Unity)
    // Quest 3 cameras are tilted ~11° downward to better capture hand interactions
    using namespace Quest3Calibration;
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera pose not available, using Quest 3 left camera calibration"));
    return FTransform(
        ConvertRotationToUE(LeftQx, LeftQy, LeftQz, LeftQw),  // ~11° downward tilt
        ConvertTranslationToUE(LeftTx, LeftTy, LeftTz),       // (6.3, -3.2, -1.7) cm
        FVector::OneVector
    );
}

void USimpleCamera2Test::GetCameraCharacteristics(bool bRedump, FString& OutJson, FString& OutFilePath)
{
	OutJson = GCameraCharacteristicsJson;
	OutFilePath = GCameraCharacteristicsJsonPath;

#if PLATFORM_ANDROID
	JNIEnv* Env = FAndroidApplication::GetJavaEnv();
	if (!Env)
	{
		UE_LOG(LogSimpleCamera2, Error, TEXT("JNI env not available for GetCameraCharacteristics"));
		return;
	}

	if (!EnsureCamera2HelperInstance(Env))
	{
		UE_LOG(LogSimpleCamera2, Error, TEXT("Unable to access Camera2Helper instance for GetCameraCharacteristics"));
		return;
	}

	jclass HelperClass = Env->GetObjectClass(Camera2HelperInstance);
	if (!HelperClass)
	{
		UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to get Camera2Helper class for GetCameraCharacteristics"));
		return;
	}

	auto UpdateFromJString = [Env](jstring InString, FString& Target)
	{
		if (!InString)
		{
			Target.Reset();
			return;
		}
		const char* Chars = Env->GetStringUTFChars(InString, nullptr);
		if (Chars)
		{
			Target = UTF8_TO_TCHAR(Chars);
			Env->ReleaseStringUTFChars(InString, Chars);
		}
		Env->DeleteLocalRef(InString);
	};

	if (bRedump)
	{
		jmethodID DumpMethod = Env->GetMethodID(HelperClass, "dumpCameraCharacteristicsAndReturnJsonAndPath", "()[Ljava/lang/String;");
		if (!DumpMethod)
		{
			UE_LOG(LogSimpleCamera2, Error, TEXT("dumpCameraCharacteristicsAndReturnJsonAndPath() not found on Camera2Helper"));
			Env->DeleteLocalRef(HelperClass);
			return;
		}

		jobjectArray ResultArray = (jobjectArray)Env->CallObjectMethod(Camera2HelperInstance, DumpMethod);
		if (Env->ExceptionCheck())
		{
			UE_LOG(LogSimpleCamera2, Error, TEXT("JNI exception while dumping camera characteristics"));
			Env->ExceptionDescribe();
			Env->ExceptionClear();
			Env->DeleteLocalRef(HelperClass);
			return;
		}

		if (ResultArray)
		{
			jsize Length = Env->GetArrayLength(ResultArray);
			if (Length >= 1)
			{
				jstring JsonString = (jstring)Env->GetObjectArrayElement(ResultArray, 0);
				UpdateFromJString(JsonString, GCameraCharacteristicsJson);
			}
			if (Length >= 2)
			{
				jstring PathString = (jstring)Env->GetObjectArrayElement(ResultArray, 1);
				UpdateFromJString(PathString, GCameraCharacteristicsJsonPath);
			}
			Env->DeleteLocalRef(ResultArray);
		}
		else
		{
			UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2Helper returned null array for characteristics dump"));
		}
	}
	else
	{
		jmethodID GetJsonMethod = Env->GetMethodID(HelperClass, "getLastCharacteristicsDumpJson", "()Ljava/lang/String;");
		jmethodID GetPathMethod = Env->GetMethodID(HelperClass, "getLastCharacteristicsDumpPath", "()Ljava/lang/String;");

		if (GetJsonMethod)
		{
			jstring JsonString = (jstring)Env->CallObjectMethod(Camera2HelperInstance, GetJsonMethod);
			if (Env->ExceptionCheck())
			{
				Env->ExceptionDescribe();
				Env->ExceptionClear();
			}
			else
			{
				UpdateFromJString(JsonString, GCameraCharacteristicsJson);
			}
		}

		if (GetPathMethod)
		{
			jstring PathString = (jstring)Env->CallObjectMethod(Camera2HelperInstance, GetPathMethod);
			if (Env->ExceptionCheck())
			{
				Env->ExceptionDescribe();
				Env->ExceptionClear();
			}
			else
			{
				UpdateFromJString(PathString, GCameraCharacteristicsJsonPath);
			}
		}
	}

	Env->DeleteLocalRef(HelperClass);

	OutJson = GCameraCharacteristicsJson;
	OutFilePath = GCameraCharacteristicsJsonPath;
#else
	UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera characteristics only available on Android"));
#endif
}

bool USimpleCamera2Test::StartCameraPreviewWithSelection(bool bUseLeftCamera)
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("StartCameraPreviewWithSelection called - bUseLeftCamera=%s"), 
        bUseLeftCamera ? TEXT("true") : TEXT("false"));
    
    // Set the preference before calling StartCameraPreview
    GPreferLeftCamera = bUseLeftCamera;
    
#if PLATFORM_ANDROID
    // On Android, we need to tell Camera2Helper which camera to use
    JNIEnv* Env = FAndroidApplication::GetJavaEnv();
    if (Env)
    {
        jobject Activity = FAndroidApplication::GetGameActivityThis();
        if (Activity)
        {
            jclass ActivityClass = Env->GetObjectClass(Activity);
            jmethodID GetClassLoaderMethod = Env->GetMethodID(ActivityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
            if (GetClassLoaderMethod)
            {
                jobject ClassLoader = Env->CallObjectMethod(Activity, GetClassLoaderMethod);
                jclass ClassLoaderClass = Env->GetObjectClass(ClassLoader);
                jmethodID LoadClassMethod = Env->GetMethodID(ClassLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
                
                if (LoadClassMethod)
                {
                    jstring ClassName = Env->NewStringUTF("com.epicgames.ue4.Camera2Helper");
                    jclass Camera2Class = (jclass)Env->CallObjectMethod(ClassLoader, LoadClassMethod, ClassName);
                    Env->DeleteLocalRef(ClassName);
                    
                    if (Camera2Class)
                    {
                        // Look for setPreferredCamera method
                        jmethodID SetPreferredMethod = Env->GetStaticMethodID(Camera2Class, 
                            "setPreferredCamera", "(Z)V");
                        
                        if (SetPreferredMethod)
                        {
                            Env->CallStaticVoidMethod(Camera2Class, SetPreferredMethod, 
                                bUseLeftCamera ? JNI_TRUE : JNI_FALSE);
                            UE_LOG(LogSimpleCamera2, Warning, TEXT("Set preferred camera to %s"), 
                                bUseLeftCamera ? TEXT("LEFT (50)") : TEXT("RIGHT (51)"));
                        }
                        else
                        {
                            UE_LOG(LogSimpleCamera2, Warning, 
                                TEXT("setPreferredCamera not found - Camera2Helper may not support camera selection"));
                        }
                        
                        Env->DeleteLocalRef(Camera2Class);
                    }
                }
                
                if (ClassLoaderClass) Env->DeleteLocalRef(ClassLoaderClass);
                Env->DeleteLocalRef(ClassLoader);
            }
            Env->DeleteLocalRef(ActivityClass);
        }
        
        if (Env->ExceptionCheck())
        {
            Env->ExceptionDescribe();
            Env->ExceptionClear();
        }
    }
#endif
    
    // Now start the camera with the preference set
    return StartCameraPreview();
}

void USimpleCamera2Test::SetPreferredCamera(bool bUseLeftCamera)
{
    GPreferLeftCamera = bUseLeftCamera;
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera preference set to %s"), 
        bUseLeftCamera ? TEXT("LEFT") : TEXT("RIGHT"));
}

bool USimpleCamera2Test::GetPreferredCamera()
{
    return GPreferLeftCamera;
}

FQuest3CameraCalibration USimpleCamera2Test::GetQuest3Calibration(bool bLeftCamera, int32 StreamWidth, int32 StreamHeight)
{
    using namespace Quest3Calibration;
    
    FQuest3CameraCalibration Calib;
    
    Calib.bIsLeftCamera = bLeftCamera;
    Calib.CameraId = bLeftCamera ? TEXT("50") : TEXT("51");
    
    // =========================================================================
    // PREFER RUNTIME CALIBRATION FROM DEVICE
    // Each Quest 3 unit has unique factory calibration stored on-device.
    // Using hardcoded values causes errors because manufacturing tolerances
    // mean each headset has slightly different lens positions and focal lengths.
    // =========================================================================
    
    bool bUsingRuntimeIntrinsics = false;
    bool bUsingRuntimePose = false;
    
    // Check if we have runtime intrinsics from Camera2 API
    // Runtime intrinsics are in CALIBRATION resolution (typically 1280x1280)
    const bool bHaveRuntimeIntrinsics = (GCameraFx > 0.0f && GCameraFy > 0.0f && 
                                          GCameraCalibWidth > 0 && GCameraCalibHeight > 0);
    
    if (bHaveRuntimeIntrinsics)
    {
        // Use runtime intrinsics from this specific device
        Calib.NativeWidth = GCameraCalibWidth;
        Calib.NativeHeight = GCameraCalibHeight;
        Calib.NativeFx = GCameraFx;
        Calib.NativeFy = GCameraFy;
        Calib.NativeCx = GCameraCx;
        Calib.NativeCy = GCameraCy;
        bUsingRuntimeIntrinsics = true;
        
        // Log comparison with hardcoded values for debugging
        const float HardcodedFx = bLeftCamera ? LeftFx : RightFx;
        const float HardcodedCx = bLeftCamera ? LeftCx : RightCx;
        const float HardcodedCy = bLeftCamera ? LeftCy : RightCy;
        
        const float FxDiff = FMath::Abs(GCameraFx - HardcodedFx);
        const float CxDiff = FMath::Abs(GCameraCx - HardcodedCx);
        const float CyDiff = FMath::Abs(GCameraCy - HardcodedCy);
        
        UE_LOG(LogSimpleCamera2, Warning,
            TEXT("CALIBRATION: Using RUNTIME intrinsics from device"));
        UE_LOG(LogSimpleCamera2, Warning,
            TEXT("  Runtime:   Fx=%.2f Fy=%.2f Cx=%.2f Cy=%.2f (%dx%d)"),
            GCameraFx, GCameraFy, GCameraCx, GCameraCy, GCameraCalibWidth, GCameraCalibHeight);
        UE_LOG(LogSimpleCamera2, Warning,
            TEXT("  Hardcoded: Fx=%.2f Fy=%.2f Cx=%.2f Cy=%.2f (1280x1280)"),
            HardcodedFx, bLeftCamera ? LeftFy : RightFy, HardcodedCx, HardcodedCy);
        UE_LOG(LogSimpleCamera2, Warning,
            TEXT("  Difference: dFx=%.2f dCx=%.2f dCy=%.2f pixels"),
            FxDiff, CxDiff, CyDiff);
        
        // Warn if differences are significant (>1 pixel can cause noticeable errors)
        if (FxDiff > 5.0f || CxDiff > 5.0f || CyDiff > 5.0f)
        {
            UE_LOG(LogSimpleCamera2, Warning,
                TEXT("  >>> SIGNIFICANT CALIBRATION DIFFERENCE DETECTED! This device differs from reference."));
        }
    }
    else
    {
        // Fall back to hardcoded values
        Calib.NativeWidth = NativeWidth;
        Calib.NativeHeight = NativeHeight;
        
        if (bLeftCamera)
        {
            Calib.NativeFx = LeftFx;
            Calib.NativeFy = LeftFy;
            Calib.NativeCx = LeftCx;
            Calib.NativeCy = LeftCy;
        }
        else
        {
            Calib.NativeFx = RightFx;
            Calib.NativeFy = RightFy;
            Calib.NativeCx = RightCx;
            Calib.NativeCy = RightCy;
        }
        
        UE_LOG(LogSimpleCamera2, Warning,
            TEXT("CALIBRATION: Using HARDCODED intrinsics (runtime not available)"));
    }
    
    // Stream resolution and adjusted intrinsics
    Calib.StreamWidth = StreamWidth;
    Calib.StreamHeight = StreamHeight;
    
    // =========================================================================
    // INTRINSICS ADJUSTMENT FOR CENTER CROP
    // =========================================================================
    // Quest 3 Camera2 API uses CENTER CROP when changing aspect ratio:
    // - Native sensor: 1280x1280 (1:1 aspect ratio)
    // - Stream output: 1280x960 (4:3 aspect ratio)
    // 
    // Center crop means:
    // - Pixels are NOT scaled, just removed from edges
    // - FOCAL LENGTHS (fx, fy) remain UNCHANGED
    // - PRINCIPAL POINT shifts by the crop offset
    //
    // For 1280x1280 -> 1280x960:
    // - CropOffsetX = 0 (width unchanged)
    // - CropOffsetY = (1280-960)/2 = 160 pixels cropped from top
    // =========================================================================
    
    const float CropOffsetX = (static_cast<float>(Calib.NativeWidth) - static_cast<float>(StreamWidth)) / 2.0f;
    const float CropOffsetY = (static_cast<float>(Calib.NativeHeight) - static_cast<float>(StreamHeight)) / 2.0f;
    
    // Focal lengths: UNCHANGED for center crop (pixels aren't scaled)
    Calib.StreamFx = Calib.NativeFx;
    Calib.StreamFy = Calib.NativeFy;
    
    // Principal point: shifts by crop offset (coordinate origin moves)
    Calib.StreamCx = Calib.NativeCx - CropOffsetX;
    Calib.StreamCy = Calib.NativeCy - CropOffsetY;
    
    // =========================================================================
    // CAMERA POSE - PREFER RUNTIME
    // =========================================================================
    if (GCameraPoseAvailable)
    {
        // Use runtime pose from this specific device
        Calib.PoseTranslationCm = GCameraPoseTranslation;
        Calib.PoseRotation = GCameraPoseRotation;
        bUsingRuntimePose = true;
        
        // Log comparison with hardcoded
        const FVector HardcodedTrans = bLeftCamera ? 
            ConvertTranslationToUE(LeftTx, LeftTy, LeftTz) :
            ConvertTranslationToUE(RightTx, RightTy, RightTz);
        
        const float TransDiff = FVector::Dist(GCameraPoseTranslation, HardcodedTrans);
        
        UE_LOG(LogSimpleCamera2, Warning,
            TEXT("CALIBRATION: Using RUNTIME pose from device"));
        UE_LOG(LogSimpleCamera2, Warning,
            TEXT("  Runtime:   [%.2f, %.2f, %.2f] cm"),
            GCameraPoseTranslation.X, GCameraPoseTranslation.Y, GCameraPoseTranslation.Z);
        UE_LOG(LogSimpleCamera2, Warning,
            TEXT("  Hardcoded: [%.2f, %.2f, %.2f] cm"),
            HardcodedTrans.X, HardcodedTrans.Y, HardcodedTrans.Z);
        UE_LOG(LogSimpleCamera2, Warning,
            TEXT("  Difference: %.2f cm"), TransDiff);
    }
    else
    {
        // Fall back to hardcoded pose
        if (bLeftCamera)
        {
            Calib.PoseTranslationCm = ConvertTranslationToUE(LeftTx, LeftTy, LeftTz);
            Calib.PoseRotation = ConvertRotationToUE(LeftQx, LeftQy, LeftQz, LeftQw);
        }
        else
        {
            Calib.PoseTranslationCm = ConvertTranslationToUE(RightTx, RightTy, RightTz);
            Calib.PoseRotation = ConvertRotationToUE(RightQx, RightQy, RightQz, RightQw);
        }
        
        UE_LOG(LogSimpleCamera2, Warning,
            TEXT("CALIBRATION: Using HARDCODED pose (runtime not available)"));
    }
    
    // Final summary log
    UE_LOG(LogSimpleCamera2, Warning, 
        TEXT("Quest3 %s camera calibration: Stream %dx%d, Fx=%.2f Fy=%.2f Cx=%.2f Cy=%.2f, Pose=[%.2f, %.2f, %.2f]cm [%s intrinsics, %s pose]"),
        bLeftCamera ? TEXT("LEFT") : TEXT("RIGHT"),
        StreamWidth, StreamHeight,
        Calib.StreamFx, Calib.StreamFy, Calib.StreamCx, Calib.StreamCy,
        Calib.PoseTranslationCm.X, Calib.PoseTranslationCm.Y, Calib.PoseTranslationCm.Z,
        bUsingRuntimeIntrinsics ? TEXT("RUNTIME") : TEXT("HARDCODED"),
        bUsingRuntimePose ? TEXT("RUNTIME") : TEXT("HARDCODED"));
    
    return Calib;
}

FQuest3CameraCalibration USimpleCamera2Test::GetCurrentQuest3Calibration(int32 StreamWidth, int32 StreamHeight)
{
    // Use the runtime-selected camera if available, otherwise use preference
    bool bUseLeft = GIsLeftCamera;
    
    // If no camera has been selected yet, use the preference
    if (GSelectedCameraId.IsEmpty())
    {
        bUseLeft = GPreferLeftCamera;
        UE_LOG(LogSimpleCamera2, Warning, 
            TEXT("No camera selected yet, using preference: %s"), 
            bUseLeft ? TEXT("LEFT") : TEXT("RIGHT"));
    }
    
    return GetQuest3Calibration(bUseLeft, StreamWidth, StreamHeight);
}

void USimpleCamera2Test::IsRuntimeCalibrationAvailable(bool& bOutHasIntrinsics, bool& bOutHasPose)
{
    bOutHasIntrinsics = (GCameraFx > 0.0f && GCameraFy > 0.0f && 
                         GCameraCalibWidth > 0 && GCameraCalibHeight > 0);
    bOutHasPose = GCameraPoseAvailable;
}

FString USimpleCamera2Test::GetCalibrationDiagnostics(bool bLeftCamera)
{
    using namespace Quest3Calibration;
    
    FString Result;
    
    // Header
    Result += TEXT("=== QUEST 3 CAMERA CALIBRATION DIAGNOSTICS ===\n");
    Result += FString::Printf(TEXT("Camera: %s\n\n"), bLeftCamera ? TEXT("LEFT (ID 50)") : TEXT("RIGHT (ID 51)"));
    
    // Check runtime availability
    const bool bHaveRuntimeIntrinsics = (GCameraFx > 0.0f && GCameraFy > 0.0f && 
                                          GCameraCalibWidth > 0 && GCameraCalibHeight > 0);
    const bool bHaveRuntimePose = GCameraPoseAvailable;
    
    Result += TEXT("--- DATA SOURCE ---\n");
    Result += FString::Printf(TEXT("Runtime Intrinsics: %s\n"), bHaveRuntimeIntrinsics ? TEXT("AVAILABLE") : TEXT("NOT AVAILABLE"));
    Result += FString::Printf(TEXT("Runtime Pose: %s\n\n"), bHaveRuntimePose ? TEXT("AVAILABLE") : TEXT("NOT AVAILABLE"));
    
    // Hardcoded values
    const float HardcodedFx = bLeftCamera ? LeftFx : RightFx;
    const float HardcodedFy = bLeftCamera ? LeftFy : RightFy;
    const float HardcodedCx = bLeftCamera ? LeftCx : RightCx;
    const float HardcodedCy = bLeftCamera ? LeftCy : RightCy;
    
    const FVector HardcodedTrans = bLeftCamera ? 
        ConvertTranslationToUE(LeftTx, LeftTy, LeftTz) :
        ConvertTranslationToUE(RightTx, RightTy, RightTz);
    const FQuat HardcodedRot = bLeftCamera ?
        ConvertRotationToUE(LeftQx, LeftQy, LeftQz, LeftQw) :
        ConvertRotationToUE(RightQx, RightQy, RightQz, RightQw);
    
    // INTRINSICS COMPARISON
    Result += TEXT("--- INTRINSICS (Native Resolution) ---\n");
    Result += FString::Printf(TEXT("Hardcoded: Fx=%.2f Fy=%.2f Cx=%.2f Cy=%.2f (1280x1280)\n"),
        HardcodedFx, HardcodedFy, HardcodedCx, HardcodedCy);
    
    if (bHaveRuntimeIntrinsics)
    {
        Result += FString::Printf(TEXT("Runtime:   Fx=%.2f Fy=%.2f Cx=%.2f Cy=%.2f (%dx%d)\n"),
            GCameraFx, GCameraFy, GCameraCx, GCameraCy, GCameraCalibWidth, GCameraCalibHeight);
        
        const float DeltaFx = GCameraFx - HardcodedFx;
        const float DeltaFy = GCameraFy - HardcodedFy;
        const float DeltaCx = GCameraCx - HardcodedCx;
        const float DeltaCy = GCameraCy - HardcodedCy;
        
        Result += FString::Printf(TEXT("Delta:     dFx=%.2f dFy=%.2f dCx=%.2f dCy=%.2f pixels\n"),
            DeltaFx, DeltaFy, DeltaCx, DeltaCy);
        
        // Percentage difference for focal length (important for scale)
        const float FxPctDiff = (DeltaFx / HardcodedFx) * 100.0f;
        const float FyPctDiff = (DeltaFy / HardcodedFy) * 100.0f;
        Result += FString::Printf(TEXT("Fx diff: %.3f%%, Fy diff: %.3f%%\n"), FxPctDiff, FyPctDiff);
        
        // Impact analysis
        if (FMath::Abs(DeltaFx) > 5.0f || FMath::Abs(DeltaCx) > 5.0f || FMath::Abs(DeltaCy) > 5.0f)
        {
            Result += TEXT("\n>>> WARNING: Significant intrinsic differences detected!\n");
            Result += TEXT("    Impact: Pose direction errors proportional to pixel offset.\n");
            
            // Estimate angular error from principal point offset
            // tan(error_angle) ≈ pixel_offset / focal_length
            const float AngularErrorDeg = FMath::RadiansToDegrees(
                FMath::Atan(FMath::Max(FMath::Abs(DeltaCx), FMath::Abs(DeltaCy)) / HardcodedFx));
            Result += FString::Printf(TEXT("    Estimated angular error from PP shift: ~%.2f deg\n"), AngularErrorDeg);
            Result += FString::Printf(TEXT("    At 10m distance: ~%.1f cm position error\n"), 
                1000.0f * FMath::Tan(FMath::DegreesToRadians(AngularErrorDeg)));
        }
    }
    else
    {
        Result += TEXT("Runtime:   NOT AVAILABLE - using hardcoded values\n");
    }
    
    // POSE COMPARISON
    Result += TEXT("\n--- CAMERA POSE (CamInHmd) ---\n");
    
    const FRotator HardcodedRotator = HardcodedRot.Rotator();
    Result += FString::Printf(TEXT("Hardcoded: Trans=[%.2f, %.2f, %.2f] cm\n"),
        HardcodedTrans.X, HardcodedTrans.Y, HardcodedTrans.Z);
    Result += FString::Printf(TEXT("           Rot=P:%.2f Y:%.2f R:%.2f deg\n"),
        HardcodedRotator.Pitch, HardcodedRotator.Yaw, HardcodedRotator.Roll);
    
    if (bHaveRuntimePose)
    {
        const FRotator RuntimeRotator = GCameraPoseRotation.Rotator();
        Result += FString::Printf(TEXT("Runtime:   Trans=[%.2f, %.2f, %.2f] cm\n"),
            GCameraPoseTranslation.X, GCameraPoseTranslation.Y, GCameraPoseTranslation.Z);
        Result += FString::Printf(TEXT("           Rot=P:%.2f Y:%.2f R:%.2f deg\n"),
            RuntimeRotator.Pitch, RuntimeRotator.Yaw, RuntimeRotator.Roll);
        
        // Differences
        const FVector TransDelta = GCameraPoseTranslation - HardcodedTrans;
        const float TransDist = TransDelta.Size();
        
        // Angular difference between quaternions
        const float AngleDiffRad = GCameraPoseRotation.AngularDistance(HardcodedRot);
        const float AngleDiffDeg = FMath::RadiansToDegrees(AngleDiffRad);
        
        Result += FString::Printf(TEXT("Delta:     Trans dist=%.2f cm, Rot diff=%.2f deg\n"),
            TransDist, AngleDiffDeg);
        
        if (TransDist > 0.5f || AngleDiffDeg > 1.0f)
        {
            Result += TEXT("\n>>> WARNING: Significant pose differences detected!\n");
            Result += TEXT("    This could cause systematic angular errors in colocation.\n");
        }
    }
    else
    {
        Result += TEXT("Runtime:   NOT AVAILABLE - using hardcoded values\n");
    }
    
    // WHAT'S ACTUALLY BEING USED
    Result += TEXT("\n--- CURRENTLY ACTIVE ---\n");
    Result += FString::Printf(TEXT("Intrinsics source: %s\n"), 
        bHaveRuntimeIntrinsics ? TEXT("RUNTIME (from device)") : TEXT("HARDCODED (reference values)"));
    Result += FString::Printf(TEXT("Pose source: %s\n"), 
        bHaveRuntimePose ? TEXT("RUNTIME (from device)") : TEXT("HARDCODED (reference values)"));
    
    return Result;
}
