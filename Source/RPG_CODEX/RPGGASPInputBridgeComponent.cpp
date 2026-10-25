#include "RPGGASPInputBridgeComponent.h"

#include "RPGCameraModeComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGControllerInputComponent.h"
#include "RPGDirectionalHoverComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGGASP, Log, All);

namespace
{
    FBoolProperty* FindInputFlag(UStruct* Struct, const TCHAR* Prefix)
    {
        // User-defined struct fields have generated GUID suffixes. Keep their actual reflected layout.
        for (TFieldIterator<FBoolProperty> It(Struct); It; ++It)
        {
            if (It->GetName().StartsWith(Prefix)) return *It;
        }
        return nullptr;
    }
}

URPGGASPInputBridgeComponent::URPGGASPInputBridgeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
    SampleMappingContext = FSoftObjectPath(TEXT("/Game/Input/IMC_Sandbox.IMC_Sandbox"));
}

void URPGGASPInputBridgeComponent::BeginPlay()
{
    Super::BeginPlay();
    // The existing RPG player controller also installs the template's movement/look contexts.
    // They reference different actions and can consume keys before GASP sees them.
    for (const TCHAR* Path : {TEXT("/Game/Input/IMC_Default.IMC_Default"), TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook")})
    {
        if (UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, Path))
            LegacyContexts.Add(Context);
    }
    SampleContext = SampleMappingContext.LoadSynchronous();
    if (SampleContext)
    {
        LocomotionContext = DuplicateObject<UInputMappingContext>(SampleContext, this, TEXT("IMC_RPGGASPLocomotion"));
        const TArray<FEnhancedActionKeyMapping> Mappings = LocomotionContext->GetMappings();
        for (const FEnhancedActionKeyMapping& Mapping : Mappings)
        {
            const FName ActionName = Mapping.Action ? Mapping.Action->GetFName() : NAME_None;
            const bool bMovementOrLook = ActionName == TEXT("IA_Move") || ActionName == TEXT("IA_Move_WorldSpace")
                || ActionName == TEXT("IA_Look") || ActionName == TEXT("IA_Look_Gamepad");
            const bool bKeyboardJumpOrWalk = !Mapping.Key.IsGamepadKey()
                && (ActionName == TEXT("IA_Jump") || ActionName == TEXT("IA_Walk"));
            if (!bMovementOrLook && !bKeyboardJumpOrWalk)
                LocomotionContext->UnmapKey(Mapping.Action, Mapping.Key);
        }
    }

    InputStateProperty = FindFProperty<FStructProperty>(GetOwner()->GetClass(), TEXT("CharacterInputState"));
    if (InputStateProperty)
    {
        SprintProperty = FindInputFlag(InputStateProperty->Struct, TEXT("WantsToSprint_"));
        CrouchProperty = FindInputFlag(InputStateProperty->Struct, TEXT("WantsToCrouch_"));
        AimProperty = FindInputFlag(InputStateProperty->Struct, TEXT("WantsToAim_"));
        StrafeProperty = FindInputFlag(InputStateProperty->Struct, TEXT("WantsToStrafe_"));
    }
    bStateReady = SprintProperty && CrouchProperty && AimProperty && StrafeProperty;
    if (!bStateReady || !LocomotionContext)
    {
        UE_LOG(LogRPGGASP, Error, TEXT("GASP input adapter could not find the sample context or input-state fields on %s"), *GetNameSafe(GetOwner()));
        return;
    }

    // Supply RPG intent before GASP calculates this frame's gait and CMC settings.
    TInlineComponentArray<UActorComponent*> Components(GetOwner());
    for (UActorComponent* Component : Components)
    {
        if (Component->GetClass()->GetFName() == TEXT("AC_PreCMCTick_C"))
            Component->AddTickPrerequisiteComponent(this);
    }
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
        Character->GetCharacterMovement()->AddTickPrerequisiteComponent(this);
}

void URPGGASPInputBridgeComponent::ConfigureInput()
{
    const APawn* Pawn = Cast<APawn>(GetOwner());
    const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
    UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    if (!Subsystem || !LocomotionContext) return;

    if (BoundSubsystem.IsValid() && BoundSubsystem.Get() != Subsystem)
        BoundSubsystem->RemoveMappingContext(LocomotionContext);
    BoundSubsystem = Subsystem;

    // SetupInput runs on possession in the sample, so remove its original context whenever it reappears.
    if (Subsystem->HasMappingContext(SampleContext)) Subsystem->RemoveMappingContext(SampleContext);
    for (UInputMappingContext* Context : LegacyContexts)
        if (Subsystem->HasMappingContext(Context)) Subsystem->RemoveMappingContext(Context);
    if (!Subsystem->HasMappingContext(LocomotionContext)) Subsystem->AddMappingContext(LocomotionContext, 0);

    if (!bKeyboardConfigured)
    {
        URPGControllerInputComponent* RPGInput = GetOwner()->FindComponentByClass<URPGControllerInputComponent>();
        UInputMappingContext* RPGContext = RPGInput ? FindObject<UInputMappingContext>(RPGInput, TEXT("IMC_RPGController")) : nullptr;
        UInputAction* Sprint = RPGInput ? FindObject<UInputAction>(RPGInput, TEXT("IA_Sprint")) : nullptr;
        UInputAction* Crouch = RPGInput ? FindObject<UInputAction>(RPGInput, TEXT("IA_CrouchStealth")) : nullptr;
        if (RPGContext && Sprint && Crouch)
        {
            RPGContext->MapKey(Sprint, EKeys::LeftShift);
            RPGContext->MapKey(Crouch, EKeys::C);
            Subsystem->RequestRebuildControlMappings();
            bKeyboardConfigured = true;
        }
    }
    bInputReady = bKeyboardConfigured && Subsystem->HasMappingContext(LocomotionContext);
}

void URPGGASPInputBridgeComponent::SynchronizeLocomotion()
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (!bStateReady || !Character || !Character->IsLocallyControlled()) return;
    URPGCombatStateComponent* Combat = Character->FindComponentByClass<URPGCombatStateComponent>();
    const URPGCameraModeComponent* Camera = Character->FindComponentByClass<URPGCameraModeComponent>();
    if (!Combat) return;
    auto* Hover=Character->FindComponentByClass<URPGDirectionalHoverComponent>();
    if(Hover)Hover->UpdateIntent();
    const auto* Move=Character->GetCharacterMovement();
    FVector Input=Character->GetPendingMovementInputVector();
    if(Input.IsNearlyZero())Input=Character->GetLastMovementInputVector();
    const bool bGround=Move->IsMovingOnGround() && !Character->IsPlayingRootMotion();
    if(bGround && (Input.Size2D()>0.05f || Move->Velocity.Size2D()>10.f))DirectionalGroundHold=0.25f;
    else DirectionalGroundHold=FMath::Max(0.f,DirectionalGroundHold-GetWorld()->GetDeltaSeconds());
    const bool bDirectional=bDirectionalGroundMovement && bGround && DirectionalGroundHold>0.f;
    if(bDirectional && Combat->bIsSprinting && Character->GetController()
        && FVector::DotProduct(Input.GetSafeNormal2D(),FRotator(0,Character->GetController()->GetControlRotation().Yaw,0).Vector())<0.65f)
        if(auto* Ability=Character->FindComponentByClass<URPGMovementAbilityComponent>())Ability->StopSprint();

    void* State = InputStateProperty->ContainerPtrToValuePtr<void>(Character);
    SprintProperty->SetPropertyValue_InContainer(State, Combat->bIsSprinting);
    CrouchProperty->SetPropertyValue_InContainer(State, Character->GetCharacterMovement()->bWantsToCrouch);
    AimProperty->SetPropertyValue_InContainer(State, Combat->bIsAiming);
    StrafeProperty->SetPropertyValue_InContainer(State, bDirectional || (Hover && Hover->WantsHoverFacing())
        || Combat->bIsBlocking || Combat->bIsAiming || (Camera && Camera->bIsFirstPerson));
}

void URPGGASPInputBridgeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    ConfigureInput();
    SynchronizeLocomotion();
}

void URPGGASPInputBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (BoundSubsystem.IsValid() && LocomotionContext)
        BoundSubsystem->RemoveMappingContext(LocomotionContext);
    Super::EndPlay(EndPlayReason);
}
