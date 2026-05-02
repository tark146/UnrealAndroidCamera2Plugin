#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/Canvas.h"
#include "QRDepthPose.generated.h"

USTRUCT(BlueprintType)
struct FCameraIntrinsicsInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Camera2|Intrinsics")
	float Fx = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Camera2|Intrinsics")
	float Fy = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Camera2|Intrinsics")
	FVector2D PrincipalPoint = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Camera2|Intrinsics")
	float Skew = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Camera2|Intrinsics")
	FIntPoint Resolution = FIntPoint::ZeroValue;

	// UE order: [K1,K2,P1,P2,K3,K4,K5,K6]
	UPROPERTY(BlueprintReadWrite, Category = "Camera2|Intrinsics")
	TArray<float> Distortion;
};

USTRUCT(BlueprintType)
struct FDepthFrameInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "QR|Depth")
	FIntPoint DepthResolution = FIntPoint::ZeroValue;

	// Mapping DepthUV = RGB * Scale + Offset
	UPROPERTY(BlueprintReadWrite, Category = "QR|Depth")
	FVector2D RgbToDepthScale = FVector2D(1.0f, 1.0f);

	UPROPERTY(BlueprintReadWrite, Category = "QR|Depth")
	FVector2D RgbToDepthOffset = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct FQrDetection2D
{
	GENERATED_BODY()

	// [TopLeft, TopRight, BottomRight, BottomLeft]
	UPROPERTY(BlueprintReadWrite, Category = "QR")
	TArray<FVector2D> Corners;
};

UCLASS()
class ANDROIDCAMERA2PLUGIN_API UQRDepthPoseLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Camera2|Intrinsics")
	static FCameraIntrinsicsInfo MakeIntrinsicsFromCamera2();

	UFUNCTION(BlueprintCallable, Category = "QR|Depth")
	static bool SolveQrPoseFromDepth(const FQrDetection2D& Detection,
		const FCameraIntrinsicsInfo& Intrinsics,
		const FDepthFrameInfo& DepthInfo,
		const TArray<float>& DepthMeters,
		float MarkerSizeMillimeters,
		FTransform& OutPose,
		float& OutConfidence,
		int32 DepthSearchRadius = 0);

	UFUNCTION(BlueprintCallable, Category = "QR|Corners")
	static FQrDetection2D ConvertFinderPatternsToCorners(
		const FVector2D& BottomLeftCenter,
		const FVector2D& TopLeftCenter,
		const FVector2D& TopRightCenter,
		float ModuleSize);

	UFUNCTION(BlueprintCallable, Category = "QR|Corners")
	static FVector2D RefineCornerSubPixel(
		const TArray<uint8>& ImageData,
		int32 Width,
		int32 Height,
		const FVector2D& InitialCorner,
		int32 WindowSize);

	UFUNCTION(BlueprintCallable, Category = "QR|Depth")
	static FDepthFrameInfo MakeDepthFrameInfo(const FIntPoint& RgbResolution, const FIntPoint& DepthResolution);

	UFUNCTION(BlueprintCallable, Category = "QR|Corners")
	static float EstimateModuleSizeFromCenters(
		const FVector2D& TopLeftCenter,
		const FVector2D& TopRightCenter,
		const FVector2D& BottomLeftCenter,
		int32 DimensionModules);

	UFUNCTION(BlueprintCallable, Category = "QR|Corners")
	static FQrDetection2D MakeQrDetectionFromFinders(
		const FVector2D& BottomLeftCenter,
		const FVector2D& TopLeftCenter,
		const FVector2D& TopRightCenter,
		int32 DimensionModules);

	// Fetch latest ZXing finder detection and convert to corners in one call
	UFUNCTION(BlueprintCallable, Category = "QR|Detection")
	static bool MakeLatestQrDetection(FQrDetection2D& OutDetection, float& OutModuleSize, int32& OutDimensionModules, int64& OutTimestampMs);

	// Draw QR detection debug overlay on Canvas (call from HUD::DrawHUD)
	UFUNCTION(BlueprintCallable, Category = "QR|Debug")
	static void DrawQrDebugOverlay(
		UCanvas* Canvas,
		const FVector2D& TextureDisplayPos,
		const FVector2D& TextureDisplaySize,
		FLinearColor CornerColor = FLinearColor::Green,
		FLinearColor LineColor = FLinearColor::White,
		float PointSize = 8.0f,
		float LineThickness = 2.0f);
};
