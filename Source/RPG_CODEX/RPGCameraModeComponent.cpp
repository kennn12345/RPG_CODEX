#include "RPGCameraModeComponent.h"
#include "RPGFlightAnimationComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "RPGAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "RPGPrototypeAnimationComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGCombatAimComponent.h"
#include "RPGEnergySpellComponent.h"

URPGCameraModeComponent::URPGCameraModeComponent()
{
	// After the mesh has its pose for the frame (the first-person camera follows the head bone).
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void URPGCameraModeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const USpringArmComponent* Boom = GetCameraBoom())
	{
		SavedArmLength = Boom->TargetArmLength;
		SavedSocketOffset = Boom->SocketOffset;
	}
}

void URPGCameraModeComponent::ClearFlightPresentation()
{
    if (USpringArmComponent* Boom=GetCameraBoom()) Boom->TargetArmLength-=FlightArmOffset;
    if (UCameraComponent* Camera=GetCamera()) Camera->SetFieldOfView(Camera->FieldOfView-FlightFOVOffset);
    FlightArmOffset=FlightFOVOffset=0.f;
}

void URPGCameraModeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearFlightPresentation();
    Super::EndPlay(Reason);
}

void URPGCameraModeComponent::UpdateFlightPresentation(float DeltaTime)
{
    const URPGFlightAnimationComponent* Flight=GetOwner()->FindComponentByClass<URPGFlightAnimationComponent>();
    const float Boost=bFlightSpeedCamera && Flight ? float(Flight->PoseBlend.Y)*Flight->FlightWeight : 0.f;
    const float Alpha=1.f-FMath::Exp(-FMath::Min(DeltaTime,0.1f)/0.35f);
    float Arm=FMath::Lerp(FlightArmOffset,FlightBoostDistance*Boost,Alpha);
    float FOV=FMath::Lerp(FlightFOVOffset,FlightBoostFOV*Boost,Alpha);
    if(Boost==0.f && Arm<0.005f) Arm=0.f;
    if(Boost==0.f && FOV<0.0005f) FOV=0.f;
    if(USpringArmComponent* Boom=GetCameraBoom()) Boom->TargetArmLength+=Arm-FlightArmOffset;
    if(UCameraComponent* Camera=GetCamera()) Camera->SetFieldOfView(Camera->FieldOfView+FOV-FlightFOVOffset);
    FlightArmOffset=Arm;FlightFOVOffset=FOV;
}

void URPGCameraModeComponent::TogglePerspective()
{
	USpringArmComponent* Boom = GetCameraBoom();
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Boom || !Character)
	{
		return;
	}

	SetCameraAdjustMode(false);
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();

	if (!bIsFirstPerson)
	{
		// Save third-person settings, then pull the camera into the head. (The socket offset was saved at Begin Play:
		// the current one may include the combat shoulder offset.)
		ClearFlightPresentation();
		SavedArmLength = Boom->TargetArmLength;
		bSavedUseControllerRotationYaw = Character->bUseControllerRotationYaw;
		bSavedOrientRotationToMovement = Movement && Movement->bOrientRotationToMovement;

		Boom->TargetArmLength = 0.f;
		Boom->SocketOffset = FVector(0.f, 0.f, FirstPersonEyeHeight);
		// The body's facing in first person is URPGCombatAimComponent's: it faces the camera, except standing in a fight,
		// where the torso turns toward the look first and the body follows.
		Character->bUseControllerRotationYaw = false;
		if (Movement)
		{
			Movement->bOrientRotationToMovement = false;
		}
		// The body stays visible (arms, legs, guard); only the head and neck go, so they never fill the camera.
		USkeletalMeshComponent* Mesh = Character->GetMesh();
		if (Mesh->GetBoneIndex(FirstPersonHiddenBone) != INDEX_NONE)
		{
			Mesh->HideBoneByName(FirstPersonHiddenBone, PBO_None);
		}
		if (UCameraComponent* Camera = GetCamera())
		{
			SavedCameraRelativeLocation = Camera->GetRelativeLocation();
		}
		bHaveSmoothedEye = false;
		SetFirstPersonShadow(true);
		bIsFirstPerson = true;
		SetAnimationPresentation(true);
	}
	else
	{
		Boom->TargetArmLength = SavedArmLength;
		Boom->SocketOffset = SavedSocketOffset;
		Character->bUseControllerRotationYaw = bSavedUseControllerRotationYaw;
		if (Movement)
		{
			Movement->bOrientRotationToMovement = bSavedOrientRotationToMovement;
		}
		Character->GetMesh()->UnHideBoneByName(FirstPersonHiddenBone);
		SetFirstPersonShadow(false);
		if (UCameraComponent* Camera = GetCamera())
		{
			Camera->SetRelativeLocation(SavedCameraRelativeLocation);
		}
		bIsFirstPerson = false;
		SetAnimationPresentation(false);
	}
}

void URPGCameraModeComponent::SetCameraAdjustMode(bool bEnabled)
{
	// Distance adjustment only makes sense in third person.
	bIsAdjustingCamera = bEnabled && !bIsFirstPerson;
}

void URPGCameraModeComponent::ApplyCameraAdjustInput(FVector2D StickInput, float DeltaSeconds)
{
	USpringArmComponent* Boom = GetCameraBoom();
	if (!bIsAdjustingCamera || !Boom)
	{
		return;
	}

	// Stick up = camera closer, stick down = camera further away.
	const float NewLength = Boom->TargetArmLength - FlightArmOffset - StickInput.Y * CameraDistanceAdjustSpeed * DeltaSeconds;
	Boom->TargetArmLength = FMath::Clamp(NewLength, MinCameraDistance, MaxCameraDistance) + FlightArmOffset;

	// StickInput.X is reserved for camera orbit / shoulder adjustment later.
}

USpringArmComponent* URPGCameraModeComponent::GetCameraBoom() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<USpringArmComponent>() : nullptr;
}

UCameraComponent* URPGCameraModeComponent::GetCamera() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UCameraComponent>() : nullptr;
}

void URPGCameraModeComponent::SetFirstPersonShadow(bool bEnabled)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}
	if (bEnabled && !ShadowBody && Mesh->GetSkeletalMeshAsset())
	{
		ShadowBody = NewObject<USkeletalMeshComponent>(Character, TEXT("FirstPersonShadowBody"));
		ShadowBody->SetSkeletalMeshAsset(Mesh->GetSkeletalMeshAsset());
		ShadowBody->SetupAttachment(Mesh);
		// Its own copy of the Animation Blueprint (locomotion from the same character), animated after the visible body.
		ShadowBody->SetAnimInstanceClass(Mesh->GetAnimClass());
		ShadowBody->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		// Shadow only: not drawn in the view (nor its depth pre-pass), no collision, no overlaps.
		ShadowBody->SetRenderInMainPass(false);
		ShadowBody->SetRenderInDepthPass(false);
		ShadowBody->SetCastShadow(true);
		ShadowBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShadowBody->SetGenerateOverlapEvents(false);
		ShadowBody->SetCanEverAffectNavigation(false);
		ShadowBody->RegisterComponent();
		ShadowBody->PrimaryComponentTick.AddPrerequisite(Mesh, Mesh->PrimaryComponentTick);
	}
	if (ShadowBody)
	{
		ShadowBody->SetVisibility(bEnabled);
		ShadowBody->SetComponentTickEnabled(bEnabled);
		if (URPGAnimInstance* Instance = Cast<URPGAnimInstance>(ShadowBody->GetAnimInstance()))
			Instance->MirrorFrom = bEnabled ? Character->FindComponentByClass<URPGPrototypeAnimationComponent>() : nullptr;
	}
	// The visible body is headless in first person; its own shadow would be too.
	Mesh->SetCastShadow(!bEnabled);
}

void URPGCameraModeComponent::SetAnimationPresentation(bool bFirstPerson)
{
	if (URPGPrototypeAnimationComponent* Animation = GetOwner() ? GetOwner()->FindComponentByClass<URPGPrototypeAnimationComponent>() : nullptr)
	{
		Animation->SetFirstPersonPresentation(bFirstPerson);
	}
}

FVector URPGCameraModeComponent::GetFirstPersonEyeTarget() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	const FName Head(TEXT("head"));
	if (!Mesh || Mesh->GetBoneIndex(Head) == INDEX_NONE)
	{
		return FVector(0.f, 0.f, FirstPersonEyeHeight);
	}
	// The eyes on the animated head, in mesh space; the camera takes only part of their motion away from the rest
	// eye, so the strikes' head drive reads as a small lean instead of a 30-60 cm camera lunge.
	const FTransform& MeshToWorld = Mesh->GetComponentTransform();
	const FVector HeadEye = MeshToWorld.InverseTransformPosition(Mesh->GetSocketTransform(Head).TransformPosition(FirstPersonEyeOffset));
	// The attack aim never moves the camera: its turn and tilt swing the chest about the eyes (URPGAnimInstance). Only
	// the attack animation's own head motion is followed, and only a little: the camera stays a steady aiming reference.
	FVector ReferenceEye=FirstPersonRestEye;
	const auto* Animation=GetOwner()->FindComponentByClass<URPGPrototypeAnimationComponent>();
	if(Animation)Animation->GetReferenceGuardEye(ReferenceEye);
	const FVector RestEye=FMath::Lerp(FirstPersonRestEye,ReferenceEye,ReferenceEyeWeight);
	const FVector Delta = HeadEye - RestEye;
	const FVector Eye = RestEye
		+ FVector(Delta.X * FirstPersonFollowHorizontal, Delta.Y * FirstPersonFollowHorizontal, Delta.Z * FirstPersonFollowVertical);
	return GetOwner()->GetActorTransform().InverseTransformPosition(MeshToWorld.TransformPosition(Eye));
}

void URPGCameraModeComponent::HoldReticleSpot(bool bActive)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* Player = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	const UCameraComponent* Camera = GetCamera();
	if (!bActive || !Player || !Camera)
	{
		bHoldingReticleSpot = false;
		return;
	}
	// Where the camera actually is this frame (the arm may lag behind its offset), looking along the view.
	const FRotator View = Player->GetControlRotation();
	const FVector Eye = Camera->GetComponentLocation();
	if (!bHoldingReticleSpot || !View.Equals(HeldViewRotation, 0.01f))
	{
		// Starting, or the player turned the view: hold what is under the reticle now (the first thing along the view,
		// else far down it).
        // The new look input was made from the last rendered camera. The component may already have moved
        // beneath it this frame; acquiring from that new origin would bake the first camera jump into the aim.
        const FVector IntentEye=Player->PlayerCameraManager?Player->PlayerCameraManager->GetLastFrameCameraCacheView().Location:Eye;
        HeldReticleSpot = IntentEye + View.Vector() * ReticleHoldRange;
        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGReticleHold),false,GetOwner());
        for(int32 Attempt=0;Attempt<4;++Attempt)
        {
            if(!GetWorld()->LineTraceSingleByChannel(Hit,IntentEye,HeldReticleSpot,ECC_Visibility,Params))break;
            if(Cast<UCapsuleComponent>(Hit.GetComponent())){Params.AddIgnoredComponent(Hit.GetComponent());continue;}
            HeldReticleSpot=Hit.ImpactPoint;break;
        }
		bHoldingReticleSpot = true;
	}
	const FRotator Kept = (HeldReticleSpot - Eye).Rotation();
	HeldViewRotation = FRotator(Kept.Pitch, Kept.Yaw, View.Roll);
	Player->SetControlRotation(HeldViewRotation);
}

void URPGCameraModeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UCameraComponent* Camera = GetCamera();
    const auto* OwnerCharacter=Cast<ACharacter>(GetOwner());
    const auto* Movement=OwnerCharacter?OwnerCharacter->GetCharacterMovement():nullptr;
    const auto* CombatAnimation=GetOwner()->FindComponentByClass<URPGPrototypeAnimationComponent>();
    const auto* CombatState=GetOwner()->FindComponentByClass<URPGCombatStateComponent>();
    // A stationary punch can turn the body beneath the shoulder camera or move the animated eyes. Keep the
    // existing world point under the reticle through that internal camera motion. Looking input immediately picks
    // a new point; this does not track an enemy, and movement keeps its normal free look.
    const bool bHoldMeleeReticle=CombatAnimation && CombatAnimation->IsCapturedStandardPunchPlaying()
        && CombatState && CombatState->AttackPhase!=ERPGAttackPhase::None && Movement && Movement->IsMovingOnGround()
        && Movement->Velocity.SizeSquared2D()<400.f && Movement->GetCurrentAcceleration().IsNearlyZero();
    if (!bIsFirstPerson)
	{
        UpdateFlightPresentation(DeltaTime);
        // Nearly straight behind for exploring and melee; over the right shoulder while casting.
		USpringArmComponent* Boom = GetCameraBoom();
		if (Boom && !bIsAdjustingCamera)
		{
            const URPGEnergySpellComponent* Spells=GetOwner()->FindComponentByClass<URPGEnergySpellComponent>();
            const float Now=GetWorld()->GetTimeSeconds();
            if(Spells && Spells->IsAiming())LastSpellTime=Now;
            // Held a moment after a cast, so a run of taps doesn't swing the camera back and forth.
            const float Desired=Now-LastSpellTime<SpellCameraHold?1.f:0.f;
            CombatCameraWeight=FMath::Lerp(CombatCameraWeight,Desired,1.f-FMath::Exp(-DeltaTime/0.15f));
            if(Desired==0.f && CombatCameraWeight<0.001f)CombatCameraWeight=0.f;
            const float Alpha=FMath::SmoothStep(0.f,1.f,CombatCameraWeight);
            const FVector Offset=SavedSocketOffset + ThirdPersonViewOffset + ThirdPersonCombatOffset * Alpha;
            // While the camera slides between framings (and a moment after, as the arm catches up), the view turns to keep
            // the reticle on the spot it was on: a quick cast lands where it was aimed when pressed.
            if(!Offset.Equals(Boom->SocketOffset,0.01f))ReticleHoldTime=0.4f;
            else ReticleHoldTime=FMath::Max(0.f,ReticleHoldTime-DeltaTime);
            HoldReticleSpot(ReticleHoldTime>0.f || bHoldMeleeReticle);
            Boom->SocketOffset = Offset;
		}
		return;
	}
	if (!Camera)
	{
		return;
	}
	// Smoothing runs relative to the body, so walking never lags the camera behind it. The camera sits at the body's
	// eyes and turns only with the body itself (smoothly); looking around never moves it, the arms swing under it.
	const auto* Animation=GetOwner()->FindComponentByClass<URPGPrototypeAnimationComponent>();FVector ReferenceEye;
	const auto* Character=Cast<ACharacter>(GetOwner());
	const bool bGrounded=Character && Character->GetCharacterMovement() && Character->GetCharacterMovement()->IsMovingOnGround() && !Character->GetMesh()->IsSimulatingPhysics();
	const bool bReference=bGrounded && Animation && Animation->IsInCombat() && GetOwner()->GetVelocity().SizeSquared2D()<400.f && Animation->GetReferenceGuardEye(ReferenceEye);
	// The reference has a lower stationary guard. Ease into its actual eye height
	// with the stance; keep the existing locomotion eye anchor outside that pose.
	ReferenceEyeWeight=FMath::FInterpConstantTo(ReferenceEyeWeight,bReference?1.f:0.f,DeltaTime,1.f/.3f);
	const FVector Target = GetFirstPersonEyeTarget();
	SmoothedEye = (!bHaveSmoothedEye || FirstPersonSmoothing <= 0.f) ? Target
		: FMath::Lerp(SmoothedEye, Target, 1.f - FMath::Exp(-FirstPersonSmoothing * DeltaTime));
	bHaveSmoothedEye = true;
	// Position only: the camera keeps the control rotation (where the player aims).
	Camera->SetWorldLocation(GetOwner()->GetActorTransform().TransformPosition(SmoothedEye));
    HoldReticleSpot(bHoldMeleeReticle);
}
