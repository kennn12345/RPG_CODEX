#include "RPGCameraModeComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "RPGAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "RPGPrototypeAnimationComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGCombatAimComponent.h"

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
	const float NewLength = Boom->TargetArmLength - StickInput.Y * CameraDistanceAdjustSpeed * DeltaSeconds;
	Boom->TargetArmLength = FMath::Clamp(NewLength, MinCameraDistance, MaxCameraDistance);

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
	const FVector Delta = HeadEye - FirstPersonRestEye;
	const FVector Eye = FirstPersonRestEye
		+ FVector(Delta.X * FirstPersonFollowHorizontal, Delta.Y * FirstPersonFollowHorizontal, Delta.Z * FirstPersonFollowVertical);
	return GetOwner()->GetActorTransform().InverseTransformPosition(MeshToWorld.TransformPosition(Eye));
}

void URPGCameraModeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UCameraComponent* Camera = GetCamera();
	if (!bIsFirstPerson)
	{
		// Third person: over the shoulder while fighting, eased in / out with the combat aim.
		const URPGCombatAimComponent* Aim = GetOwner()->FindComponentByClass<URPGCombatAimComponent>();
		USpringArmComponent* Boom = GetCameraBoom();
		if (Boom && !bIsAdjustingCamera)
		{
			const float Alpha = Aim ? FMath::SmoothStep(0.f, 1.f, Aim->AimAlpha) : 0.f;
			Boom->SocketOffset = SavedSocketOffset + ThirdPersonCombatOffset * Alpha;
		}
		return;
	}
	if (!Camera)
	{
		return;
	}
	// Smoothing runs relative to the body, so walking never lags the camera behind it. The camera sits at the body's
	// eyes and turns only with the body itself (smoothly); looking around never moves it, the arms swing under it.
	const FVector Target = GetFirstPersonEyeTarget();
	SmoothedEye = (!bHaveSmoothedEye || FirstPersonSmoothing <= 0.f) ? Target
		: FMath::Lerp(SmoothedEye, Target, 1.f - FMath::Exp(-FirstPersonSmoothing * DeltaTime));
	bHaveSmoothedEye = true;
	// Position only: the camera keeps the control rotation (where the player aims).
	Camera->SetWorldLocation(GetOwner()->GetActorTransform().TransformPosition(SmoothedEye));
}
