#include "RPGBodyMass.h"

#include "RPGPowerSettings.h"
#include "RPG_CODEX.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
	struct FShare { const TCHAR* Bone; float Share; };
	// Adult male segment mass fractions (de Leva 1996), spread over the skeleton's 23 bodies. Sums to 1.
	//   head + neck 6.94%; upper trunk 15.96% (chest, collarbones); middle trunk 16.33%; lower trunk 11.17% (pelvis);
	//   upper arm 2.71%, forearm 1.62%, hand 0.61%; thigh 14.16%, shank 4.33%, foot 1.37% (each side).
	const FShare Shares[] = {
		{ TEXT("head"), 0.0520f }, { TEXT("neck_01"), 0.0087f }, { TEXT("neck_02"), 0.0087f },
		{ TEXT("spine_05"), 0.0720f }, { TEXT("spine_04"), 0.0600f }, { TEXT("clavicle_l"), 0.0138f }, { TEXT("clavicle_r"), 0.0138f },
		{ TEXT("spine_03"), 0.0590f }, { TEXT("spine_02"), 0.0550f }, { TEXT("spine_01"), 0.0493f },
		{ TEXT("pelvis"), 0.1117f },
		{ TEXT("upperarm_l"), 0.0271f }, { TEXT("upperarm_r"), 0.0271f },
		{ TEXT("lowerarm_l"), 0.0162f }, { TEXT("lowerarm_r"), 0.0162f },
		{ TEXT("hand_l"), 0.0061f }, { TEXT("hand_r"), 0.0061f },
		{ TEXT("thigh_l"), 0.1416f }, { TEXT("thigh_r"), 0.1416f },
		{ TEXT("calf_l"), 0.0433f }, { TEXT("calf_r"), 0.0433f },
		{ TEXT("foot_l"), 0.0137f }, { TEXT("foot_r"), 0.0137f },
	};
}

float RPGBodyMass::Fraction(FName Bone)
{
	for (const FShare& S : Shares) if (Bone == FName(S.Bone)) return S.Share;
	return 0.f;
}

float RPGBodyMass::TotalKgFor(float OverrideLbs)
{
	const float Lbs = OverrideLbs > 0.f ? OverrideLbs : URPGPowerSettings::Get()->HumanoidBodyWeightLbs;
	return Lbs * KgPerLb;
}

float RPGBodyMass::Apply(USkeletalMeshComponent* Mesh, float TotalKg)
{
	if (!Mesh || TotalKg <= 0.f) return 0.f;
	// Known shares present on this mesh, and the bodies the table doesn't know.
	float Known = 0.f;
	int32 Unknown = 0;
	for (const FBodyInstance* Body : Mesh->Bodies)
	{
		if (!Body) continue;
		const float Share = Fraction(Mesh->GetBoneName(Body->InstanceBoneIndex));
		if (Share > 0.f) Known += Share; else ++Unknown;
	}
	if (Known <= 0.f) return 0.f;
	// A few extra bodies (props, twist bones) get a small share each; the table's bodies split the rest by their shares.
	const float ExtraShare = Unknown > 0 ? FMath::Min(0.005f, 0.05f / Unknown) : 0.f;
	const float TableScale = (1.f - ExtraShare * Unknown) / Known;
	float Total = 0.f;
	for (FBodyInstance* Body : Mesh->Bodies)
	{
		if (!Body) continue;
		const float Share = Fraction(Mesh->GetBoneName(Body->InstanceBoneIndex));
		const float Kg = TotalKg * (Share > 0.f ? Share * TableScale : ExtraShare);
		Body->SetMassOverride(Kg, true);
		Body->UpdateMassProperties();
		Total += Kg;
	}
	UE_LOG(LogRPGInput, Log, TEXT("Body weight on %s: %.1f kg (%.0f lb) over %d bodies"), *GetNameSafe(Mesh->GetOwner()), Total, Total / KgPerLb, Mesh->Bodies.Num());
	return Total;
}
