#include "QRDepthPose.h"
#include "SimpleCamera2Test.h"

namespace
{
	float SampleLuma(const TArray<uint8>& Image, int32 Width, int32 Height, int32 X, int32 Y)
	{
		if (X < 0 || Y < 0 || X >= Width || Y >= Height)
		{
			return 0.0f;
		}
		const int32 Index = Y * Width + X;
		if (Image.Num() == Width * Height)
		{
			return static_cast<float>(Image[Index]);
		}

		// Assume BGRA
		const int32 Base = Index * 4;
		if (!Image.IsValidIndex(Base + 3))
		{
			return 0.0f;
		}
		const float B = Image[Base + 0];
		const float G = Image[Base + 1];
		const float R = Image[Base + 2];
		return 0.114f * B + 0.587f * G + 0.299f * R;
	}

	// Clamp-safe depth lookup with optional radius search
	bool SampleDepthAt(const FDepthFrameInfo& DepthInfo, const FIntPoint DepthRes, const FVector2D& RgbPoint, const TArray<float>& DepthMeters, int32 SearchRadius, float& OutDepth)
	{
		if (DepthRes.X <= 0 || DepthRes.Y <= 0)
		{
			return false;
		}

		FVector2D DepthUV = FVector2D(RgbPoint.X * DepthInfo.RgbToDepthScale.X + DepthInfo.RgbToDepthOffset.X,
			RgbPoint.Y * DepthInfo.RgbToDepthScale.Y + DepthInfo.RgbToDepthOffset.Y);

		const int32 X0 = FMath::FloorToInt(DepthUV.X);
		const int32 Y0 = FMath::FloorToInt(DepthUV.Y);
		const int32 Radius = FMath::Max(0, SearchRadius);

		float BestDepth = 0.0f;
		for (int32 dy = -Radius; dy <= Radius; ++dy)
		{
			for (int32 dx = -Radius; dx <= Radius; ++dx)
			{
				const int32 X = X0 + dx;
				const int32 Y = Y0 + dy;
				if (X < 0 || Y < 0 || X >= DepthRes.X || Y >= DepthRes.Y)
				{
					continue;
				}
				const int32 Index = Y * DepthRes.X + X;
				if (!DepthMeters.IsValidIndex(Index))
				{
					continue;
				}
				const float D = DepthMeters[Index];
				if (D > 0.0f)
				{
					BestDepth = D;
					break;
				}
			}
			if (BestDepth > 0.0f)
			{
				break;
			}
		}

		OutDepth = BestDepth;
		return OutDepth > 0.0f;
	}

	FVector UndistortPoint(const FVector2D& Distorted, const FCameraIntrinsicsInfo& K)
	{
		// Normalize to camera coordinates
		const float x = (Distorted.X - K.PrincipalPoint.X) / K.Fx;
		const float y = (Distorted.Y - K.PrincipalPoint.Y) / K.Fy;

		// UE order: [K1,K2,P1,P2,K3,K4,K5,K6]
		const float k1 = K.Distortion.Num() > 0 ? K.Distortion[0] : 0.0f;
		const float k2 = K.Distortion.Num() > 1 ? K.Distortion[1] : 0.0f;
		const float p1 = K.Distortion.Num() > 2 ? K.Distortion[2] : 0.0f;
		const float p2 = K.Distortion.Num() > 3 ? K.Distortion[3] : 0.0f;
		const float k3 = K.Distortion.Num() > 4 ? K.Distortion[4] : 0.0f;
		const float k4 = K.Distortion.Num() > 5 ? K.Distortion[5] : 0.0f;
		const float k5 = K.Distortion.Num() > 6 ? K.Distortion[6] : 0.0f;
		const float k6 = K.Distortion.Num() > 7 ? K.Distortion[7] : 0.0f;

		// Iterative undistortion
		FVector2D Guess(x, y);
		for (int32 Iter = 0; Iter < 5; ++Iter)
		{
			const float r2 = Guess.X * Guess.X + Guess.Y * Guess.Y;
			const float r4 = r2 * r2;
			const float r6 = r4 * r2;
			const float radial = 1.0f + k1 * r2 + k2 * r4 + k3 * r6;
			const float radial2 = 1.0f + k4 * r2 + k5 * r4 + k6 * r6;
			const float denom = FMath::Max(radial * radial2, KINDA_SMALL_NUMBER);

			const float xTangential = 2.0f * p1 * Guess.X * Guess.Y + p2 * (r2 + 2.0f * Guess.X * Guess.X);
			const float yTangential = p1 * (r2 + 2.0f * Guess.Y * Guess.Y) + 2.0f * p2 * Guess.X * Guess.Y;

			const FVector2D DistortedGuess(
				Guess.X * denom + xTangential,
				Guess.Y * denom + yTangential);

			const FVector2D Error = FVector2D(x, y) - DistortedGuess;
			Guess += Error * 0.5f;
			if (Error.SizeSquared() < 1e-9f)
			{
				break;
			}
		}

		return FVector(Guess.X, Guess.Y, 1.0f);
	}

	bool ComputePoseFromPoints(const TArray<FVector>& Points, FTransform& OutPose, float& OutConfidence)
	{
		if (Points.Num() < 3)
		{
			OutConfidence = 0.0f;
			return false;
		}

		const FVector P0 = Points[0];
		const FVector P1 = Points[1];
		const FVector P3 = Points.Last(); // assume [TL,TR,BR,BL]

		FVector XAxis = (P1 - P0).GetSafeNormal();
		FVector YAxis = (P3 - P0).GetSafeNormal();
		FVector ZAxis = FVector::CrossProduct(XAxis, YAxis).GetSafeNormal();

		if (ZAxis.IsNearlyZero())
		{
			OutConfidence = 0.0f;
			return false;
		}

		if (ZAxis.Z > 0.0f)
		{
			ZAxis *= -1.0f;
		}

		YAxis = FVector::CrossProduct(ZAxis, XAxis).GetSafeNormal();
		XAxis = FVector::CrossProduct(YAxis, ZAxis).GetSafeNormal();

		const FVector Center = (P0 + P1 + Points[2] + P3) * 0.25f;
		FMatrix Basis(
			FPlane(XAxis, 0.0f),
			FPlane(YAxis, 0.0f),
			FPlane(ZAxis, 0.0f),
			FPlane(Center, 1.0f));

		OutPose = FTransform(Basis);
		OutConfidence = 1.0f;
		return true;
	}
}

FCameraIntrinsicsInfo UQRDepthPoseLibrary::MakeIntrinsicsFromCamera2()
{
	FCameraIntrinsicsInfo Out;
	Out.Fx = USimpleCamera2Test::GetCameraFx();
	Out.Fy = USimpleCamera2Test::GetCameraFy();
	Out.PrincipalPoint = USimpleCamera2Test::GetPrincipalPoint();
	Out.Skew = USimpleCamera2Test::GetCameraSkew();
	Out.Resolution = USimpleCamera2Test::GetCalibrationResolution();
	Out.Distortion = USimpleCamera2Test::GetLensDistortionUE();
	return Out;
}

bool UQRDepthPoseLibrary::SolveQrPoseFromDepth(const FQrDetection2D& Detection,
	const FCameraIntrinsicsInfo& Intrinsics,
	const FDepthFrameInfo& DepthInfo,
	const TArray<float>& DepthMeters,
	float MarkerSizeMillimeters,
	FTransform& OutPose,
	float& OutConfidence,
	int32 DepthSearchRadius)
{
	OutConfidence = 0.0f;
	if (Detection.Corners.Num() < 4)
	{
		return false;
	}

	const FIntPoint DepthRes = DepthInfo.DepthResolution;
	TArray<FVector> PointsCam;
	PointsCam.Reserve(4);

	for (int32 i = 0; i < 4; ++i)
	{
		const FVector UndistortedRay = UndistortPoint(Detection.Corners[i], Intrinsics);
		float Depth = 0.0f;
		if (!SampleDepthAt(DepthInfo, DepthRes, Detection.Corners[i], DepthMeters, DepthSearchRadius, Depth))
		{
			return false;
		}

		const FVector Dir = FVector(UndistortedRay.X, UndistortedRay.Y, 1.0f).GetSafeNormal();
		const FVector CamPoint = Dir * Depth;
		PointsCam.Add(CamPoint);
	}

	const bool bPoseOk = ComputePoseFromPoints(PointsCam, OutPose, OutConfidence);
	if (!bPoseOk)
	{
		return false;
	}

	if (MarkerSizeMillimeters > 0.0f)
	{
		const float MaxReasonableDepth = 3.0f;
		const float DepthAtCenter = PointsCam[0].Size();
		const float DepthFactor = FMath::Clamp(1.0f - (DepthAtCenter / MaxReasonableDepth), 0.1f, 1.0f);
		OutConfidence *= DepthFactor;
	}

	return true;
}

FQrDetection2D UQRDepthPoseLibrary::ConvertFinderPatternsToCorners(
	const FVector2D& BottomLeftCenter,
	const FVector2D& TopLeftCenter,
	const FVector2D& TopRightCenter,
	float ModuleSize)
{
	FQrDetection2D Out;
	if (ModuleSize <= KINDA_SMALL_NUMBER)
	{
		return Out;
	}

	const FVector2D AxisX = (TopRightCenter - TopLeftCenter).GetSafeNormal();
	const FVector2D AxisY = (BottomLeftCenter - TopLeftCenter).GetSafeNormal();
	const float Offset = 3.5f * ModuleSize;

	const FVector2D CornerTL = TopLeftCenter - AxisX * Offset - AxisY * Offset;
	const FVector2D CornerTR = TopRightCenter + AxisX * Offset - AxisY * Offset;
	const FVector2D CornerBL = BottomLeftCenter - AxisX * Offset + AxisY * Offset;
	const FVector2D CornerBR = CornerTR + (CornerBL - CornerTL);

	Out.Corners = { CornerTL, CornerTR, CornerBR, CornerBL };
	return Out;
}

FVector2D UQRDepthPoseLibrary::RefineCornerSubPixel(
	const TArray<uint8>& ImageData,
	int32 Width,
	int32 Height,
	const FVector2D& InitialCorner,
	int32 WindowSize)
{
	if (Width <= 0 || Height <= 0 || ImageData.Num() == 0)
	{
		return InitialCorner;
	}

	const int32 Half = FMath::Max(1, WindowSize / 2);
	const int32 X0 = FMath::Clamp(static_cast<int32>(FMath::RoundToInt(InitialCorner.X)) - Half, 0, Width - 1);
	const int32 Y0 = FMath::Clamp(static_cast<int32>(FMath::RoundToInt(InitialCorner.Y)) - Half, 0, Height - 1);
	const int32 X1 = FMath::Clamp(X0 + WindowSize, 0, Width - 1);
	const int32 Y1 = FMath::Clamp(Y0 + WindowSize, 0, Height - 1);

	float SumW = 0.0f;
	FVector2D SumPos(0.0f, 0.0f);

	for (int32 y = Y0; y <= Y1; ++y)
	{
		for (int32 x = X0; x <= X1; ++x)
		{
			const float W = SampleLuma(ImageData, Width, Height, x, y);
			SumW += W;
			SumPos.X += W * static_cast<float>(x);
			SumPos.Y += W * static_cast<float>(y);
		}
	}

	if (SumW <= KINDA_SMALL_NUMBER)
	{
		return InitialCorner;
	}

	const FVector2D Refined(SumPos.X / SumW, SumPos.Y / SumW);
	return Refined;
}

FDepthFrameInfo UQRDepthPoseLibrary::MakeDepthFrameInfo(const FIntPoint& RgbResolution, const FIntPoint& DepthResolution)
{
	FDepthFrameInfo Info;
	Info.DepthResolution = DepthResolution;

	if (RgbResolution.X > 0 && RgbResolution.Y > 0 && DepthResolution.X > 0 && DepthResolution.Y > 0)
	{
		Info.RgbToDepthScale = FVector2D(
			static_cast<float>(DepthResolution.X) / static_cast<float>(RgbResolution.X),
			static_cast<float>(DepthResolution.Y) / static_cast<float>(RgbResolution.Y));
		Info.RgbToDepthOffset = FVector2D::ZeroVector;
	}

	return Info;
}

float UQRDepthPoseLibrary::EstimateModuleSizeFromCenters(
	const FVector2D& TopLeftCenter,
	const FVector2D& TopRightCenter,
	const FVector2D& BottomLeftCenter,
	int32 DimensionModules)
{
	if (DimensionModules <= 7)
	{
		return 0.0f;
	}

	const float SpanX = (TopRightCenter - TopLeftCenter).Size();
	const float SpanY = (BottomLeftCenter - TopLeftCenter).Size();
	const float ModulesBetweenCenters = static_cast<float>(DimensionModules - 7);
	const float ModuleX = SpanX / ModulesBetweenCenters;
	const float ModuleY = SpanY / ModulesBetweenCenters;
	return (ModuleX + ModuleY) * 0.5f;
}

FQrDetection2D UQRDepthPoseLibrary::MakeQrDetectionFromFinders(
	const FVector2D& BottomLeftCenter,
	const FVector2D& TopLeftCenter,
	const FVector2D& TopRightCenter,
	int32 DimensionModules)
{
	FQrDetection2D Out;
	const float ModuleSize = EstimateModuleSizeFromCenters(TopLeftCenter, TopRightCenter, BottomLeftCenter, DimensionModules);
	if (ModuleSize <= KINDA_SMALL_NUMBER)
	{
		return Out;
	}
	return ConvertFinderPatternsToCorners(BottomLeftCenter, TopLeftCenter, TopRightCenter, ModuleSize);
}

bool UQRDepthPoseLibrary::MakeLatestQrDetection(FQrDetection2D& OutDetection, float& OutModuleSize, int32& OutDimensionModules, int64& OutTimestampMs)
{
	FVector2D BL, TL, TR;
	float ModuleSize = 0.0f;
	int32 Dimension = 0;
	int64 Timestamp = 0;
	const bool bHas = USimpleCamera2Test::GetLatestQrFinderDetection(BL, TL, TR, ModuleSize, Dimension, Timestamp);
	if (!bHas)
	{
		OutDetection.Corners.Reset();
		OutModuleSize = 0.0f;
		OutDimensionModules = 0;
		OutTimestampMs = 0;
		return false;
	}

	// Prefer provided dimension; fall back to module size-based path if >0
	if (Dimension > 0)
	{
		OutDetection = MakeQrDetectionFromFinders(BL, TL, TR, Dimension);
	}
	else
	{
		OutDetection = ConvertFinderPatternsToCorners(BL, TL, TR, ModuleSize);
	}

	OutModuleSize = ModuleSize;
	OutDimensionModules = Dimension;
	OutTimestampMs = Timestamp;
	return OutDetection.Corners.Num() == 4;
}

void UQRDepthPoseLibrary::DrawQrDebugOverlay(
	UCanvas* Canvas,
	const FVector2D& TextureDisplayPos,
	const FVector2D& TextureDisplaySize,
	FLinearColor CornerColor,
	FLinearColor LineColor,
	float PointSize,
	float LineThickness)
{
	if (!Canvas)
	{
		return;
	}

	// 1. Get QR detection
	FQrDetection2D Detection;
	float ModuleSize;
	int32 Dimension;
	int64 Timestamp;

	if (!MakeLatestQrDetection(Detection, ModuleSize, Dimension, Timestamp))
	{
		return;
	}

	if (Detection.Corners.Num() != 4)
	{
		return;
	}

	// 2. Get texture resolution
	FIntPoint TexRes = USimpleCamera2Test::GetCalibrationResolution();
	if (TexRes.X <= 0 || TexRes.Y <= 0)
	{
		TexRes = FIntPoint(1280, 960); // Fallback
	}

	// 3. Transform to screen coordinates
	TArray<FVector2D> ScreenCorners;
	ScreenCorners.Reserve(4);
	for (const FVector2D& Corner : Detection.Corners)
	{
		const FVector2D Normalized(Corner.X / static_cast<float>(TexRes.X), Corner.Y / static_cast<float>(TexRes.Y));
		const FVector2D Screen = TextureDisplayPos + Normalized * TextureDisplaySize;
		ScreenCorners.Add(Screen);
	}

	// 4. Draw corner points
	const FVector2D HalfSize(PointSize * 0.5f, PointSize * 0.5f);
	for (const FVector2D& Screen : ScreenCorners)
	{
		Canvas->K2_DrawBox(Screen - HalfSize, FVector2D(PointSize, PointSize), 1.0f, CornerColor);
	}

	// 5. Draw lines (TL->TR->BR->BL->TL)
	Canvas->K2_DrawLine(ScreenCorners[0], ScreenCorners[1], LineThickness, LineColor);
	Canvas->K2_DrawLine(ScreenCorners[1], ScreenCorners[2], LineThickness, LineColor);
	Canvas->K2_DrawLine(ScreenCorners[2], ScreenCorners[3], LineThickness, LineColor);
	Canvas->K2_DrawLine(ScreenCorners[3], ScreenCorners[0], LineThickness, LineColor);
}
