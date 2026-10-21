#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGPlayerTypes.h"
#include "RPGStrikeContactComponent.generated.h"

class USkeletalMeshComponent;
class UPrimitiveComponent;

namespace RPGStrikeContact
{
	/** The four limbs that can make contact, in this order everywhere (anim instance, proxy, component). */
	enum ELimbIndex : int32 { RightHand, LeftHand, RightFoot, LeftFoot, NumLimbs };

	/** Surfaces one limb can be held against at once (one per body touched: chest and neck, say). */
	constexpr int32 MaxPlanes = 8;

	/** A sphere on the limb at T between bones A and B, this thick (cm). */
	struct FSurfaceSample { const TCHAR* A; const TCHAR* B; float T; float Radius; };

	struct FLimbDef
	{
		const TCHAR* Root;		// upper arm / thigh
		const TCHAR* Mid;		// forearm / calf (elbow / knee)
		const TCHAR* End;		// hand / foot: moved by the correction, children follow
		/** The end segment (fist / foot): what the correction keeps out of the surface. */
		FSurfaceSample Samples[3];
		/** The elbow / knee and the forearm / shin: what the limb cannot pull out by bending, so the body backs off. */
		FSurfaceSample Inner[2];
		float Compression;		// cm the surface may give before it stops the limb
		float MinFold;			// degrees: bending the elbow / knee to stay out of the surface stops here (or at the
								// animation's own angle, if that is tighter)
	};

	/** Mannequin limbs. Radii are the mesh's thickness there (the fist ~9 cm across, the foot ~10). */
	inline const FLimbDef& Limb(int32 Index)
	{
		static const FLimbDef Defs[NumLimbs] = {
			{TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"),
				{{TEXT("hand_r"), TEXT("middle_01_r"), 0.55f, 4.5f}, {TEXT("middle_01_r"), TEXT("middle_01_r"), 0.f, 3.5f}, {TEXT("hand_r"), TEXT("hand_r"), 0.f, 3.5f}},
				{{TEXT("lowerarm_r"), TEXT("lowerarm_r"), 0.f, 4.5f}, {TEXT("lowerarm_r"), TEXT("hand_r"), 0.5f, 4.f}}, 1.5f, 35.f},
			{TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"),
				{{TEXT("hand_l"), TEXT("middle_01_l"), 0.55f, 4.5f}, {TEXT("middle_01_l"), TEXT("middle_01_l"), 0.f, 3.5f}, {TEXT("hand_l"), TEXT("hand_l"), 0.f, 3.5f}},
				{{TEXT("lowerarm_l"), TEXT("lowerarm_l"), 0.f, 4.5f}, {TEXT("lowerarm_l"), TEXT("hand_l"), 0.5f, 4.f}}, 1.5f, 35.f},
			{TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"),
				{{TEXT("foot_r"), TEXT("ball_r"), 0.5f, 5.f}, {TEXT("ball_r"), TEXT("ball_r"), 0.f, 4.f}, {TEXT("foot_r"), TEXT("foot_r"), 0.f, 5.f}},
				{{TEXT("calf_r"), TEXT("calf_r"), 0.f, 6.f}, {TEXT("calf_r"), TEXT("foot_r"), 0.5f, 5.5f}}, 2.f, 30.f},
			{TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"),
				{{TEXT("foot_l"), TEXT("ball_l"), 0.5f, 5.f}, {TEXT("ball_l"), TEXT("ball_l"), 0.f, 4.f}, {TEXT("foot_l"), TEXT("foot_l"), 0.f, 5.f}},
				{{TEXT("calf_l"), TEXT("calf_l"), 0.f, 6.f}, {TEXT("calf_l"), TEXT("foot_l"), 0.5f, 5.5f}}, 2.f, 30.f},
		};
		return Defs[Index];
	}

	inline int32 IndexOf(ERPGStrikeLimb Limb)
	{
		switch (Limb)
		{
		case ERPGStrikeLimb::RightHand: return RightHand;
		case ERPGStrikeLimb::LeftHand:	return LeftHand;
		case ERPGStrikeLimb::RightFoot: return RightFoot;
		case ERPGStrikeLimb::LeftFoot:	return LeftFoot;
		default:						return INDEX_NONE;
		}
	}

	/** How far a sample is pushed back out for a given depth past the surface: none at the surface, then smoothly up to
	 *  the full depth minus the give (so the limb sinks in at most Compression, with no kink where contact starts). */
	inline float SoftPush(float Depth, float Compression)
	{
		if (Depth <= 0.f) return 0.f;
		if (Compression <= 0.f) return Depth;
		return Depth - Compression * (1.f - FMath::Exp(-Depth / Compression));
	}
}

/** One surface a limb is held behind, world space. */
USTRUCT()
struct FRPGLimbContactPlane
{
	GENERATED_BODY()

	/** On the target's surface. */
	FVector Point = FVector::ZeroVector;
	/** Out of the target, toward the attacker. */
	FVector Normal = FVector::UpVector;
};

/** The surfaces one limb is held behind this frame. */
struct FRPGLimbContactSet
{
	FRPGLimbContactPlane Planes[RPGStrikeContact::MaxPlanes];
	int32 Num = 0;
};

/** What the contact did to one limb in a pose (component space; filled by the anim instance proxy). */
struct FRPGLimbContactResult
{
	/** How far the hand / foot was moved off its animation in this pose. */
	FVector Moved = FVector::ZeroVector;
	/** The offset the surfaces ask for (kept between poses: grows at once, eases off); Moved can be less (fold limit). */
	FVector Applied = FVector::ZeroVector;
	/** What the limb could not take by bending (the elbow / knee at its fold limit, or the forearm / shin itself in the
	 *  surface): the body has to back off this much. */
	FVector Residual = FVector::ZeroVector;
	/** Elbow / knee angle (180 = straight) as animated and as corrected. */
	float AnimatedAngle = 180.f;
	float CorrectedAngle = 180.f;
};

/**
 * Keeps the player's fists and feet from passing through what they strike (the training dummy now, any IRPGMeleeTarget
 * with a skeletal mesh later), on the actual animated limbs, in both perspectives.
 *
 * Every frame, after the pose and the target's physics are final, for each limb while an attack is on: where the
 * animation alone puts the fist / foot (the pose minus the correction applied to it), and where that is heading next
 * frame. Sphere sweeps along that path against the target's bodies find contact even for a fist moving 20 cm a frame.
 * Each contact is then held on the body it touched (point and normal in that body's frame), so it moves with the target as
 * the target gives way: the strike follows through as the body yields. A contact lets go once the animation pulls the
 * limb back out in front of it, and every contact lets go when the attack ends.
 *
 * URPGAnimInstance's proxy does the rest when the next pose is evaluated: the fist / foot may sink into the surface by its
 * limb's Compression, then stops; the arm / leg is re-solved with two-bone IK to the corrected hand / foot (elbow / knee
 * kept in the animation's plane, wrist / ankle kept at the animation's angle to the forearm / shin). The correction
 * eases off over ReleaseTime instead of snapping back, so a target moving away never yanks or locks the limb.
 *
 * At very close range the limb cannot get out of the way by bending (the knee of a chambered kick is already at the
 * target): the elbow / knee bends no tighter than its fold limit, and what is left pushes the attacker back (its capsule,
 * swept, horizontally), the way a close strike shoves both fighters apart.
 *
 * Console: rpg.StrikeContact 0 turns it off (limbs pass through as before), 1 on (default); rpg.StrikeContactDebug 1 draws.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGStrikeContactComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGStrikeContactComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** World-space offset the contact applied to this limb's hand / foot in the current pose (zero when free). */
	UFUNCTION(BlueprintPure, Category = "RPG|Strike Contact")
	FVector GetCorrection(ERPGStrikeLimb Limb) const;

	/** The limb is held at a target's surface this frame. */
	UFUNCTION(BlueprintPure, Category = "RPG|Strike Contact")
	bool IsInContact(ERPGStrikeLimb Limb) const;

	/** In contact and the animation is still driving it into the target (faster than MinStrikeSpeed): the strike has
	 *  landed, even if that is earlier than its animated impact (a close target). */
	UFUNCTION(BlueprintPure, Category = "RPG|Strike Contact")
	bool HasStrikingContact(ERPGStrikeLimb Limb) const;

	/** This frame's motion of the limb's hand / foot bone as the animation alone has it (the correction taken out). */
	UFUNCTION(BlueprintPure, Category = "RPG|Strike Contact")
	FVector GetAnimatedTravel(ERPGStrikeLimb Limb) const;

	/** The surface the limb is held at (the first held contact): what it touched, which body, where. False if none. */
	bool GetContact(ERPGStrikeLimb Limb, UPrimitiveComponent*& OutTarget, FName& OutBone, FVector& OutPoint, FVector& OutNormal) const;

	/** Elbow / knee angle (degrees, 180 = straight) in the current pose: as animated, and as the contact left it. */
	void GetJointAngles(ERPGStrikeLimb Limb, float& OutAnimated, float& OutCorrected) const;

	/** Total distance (cm) the attacker has been pushed back by contacts its limbs could not absorb. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|Strike Contact|Debug")
	float PushbackDistance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Strike Contact")
	bool bEnabled = true;

	/** Targets this close (centre to centre, cm) are checked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Strike Contact", meta = (ClampMin = "50"))
	float SearchRadius = 250.f;

	/** Seconds (time constant) the correction takes to ease off once the surface stops holding the limb back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Strike Contact", meta = (ClampMin = "0.005"))
	float ReleaseTime = 0.06f;

	/** A contact lets go when the animated limb is this far back out in front of it, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Strike Contact", meta = (ClampMin = "0"))
	float ReleaseDistance = 2.f;

	/** Contacts let go this long after the attack ends (the recovery has finished). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Strike Contact", meta = (ClampMin = "0"))
	float AttackEndGrace = 0.1f;

	/** cm/s into the surface for a contact to count as the strike landing (HasStrikingContact). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Strike Contact", meta = (ClampMin = "0"))
	float MinStrikeSpeed = 150.f;

	/** Share of the limbs' unabsorbed overlap the attacker backs off each frame, and the most per frame (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Strike Contact", meta = (ClampMin = "0", ClampMax = "1"))
	float PushbackRate = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Strike Contact", meta = (ClampMin = "0"))
	float MaxPushbackPerFrame = 1.5f;

	/** The most one strike pushes the attacker back, cm (the rest the target's own give and shove take). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Strike Contact", meta = (ClampMin = "0"))
	float MaxPushbackPerStrike = 10.f;

	/** A contact lets go if its body has moved this far (cm) or turned this much (degrees) since it was made: the target
	 *  went down or spun away (a ragdoll), and holding the limb to it would drag it along. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Strike Contact", meta = (ClampMin = "0"))
	float MaxContactDrift = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Strike Contact", meta = (ClampMin = "0", ClampMax = "180"))
	float MaxContactTurn = 50.f;

	/** Draw the contact planes and the animated (red) / corrected (green) limb points (also rpg.StrikeContactDebug 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Strike Contact|Debug")
	bool bDrawDebug = false;

private:
	struct FLatch
	{
		TWeakObjectPtr<UPrimitiveComponent> Target;
		FName Bone;
		/** In the touched body's frame. */
		FVector LocalPoint = FVector::ZeroVector;
		FVector LocalNormal = FVector::UpVector;
		/** Last frame's world point, for the surface's velocity. */
		FVector LastPoint = FVector::ZeroVector;
		bool bHaveLast = false;
		/** The limb sphere that made the contact (the surface is re-taken under it each frame). */
		int32 Sample = 0;
		/** World point and normal when the contact was made. */
		FVector StartPoint = FVector::ZeroVector;
		FVector StartNormal = FVector::UpVector;
	};

	struct FLimbState
	{
		TArray<FLatch, TInlineAllocator<RPGStrikeContact::MaxPlanes>> Latches;
		/** Where the animation alone has the end point (fist / foot centre) and the end bone, this frame and last. */
		FVector Animated = FVector::ZeroVector;
		FVector LastAnimated = FVector::ZeroVector;
		FVector AnimatedBone = FVector::ZeroVector;
		FVector LastAnimatedBone = FVector::ZeroVector;
		bool bHaveLast = false;
		float ApproachSpeed = 0.f;
		FVector Correction = FVector::ZeroVector;
		FVector Residual = FVector::ZeroVector;
		float AnimatedAngle = 180.f;
		float CorrectedAngle = 180.f;
		FRPGLimbContactSet Set;
	};

	USkeletalMeshComponent* GetMesh() const;
	void GatherTargets(TArray<UPrimitiveComponent*>& Out) const;
	/** World transform of the body a contact is held on. */
	bool BodyTransform(const UPrimitiveComponent* Target, FName Bone, FTransform& Out) const;
	void UpdateLimb(int32 Index, const TArray<UPrimitiveComponent*>& Targets, float DeltaTime);
	void PushPlanes();
	void PushBack(float DeltaTime);
	bool IsActive() const;
	/** An attack is playing (or just ended, within AttackEndGrace). */
	bool IsAttacking(float DeltaTime);

	FLimbState Limbs[RPGStrikeContact::NumLimbs];
	float SinceAttack = 1000.f;
	/** Pushback so far in the current strike (StrikeId). */
	float StrikePushback = 0.f;
	int32 PushbackStrikeId = -1;
};
