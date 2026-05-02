#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SimpleCamera2Test.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSimpleCamera2, Log, All);

/**
 * Simple Camera2 API - Basic camera to texture functionality
 */
UCLASS(BlueprintType)
class ANDROIDCAMERA2PLUGIN_API USimpleCamera2Test : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Start camera preview using Camera2 API
     * @return true if camera started successfully
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2")
    static bool StartCameraPreview();

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

    // ===== Camera Intrinsics =====

    /** Get camera intrinsic parameter Fx (focal length in pixels) */
    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static float GetCameraFx();

    /** Get camera intrinsic parameter Fy (focal length in pixels) */
    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static float GetCameraFy();

    /** Get principal point (cx, cy) */
    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static FVector2D GetPrincipalPoint();

    /** Get camera skew */
    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static float GetCameraSkew();

    /** Get calibration resolution (width, height) */
    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static FIntPoint GetCalibrationResolution();

    /** Get lens distortion coefficients */
    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static TArray<float> GetLensDistortion();

    /** Get original sensor resolution */
    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static FIntPoint GetOriginalResolution();

    /** Get lens distortion in UE format [K1,K2,P1,P2,K3,K4,K5,K6] */
    UFUNCTION(BlueprintPure, Category = "Camera2|Lens Distortion")
    static TArray<float> GetLensDistortionUE();

    /** Get camera characteristics as JSON string */
    UFUNCTION(BlueprintCallable, Category = "Camera2|Characteristics")
    static FString GetCameraCharacteristics();

    /** Last detected QR finder patterns (ZXing): bottom-left, top-left, top-right centers */
    UFUNCTION(BlueprintCallable, Category = "Camera2|QR")
    static bool GetLatestQrFinderDetection(
        FVector2D& OutBottomLeft,
        FVector2D& OutTopLeft,
        FVector2D& OutTopRight,
        float& OutModuleSize,
        int32& OutDimensionModules,
        int64& OutTimestampMs);
};
