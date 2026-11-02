#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "RPGCharacterContactComponent.generated.h"

class ACharacter;
class UAnimInstance;

/** Ground support and passive recovery contact, independent of weapon/visibility traces. */
UCLASS(ClassGroup=(RPG), meta=(BlueprintSpawnableComponent))
class RPG_CODEX_API URPGCharacterContactComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGCharacterContactComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick) override;
	static constexpr ECollisionChannel GroundChannel = ECC_GameTraceChannel4;
	static bool IsRecovering(const AActor* Actor);
	int32 GetConfiguredFootNodes() const { return ConfiguredFootNodes; }
private:
	struct FContactPair
	{
		TWeakObjectPtr<ACharacter> Other;
		bool bAddedOwnIgnore = false, bAddedOtherIgnore = false;
	};
	void ConfigureGroundQueries(ACharacter* Character);
	void ReleasePair(ACharacter* Character, const FContactPair& Pair);
	TArray<FContactPair> Pairs;
	TSet<TWeakObjectPtr<UAnimInstance>> ConfiguredInstances;
	int32 ConfiguredFootNodes = 0;
};
