#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RPGHUD.generated.h"

/**
 * The combat reticle: a small dot at the screen centre with a thin ring around it, in first and third person. The
 * attack aim (URPGCombatAimComponent) is traced from the camera through exactly this point. The ring shows while
 * fighting; it fades when the aim is beyond what the body can reach (the attack then goes as far toward it as it can).
 * Console: rpg.Reticle 0 / 1.
 */
UCLASS()
class RPG_CODEX_API ARPGHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Dot radius and ring radius, in pixels at 1080p (scaled with the screen height). */
	UPROPERTY(EditAnywhere, Category = "RPG|Reticle")
	float DotRadius = 1.5f;

	UPROPERTY(EditAnywhere, Category = "RPG|Reticle")
	float RingRadius = 7.f;

	UPROPERTY(EditAnywhere, Category = "RPG|Reticle")
	FLinearColor Color = FLinearColor(1.f, 1.f, 1.f, 0.85f);
};
