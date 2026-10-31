#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGAnimInstance.h"
#include "RPGBodyImpactComponent.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGHitOutcomeComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGPowerArena.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Editor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
class FBraceRecoveryCommand : public IAutomationLatentCommand
{
public:
    explicit FBraceRecoveryCommand(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        UWorld* W = GEditor ? GEditor->PlayWorld : nullptr;
        if (!W) return ++Wait > 600 ? Fail(TEXT("no PIE world")) : false;
        if (!Npc.IsValid()) Npc = RPGPowerArena::FindReferenceNpc(W);
        ACharacter* Player = UGameplayStatics::GetPlayerCharacter(W, 0);
        if (!Npc.IsValid() || !Player) return ++Wait > 600 ? Fail(TEXT("missing actors")) : false;
        auto* N = Npc.Get(); URPGHitOutcomeComponent* Outcome = N->HitOutcome;
        auto* Anim = Cast<URPGAnimInstance>(N->GetMesh()->GetAnimInstance());
        if (!Anim) return Fail(TEXT("missing RPG anim instance"));
        const float Now = W->GetTimeSeconds();
        if (Phase == 0)
        {
            if (Now < 1.5f) return false;
            Player->SetActorLocation(N->GetActorLocation() + FVector(0, 1000, 0));
            Player->GetCharacterMovement()->StopMovementImmediately();
            N->BodyImpact->KnockDown(FVector(140, 0, 35), N->GetMesh()->GetBoneLocation(TEXT("spine_05")));
            Started = Now; Phase = 1; return false;
        }
        if (Phase == 1)
        {
            if (Now - Started < .25f) return false;
            Test->TestTrue(TEXT("Conscious fall actually published a brace pose"), Anim->Brace.Weight > .8f);
            // A lethal follow-up while down: the normal NPC route clears its brace while keeping full ragdoll.
            N->Stats->SetStatBaseByName(TEXT("Health"), 1.f);
            FRPGMeleeHitInfo H; H.Attacker = Player; H.HitActor = N; H.HitBone = TEXT("spine_05");
            H.ImpactLocation = N->GetMesh()->GetBoneLocation(H.HitBone); H.StrikeDirection = FVector(1, 0, 0);
            N->ReceiveMeleeHit_Implementation(H);
            Started = Now; Phase = 2; return false;
        }
        if (Phase == 2)
        {
            if (Now - Started < .1f) return false;
            Test->TestTrue(TEXT("Knockout clears component brace"), Outcome->GetBraceWeight() < .001f);
            Test->TestTrue(TEXT("Knockout clears visible animation brace"), Anim->Brace.Weight < .001f);
            Phase = 3;
        }
        if (N->IsBusy() || N->PhysicsReaction->GetReactionState() != ERPGPhysicsReactionState::Animated)
            return Now - Started < 15.f ? false : Fail(TEXT("recovery did not finish"));
        Test->TestTrue(TEXT("Standing NPC has no stale brace overlay"), Anim->Brace.Weight < .001f);
        Test->AddInfo(FString::Printf(TEXT("BRACERECOVERY episode=%d component=%.3f animation=%.3f"), Episode, Outcome->GetBraceWeight(), Anim->Brace.Weight));
        if (++Episode == 3) return true;
        Phase = 0; return false;
    }
private:
    bool Fail(const TCHAR* Why) { Test->AddError(Why); return true; }
    FAutomationTestBase* Test; TWeakObjectPtr<ARPGNpcCharacter> Npc;
    int32 Wait = 0, Phase = 0, Episode = 0; float Started = 0.f;
};

class FCatchBumpCommand : public IAutomationLatentCommand
{
public:
    explicit FCatchBumpCommand(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        UWorld* W = GEditor ? GEditor->PlayWorld : nullptr;
        if (!W) return ++Wait > 600 ? Fail(TEXT("no PIE world")) : false;
        if (!Npc.IsValid()) Npc = RPGPowerArena::FindReferenceNpc(W);
        ACharacter* Player = UGameplayStatics::GetPlayerCharacter(W, 0);
        if (!Npc.IsValid() || !Player) return ++Wait > 600 ? Fail(TEXT("missing actors")) : false;
        auto* N = Npc.Get(); const float Now = W->GetTimeSeconds();
        if (Phase == 0)
        {
            if (Now < 1.5f) return false;
            Player->SetActorLocation(N->GetActorLocation() + FVector(0, 1000, 0));
            Player->GetCharacterMovement()->StopMovementImmediately();
            N->HitOutcome->TestKneeCatch = 1;
            FRPGPowerResult Catch; Catch.Reaction = ERPGPowerReaction::Catch; Catch.ReactionLevel = 8.f;
            Knee = N->HitOutcome->TryKneeCatch(N->GetActorForwardVector(), false, Catch);
            if (!Knee) return Fail(TEXT("knee catch did not start"));
            Started = Now; Phase = 1; return false;
        }
        if (Phase == 1)
        {
            if (Now - Started < .1f) return false;
            // Same transferred speed as an equal-mass 180 cm/s walking collision (restitution .3).
            // Footing can classify it as knockback; that must not manufacture a 400 cm/s attack shove.
            N->BodyImpact->Bump(N->GetActorRightVector() * 117.f, N->GetMesh()->GetBoneLocation(TEXT("spine_03")), ERPGPowerReaction::Knockback);
            Test->AddInfo(FString::Printf(TEXT("CATCHBUMP transferred=117 queued=%.1f cm/s playerPhysicsPush=%d pushFactor=%.0f"), N->GetDrivenShoveVelocity().Size(), Player->GetCharacterMovement()->bEnablePhysicsInteraction, Player->GetCharacterMovement()->PushForceFactor));
            Test->TestTrue(TEXT("A walking bump must not queue an amplified drive during a knee catch"), N->GetDrivenShoveVelocity().Size() <= 117.1f);
            BumpAt = Now; Phase = 2;
        }
        const float SideSpeed = FMath::Abs(float(FVector::DotProduct(N->GetVelocity(), N->GetActorRightVector())));
        PeakSideSpeed = FMath::Max(PeakSideSpeed, SideSpeed);
        Test->TestTrue(TEXT("Walking into a catch never starts a ragdoll launch"), N->PhysicsReaction->GetReactionState() != ERPGPhysicsReactionState::Limp);
        if (Now - BumpAt < 2.5f) return false;
        Test->AddInfo(FString::Printf(TEXT("CATCHBUMP transferred=117 peakSide=%.1f cm/s"), PeakSideSpeed));
        Test->TestTrue(TEXT("Walking momentum is not promoted to an attack-strength shove"), PeakSideSpeed < 180.f);
        return true;
    }
private:
    bool Fail(const TCHAR* Why) { Test->AddError(Why); return true; }
    FAutomationTestBase* Test; TWeakObjectPtr<ARPGNpcCharacter> Npc; UAnimMontage* Knee = nullptr;
    int32 Wait = 0, Phase = 0; float Started = 0.f, BumpAt = 0.f, PeakSideSpeed = 0.f;
};

class FRagdollWalkCommand : public IAutomationLatentCommand
{
public:
    explicit FRagdollWalkCommand(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        UWorld* W = GEditor ? GEditor->PlayWorld : nullptr;
        if (!W) return ++Wait > 600 ? Fail(TEXT("no PIE world")) : false;
        if (!Npc.IsValid()) Npc = RPGPowerArena::FindReferenceNpc(W);
        ACharacter* Player = UGameplayStatics::GetPlayerCharacter(W, 0);
        if (!Npc.IsValid() || !Player) return ++Wait > 600 ? Fail(TEXT("missing actors")) : false;
        auto* N = Npc.Get(); const float Now = W->GetTimeSeconds();
        if (Phase == 0)
        {
            if (Now < 1.5f) return false;
            Top = N->GetActorLocation() + FVector(0, 0, 8000);
            if (!RPGPowerArena::BuildFlatFloor(W, Top, 30.f)) return Fail(TEXT("floor failed"));
            N->SetActorLocationAndRotation(Top + FVector(0, 0, N->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1), FRotator(0, 180, 0), false, nullptr, ETeleportType::ResetPhysics);
            N->GetCharacterMovement()->StopMovementImmediately();
            Player->SetActorLocationAndRotation(Top + FVector(-150, 0, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1), FRotator::ZeroRotator, false, nullptr, ETeleportType::ResetPhysics);
            Player->GetCharacterMovement()->StopMovementImmediately();
            Started = Now; Phase = 1; return false;
        }
        if (Phase == 1)
        {
            if (Now - Started < .5f) return false;
            N->BodyImpact->DownTime = 5.f;
            N->BodyImpact->KnockDown(FVector::ZeroVector, N->GetMesh()->GetBoneLocation(TEXT("pelvis")));
            bLegacyBlock = FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_TEST_RESTORE_PAWN_BLOCK")) == TEXT("1");
            if (bLegacyBlock) N->GetMesh()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
            StartPelvis = N->GetMesh()->GetBoneLocation(TEXT("pelvis"));
            Test->TestEqual(TEXT("Ragdolls still collide with other physical bodies"), N->GetMesh()->GetCollisionResponseToChannel(ECC_PhysicsBody), ECR_Block);
            if (!bLegacyBlock) Test->TestEqual(TEXT("Movement capsules do not push simulated bones"), N->GetMesh()->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
            Started = Now; Phase = 2; return false;
        }
        const FVector Pelvis = N->GetMesh()->GetBoneLocation(TEXT("pelvis"));
        MinDistance = FMath::Min(MinDistance, float(FVector::Dist2D(Player->GetActorLocation(), Pelvis)));
        MaxTravel = FMath::Max(MaxTravel, float(FVector::Dist2D(Pelvis, StartPelvis)));
        PeakSpeed = FMath::Max(PeakSpeed, float(N->GetMesh()->GetPhysicsLinearVelocity(TEXT("pelvis")).Size2D()));
        if (Now - Started < 1.4f)
        {
            Player->GetCharacterMovement()->Velocity = FVector(180, 0, 0);
            return false;
        }
        Player->GetCharacterMovement()->StopMovementImmediately();
        Test->AddInfo(FString::Printf(TEXT("RAGDOLLWALK legacyBlock=%d nearest=%.1f cm travel=%.1f cm peakSpeed=%.1f cm/s"), bLegacyBlock, MinDistance, MaxTravel, PeakSpeed));
        Test->TestTrue(TEXT("Walking fixture actually approached the body"), MinDistance < 70.f);
        // A zero-launch fall plus walking contact stays within two feet; the restored old Pawn-blocking
        // response moved this fixture about 80 cm despite the player walking at only 180 cm/s.
        Test->TestTrue(TEXT("Walking cannot catapult this near-stationary ragdoll"), MaxTravel <= 2.f * 30.48f && PeakSpeed < 450.f);
        return true;
    }
private:
    bool Fail(const TCHAR* Why) { Test->AddError(Why); return true; }
    FAutomationTestBase* Test; TWeakObjectPtr<ARPGNpcCharacter> Npc;
    int32 Wait = 0, Phase = 0; float Started = 0.f, MinDistance = 1.e9f, MaxTravel = 0.f, PeakSpeed = 0.f;
    FVector Top, StartPelvis; bool bLegacyBlock = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBraceRecoveryTest, "RPG.Npc.BraceRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGBraceRecoveryTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.1f));
    ADD_LATENT_AUTOMATION_COMMAND(FBraceRecoveryCommand(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGCatchBumpTest, "RPG.Npc.CatchBump", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGCatchBumpTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.1f));
    ADD_LATENT_AUTOMATION_COMMAND(FCatchBumpCommand(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRagdollWalkTest, "RPG.Npc.RagdollWalkContact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGRagdollWalkTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.1f));
    ADD_LATENT_AUTOMATION_COMMAND(FRagdollWalkCommand(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
