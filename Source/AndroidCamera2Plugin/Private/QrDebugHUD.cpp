#include "QrDebugHUD.h"
#include "QRDepthPose.h"
#include "SimpleCamera2Test.h"
#include "Engine/Canvas.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"

void AQrDebugHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	// 左上にステータス表示
	FString StatusText = TEXT("QR Debug HUD Active");

	// QR検出状態を取得
	FQrDetection2D Detection;
	float ModuleSize = 0.0f;
	int32 Dimension = 0;
	int64 Timestamp = 0;
	const bool bDetected = UQRDepthPoseLibrary::MakeLatestQrDetection(Detection, ModuleSize, Dimension, Timestamp);

	if (bDetected)
	{
		StatusText = FString::Printf(TEXT("QR Detected (Dim:%d, Mod:%.1f)"), Dimension, ModuleSize);
	}

	// テキスト描画（中央やや上）
	Canvas->SetDrawColor(FColor::Green);
	const float TextWidth = StatusText.Len() * 8.0f; // 概算
	const float TextX = (Canvas->SizeX - TextWidth) * 0.5f;
	const float TextY = Canvas->SizeY * 0.3f; // 上から30%の位置
	Canvas->DrawText(GEngine->GetSmallFont(), StatusText, TextX, TextY);

	FVector2D DisplayPos = TextureDisplayPos;
	FVector2D DisplaySize = TextureDisplaySize;

	if (bFullscreen)
	{
		DisplayPos = FVector2D::ZeroVector;
		DisplaySize = FVector2D(Canvas->SizeX, Canvas->SizeY);
	}

	// QR検出済みの場合のみ描画（既に取得済みなので直接描画）
	if (bDetected && Detection.Corners.Num() == 4)
	{
		FIntPoint TexRes = USimpleCamera2Test::GetCalibrationResolution();
		if (TexRes.X <= 0 || TexRes.Y <= 0)
		{
			TexRes = FIntPoint(1280, 960);
		}

		TArray<FVector2D> ScreenCorners;
		ScreenCorners.Reserve(4);
		for (const FVector2D& Corner : Detection.Corners)
		{
			const FVector2D Normalized(Corner.X / static_cast<float>(TexRes.X), Corner.Y / static_cast<float>(TexRes.Y));
			const FVector2D Screen = DisplayPos + Normalized * DisplaySize;
			ScreenCorners.Add(Screen);
		}

		const FVector2D HalfSize(PointSize * 0.5f, PointSize * 0.5f);
		for (const FVector2D& Screen : ScreenCorners)
		{
			Canvas->K2_DrawBox(Screen - HalfSize, FVector2D(PointSize, PointSize), 1.0f, CornerColor);
		}

		Canvas->K2_DrawLine(ScreenCorners[0], ScreenCorners[1], LineThickness, LineColor);
		Canvas->K2_DrawLine(ScreenCorners[1], ScreenCorners[2], LineThickness, LineColor);
		Canvas->K2_DrawLine(ScreenCorners[2], ScreenCorners[3], LineThickness, LineColor);
		Canvas->K2_DrawLine(ScreenCorners[3], ScreenCorners[0], LineThickness, LineColor);
	}
}

void AQrDebugHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	DrawDebug3D();
}

void AQrDebugHUD::DrawDebug3D()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}

	// カメラ位置と向きを取得
	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

	// QR検出を取得
	FQrDetection2D Detection;
	float ModuleSize = 0.0f;
	int32 Dimension = 0;
	int64 Timestamp = 0;
	const bool bDetected = UQRDepthPoseLibrary::MakeLatestQrDetection(Detection, ModuleSize, Dimension, Timestamp);

	// ステータステキストを3D空間に表示
	const FVector TextLocation = CameraLocation + CameraRotation.Vector() * DebugDrawDistance + FVector(0, 0, DebugDrawScale * 0.5f);
	FString StatusText = bDetected
		? FString::Printf(TEXT("QR Detected (Dim:%d)"), Dimension)
		: TEXT("QR Debug Active");
	DrawDebugString(World, TextLocation, StatusText, nullptr, FColor::Green, 0.0f, true);

	if (!bDetected || Detection.Corners.Num() != 4)
	{
		return;
	}

	// テクスチャ解像度
	FIntPoint TexRes = USimpleCamera2Test::GetCalibrationResolution();
	if (TexRes.X <= 0 || TexRes.Y <= 0)
	{
		TexRes = FIntPoint(1280, 960);
	}

	// カメラ前方にデバッグ平面を配置
	const FVector Forward = CameraRotation.Vector();
	const FVector Right = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Y);
	const FVector Up = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Z);
	const FVector PlaneCenter = CameraLocation + Forward * DebugDrawDistance;

	// 4隅を3D座標に変換
	TArray<FVector> WorldCorners;
	WorldCorners.Reserve(4);
	for (const FVector2D& Corner : Detection.Corners)
	{
		// 正規化座標（-0.5 ~ 0.5）
		const float NormX = (Corner.X / static_cast<float>(TexRes.X)) - 0.5f;
		const float NormY = (Corner.Y / static_cast<float>(TexRes.Y)) - 0.5f;

		// 3D位置
		const FVector WorldPos = PlaneCenter
			+ Right * NormX * DebugDrawScale
			- Up * NormY * DebugDrawScale; // Yは反転（画像座標系）
		WorldCorners.Add(WorldPos);
	}

	// 4辺を描画
	const FColor DrawColor = FColor::Green;
	DrawDebugLine(World, WorldCorners[0], WorldCorners[1], DrawColor, false, 0.0f, 0, 2.0f);
	DrawDebugLine(World, WorldCorners[1], WorldCorners[2], DrawColor, false, 0.0f, 0, 2.0f);
	DrawDebugLine(World, WorldCorners[2], WorldCorners[3], DrawColor, false, 0.0f, 0, 2.0f);
	DrawDebugLine(World, WorldCorners[3], WorldCorners[0], DrawColor, false, 0.0f, 0, 2.0f);

	// 4隅に点を描画
	for (const FVector& Pos : WorldCorners)
	{
		DrawDebugPoint(World, Pos, 10.0f, FColor::Yellow, false, 0.0f);
	}
}
