#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGCharacterStatsComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGControllerInputComponent.h"
#include "RPGHitOutcomeComponent.h"
#include "RPGMeleeHitComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGPowerArena.h"
#include "RPGPrototypeAnimationComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "InputAction.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
struct FPunchCase { const TCHAR* Bone; float Distance; float Strength; bool bMiss; };
const FPunchCase Cases[] = {
    {TEXT("spine_05"), 90.f, 5.f, false},
    {TEXT("spine_05"), 100.f, 12.f, false},
    {TEXT("head"), 105.f, 12.f, false},
    {TEXT("head"), 100.f, 5.f, false},
    {TEXT("spine_05"), 350.f, 5.f, true},
};

// Actual animation/contact/Power/NPC path, including the first quarter-second of body motion.
// Each case gets fresh PIE actors; no synthetic hit or reused post-get-up pose.
class FNpcPunchImpactCommand : public IAutomationLatentCommand
{
public:
    FNpcPunchImpactCommand(FAutomationTestBase* InTest, int32 InCase) : Test(InTest), Index(InCase) {}
    bool Update() override
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        // StartPIE/EndPlay are deferred. Never keep setup or an actor from the preceding world while the new
        // instance is still starting (the old actor can remain a valid UObject until garbage collection).
        if (World && FixtureWorld.Get() != World)
        {
            FixtureWorld = World;
            Npc.Reset();
            Phase = 0; Started = 0.f; PeakSpin = 0.f; PeakBodies = 0;
        }
        ACharacter* Player = World ? UGameplayStatics::GetPlayerCharacter(World, 0) : nullptr;
        APlayerController* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
        auto* State = Player ? Player->FindComponentByClass<URPGCombatStateComponent>() : nullptr;
        auto* Melee = Player ? Player->FindComponentByClass<URPGMeleeHitComponent>() : nullptr;
        if (!Npc.IsValid() && World) Npc = RPGPowerArena::FindReferenceNpc(World);
        if (!Player || !PC || !PC->PlayerCameraManager || !State || !Melee || !Npc.IsValid())
        { if (++Wait < 600) return false; return Fail(TEXT("missing PIE actors/components")); }
        auto* N = Npc.Get();
        const FPunchCase& C = Cases[Index];
        const float Now = World->GetTimeSeconds();
        if (Phase == 0)
        {
            if (Now < 1.5f) return false;
            const FVector Base = N->GetActorLocation();
            N->GetCharacterMovement()->StopMovementImmediately();
            N->SetActorRotation(FRotator(0, 180, 0), ETeleportType::ResetPhysics);
            Player->SetActorLocationAndRotation(Base - FVector(C.Distance, 0, 0), FRotator::ZeroRotator, false, nullptr, ETeleportType::ResetPhysics);
            Player->GetCharacterMovement()->StopMovementImmediately();
            Player->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            N->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            if (auto* Stats = URPGCharacterStatsComponent::FindStats(Player)) Stats->SetStatBaseByName(TEXT("Strength"), C.Strength);
            N->HitOutcome->TestSeed = 42;
            CountBefore = Melee->ConfirmedHitCount;
            Started = Now; Phase = 1;
            return false;
        }
        if (Phase < 2)
        {
            const FVector Aim = N->GetMesh()->GetBoneLocation(C.Bone) + (FName(C.Bone) == TEXT("head") ? FVector(0, 0, -6) : FVector::ZeroVector);
            PC->SetControlRotation((Aim - PC->PlayerCameraManager->GetCameraLocation()).Rotation());
        }
        if (Phase == 1)
        {
            if (Now - Started < .8f) return false;
            Started = Now; Phase = 2;
        }
        if (Phase == 2)
        {
            auto* Input = Player->FindComponentByClass<URPGControllerInputComponent>();
            auto* Action = Input ? FindObject<UInputAction>(Input, TEXT("IA_RightAttack")) : nullptr;
            auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
            if (!Action || !Subsystem) return Fail(TEXT("Enhanced Input action unavailable"));
            if (Now - Started < .08f) Subsystem->InjectInputVectorForAction(Action, FVector(1, 0, 0), {}, {});
            if (Melee->ConfirmedHitCount > CountBefore)
            {
                if (C.bMiss) return Fail(TEXT("out-of-reach punch confirmed a hit"));
                const FRPGMeleeHitInfo& H = Melee->LastHit;
                if (H.HitActor != N) return Fail(TEXT("hit a different actor"));
                const float Into = FVector::DotProduct(H.StrikeDirection, H.ImpactNormal);
                Test->TestTrue(TEXT("Contact has incoming motion"), Into < -.05f && H.LimbSpeed > 150.f && H.LimbSpeed < 3000.f);
                Test->TestTrue(TEXT("Standard punch retains standard intent"), !H.bCharged && H.ChargePercent == 0.f);
                Test->AddInfo(FString::Printf(TEXT("PUNCHIMPACT case=%d bone=%s speed=%.1f normalDot=%.3f reaction=%s"), Index, *H.HitBone.ToString(), H.LimbSpeed, Into, *UEnum::GetDisplayValueAsText(N->LastPowerResult.Reaction).ToString()));
                CountAtHit = Melee->ConfirmedHitCount;
                Started = Now; Phase = 3;
            }
            else if (Now - Started > 1.2f)
            {
                if (C.bMiss) return true;
                Test->AddInfo(FString::Printf(TEXT("PUNCHIMPACT miss player=%s target=%s fist=%s aim=%s"),
                    *Player->GetActorLocation().ToCompactString(), *N->GetActorLocation().ToCompactString(),
                    *Player->GetMesh()->GetBoneLocation(TEXT("middle_01_r")).ToCompactString(), *PC->GetControlRotation().ToCompactString()));
                return Fail(TEXT("punch never hit"));
            }
            return false;
        }
        int32 Simulating = 0;
        for (const FBodyInstance* Body : N->GetMesh()->Bodies)
            if (Body && Body->IsInstanceSimulatingPhysics())
            {
                ++Simulating;
                const float Spin = FMath::RadiansToDegrees(float(Body->GetUnrealWorldAngularVelocityInRadians().Size()));
                if (!FMath::IsFinite(Spin)) return Fail(TEXT("non-finite body velocity"));
                PeakSpin = FMath::Max(PeakSpin, Spin);
            }
        PeakBodies = FMath::Max(PeakBodies, Simulating);
        if (Now - Started < .25f) return false;
        Test->TestEqual(TEXT("One confirmation per target per strike"), Melee->ConfirmedHitCount, CountAtHit);
        const bool bLaunch = C.Strength >= 12.f;
        Test->TestTrue(TEXT("Expected standing or whole-body response executed"), bLaunch ? PeakBodies == 23 : PeakBodies > 0 && PeakBodies < 23);
        // This fixture previously produced 40,000+ deg/s hands. A diagnostic guard for these controlled punches,
        // not a gameplay angular-speed clamp or an upper bound on fantasy Strength.
        Test->TestTrue(TEXT("No explosive transition spin in controlled fixture"), PeakSpin < 6000.f);
        Test->AddInfo(FString::Printf(TEXT("PUNCHIMPACT case=%d peakSpin=%.1f deg/s simBodies=%d"), Index, PeakSpin, PeakBodies));
        return true;
    }
private:
    bool Fail(const TCHAR* Why) { Test->AddError(FString::Printf(TEXT("PUNCHIMPACT case=%d: %s"), Index, Why)); return true; }
    FAutomationTestBase* Test;
    int32 Index, Wait = 0, Phase = 0, CountBefore = 0, CountAtHit = 0, PeakBodies = 0;
    float Started = 0.f, PeakSpin = 0.f;
    TWeakObjectPtr<ARPGNpcCharacter> Npc;
    TWeakObjectPtr<UWorld> FixtureWorld;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNpcPunchImpactTest, "RPG.Npc.PunchImpact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGNpcPunchImpactTest::RunTest(const FString& Parameters)
{
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.1f));
        ADD_LATENT_AUTOMATION_COMMAND(FNpcPunchImpactCommand(this, Index));
        ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    }
    return true;
}
#endif
