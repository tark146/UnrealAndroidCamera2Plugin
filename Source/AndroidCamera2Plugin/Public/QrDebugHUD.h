#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "QrDebugHUD.generated.h"

UCLASS()
class ANDROIDCAMERA2PLUGIN_API AQrDebugHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	void DrawDebug3D();

	// テクスチャ表示領域（スクリーン座標）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QR Debug")
	FVector2D TextureDisplayPos = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QR Debug")
	FVector2D TextureDisplaySize = FVector2D::ZeroVector;

	// 描画設定
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QR Debug")
	FLinearColor CornerColor = FLinearColor::Green;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QR Debug")
	FLinearColor LineColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QR Debug")
	float PointSize = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QR Debug")
	float LineThickness = 2.0f;

	// 全画面モード（TextureDisplaySizeを自動設定）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QR Debug")
	bool bFullscreen = true;

	// 3Dデバッグ表示の距離（カメラ前方）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QR Debug")
	float DebugDrawDistance = 100.0f;

	// 3Dデバッグ表示のスケール
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QR Debug")
	float DebugDrawScale = 50.0f;
};
