#include "RPGHUD.h"

#include "RPGCombatAimComponent.h"
#include "RPGCombatStateComponent.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarReticle(TEXT("rpg.Reticle"), 1, TEXT("Combat reticle at the screen centre: 0 = off, 1 = on."));
}

void ARPGHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || CVarReticle.GetValueOnGameThread() <= 0) return;

	const APawn* Pawn = GetOwningPawn();
	const URPGCombatAimComponent* Aim = Pawn ? Pawn->FindComponentByClass<URPGCombatAimComponent>() : nullptr;
	const URPGCombatStateComponent* State = Pawn ? Pawn->FindComponentByClass<URPGCombatStateComponent>() : nullptr;
	const float Fighting = Aim ? Aim->AimAlpha : 0.f;
	const bool bInReach = !State || State->bAttackAimInReach;

	const float Scale = Canvas->ClipY / 1080.f;
	const FVector2D Centre(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	const FLinearColor Shadow(0.f, 0.f, 0.f, Color.A * 0.6f);

	// Dot, with a dark edge so it reads on bright and dark backgrounds.
	const float Dot = FMath::Max(1.f, DotRadius * Scale);
	DrawRect(Shadow, Centre.X - Dot - 1.f, Centre.Y - Dot - 1.f, Dot * 2.f + 2.f, Dot * 2.f + 2.f);
	DrawRect(Color, Centre.X - Dot, Centre.Y - Dot, Dot * 2.f, Dot * 2.f);

	// Thin ring while fighting; fainter when the reticle asks for more than the body can reach.
	if (Fighting > 0.01f)
	{
		FLinearColor Ring = Color;
		Ring.A *= Fighting * (bInReach ? 0.6f : 0.25f);
		const float Radius = RingRadius * Scale;
		constexpr int32 Segments = 20;
		for (int32 I = 0; I < Segments; ++I)
		{
			const float A0 = 2.f * PI * I / Segments, A1 = 2.f * PI * (I + 1) / Segments;
			DrawLine(Centre.X + Radius * FMath::Cos(A0), Centre.Y + Radius * FMath::Sin(A0),
				Centre.X + Radius * FMath::Cos(A1), Centre.Y + Radius * FMath::Sin(A1), Ring, 1.f);
		}
	}
}
