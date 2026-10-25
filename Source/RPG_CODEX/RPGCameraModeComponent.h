#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGCameraModeComponent.generated.h"

class USpringArmComponent;

/**
 * First-person / third-person toggle and the "hold D-Pad Right" camera distance adjustment.
 * Works with the template's CameraBoom (Spring Arm).
 *
 * First person shows the same body from its eyes: the head and neck are hidden, the camera sits at the eyes and the
 * prototype animations switch to their first-person presentation (RPG_FP_* clips, same timing; see
 * URPGPrototypeAnimationComponent::SetFirstPersonPresentation). The camera follows only part of the head's motion
 * (the strikes drive the head 30-60 cm), smoothed, and never rotates with it: the view stays where the player aims.
 *
 * Hiding the head bone also removes it from the body's shadow, so in first person the visible body casts no shadow and
 * a shadow-only copy casts it instead: a second skeletal mesh running the same Animation Blueprint, never drawn in the
 * view, no collision, whose montages URPGPrototypeAnimationComponent mirrors with the approved third-person clips at the
 * same time. So the world sees the full-body pose of the same action (head included, no first-person cheats).
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGCameraModeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGCameraModeComponent();
	virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Camera height above the capsule center in first person, used only if the mesh has no head bone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera")
	float FirstPersonEyeHeight = 70.f;

	/** The eyes in the head bone's space (Manny: X up, Y forward). Must match EYE_LOCAL in Tools/ProtoAnim/fpanim.py. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|First Person")
	FVector FirstPersonEyeOffset = FVector(15.f, 13.f, 0.f);

	/** The eyes in the combat stance, in mesh space. The first-person clips are laid out around this point (REST_EYE in fpanim.py). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|First Person")
	FVector FirstPersonRestEye = FVector(-5.96f, 25.24f, 167.2f);

	/** Share of the head's horizontal / vertical motion (relative to the rest eye) the camera follows (FOLLOW_H / FOLLOW_V in fpanim.py). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|First Person", meta = (ClampMin = "0", ClampMax = "1"))
	float FirstPersonFollowHorizontal = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|First Person", meta = (ClampMin = "0", ClampMax = "1"))
	float FirstPersonFollowVertical = 0.4f;

	/** How quickly the camera catches up with the eye point (per second); filters out small jitter. 0 = no smoothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|First Person", meta = (ClampMin = "0"))
	float FirstPersonSmoothing = 25.f;

	/**
	 * Third person while fighting: the camera eases this far over the right shoulder (spring-arm socket offset: X
	 * forward, Y right, Z up), so the reticle sits beside the character instead of on its back. Zero to turn off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera")
    FVector ThirdPersonCombatOffset = FVector(0.f, 35.f, 25.f);

    /** Raised shoulder framing even outside combat. Added to the original boom socket offset. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera")
    FVector ThirdPersonViewOffset = FVector(0.f,25.f,35.f);

    /** Speed presentation only during third-person flight; restored when flight ends. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|Flight")
    bool bFlightSpeedCamera = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|Flight", meta = (ClampMin = "0", ClampMax = "30"))
    float FlightBoostFOV = 8.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|Flight", meta = (ClampMin = "0"))
    float FlightBoostDistance = 140.f;

	/** Hidden in first person (with everything below it: the head), so it never sits in the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|First Person")
	FName FirstPersonHiddenBone = TEXT("neck_01");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera")
	float MinCameraDistance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera")
	float MaxCameraDistance = 900.f;

	/** Units per second the camera moves while adjusting with the Right Stick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera")
	float CameraDistanceAdjustSpeed = 600.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Camera")
	bool bIsFirstPerson = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Camera")
	bool bIsAdjustingCamera = false;

	UFUNCTION(BlueprintCallable, Category = "RPG|Camera")
	void TogglePerspective();

	UFUNCTION(BlueprintCallable, Category = "RPG|Camera")
	void SetCameraAdjustMode(bool bEnabled);

	/** Right Stick input while in adjust mode. Y = distance. X is reserved for orbit later. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Camera")
	void ApplyCameraAdjustInput(FVector2D StickInput, float DeltaSeconds);

private:
    void UpdateFlightPresentation(float DeltaTime);
    void ClearFlightPresentation();
    float FlightArmOffset = 0.f, FlightFOVOffset = 0.f;
    float CombatCameraWeight = 0.f;
	USpringArmComponent* GetCameraBoom() const;
	class UCameraComponent* GetCamera() const;
	/** Where the first-person camera wants to be this frame, relative to the body (actor space). */
	FVector GetFirstPersonEyeTarget() const;
	void SetAnimationPresentation(bool bFirstPerson);
	/** First person: the full-body shadow comes from the shadow-only copy, not the (headless) visible body. */
	void SetFirstPersonShadow(bool bEnabled);

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ShadowBody;

	FVector SmoothedEye = FVector::ZeroVector;
	bool bHaveSmoothedEye = false;
	FVector SavedCameraRelativeLocation = FVector::ZeroVector;

	// Third-person settings saved when switching to first person, restored when switching back.
	float SavedArmLength = 400.f;
	FVector SavedSocketOffset = FVector::ZeroVector;
	bool bSavedUseControllerRotationYaw = false;
	bool bSavedOrientRotationToMovement = true;
};
