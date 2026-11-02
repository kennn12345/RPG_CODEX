#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGBodyImpactComponent.h"
#include "RPGCharacterContactComponent.h"
#include "RPGControllerInputComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGPowerArena.h"
#include "RPGStrikeContactComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"

namespace
{
TArray<ECollisionChannel> FootChannels(USkeletalMeshComponent* Mesh)
{
	TArray<UAnimInstance*> Instances; TArray<ECollisionChannel> Channels;
	if (Mesh->GetAnimInstance()) Instances.Add(Mesh->GetAnimInstance());
	for (UAnimInstance* Instance : static_cast<const USkeletalMeshComponent*>(Mesh)->GetLinkedAnimInstances()) Instances.AddUnique(Instance);
	for (UAnimInstance* Instance : Instances)
		for (TFieldIterator<FStructProperty> It(Instance->GetClass()); It; ++It)
		{
			if (It->Struct->GetFName() != TEXT("AnimNode_FootPlacement")) continue;
			const FStructProperty* Settings = FindFProperty<FStructProperty>(It->Struct, TEXT("TraceSettings"));
			if (!Settings) continue;
			const void* Node = It->ContainerPtrToValuePtr<void>(Instance);
			const void* Trace = Settings->ContainerPtrToValuePtr<void>(Node);
			for (const TCHAR* Name : {TEXT("SimpleTraceChannel"), TEXT("ComplexTraceChannel")})
				if (const FByteProperty* Property = FindFProperty<FByteProperty>(Settings->Struct, Name))
					Channels.Add(UEngineTypes::ConvertToCollisionChannel(ETraceTypeQuery(Property->GetPropertyValue_InContainer(Trace))));
		}
	return Channels;
}

class FRecoveryOverlap : public IAutomationLatentCommand
{
public:
	FRecoveryOverlap(FAutomationTestBase* InTest, bool bPreexisting) : Test(InTest), bExistingIgnore(bPreexisting) {}
	bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (!World || World->bIsTearingDown) return ++Wait < 900 ? false : Fail(TEXT("PIE unavailable"));
		if (Fixture.Get() != World)
		{
			Fixture = World; Npc.Reset(); Phase = 0; Started = 0.f;
			PeakPassiveCorrection = PeakFootHeight = PeakPlayerTravel = 0.f;
			bSawProtectedRecovery = bSawReturn = bSawAnimatedRecovery = bAlignedRecoveryOverlap = false;
		}
		if (!Npc.IsValid()) Npc = RPGPowerArena::FindReferenceNpc(World);
		ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
		auto* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
		if (!Npc.IsValid() || !PC) return ++Wait < 900 ? false : Fail(TEXT("missing actors"));
		ARPGNpcCharacter* N = Npc.Get();
		const float Now = World->GetTimeSeconds();
		if (Phase == 0)
		{
			if (Now < 1.5f) return false;
			Player->SetActorLocation(N->GetActorLocation() + FVector(0, 1000, 0), false, nullptr, ETeleportType::TeleportPhysics);
			if (bExistingIgnore) Player->GetCapsuleComponent()->IgnoreActorWhenMoving(N, true);
			N->BodyImpact->KnockDown(FVector(0, 0, 15), N->GetMesh()->GetBoneLocation(TEXT("pelvis")));
			Started = Now; Phase = 1; return false;
		}
		if (Phase == 1)
		{
			if (Now - Started < .3f) return false;
			Test->TestTrue(TEXT("Fixture enters real ragdoll"), N->PhysicsReaction->GetNumSimulatingBodies() > 0);
			const FVector Pelvis = N->GetMesh()->GetBoneLocation(TEXT("pelvis"));
			FHitResult Ground; FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGRecoveryFloor), false, Player);
			if (!World->LineTraceSingleByObjectType(Ground, Pelvis + FVector(0, 0, 150), Pelvis - FVector(0, 0, 300), Objects, Params)) return Fail(TEXT("fixture has no world floor"));
			FloorZ = Ground.ImpactPoint.Z;
			StartPosition = FVector(Pelvis.X, Pelvis.Y, FloorZ + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f);
			Player->SetActorLocationAndRotation(StartPosition, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
			Player->GetCharacterMovement()->StopMovementImmediately(); PC->SetControlRotation(FRotator(-85, 0, 0));
			const TArray<ECollisionChannel> Channels = FootChannels(Player->GetMesh());
			Test->TestTrue(TEXT("Observed native GASP foot-placement trace settings"), Channels.Num() > 0);
			for (ECollisionChannel Channel : Channels)
			{
				FHitResult Hit;
				World->LineTraceSingleByChannel(Hit, Pelvis + FVector(0, 0, 150), Pelvis - FVector(0, 0, 300), Channel, Params);
				Test->TestTrue(TEXT("Foot support ignores the body and still finds the world floor"), Hit.bBlockingHit && Hit.GetActor() != N && FMath::Abs(Hit.ImpactPoint.Z - FloorZ) < 3.f);
				Test->TestEqual(TEXT("Native foot node uses dedicated ground query"), int32(Channel), int32(URPGCharacterContactComponent::GroundChannel));
			}
			FHitResult Weapon;
			World->LineTraceSingleByChannel(Weapon, Pelvis + FVector(0, 0, 150), Pelvis - FVector(0, 0, 100), ECC_Visibility, Params);
			Test->TestTrue(TEXT("Weapon visibility traces still hit the ragdoll"), Weapon.GetActor() == N);
			AStaticMeshActor* Step = RPGPowerArena::BuildFlatFloor(World, StartPosition + FVector(500, 0, -Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 50), 2.f);
			if (!Step) return Fail(TEXT("could not create raised world surface"));
			FHitResult StepHit;
			World->LineTraceSingleByChannel(StepHit, Step->GetActorLocation() + FVector(0, 0, 200), Step->GetActorLocation() - FVector(0, 0, 200), URPGCharacterContactComponent::GroundChannel, Params);
			Test->TestTrue(TEXT("Raised world surfaces remain valid foot support"), StepHit.GetActor() == Step);
			Started = Now; Phase = 2; return false;
		}
		if (Phase == 2)
		{
			const float T = Now - Started;
			// Exercise the passive legs during a genuine standard punch while the body is still down.
			if (T < .08f)
			{
				auto* Input = Player->FindComponentByClass<URPGControllerInputComponent>();
				auto* Action = Input ? FindObject<UInputAction>(Input, TEXT("IA_RightAttack")) : nullptr;
				auto* Subsystem = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
				if (!Action || !Subsystem) return Fail(TEXT("missing actual punch input"));
				Subsystem->InjectInputVectorForAction(Action, FVector(1, 0, 0), {}, {});
			}
			const bool PairIgnored = Player->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(N) && N->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Player);
			if (N->BodyImpact->IsRecovering()) bSawProtectedRecovery |= PairIgnored;
			if (N->PhysicsReaction->IsReturning())
			{
				bSawReturn = true;
				if (!bAlignedRecoveryOverlap)
				{
					// The selected get-up can align its capsule away from the fallen pelvis. Set up a real capsule
					// overlap at that destination, then verify it remains safe after the montage finishes.
					StartPosition = FVector(N->GetActorLocation().X, N->GetActorLocation().Y, FloorZ + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f);
					Player->SetActorLocation(StartPosition, false, nullptr, ETeleportType::TeleportPhysics);
					Player->GetCharacterMovement()->StopMovementImmediately();
					bAlignedRecoveryOverlap = true;
				}
			}
			if (N->PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Animated && N->BodyImpact->IsRecovering()) bSawAnimatedRecovery = true;
			if (auto* Contact = Player->FindComponentByClass<URPGStrikeContactComponent>())
				for (ERPGStrikeLimb Limb : {ERPGStrikeLimb::LeftFoot, ERPGStrikeLimb::RightFoot}) PeakPassiveCorrection = FMath::Max(PeakPassiveCorrection, float(Contact->GetCorrection(Limb).Size()));
			for (const TCHAR* Bone : {TEXT("foot_l"), TEXT("foot_r")}) PeakFootHeight = FMath::Max(PeakFootHeight, float(Player->GetMesh()->GetBoneLocation(Bone).Z - FloorZ));
			PeakPlayerTravel = FMath::Max(PeakPlayerTravel, float(FVector::Dist(Player->GetActorLocation(), StartPosition)));
			if (N->IsBusy() || N->BodyImpact->IsRecovering()) return T < 15.f ? false : Fail(TEXT("passive overlap interrupted or stalled recovery"));
			Test->TestTrue(TEXT("Observed protected physics-to-animation recovery"), bSawProtectedRecovery && bSawReturn && bSawAnimatedRecovery);
			Test->TestTrue(TEXT("Passive feet did not latch to moving NPC bones"), PeakPassiveCorrection < .5f);
			Test->TestTrue(TEXT("Planted feet stayed near world floor throughout get-up"), PeakFootHeight < 35.f);
			Test->TestTrue(TEXT("Recovery did not launch or drag the player"), PeakPlayerTravel < 25.f);
			Test->TestTrue(TEXT("Finished get-up does not re-enable capsule blocking inside the player"), PairIgnored);
			Player->SetActorLocation(StartPosition + FVector(400, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
			Started = Now; Phase = 3; return false;
		}
		if (Now - Started < .3f) return false;
		Test->TestEqual(TEXT("Own movement ignore entry restored without deleting pre-existing ignore"), Player->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(N), bExistingIgnore);
		Test->TestFalse(TEXT("Reciprocal recovery ignore restored once safely separated"), N->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Player));
		if (bExistingIgnore) Player->GetCapsuleComponent()->IgnoreActorWhenMoving(N, false);
		Test->AddInfo(FString::Printf(TEXT("RECOVERYCONTACT existing=%d feetCorrection=%.3f cm footHeight=%.3f cm playerTravel=%.3f cm"), bExistingIgnore, PeakPassiveCorrection, PeakFootHeight, PeakPlayerTravel));
		return true;
	}
private:
	bool Fail(const TCHAR* Why) { Test->AddError(Why); return true; }
	FAutomationTestBase* Test; bool bExistingIgnore;
	TWeakObjectPtr<UWorld> Fixture; TWeakObjectPtr<ARPGNpcCharacter> Npc;
	int32 Wait = 0, Phase = 0; float Started = 0.f, FloorZ = 0.f;
	FVector StartPosition;
	float PeakPassiveCorrection = 0.f, PeakFootHeight = 0.f, PeakPlayerTravel = 0.f;
	bool bSawProtectedRecovery = false, bSawReturn = false, bSawAnimatedRecovery = false;
	bool bAlignedRecoveryOverlap = false;
};

class FRecoveryFlightImpact : public IAutomationLatentCommand
{
public:
	explicit FRecoveryFlightImpact(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (!World || World->bIsTearingDown) return ++Wait < 900 ? false : Fail(TEXT("PIE unavailable"));
		if (Fixture.Get() != World) { Fixture = World; Npc.Reset(); Phase = 0; }
		if (!Npc.IsValid()) Npc = RPGPowerArena::FindReferenceNpc(World);
		ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
		if (!Player || !Npc.IsValid()) return ++Wait < 900 ? false : Fail(TEXT("missing actors"));
		ARPGNpcCharacter* N = Npc.Get(); const float Now = World->GetTimeSeconds();
		if (Phase == 0)
		{
			if (Now < 1.5f) return false;
			Player->SetActorLocation(N->GetActorLocation() + FVector(0, 1000, 0), false, nullptr, ETeleportType::TeleportPhysics);
			N->BodyImpact->KnockDown(FVector(0, 0, 20), N->GetMesh()->GetBoneLocation(TEXT("pelvis")));
			Started = Now; Phase = 1; return false;
		}
		if (Phase == 1)
		{
			if (N->PhysicsReaction->GetReactionState() != ERPGPhysicsReactionState::Animated || !N->BodyImpact->IsRecovering())
				return Now - Started < 12.f ? false : Fail(TEXT("no active get-up window"));
			Player->SetActorLocationAndRotation(N->GetActorLocation() - FVector(350, 0, 0), FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
			if (auto* Ability = Player->FindComponentByClass<URPGMovementAbilityComponent>()) Ability->StartFlight();
			else return Fail(TEXT("flight ability missing"));
			bRecoveryBeforeImpact = true;
			Started = Now; Phase = 2; return false;
		}
		if (N->BodyImpact->IsDown())
		{
			Test->TestTrue(TEXT("Impact reached the NPC while its get-up was still active"), bRecoveryBeforeImpact);
			Test->AddInfo(TEXT("RECOVERYFLIGHT: actual flying capsule collision interrupted the active get-up and knocked the NPC down."));
			return true;
		}
		if (Now - Started > 2.f) return Fail(TEXT("passive recovery protection swallowed a deliberate flying impact"));
		bRecoveryBeforeImpact = N->BodyImpact->IsRecovering();
		Player->GetCharacterMovement()->Velocity = FVector(1500, 0, 0);
		return false;
	}
private:
	bool Fail(const TCHAR* Why) { Test->AddError(Why); return true; }
	FAutomationTestBase* Test;
	TWeakObjectPtr<UWorld> Fixture; TWeakObjectPtr<ARPGNpcCharacter> Npc;
	int32 Wait = 0, Phase = 0; float Started = 0.f;
	bool bRecoveryBeforeImpact = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRecoveryContactTest, "RPG.Npc.RecoveryOverlap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGRecoveryContactTest::RunTest(const FString&)
{
	for (bool bExisting : {false, true})
	{
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		ADD_LATENT_AUTOMATION_COMMAND(FRecoveryOverlap(this, bExisting));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRecoveryFlightTest, "RPG.Npc.RecoveryFlightImpact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGRecoveryFlightTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRecoveryFlightImpact(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
