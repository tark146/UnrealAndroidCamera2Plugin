#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SimpleCamera2Test.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSimpleCamera2, Log, All);

// Quest 3 camera calibration data structure
USTRUCT(BlueprintType)
struct FQuest3CameraCalibration
{
    GENERATED_BODY()

    // Camera identifier (50=left, 51=right)
    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    FString CameraId;

    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    bool bIsLeftCamera = true;

    // Intrinsics for NATIVE sensor resolution (1280x1280)
    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    float NativeFx = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    float NativeFy = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    float NativeCx = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    float NativeCy = 0.0f;

    // Native sensor resolution
    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    int32 NativeWidth = 1280;

    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    int32 NativeHeight = 1280;

    // Intrinsics ADJUSTED for stream resolution (e.g., 1280x960)
    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    float StreamFx = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    float StreamFy = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    float StreamCx = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    float StreamCy = 0.0f;

    // Stream resolution
    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    int32 StreamWidth = 1280;

    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    int32 StreamHeight = 960;

    // Camera pose in HMD space (CamInHmd) - translation in cm
    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    FVector PoseTranslationCm = FVector::ZeroVector;

    // Camera rotation relative to HMD
    UPROPERTY(BlueprintReadOnly, Category = "Calibration")
    FQuat PoseRotation = FQuat::Identity;

    // Full CamInHmd transform
    FTransform GetCamInHmdTransform() const
    {
        return FTransform(PoseRotation, PoseTranslationCm, FVector::OneVector);
    }
};

/**
 * Simple Camera2 API - Basic camera to texture functionality
 */
UCLASS(BlueprintType)
class ANDROIDCAMERA2PLUGIN_API USimpleCamera2Test : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Start camera preview using Camera2 API (defaults to LEFT camera)
     * @return true if camera started successfully
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2")
    static bool StartCameraPreview();

    /**
     * Start camera preview with explicit camera selection
     * @param bUseLeftCamera - true for left camera (ID 50), false for right (ID 51)
     * @return true if camera started successfully
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2")
    static bool StartCameraPreviewWithSelection(bool bUseLeftCamera = true);

    /**
     * Stop camera preview and cleanup resources
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2")
    static void StopCameraPreview();

    /**
     * Get the camera preview texture (null if preview not started)
     * @return texture containing camera feed
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2")
    static class UTexture2D* GetCameraTexture();

    // Intrinsic calibration accessors (pixels)
    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static float GetCameraFx();

    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static float GetCameraFy();

    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static FVector2D GetPrincipalPoint();

    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static float GetCameraSkew();

    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static FIntPoint GetCalibrationResolution();

    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static TArray<float> GetLensDistortion();

    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static FIntPoint GetOriginalResolution();

    // Mapped coefficients for typical UE usage: [K1,K2,P1,P2,K3,K4,K5,K6]
    UFUNCTION(BlueprintPure, Category = "Camera2|Lens Distortion")
    static TArray<float> GetLensDistortionUE();
    
    // Unified access to camera characteristics JSON + saved file path
    UFUNCTION(BlueprintCallable, Category = "Camera2|Characteristics")
    static void GetCameraCharacteristics(bool bRedump, FString& OutJson, FString& OutFilePath);
    
    // ============================================================================
    // CAMERA SELECTION (Quest 3: camera 50 = left, camera 51 = right)
    // ============================================================================
    
    // Get the selected camera ID (e.g., "50" for Quest 3 left camera)
    UFUNCTION(BlueprintPure, Category = "Camera2|Selection")
    static FString GetSelectedCameraId();
    
    // Check if the selected camera is the left camera (Quest 3: ID 50)
    // Always true on Quest 3 since we deterministically select the left camera
    UFUNCTION(BlueprintPure, Category = "Camera2|Selection")
    static bool IsLeftCamera();
    
    // ============================================================================
    // CAMERA POSE (CamInHmd transform from device calibration)
    // ============================================================================
    
    // Check if camera pose data is available from the device
    UFUNCTION(BlueprintPure, Category = "Camera2|Pose")
    static bool IsCameraPoseAvailable();
    
    // Get the camera position relative to HMD center (in cm, UE coordinate system)
    // Quest 3 left camera is approximately: X=6.3 (forward), Y=-3.2 (left), Z=-1.7 (down)
    UFUNCTION(BlueprintPure, Category = "Camera2|Pose")
    static FVector GetCameraPoseTranslation();
    
    // Get the camera rotation relative to HMD (UE coordinate system)
    UFUNCTION(BlueprintPure, Category = "Camera2|Pose")
    static FQuat GetCameraPoseRotation();
    
    // Get the full CamInHmd transform (camera pose in HMD space)
    // Use this directly in ComputeTagPose as the CamInHmd parameter
    UFUNCTION(BlueprintPure, Category = "Camera2|Pose")
    static FTransform GetCamInHmdTransform();
    
    // ============================================================================
    // QUEST 3 HARDCODED CALIBRATION DATA
    // These values are extracted from actual Quest 3 device dumps and can be used
    // as reliable fallbacks when the Camera2 API doesn't provide data.
    // ============================================================================
    
    /**
     * Get hardcoded Quest 3 camera calibration data.
     * This uses the exact values from device dumps and is completely deterministic.
     * 
     * @param bLeftCamera - true for left camera (ID 50), false for right (ID 51)
     * @param StreamWidth - the actual stream width you're processing (e.g., 1280)
     * @param StreamHeight - the actual stream height you're processing (e.g., 960)
     * @return Full calibration data including intrinsics (native and adjusted) and CamInHmd pose
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2|Quest3")
    static FQuest3CameraCalibration GetQuest3Calibration(bool bLeftCamera = true, int32 StreamWidth = 1280, int32 StreamHeight = 960);
    
    /**
     * Convenience: Get Quest 3 calibration using the currently selected camera.
     * Uses the camera determined during StartCameraPreview[WithSelection].
     * Falls back to left camera if no camera has been started.
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2|Quest3")
    static FQuest3CameraCalibration GetCurrentQuest3Calibration(int32 StreamWidth = 1280, int32 StreamHeight = 960);
    
    /**
     * Request a specific camera on the next StartCameraPreview call.
     * Call this BEFORE StartCameraPreview to select left or right.
     * 
     * @param bUseLeftCamera - true for left (ID 50), false for right (ID 51)
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2|Selection")
    static void SetPreferredCamera(bool bUseLeftCamera);
    
    /**
     * Check if we should use the left camera (returns the preference set by SetPreferredCamera)
     */
    UFUNCTION(BlueprintPure, Category = "Camera2|Selection")
    static bool GetPreferredCamera();
    
    /**
     * Get a diagnostic string comparing runtime vs hardcoded calibration values.
     * Useful for debugging calibration differences between headsets.
     * 
     * @param bLeftCamera - true for left camera, false for right
     * @return Multi-line string showing runtime values, hardcoded values, and differences
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2|Diagnostics")
    static FString GetCalibrationDiagnostics(bool bLeftCamera = true);
    
    /**
     * Check if runtime calibration data is available from the device.
     * @param bOutHasIntrinsics - set to true if runtime intrinsics are available
     * @param bOutHasPose - set to true if runtime pose is available
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2|Diagnostics")
    static void IsRuntimeCalibrationAvailable(bool& bOutHasIntrinsics, bool& bOutHasPose);
    
};