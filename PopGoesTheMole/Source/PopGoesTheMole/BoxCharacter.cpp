
#include "BoxCharacter.h"

#include "GameFramework/Character.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Controller.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ArrowComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "DisplayDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Logging/LogMacros.h"
#include "Animation/AnimInstance.h"
#include "UObject/UObjectGlobals.h"

FName ABoxCharacter::MeshComponentName(TEXT("CharacterMesh0"));
FName ABoxCharacter::CharacterMovementComponentName(TEXT("CharMoveComp"));
FName ABoxCharacter::BoxComponentName(TEXT("CollisionBox"));

ABoxCharacter::ABoxCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FName ID_Characters;
		FText NAME_Characters;
		FConstructorStatics()
			: ID_Characters(TEXT("Characters"))
			, NAME_Characters(NSLOCTEXT("SpriteCategory", "Characters", "Characters"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	// Character rotation only changes in Yaw, to prevent the capsule from changing orientation.
	// Ask the Controller for the full rotation if desired (ie for aiming).
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = true;

	BoxComponent = CreateDefaultSubobject<UBoxComponent>(ABoxCharacter::BoxComponentName);
	BoxComponent->SetBoxExtent(FVector(34.0f, 34.0f, 34.0f));
	BoxComponent->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);

	BoxComponent->CanCharacterStepUpOn = ECB_No;
	BoxComponent->SetShouldUpdatePhysicsVolume(true);
	BoxComponent->SetCanEverAffectNavigation(false);
	BoxComponent->bDynamicObstacle = true;
	RootComponent = BoxComponent;

	bClientCheckEncroachmentOnNetUpdate = true;
	JumpKeyHoldTime = 0.0f;
	JumpMaxHoldTime = 0.0f;
	JumpMaxCount = 1;
	JumpCurrentCount = 0;
	bWasJumping = false;

	AnimRootMotionTranslationScale = 1.0f;

#if WITH_EDITORONLY_DATA
	ArrowComponent = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	if (ArrowComponent)
	{
		ArrowComponent->ArrowColor = FColor(150, 200, 255);
		ArrowComponent->bTreatAsASprite = true;
		ArrowComponent->SpriteInfo.Category = ConstructorStatics.ID_Characters;
		ArrowComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_Characters;
		ArrowComponent->SetupAttachment(BoxComponent);
		ArrowComponent->bIsScreenSizeScaled = true;
	}
#endif // WITH_EDITORONLY_DATA

	CharacterMovement = CreateDefaultSubobject<UCharacterMovementComponent>(ABoxCharacter::CharacterMovementComponentName);
	if (CharacterMovement)
	{
		CharacterMovement->UpdatedComponent = BoxComponent;
		CrouchedEyeHeight = CharacterMovement->CrouchedHalfHeight * 0.80f;
	}

	Mesh = CreateOptionalDefaultSubobject<USkeletalMeshComponent>(ABoxCharacter::MeshComponentName);
	if (Mesh)
	{
		Mesh->AlwaysLoadOnClient = true;
		Mesh->AlwaysLoadOnServer = true;
		Mesh->bOwnerNoSee = false;
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
		Mesh->bCastDynamicShadow = true;
		Mesh->bAffectDynamicIndirectLighting = true;
		Mesh->PrimaryComponentTick.TickGroup = TG_PrePhysics;
		Mesh->SetupAttachment(BoxComponent);
		static FName MeshCollisionProfileName(TEXT("CharacterMesh"));
		Mesh->SetCollisionProfileName(MeshCollisionProfileName);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
	}

	BaseRotationOffset = FQuat::Identity;
}

void ABoxCharacter::PostInitializeComponents()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Character_PostInitComponents);

	Super::PostInitializeComponents();

	if (!IsPendingKill())
	{
		if (Mesh)
		{
			CacheInitialMeshOffset(Mesh->GetRelativeLocation(), Mesh->GetRelativeRotation());

			// force animation tick after movement component updates
			if (Mesh->PrimaryComponentTick.bCanEverTick && CharacterMovement)
			{
				Mesh->PrimaryComponentTick.AddPrerequisite(CharacterMovement, CharacterMovement->PrimaryComponentTick);
			}
		}

		if (CharacterMovement && BoxComponent)
		{
			UCapsuleComponent * TempCapsuleComponent = NewObject<UCapsuleComponent>(this, UCapsuleComponent::StaticClass(), FName(TEXT("TempCapsule")));
			float halfSize = BoxComponent->GetScaledBoxExtent().Z / 2;
			TempCapsuleComponent->SetCapsuleSize(halfSize, halfSize);
			CharacterMovement->UpdateNavAgent(*TempCapsuleComponent);
		}

		if (Controller == nullptr && GetNetMode() != NM_Client)
		{
			if (CharacterMovement && CharacterMovement->bRunPhysicsWithNoController)
			{
				CharacterMovement->SetDefaultMovementMode();
			}
		}
	}
}

void ABoxCharacter::BeginPlay()
{
	Super::BeginPlay();
}


void ABoxCharacter::CacheInitialMeshOffset(FVector MeshRelativeLocation, FRotator MeshRelativeRotation)
{
	BaseTranslationOffset = MeshRelativeLocation;
	BaseRotationOffset = MeshRelativeRotation.Quaternion();

#if ENABLE_NAN_DIAGNOSTIC
	if (BaseRotationOffset.ContainsNaN())
	{
		logOrEnsureNanError(TEXT("ABoxCharacter::PostInitializeComponents detected NaN in BaseRotationOffset! (%s)"), *BaseRotationOffset.ToString());
	}

	const FRotator LocalRotation = Mesh->GetRelativeRotation();
	if (LocalRotation.ContainsNaN())
	{
		logOrEnsureNanError(TEXT("ABoxCharacter::PostInitializeComponents detected NaN in Mesh->RelativeRotation! (%s)"), *LocalRotation.ToString());
	}
#endif
}


UPawnMovementComponent* ABoxCharacter::GetMovementComponent() const
{
	return CharacterMovement;
}


void ABoxCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	check(PlayerInputComponent);
}


void ABoxCharacter::GetSimpleCollisionCylinder(float& CollisionRadius, float& CollisionHalfHeight) const
{
	CollisionHalfHeight = BoxComponent->GetScaledBoxExtent().Z / 2;
	CollisionRadius = CollisionHalfHeight;
}

void ABoxCharacter::UpdateNavigationRelevance()
{
	if (BoxComponent)
	{
		BoxComponent->SetCanEverAffectNavigation(bCanAffectNavigationGeneration);
	}
}

float ABoxCharacter::GetDefaultHalfHeight() const
{
	return Super::GetDefaultHalfHeight();
}


UActorComponent* ABoxCharacter::FindComponentByClass(const TSubclassOf<UActorComponent> ComponentClass) const
{
	// If the character has a Mesh, treat it as the first 'hit' when finding components
	if (Mesh && ComponentClass && Mesh->IsA(ComponentClass))
	{
		return Mesh;
	}

	return Super::FindComponentByClass(ComponentClass);
}

void ABoxCharacter::OnWalkingOffLedge_Implementation(const FVector& PreviousFloorImpactNormal, const FVector& PreviousFloorContactNormal, const FVector& PreviousLocation, float TimeDelta)
{
}

void ABoxCharacter::NotifyJumpApex()
{
	// Call delegate callback
	if (OnReachedJumpApex.IsBound())
	{
		OnReachedJumpApex.Broadcast();
	}
}

void ABoxCharacter::Landed(const FHitResult& Hit)
{
	OnLanded(Hit);

	LandedDelegate.Broadcast(Hit);
}

bool ABoxCharacter::CanJump() const
{
	return CanJumpInternal();
}

bool ABoxCharacter::CanJumpInternal_Implementation() const
{
	// Ensure the character isn't currently crouched.
	bool bCanJump = !bIsCrouched;

	// Ensure that the CharacterMovement state is valid
	bCanJump &= CharacterMovement->CanAttemptJump();

	if (bCanJump)
	{
		// Ensure JumpHoldTime and JumpCount are valid.
		if (!bWasJumping || GetJumpMaxHoldTime() <= 0.0f)
		{
			if (JumpCurrentCount == 0 && CharacterMovement->IsFalling())
			{
				bCanJump = JumpCurrentCount + 1 < JumpMaxCount;
			}
			else
			{
				bCanJump = JumpCurrentCount < JumpMaxCount;
			}
		}
		else
		{
			// Only consider JumpKeyHoldTime as long as:
			// A) The jump limit hasn't been met OR
			// B) The jump limit has been met AND we were already jumping
			const bool bJumpKeyHeld = (bPressedJump && JumpKeyHoldTime < GetJumpMaxHoldTime());
			bCanJump = bJumpKeyHeld &&
				((JumpCurrentCount < JumpMaxCount) || (bWasJumping && JumpCurrentCount == JumpMaxCount));
		}
	}

	return bCanJump;
}

void ABoxCharacter::ResetJumpState()
{
	bPressedJump = false;
	bWasJumping = false;
	JumpKeyHoldTime = 0.0f;
	JumpForceTimeRemaining = 0.0f;

	if (CharacterMovement && !CharacterMovement->IsFalling())
	{
		JumpCurrentCount = 0;
	}
}

void ABoxCharacter::OnJumped_Implementation()
{
}

bool ABoxCharacter::IsJumpProvidingForce() const
{
	if (JumpForceTimeRemaining > 0.0f)
	{
		return true;
	}
	else if (bProxyIsJumpForceApplied && (GetLocalRole() == ROLE_SimulatedProxy))
	{
		return GetWorld()->TimeSince(ProxyJumpForceStartedTime) <= GetJumpMaxHoldTime();
	}

	return false;
}

void ABoxCharacter::RecalculateBaseEyeHeight()
{
	if (!bIsCrouched)
	{
		Super::RecalculateBaseEyeHeight();
	}
	else
	{
		BaseEyeHeight = CrouchedEyeHeight;
	}
}


void ABoxCharacter::OnRep_IsCrouched()
{
	if (CharacterMovement)
	{
		if (bIsCrouched)
		{
			CharacterMovement->bWantsToCrouch = true;
			CharacterMovement->Crouch(true);
		}
		else
		{
			CharacterMovement->bWantsToCrouch = false;
			CharacterMovement->UnCrouch(true);
		}
		CharacterMovement->bNetworkUpdateReceived = true;
	}
}

void ABoxCharacter::SetReplicateMovement(bool bInReplicateMovement)
{
	Super::SetReplicateMovement(bInReplicateMovement);

	if (CharacterMovement != nullptr && GetLocalRole() == ROLE_Authority)
	{
		// Set prediction data time stamp to current time to stop extrapolating
		// from time bReplicateMovement was turned off to when it was turned on again
		FNetworkPredictionData_Server* NetworkPrediction = CharacterMovement->HasPredictionData_Server() ? CharacterMovement->GetPredictionData_Server() : nullptr;

		if (NetworkPrediction != nullptr)
		{
			NetworkPrediction->ServerTimeStamp = GetWorld()->GetTimeSeconds();
		}
	}
}

bool ABoxCharacter::CanCrouch() const
{
	return !bIsCrouched && CharacterMovement && CharacterMovement->CanEverCrouch() && GetRootComponent() && !GetRootComponent()->IsSimulatingPhysics();
}

void ABoxCharacter::Crouch(bool bClientSimulation)
{
	if (CharacterMovement)
	{
		if (CanCrouch())
		{
			CharacterMovement->bWantsToCrouch = true;
		}
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		else if (!CharacterMovement->CanEverCrouch())
		{
			//UE_LOG(LogCharacter, Log, TEXT("%s is trying to crouch, but crouching is disabled on this character! (check CharacterMovement NavAgentSettings)"), *GetName());
		}
#endif
	}
}

void ABoxCharacter::UnCrouch(bool bClientSimulation)
{
	if (CharacterMovement)
	{
		CharacterMovement->bWantsToCrouch = false;
	}
}


void ABoxCharacter::OnEndCrouch(float HeightAdjust, float ScaledHeightAdjust)
{
	RecalculateBaseEyeHeight();

	const ABoxCharacter* DefaultChar = GetDefault<ABoxCharacter>(GetClass());
	if (Mesh && DefaultChar->Mesh)
	{
		FVector& MeshRelativeLocation = Mesh->GetRelativeLocation_DirectMutable();
		MeshRelativeLocation.Z = DefaultChar->Mesh->GetRelativeLocation().Z;
		BaseTranslationOffset.Z = MeshRelativeLocation.Z;
	}
	else
	{
		BaseTranslationOffset.Z = DefaultChar->BaseTranslationOffset.Z;
	}

	K2_OnEndCrouch(HeightAdjust, ScaledHeightAdjust);
}

void ABoxCharacter::OnStartCrouch(float HeightAdjust, float ScaledHeightAdjust)
{
	RecalculateBaseEyeHeight();

	const ABoxCharacter* DefaultChar = GetDefault<ABoxCharacter>(GetClass());
	if (Mesh && DefaultChar->Mesh)
	{
		FVector& MeshRelativeLocation = Mesh->GetRelativeLocation_DirectMutable();
		MeshRelativeLocation.Z = DefaultChar->Mesh->GetRelativeLocation().Z + HeightAdjust;
		BaseTranslationOffset.Z = MeshRelativeLocation.Z;
	}
	else
	{
		BaseTranslationOffset.Z = DefaultChar->BaseTranslationOffset.Z + HeightAdjust;
	}

	K2_OnStartCrouch(HeightAdjust, ScaledHeightAdjust);
}

void ABoxCharacter::ApplyDamageMomentum(float DamageTaken, FDamageEvent const& DamageEvent, APawn* PawnInstigator, AActor* DamageCauser)
{
	UDamageType const* const DmgTypeCDO = DamageEvent.DamageTypeClass->GetDefaultObject<UDamageType>();
	float const ImpulseScale = DmgTypeCDO->DamageImpulse;

	if ((ImpulseScale > 3.f) && (CharacterMovement != nullptr))
	{
		FHitResult HitInfo;
		FVector ImpulseDir;
		DamageEvent.GetBestHitInfo(this, PawnInstigator, HitInfo, ImpulseDir);

		FVector Impulse = ImpulseDir * ImpulseScale;
		bool const bMassIndependentImpulse = !DmgTypeCDO->bScaleMomentumByMass;

		// limit Z momentum added if already going up faster than jump (to avoid blowing character way up into the sky)
		{
			FVector MassScaledImpulse = Impulse;
			if (!bMassIndependentImpulse && CharacterMovement->Mass > SMALL_NUMBER)
			{
				MassScaledImpulse = MassScaledImpulse / CharacterMovement->Mass;
			}

			if ((CharacterMovement->Velocity.Z > GetDefault<UCharacterMovementComponent>(CharacterMovement->GetClass())->JumpZVelocity) && (MassScaledImpulse.Z > 0.f))
			{
				Impulse.Z *= 0.5f;
			}
		}

		CharacterMovement->AddImpulse(Impulse, bMassIndependentImpulse);
	}
}

void ABoxCharacter::ClearCrossLevelReferences()
{
	if (BasedMovement.MovementBase != nullptr && GetOutermost() != BasedMovement.MovementBase->GetOutermost())
	{
		SetBase(nullptr);
	}

	Super::ClearCrossLevelReferences();
}

//namespace MovementBaseUtility
//{
//	bool IsDynamicBase(const UPrimitiveComponent* MovementBase)
//	{
//		return (MovementBase && MovementBase->Mobility == EComponentMobility::Movable);
//	}
//
//	bool IsSimulatedBase(const UPrimitiveComponent* MovementBase)
//	{
//		bool bBaseIsSimulatingPhysics = false;
//		const USceneComponent* AttachParent = MovementBase;
//		while (!bBaseIsSimulatingPhysics && AttachParent)
//		{
//			bBaseIsSimulatingPhysics = AttachParent->IsSimulatingPhysics();
//			AttachParent = AttachParent->GetAttachParent();
//		}
//		return bBaseIsSimulatingPhysics;
//	}
//
//	void AddTickDependency(FTickFunction& BasedObjectTick, UPrimitiveComponent* NewBase)
//	{
//		if (NewBase && MovementBaseUtility::UseRelativeLocation(NewBase))
//		{
//			if (NewBase->PrimaryComponentTick.bCanEverTick)
//			{
//				BasedObjectTick.AddPrerequisite(NewBase, NewBase->PrimaryComponentTick);
//			}
//
//			AActor* NewBaseOwner = NewBase->GetOwner();
//			if (NewBaseOwner)
//			{
//				if (NewBaseOwner->PrimaryActorTick.bCanEverTick)
//				{
//					BasedObjectTick.AddPrerequisite(NewBaseOwner, NewBaseOwner->PrimaryActorTick);
//				}
//
//				// @TODO: We need to find a more efficient way of finding all ticking components in an actor.
//				for (UActorComponent* Component : NewBaseOwner->GetComponents())
//				{
//					// Dont allow a based component (e.g. a particle system) to push us into a different tick group
//					if (Component && Component->PrimaryComponentTick.bCanEverTick && Component->PrimaryComponentTick.TickGroup <= BasedObjectTick.TickGroup)
//					{
//						BasedObjectTick.AddPrerequisite(Component, Component->PrimaryComponentTick);
//					}
//				}
//			}
//		}
//	}
//
//	void RemoveTickDependency(FTickFunction& BasedObjectTick, UPrimitiveComponent* OldBase)
//	{
//		if (OldBase && MovementBaseUtility::UseRelativeLocation(OldBase))
//		{
//			BasedObjectTick.RemovePrerequisite(OldBase, OldBase->PrimaryComponentTick);
//			AActor* OldBaseOwner = OldBase->GetOwner();
//			if (OldBaseOwner)
//			{
//				BasedObjectTick.RemovePrerequisite(OldBaseOwner, OldBaseOwner->PrimaryActorTick);
//
//				// @TODO: We need to find a more efficient way of finding all ticking components in an actor.
//				for (UActorComponent* Component : OldBaseOwner->GetComponents())
//				{
//					if (Component && Component->PrimaryComponentTick.bCanEverTick)
//					{
//						BasedObjectTick.RemovePrerequisite(Component, Component->PrimaryComponentTick);
//					}
//				}
//			}
//		}
//	}
//
//	FVector GetMovementBaseVelocity(const UPrimitiveComponent* MovementBase, const FName BoneName)
//	{
//		FVector BaseVelocity = FVector::ZeroVector;
//		if (MovementBaseUtility::IsDynamicBase(MovementBase))
//		{
//			if (BoneName != NAME_None)
//			{
//				const FBodyInstance* BodyInstance = MovementBase->GetBodyInstance(BoneName);
//				if (BodyInstance)
//				{
//					BaseVelocity = BodyInstance->GetUnrealWorldVelocity();
//					return BaseVelocity;
//				}
//			}
//
//			BaseVelocity = MovementBase->GetComponentVelocity();
//			if (BaseVelocity.IsZero())
//			{
//				// Fall back to actor's Root component
//				const AActor* Owner = MovementBase->GetOwner();
//				if (Owner)
//				{
//					// Component might be moved manually (not by simulated physics or a movement component), see if the root component of the actor has a velocity.
//					BaseVelocity = MovementBase->GetOwner()->GetVelocity();
//				}
//			}
//
//			// Fall back to physics velocity.
//			if (BaseVelocity.IsZero())
//			{
//				if (FBodyInstance* BaseBodyInstance = MovementBase->GetBodyInstance())
//				{
//					BaseVelocity = BaseBodyInstance->GetUnrealWorldVelocity();
//				}
//			}
//		}
//
//		return BaseVelocity;
//	}
//
//	FVector GetMovementBaseTangentialVelocity(const UPrimitiveComponent* MovementBase, const FName BoneName, const FVector& WorldLocation)
//	{
//		if (MovementBaseUtility::IsDynamicBase(MovementBase))
//		{
//			if (const FBodyInstance* BodyInstance = MovementBase->GetBodyInstance(BoneName))
//			{
//				const FVector BaseAngVelInRad = BodyInstance->GetUnrealWorldAngularVelocityInRadians();
//				if (!BaseAngVelInRad.IsNearlyZero())
//				{
//					FVector BaseLocation;
//					FQuat BaseRotation;
//					if (MovementBaseUtility::GetMovementBaseTransform(MovementBase, BoneName, BaseLocation, BaseRotation))
//					{
//						const FVector RadialDistanceToBase = WorldLocation - BaseLocation;
//						const FVector TangentialVel = BaseAngVelInRad ^ RadialDistanceToBase;
//						return TangentialVel;
//					}
//				}
//			}
//		}
//
//		return FVector::ZeroVector;
//	}
//
//	bool GetMovementBaseTransform(const UPrimitiveComponent* MovementBase, const FName BoneName, FVector& OutLocation, FQuat& OutQuat)
//	{
//		if (MovementBase)
//		{
//			if (BoneName != NAME_None)
//			{
//				bool bFoundBone = false;
//				if (MovementBase)
//				{
//					// Check if this socket or bone exists (DoesSocketExist checks for either, as does requesting the transform).
//					if (MovementBase->DoesSocketExist(BoneName))
//					{
//						MovementBase->GetSocketWorldLocationAndRotation(BoneName, OutLocation, OutQuat);
//						bFoundBone = true;
//					}
//					else
//					{
//						//UE_LOG(LogCharacter, Warning, TEXT("GetMovementBaseTransform(): Invalid bone or socket '%s' for PrimitiveComponent base %s"), *BoneName.ToString(), *GetPathNameSafe(MovementBase));
//					}
//				}
//
//				if (!bFoundBone)
//				{
//					OutLocation = MovementBase->GetComponentLocation();
//					OutQuat = MovementBase->GetComponentQuat();
//				}
//				return bFoundBone;
//			}
//
//			// No bone supplied
//			OutLocation = MovementBase->GetComponentLocation();
//			OutQuat = MovementBase->GetComponentQuat();
//			return true;
//		}
//
//		// nullptr MovementBase
//		OutLocation = FVector::ZeroVector;
//		OutQuat = FQuat::Identity;
//		return false;
//	}
//}


/**	Change the Pawn's base. */
void ABoxCharacter::SetBase(UPrimitiveComponent* NewBaseComponent, const FName InBoneName, bool bNotifyPawn)
{
	// If NewBaseComponent is nullptr, ignore bone name.
	const FName BoneName = (NewBaseComponent ? InBoneName : NAME_None);

	// See what changed.
	const bool bBaseChanged = (NewBaseComponent != BasedMovement.MovementBase);
	const bool bBoneChanged = (BoneName != BasedMovement.BoneName);

	if (bBaseChanged || bBoneChanged)
	{
		// Verify no recursion.
		APawn* Loop = (NewBaseComponent ? Cast<APawn>(NewBaseComponent->GetOwner()) : nullptr);
		while (Loop)
		{
			if (Loop == this)
			{
				//UE_LOG(LogCharacter, Warning, TEXT(" SetBase failed! Recursion detected. Pawn %s already based on %s."), *GetName(), *NewBaseComponent->GetName()); //-V595
				return;
			}
			if (UPrimitiveComponent* LoopBase = Loop->GetMovementBase())
			{
				Loop = Cast<APawn>(LoopBase->GetOwner());
			}
			else
			{
				break;
			}
		}

		// Set base.
		UPrimitiveComponent* OldBase = BasedMovement.MovementBase;
		BasedMovement.MovementBase = NewBaseComponent;
		BasedMovement.BoneName = BoneName;

		if (CharacterMovement)
		{
			const bool bBaseIsSimulating = MovementBaseUtility::IsSimulatedBase(NewBaseComponent);
			if (bBaseChanged)
			{
				MovementBaseUtility::RemoveTickDependency(CharacterMovement->PrimaryComponentTick, OldBase);
				// We use a special post physics function if simulating, otherwise add normal tick prereqs.
				if (!bBaseIsSimulating)
				{
					MovementBaseUtility::AddTickDependency(CharacterMovement->PrimaryComponentTick, NewBaseComponent);
				}
			}

			if (NewBaseComponent)
			{
				// Update OldBaseLocation/Rotation as those were referring to a different base
				// ... but not when handling replication for proxies (since they are going to copy this data from the replicated values anyway)
				if (!bInBaseReplication)
				{
					// Force base location and relative position to be computed since we have a new base or bone so the old relative offset is meaningless.
					CharacterMovement->SaveBaseLocation();
				}

				// Enable PostPhysics tick if we are standing on a physics object, as we need to to use post-physics transforms
				CharacterMovement->PostPhysicsTickFunction.SetTickFunctionEnable(bBaseIsSimulating);
			}
			else
			{
				BasedMovement.BoneName = NAME_None; // None, regardless of whether user tried to set a bone name, since we have no base component.
				BasedMovement.bRelativeRotation = false;
				CharacterMovement->CurrentFloor.Clear();
				CharacterMovement->PostPhysicsTickFunction.SetTickFunctionEnable(false);
			}

			const ENetRole LocalRole = GetLocalRole();
			if (LocalRole == ROLE_Authority || LocalRole == ROLE_AutonomousProxy)
			{
				BasedMovement.bServerHasBaseComponent = (BasedMovement.MovementBase != nullptr); // Also set on proxies for nicer debugging.
				//UE_LOG(LogCharacter, Verbose, TEXT("Setting base on %s for '%s' to '%s'"), LocalRole == ROLE_Authority ? TEXT("Server") : TEXT("AutoProxy"), *GetName(), *GetFullNameSafe(NewBaseComponent));
			}
			else
			{
				//UE_LOG(LogCharacter, Verbose, TEXT("Setting base on Client for '%s' to '%s'"), *GetName(), *GetFullNameSafe(NewBaseComponent));
			}

		}

		// Notify this actor of his new floor.
		if (bNotifyPawn)
		{
			BaseChange();
		}
	}
}


void ABoxCharacter::SaveRelativeBasedMovement(const FVector& NewRelativeLocation, const FRotator& NewRotation, bool bRelativeRotation)
{
	checkSlow(BasedMovement.HasRelativeLocation());
	BasedMovement.Location = NewRelativeLocation;
	BasedMovement.Rotation = NewRotation;
	BasedMovement.bRelativeRotation = bRelativeRotation;
}

FVector ABoxCharacter::GetNavAgentLocation() const
{
	FVector AgentLocation = FNavigationSystem::InvalidLocation;

	if (GetCharacterMovement() != nullptr)
	{
		AgentLocation = GetCharacterMovement()->GetActorFeetLocation();
	}

	if (FNavigationSystem::IsValidLocation(AgentLocation) == false && BoxComponent != nullptr)
	{
		int halfHeight = BoxComponent->GetScaledBoxExtent().Z / 2;
		AgentLocation = GetActorLocation() - FVector(0, 0, halfHeight);
	}

	return AgentLocation;
}

void ABoxCharacter::TurnOff()
{
	if (CharacterMovement != nullptr)
	{
		CharacterMovement->StopMovementImmediately();
		CharacterMovement->DisableMovement();
	}

	if (GetNetMode() != NM_DedicatedServer && Mesh != nullptr)
	{
		Mesh->bPauseAnims = true;
		if (Mesh->IsSimulatingPhysics())
		{
			Mesh->bBlendPhysics = true;
			Mesh->KinematicBonesUpdateType = EKinematicBonesUpdateToPhysics::SkipAllBones;
		}
	}

	Super::TurnOff();
}

void ABoxCharacter::Restart()
{
	Super::Restart();

	JumpCurrentCount = 0;

	bPressedJump = false;
	ResetJumpState();
	UnCrouch(true);

	if (CharacterMovement)
	{
		CharacterMovement->SetDefaultMovementMode();
	}
}

void ABoxCharacter::PawnClientRestart()
{
	if (CharacterMovement != nullptr)
	{
		CharacterMovement->StopMovementImmediately();
		CharacterMovement->ResetPredictionData_Client();
	}

	Super::PawnClientRestart();
}

void ABoxCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// If we are controlled remotely, set animation timing to be driven by client's network updates. So timing and events remain in sync.
	if (Mesh && IsReplicatingMovement() && (GetRemoteRole() == ROLE_AutonomousProxy && GetNetConnection() != nullptr))
	{
		Mesh->bOnlyAllowAutonomousTickPose = true;
	}
}

void ABoxCharacter::UnPossessed()
{
	Super::UnPossessed();

	if (CharacterMovement)
	{
		CharacterMovement->ResetPredictionData_Client();
		CharacterMovement->ResetPredictionData_Server();
	}

	// We're no longer controlled remotely, resume regular ticking of animations.
	if (Mesh)
	{
		Mesh->bOnlyAllowAutonomousTickPose = false;
	}
}


void ABoxCharacter::TornOff()
{
	Super::TornOff();

	if (CharacterMovement)
	{
		CharacterMovement->ResetPredictionData_Client();
		CharacterMovement->ResetPredictionData_Server();
	}

	// We're no longer controlled remotely, resume regular ticking of animations.
	if (Mesh)
	{
		Mesh->bOnlyAllowAutonomousTickPose = false;
	}
}


void ABoxCharacter::NotifyActorBeginOverlap(AActor* OtherActor)
{
	NumActorOverlapEventsCounter++;
	Super::NotifyActorBeginOverlap(OtherActor);
}

void ABoxCharacter::NotifyActorEndOverlap(AActor* OtherActor)
{
	NumActorOverlapEventsCounter++;
	Super::NotifyActorEndOverlap(OtherActor);
}

void ABoxCharacter::BaseChange()
{
	if (CharacterMovement && CharacterMovement->MovementMode != MOVE_None)
	{
		AActor* ActualMovementBase = GetMovementBaseActor(this);
		if ((ActualMovementBase != nullptr) && !ActualMovementBase->CanBeBaseForCharacter(this))
		{
			CharacterMovement->JumpOff(ActualMovementBase);
		}
	}
}

void ABoxCharacter::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	Super::DisplayDebug(Canvas, DebugDisplay, YL, YPos);

	float Indent = 0.f;

	static FName NAME_Physics = FName(TEXT("Physics"));
	if (DebugDisplay.IsDisplayOn(NAME_Physics))
	{
		FIndenter PhysicsIndent(Indent);

		FString BaseString;
		if (CharacterMovement == nullptr || BasedMovement.MovementBase == nullptr)
		{
			BaseString = "Not Based";
		}
		else
		{
			BaseString = BasedMovement.MovementBase->IsWorldGeometry() ? "World Geometry" : BasedMovement.MovementBase->GetName();
			BaseString = FString::Printf(TEXT("Based On %s"), *BaseString);
		}

		FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;
		DisplayDebugManager.DrawString(FString::Printf(TEXT("RelativeLoc: %s Rot: %s %s"), *BasedMovement.Location.ToCompactString(), *BasedMovement.Rotation.ToCompactString(), *BaseString), Indent);

		if (CharacterMovement != nullptr)
		{
			CharacterMovement->DisplayDebug(Canvas, DebugDisplay, YL, YPos);
		}
		const bool Crouched = CharacterMovement && CharacterMovement->IsCrouching();
		FString T = FString::Printf(TEXT("Crouched %i"), Crouched);
		DisplayDebugManager.DrawString(T, Indent);
	}
}

void ABoxCharacter::LaunchCharacter(FVector LaunchVelocity, bool bXYOverride, bool bZOverride)
{
	//UE_LOG(LogCharacter, Verbose, TEXT("ABoxCharacter::LaunchCharacter '%s' (%f,%f,%f)"), *GetName(), LaunchVelocity.X, LaunchVelocity.Y, LaunchVelocity.Z);

	if (CharacterMovement)
	{
		FVector FinalVel = LaunchVelocity;
		const FVector Velocity = GetVelocity();

		if (!bXYOverride)
		{
			FinalVel.X += Velocity.X;
			FinalVel.Y += Velocity.Y;
		}
		if (!bZOverride)
		{
			FinalVel.Z += Velocity.Z;
		}

		CharacterMovement->Launch(FinalVel);

		OnLaunched(LaunchVelocity, bXYOverride, bZOverride);
	}
}


void ABoxCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PrevCustomMode)
{
	if (!bPressedJump || !CharacterMovement->IsFalling())
	{
		ResetJumpState();
	}

	// Record jump force start time for proxies. Allows us to expire the jump even if not continually ticking down a timer.
	if (bProxyIsJumpForceApplied && CharacterMovement->IsFalling())
	{
		ProxyJumpForceStartedTime = GetWorld()->GetTimeSeconds();
	}

	K2_OnMovementModeChanged(PrevMovementMode, CharacterMovement->MovementMode, PrevCustomMode, CharacterMovement->CustomMovementMode);
	//MovementModeChangedDelegate.Broadcast(this, PrevMovementMode, PrevCustomMode);
}


/** Don't process landed notification if updating client position by replaying moves.
 * Allow event to be called if Pawn was initially falling (before starting to replay moves),
 * and this is going to cause him to land. . */
bool ABoxCharacter::ShouldNotifyLanded(const FHitResult& Hit)
{
	if (bClientUpdating && !bClientWasFalling)
	{
		return false;
	}

	// Just in case, only allow Landed() to be called once when replaying moves.
	bClientWasFalling = false;
	return true;
}

void ABoxCharacter::Jump()
{
	bPressedJump = true;
	JumpKeyHoldTime = 0.0f;
}

void ABoxCharacter::StopJumping()
{
	bPressedJump = false;
	ResetJumpState();
}

void ABoxCharacter::CheckJumpInput(float DeltaTime)
{
	if (CharacterMovement)
	{
		if (bPressedJump)
		{
			// If this is the first jump and we're already falling,
			// then increment the JumpCount to compensate.
			const bool bFirstJump = JumpCurrentCount == 0;
			if (bFirstJump && CharacterMovement->IsFalling())
			{
				JumpCurrentCount++;
			}

			const bool bDidJump = CanJump() && CharacterMovement->DoJump(bClientUpdating);
			if (bDidJump)
			{
				// Transition from not (actively) jumping to jumping.
				if (!bWasJumping)
				{
					JumpCurrentCount++;
					JumpForceTimeRemaining = GetJumpMaxHoldTime();
					OnJumped();
				}
			}

			bWasJumping = bDidJump;
		}
	}
}


void ABoxCharacter::ClearJumpInput(float DeltaTime)
{
	if (bPressedJump)
	{
		JumpKeyHoldTime += DeltaTime;

		// Don't disable bPressedJump right away if it's still held.
		// Don't modify JumpForceTimeRemaining because a frame of update may be remaining.
		if (JumpKeyHoldTime >= GetJumpMaxHoldTime())
		{
			bPressedJump = false;
		}
	}
	else
	{
		JumpForceTimeRemaining = 0.0f;
		bWasJumping = false;
	}
}

float ABoxCharacter::GetJumpMaxHoldTime() const
{
	return JumpMaxHoldTime;
}

//
// Static variables for networking.
//
static uint8 SavedMovementMode;

void ABoxCharacter::PreNetReceive()
{
	SavedMovementMode = ReplicatedMovementMode;
	Super::PreNetReceive();
}

void ABoxCharacter::PostNetReceive()
{
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		CharacterMovement->bNetworkMovementModeChanged |= (SavedMovementMode != ReplicatedMovementMode);
		CharacterMovement->bNetworkUpdateReceived |= CharacterMovement->bNetworkMovementModeChanged || CharacterMovement->bJustTeleported;
	}

	Super::PostNetReceive();
}

void ABoxCharacter::OnRep_ReplicatedBasedMovement()
{
	if (GetLocalRole() != ROLE_SimulatedProxy)
	{
		return;
	}

	// Skip base updates while playing root motion, it is handled inside of OnRep_RootMotion
	if (IsPlayingNetworkedRootMotionMontage())
	{
		return;
	}

	CharacterMovement->bNetworkUpdateReceived = true;
	TGuardValue<bool> bInBaseReplicationGuard(bInBaseReplication, true);

	const bool bBaseChanged = (BasedMovement.MovementBase != ReplicatedBasedMovement.MovementBase || BasedMovement.BoneName != ReplicatedBasedMovement.BoneName);
	if (bBaseChanged)
	{
		// Even though we will copy the replicated based movement info, we need to use SetBase() to set up tick dependencies and trigger notifications.
		SetBase(ReplicatedBasedMovement.MovementBase, ReplicatedBasedMovement.BoneName);
	}

	// Make sure to use the values of relative location/rotation etc from the server.
	BasedMovement = ReplicatedBasedMovement;

	if (ReplicatedBasedMovement.HasRelativeLocation())
	{
		// Update transform relative to movement base
		const FVector OldLocation = GetActorLocation();
		const FQuat OldRotation = GetActorQuat();
		MovementBaseUtility::GetMovementBaseTransform(ReplicatedBasedMovement.MovementBase, ReplicatedBasedMovement.BoneName, CharacterMovement->OldBaseLocation, CharacterMovement->OldBaseQuat);
		const FVector NewLocation = CharacterMovement->OldBaseLocation + ReplicatedBasedMovement.Location;
		FRotator NewRotation;

		if (ReplicatedBasedMovement.HasRelativeRotation())
		{
			// Relative location, relative rotation
			NewRotation = (FRotationMatrix(ReplicatedBasedMovement.Rotation) * FQuatRotationMatrix(CharacterMovement->OldBaseQuat)).Rotator();

			if (CharacterMovement->ShouldRemainVertical())
			{
				NewRotation.Pitch = 0.f;
				NewRotation.Roll = 0.f;
			}
		}
		else
		{
			// Relative location, absolute rotation
			NewRotation = ReplicatedBasedMovement.Rotation;
		}

		// When position or base changes, movement mode will need to be updated. This assumes rotation changes don't affect that.
		CharacterMovement->bJustTeleported |= (bBaseChanged || NewLocation != OldLocation);
		CharacterMovement->bNetworkSmoothingComplete = false;
		CharacterMovement->SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation.Quaternion());
		OnUpdateSimulatedPosition(OldLocation, OldRotation);
	}
}

void ABoxCharacter::OnRep_ReplicatedMovement()
{
	if (CharacterMovement && (CharacterMovement->NetworkSmoothingMode == ENetworkSmoothingMode::Replay))
	{
		return;
	}

	// Skip standard position correction if we are playing root motion, OnRep_RootMotion will handle it.
	if (!IsPlayingNetworkedRootMotionMontage()) // animation root motion
	{
		if (!CharacterMovement || !CharacterMovement->CurrentRootMotion.HasActiveRootMotionSources()) // root motion sources
		{
			Super::OnRep_ReplicatedMovement();
		}
	}
}

void ABoxCharacter::OnRep_ReplayLastTransformUpdateTimeStamp()
{
	ReplicatedServerLastTransformUpdateTimeStamp = ReplayLastTransformUpdateTimeStamp;
}

/** Get FAnimMontageInstance playing RootMotion */
FAnimMontageInstance * ABoxCharacter::GetRootMotionAnimMontageInstance() const
{
	return (Mesh && Mesh->GetAnimInstance()) ? Mesh->GetAnimInstance()->GetRootMotionMontageInstance() : nullptr;
}

void ABoxCharacter::OnRep_RootMotion()
{
	if (CharacterMovement && (CharacterMovement->NetworkSmoothingMode == ENetworkSmoothingMode::Replay))
	{
		return;
	}

	if (GetLocalRole() == ROLE_SimulatedProxy)
	{

		UE_LOG(LogRootMotion, Log, TEXT("ABoxCharacter::OnRep_RootMotion"));

		// Save received move in queue, we'll try to use it during Tick().
		if (RepRootMotion.bIsActive)
		{
			// Add new move
			RootMotionRepMoves.AddZeroed(1);
			FSimulatedRootMotionReplicatedMove& NewMove = RootMotionRepMoves.Last();
			NewMove.RootMotion = RepRootMotion;
			NewMove.Time = GetWorld()->GetTimeSeconds();
		}
		else
		{
			// Clear saved moves.
			RootMotionRepMoves.Empty();
		}

		if (CharacterMovement)
		{
			CharacterMovement->bNetworkUpdateReceived = true;
		}
	}
}

void ABoxCharacter::SimulatedRootMotionPositionFixup(float DeltaSeconds)
{
	const FAnimMontageInstance* ClientMontageInstance = GetRootMotionAnimMontageInstance();
	if (ClientMontageInstance && CharacterMovement && Mesh)
	{
		// Find most recent buffered move that we can use.
		const int32 MoveIndex = FindRootMotionRepMove(*ClientMontageInstance);
		if (MoveIndex != INDEX_NONE)
		{
			const FVector OldLocation = GetActorLocation();
			const FQuat OldRotation = GetActorQuat();
			// Move Actor back to position of that buffered move. (server replicated position).
			const FSimulatedRootMotionReplicatedMove& RootMotionRepMove = RootMotionRepMoves[MoveIndex];
			if (RestoreReplicatedMove(RootMotionRepMove))
			{
				const float ServerPosition = RootMotionRepMove.RootMotion.Position;
				const float ClientPosition = ClientMontageInstance->GetPosition();
				const float DeltaPosition = (ClientPosition - ServerPosition);
				if (FMath::Abs(DeltaPosition) > KINDA_SMALL_NUMBER)
				{
					// Find Root Motion delta move to get back to where we were on the client.
					const FTransform LocalRootMotionTransform = ClientMontageInstance->Montage->ExtractRootMotionFromTrackRange(ServerPosition, ClientPosition);

					// Simulate Root Motion for delta move.
					if (CharacterMovement)
					{
						const float MontagePlayRate = ClientMontageInstance->GetPlayRate();
						// Guess time it takes for this delta track position, so we can get falling physics accurate.
						if (!FMath::IsNearlyZero(MontagePlayRate))
						{
							const float DeltaTime = DeltaPosition / MontagePlayRate;

							// Even with negative playrate deltatime should be positive.
							check(DeltaTime > 0.f);
							CharacterMovement->SimulateRootMotion(DeltaTime, LocalRootMotionTransform);

							// After movement correction, smooth out error in position if any.
							const FVector NewLocation = GetActorLocation();
							CharacterMovement->bNetworkSmoothingComplete = false;
							CharacterMovement->bJustTeleported |= (OldLocation != NewLocation);
							CharacterMovement->SmoothCorrection(OldLocation, OldRotation, NewLocation, GetActorQuat());
						}
					}
				}
			}

			// Delete this move and any prior one, we don't need them anymore.
			UE_LOG(LogRootMotion, Log, TEXT("\tClearing old moves (%d)"), MoveIndex + 1);
			RootMotionRepMoves.RemoveAt(0, MoveIndex + 1);
		}
	}
}

int32 ABoxCharacter::FindRootMotionRepMove(const FAnimMontageInstance& ClientMontageInstance) const
{
	int32 FoundIndex = INDEX_NONE;

	// Start with most recent move and go back in time to find a usable move.
	for (int32 MoveIndex = RootMotionRepMoves.Num() - 1; MoveIndex >= 0; MoveIndex--)
	{
		if (CanUseRootMotionRepMove(RootMotionRepMoves[MoveIndex], ClientMontageInstance))
		{
			FoundIndex = MoveIndex;
			break;
		}
	}

	UE_LOG(LogRootMotion, Log, TEXT("\tABoxCharacter::FindRootMotionRepMove FoundIndex: %d, NumSavedMoves: %d"), FoundIndex, RootMotionRepMoves.Num());
	return FoundIndex;
}

bool ABoxCharacter::CanUseRootMotionRepMove(const FSimulatedRootMotionReplicatedMove& RootMotionRepMove, const FAnimMontageInstance& ClientMontageInstance) const
{
	// Ignore outdated moves.
	if (GetWorld()->TimeSince(RootMotionRepMove.Time) <= 0.5f)
	{
		// Make sure montage being played matched between client and server.
		if (RootMotionRepMove.RootMotion.AnimMontage && (RootMotionRepMove.RootMotion.AnimMontage == ClientMontageInstance.Montage))
		{
			UAnimMontage * AnimMontage = ClientMontageInstance.Montage;
			const float ServerPosition = RootMotionRepMove.RootMotion.Position;
			const float ClientPosition = ClientMontageInstance.GetPosition();
			const float DeltaPosition = (ClientPosition - ServerPosition);
			const int32 CurrentSectionIndex = AnimMontage->GetSectionIndexFromPosition(ClientPosition);
			if (CurrentSectionIndex != INDEX_NONE)
			{
				const int32 NextSectionIndex = ClientMontageInstance.GetNextSectionID(CurrentSectionIndex);

				// We can only extract root motion if we are within the same section.
				// It's not trivial to jump through sections in a deterministic manner, but that is luckily not frequent. 
				const bool bSameSections = (AnimMontage->GetSectionIndexFromPosition(ServerPosition) == CurrentSectionIndex);
				// if we are looping and just wrapped over, skip. That's also not easy to handle and not frequent.
				const bool bHasLooped = (NextSectionIndex == CurrentSectionIndex) && (FMath::Abs(DeltaPosition) > (AnimMontage->GetSectionLength(CurrentSectionIndex) / 2.f));
				// Can only simulate forward in time, so we need to make sure server move is not ahead of the client.
				const bool bServerAheadOfClient = ((DeltaPosition * ClientMontageInstance.GetPlayRate()) < 0.f);

				UE_LOG(LogRootMotion, Log, TEXT("\t\tABoxCharacter::CanUseRootMotionRepMove ServerPosition: %.3f, ClientPosition: %.3f, DeltaPosition: %.3f, bSameSections: %d, bHasLooped: %d, bServerAheadOfClient: %d"),
					ServerPosition, ClientPosition, DeltaPosition, bSameSections, bHasLooped, bServerAheadOfClient);

				return bSameSections && !bHasLooped && !bServerAheadOfClient;
			}
		}
	}
	return false;
}

bool ABoxCharacter::RestoreReplicatedMove(const FSimulatedRootMotionReplicatedMove& RootMotionRepMove)
{
	UPrimitiveComponent* ServerBase = RootMotionRepMove.RootMotion.MovementBase;
	const FName ServerBaseBoneName = RootMotionRepMove.RootMotion.MovementBaseBoneName;

	// Relative Position
	if (RootMotionRepMove.RootMotion.bRelativePosition)
	{
		bool bSuccess = false;
		if (MovementBaseUtility::UseRelativeLocation(ServerBase))
		{
			FVector BaseLocation;
			FQuat BaseRotation;
			MovementBaseUtility::GetMovementBaseTransform(ServerBase, ServerBaseBoneName, BaseLocation, BaseRotation);

			const FVector ServerLocation = BaseLocation + RootMotionRepMove.RootMotion.Location;
			FRotator ServerRotation;
			if (RootMotionRepMove.RootMotion.bRelativeRotation)
			{
				// Relative rotation
				ServerRotation = (FRotationMatrix(RootMotionRepMove.RootMotion.Rotation) * FQuatRotationTranslationMatrix(BaseRotation, FVector::ZeroVector)).Rotator();
			}
			else
			{
				// Absolute rotation
				ServerRotation = RootMotionRepMove.RootMotion.Rotation;
			}

			SetActorLocationAndRotation(ServerLocation, ServerRotation);
			bSuccess = true;
		}
		// If we received local space position, but can't resolve parent, then move can't be used. :(
		if (!bSuccess)
		{
			return false;
		}
	}
	// Absolute position
	else
	{
		FVector LocalLocation = FRepMovement::RebaseOntoLocalOrigin(RootMotionRepMove.RootMotion.Location, this);
		SetActorLocationAndRotation(LocalLocation, RootMotionRepMove.RootMotion.Rotation);
	}

	CharacterMovement->bJustTeleported = true;
	SetBase(ServerBase, ServerBaseBoneName);

	return true;
}

void ABoxCharacter::OnUpdateSimulatedPosition(const FVector& OldLocation, const FQuat& OldRotation)
{
	//SCOPE_CYCLE_COUNTER(STAT_CharacterOnNetUpdateSimulatedPosition);

	bSimGravityDisabled = false;
	const bool bLocationChanged = (OldLocation != GetActorLocation());
	if (bClientCheckEncroachmentOnNetUpdate)
	{
		// Only need to check for encroachment when teleported without any velocity.
		// Normal movement pops the character out of geometry anyway, no use doing it before and after (with different rules).
		// Always consider Location as changed if we were spawned this tick as in that case our replicated Location was set as part of spawning, before PreNetReceive()
		if (CharacterMovement->Velocity.IsZero() && (bLocationChanged || CreationTime == GetWorld()->TimeSeconds))
		{
			if (GetWorld()->EncroachingBlockingGeometry(this, GetActorLocation(), GetActorRotation()))
			{
				bSimGravityDisabled = true;
			}
		}
	}
	CharacterMovement->bJustTeleported |= bLocationChanged;
	CharacterMovement->bNetworkUpdateReceived = true;
}

void ABoxCharacter::PostNetReceiveLocationAndRotation()
{
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		// Don't change transform if using relative position (it should be nearly the same anyway, or base may be slightly out of sync)
		if (!ReplicatedBasedMovement.HasRelativeLocation())
		{
			const FRepMovement& ConstRepMovement = GetReplicatedMovement();
			const FVector OldLocation = GetActorLocation();
			const FVector NewLocation = FRepMovement::RebaseOntoLocalOrigin(ConstRepMovement.Location, this);
			const FQuat OldRotation = GetActorQuat();

			CharacterMovement->bNetworkSmoothingComplete = false;
			CharacterMovement->bJustTeleported |= (OldLocation != NewLocation);
			CharacterMovement->SmoothCorrection(OldLocation, OldRotation, NewLocation, ConstRepMovement.Rotation.Quaternion());
			OnUpdateSimulatedPosition(OldLocation, OldRotation);
		}
		CharacterMovement->bNetworkUpdateReceived = true;
	}
}

void ABoxCharacter::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	if (CharacterMovement->CurrentRootMotion.HasActiveRootMotionSources() || IsPlayingNetworkedRootMotionMontage())
	{
		const FAnimMontageInstance* RootMotionMontageInstance = GetRootMotionAnimMontageInstance();

		RepRootMotion.bIsActive = true;
		// Is position stored in local space?
		RepRootMotion.bRelativePosition = BasedMovement.HasRelativeLocation();
		RepRootMotion.bRelativeRotation = BasedMovement.HasRelativeRotation();
		RepRootMotion.Location = RepRootMotion.bRelativePosition ? BasedMovement.Location : FRepMovement::RebaseOntoZeroOrigin(GetActorLocation(), GetWorld()->OriginLocation);
		RepRootMotion.Rotation = RepRootMotion.bRelativeRotation ? BasedMovement.Rotation : GetActorRotation();
		RepRootMotion.MovementBase = BasedMovement.MovementBase;
		RepRootMotion.MovementBaseBoneName = BasedMovement.BoneName;
		if (RootMotionMontageInstance)
		{
			RepRootMotion.AnimMontage = RootMotionMontageInstance->Montage;
			RepRootMotion.Position = RootMotionMontageInstance->GetPosition();
		}
		else
		{
			RepRootMotion.AnimMontage = nullptr;
		}

		RepRootMotion.AuthoritativeRootMotion = CharacterMovement->CurrentRootMotion;
		RepRootMotion.Acceleration = CharacterMovement->GetCurrentAcceleration();
		RepRootMotion.LinearVelocity = CharacterMovement->Velocity;

		DOREPLIFETIME_ACTIVE_OVERRIDE(ABoxCharacter, RepRootMotion, true);
	}
	else
	{
		RepRootMotion.Clear();

		DOREPLIFETIME_ACTIVE_OVERRIDE(ABoxCharacter, RepRootMotion, false);
	}

	bProxyIsJumpForceApplied = (JumpForceTimeRemaining > 0.0f);
	ReplicatedMovementMode = CharacterMovement->PackNetworkMovementMode();
	ReplicatedBasedMovement = BasedMovement;

	// Optimization: only update and replicate these values if they are actually going to be used.
	if (BasedMovement.HasRelativeLocation())
	{
		// When velocity becomes zero, force replication so the position is updated to match the server (it may have moved due to simulation on the client).
		ReplicatedBasedMovement.bServerHasVelocity = !CharacterMovement->Velocity.IsZero();

		// Make sure absolute rotations are updated in case rotation occurred after the base info was saved.
		if (!BasedMovement.HasRelativeRotation())
		{
			ReplicatedBasedMovement.Rotation = GetActorRotation();
		}
	}

	// Save bandwidth by not replicating this value unless it is necessary, since it changes every update.
	if ((CharacterMovement->NetworkSmoothingMode == ENetworkSmoothingMode::Linear) || CharacterMovement->bNetworkAlwaysReplicateTransformUpdateTimestamp)
	{
		ReplicatedServerLastTransformUpdateTimeStamp = CharacterMovement->GetServerLastTransformUpdateTimeStamp();
	}
	else
	{
		ReplicatedServerLastTransformUpdateTimeStamp = 0.f;
	}
}

void ABoxCharacter::PreReplicationForReplay(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplicationForReplay(ChangedPropertyTracker);

	const UWorld* World = GetWorld();
	if (World)
	{
		// On client replays, our view pitch will be set to 0 as by default we do not replicate
		// pitch for owners, just for simulated. So instead push our rotation into the sampler
		if (World->IsRecordingClientReplay() && Controller != nullptr && GetLocalRole() == ROLE_AutonomousProxy && GetNetMode() == NM_Client)
		{
			SetRemoteViewPitch(Controller->GetControlRotation().Pitch);
		}

		ReplayLastTransformUpdateTimeStamp = World->GetTimeSeconds();
	}
}

void ABoxCharacter::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DISABLE_REPLICATED_PROPERTY(ABoxCharacter, JumpMaxHoldTime);
	DISABLE_REPLICATED_PROPERTY(ABoxCharacter, JumpMaxCount);

	DOREPLIFETIME_CONDITION(ABoxCharacter, RepRootMotion, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(ABoxCharacter, ReplicatedBasedMovement, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(ABoxCharacter, ReplicatedServerLastTransformUpdateTimeStamp, COND_SimulatedOnlyNoReplay);
	DOREPLIFETIME_CONDITION(ABoxCharacter, ReplicatedMovementMode, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(ABoxCharacter, bIsCrouched, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(ABoxCharacter, bProxyIsJumpForceApplied, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(ABoxCharacter, AnimRootMotionTranslationScale, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(ABoxCharacter, ReplayLastTransformUpdateTimeStamp, COND_ReplayOnly);
}

bool ABoxCharacter::IsPlayingRootMotion() const
{
	if (Mesh)
	{
		return Mesh->IsPlayingRootMotion();
	}
	return false;
}

bool ABoxCharacter::HasAnyRootMotion() const
{
	return CharacterMovement ? CharacterMovement->HasRootMotionSources() : false;
}

bool ABoxCharacter::IsPlayingNetworkedRootMotionMontage() const
{
	if (Mesh)
	{
		return Mesh->IsPlayingNetworkedRootMotionMontage();
	}
	return false;
}

void ABoxCharacter::SetAnimRootMotionTranslationScale(float InAnimRootMotionTranslationScale)
{
	AnimRootMotionTranslationScale = InAnimRootMotionTranslationScale;
}

float ABoxCharacter::GetAnimRootMotionTranslationScale() const
{
	return AnimRootMotionTranslationScale;
}

float ABoxCharacter::PlayAnimMontage(class UAnimMontage* AnimMontage, float InPlayRate, FName StartSectionName)
{
	UAnimInstance * AnimInstance = (Mesh) ? Mesh->GetAnimInstance() : nullptr;
	if (AnimMontage && AnimInstance)
	{
		float const Duration = AnimInstance->Montage_Play(AnimMontage, InPlayRate);

		if (Duration > 0.f)
		{
			// Start at a given Section.
			if (StartSectionName != NAME_None)
			{
				AnimInstance->Montage_JumpToSection(StartSectionName, AnimMontage);
			}

			return Duration;
		}
	}

	return 0.f;
}

void ABoxCharacter::StopAnimMontage(class UAnimMontage* AnimMontage)
{
	UAnimInstance * AnimInstance = (Mesh) ? Mesh->GetAnimInstance() : nullptr;
	UAnimMontage * MontageToStop = (AnimMontage) ? AnimMontage : GetCurrentMontage();
	bool bShouldStopMontage = AnimInstance && MontageToStop && !AnimInstance->Montage_GetIsStopped(MontageToStop);

	if (bShouldStopMontage)
	{
		AnimInstance->Montage_Stop(MontageToStop->BlendOut.GetBlendTime(), MontageToStop);
	}
}

class UAnimMontage * ABoxCharacter::GetCurrentMontage()
{
	UAnimInstance * AnimInstance = (Mesh) ? Mesh->GetAnimInstance() : nullptr;
	if (AnimInstance)
	{
		return AnimInstance->GetCurrentActiveMontage();
	}

	return nullptr;
}

void ABoxCharacter::ClientCheatWalk_Implementation()
{
#if !UE_BUILD_SHIPPING
	SetActorEnableCollision(true);
	if (CharacterMovement)
	{
		CharacterMovement->bCheatFlying = false;
		CharacterMovement->SetMovementMode(MOVE_Falling);
	}
#endif
}

void ABoxCharacter::ClientCheatFly_Implementation()
{
#if !UE_BUILD_SHIPPING
	SetActorEnableCollision(true);
	if (CharacterMovement)
	{
		CharacterMovement->bCheatFlying = true;
		CharacterMovement->SetMovementMode(MOVE_Flying);
	}
#endif
}

void ABoxCharacter::ClientCheatGhost_Implementation()
{
#if !UE_BUILD_SHIPPING
	SetActorEnableCollision(false);
	if (CharacterMovement)
	{
		CharacterMovement->bCheatFlying = true;
		CharacterMovement->SetMovementMode(MOVE_Flying);
	}
#endif
}

void ABoxCharacter::RootMotionDebugClientPrintOnScreen_Implementation(const FString& InString)
{
#if ROOT_MOTION_DEBUG
	RootMotionSourceDebug::PrintOnScreenServerMsg(InString);
#endif
}


// ServerMove
void ABoxCharacter::ServerMove_Implementation(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	GetCharacterMovement()->ServerMove_Implementation(TimeStamp, InAccel, ClientLoc, CompressedMoveFlags, ClientRoll, View, ClientMovementBase, ClientBaseBoneName, ClientMovementMode);
}

bool ABoxCharacter::ServerMove_Validate(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	return GetCharacterMovement()->ServerMove_Validate(TimeStamp, InAccel, ClientLoc, CompressedMoveFlags, ClientRoll, View, ClientMovementBase, ClientBaseBoneName, ClientMovementMode);
}

// ServerMoveNoBase
void ABoxCharacter::ServerMoveNoBase_Implementation(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, uint8 ClientMovementMode)
{
	GetCharacterMovement()->ServerMove_Implementation(TimeStamp, InAccel, ClientLoc, CompressedMoveFlags, ClientRoll, View, /*ClientMovementBase=*/ nullptr, /*ClientBaseBoneName=*/ NAME_None, ClientMovementMode);
}

bool ABoxCharacter::ServerMoveNoBase_Validate(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, uint8 ClientMovementMode)
{
	return GetCharacterMovement()->ServerMove_Validate(TimeStamp, InAccel, ClientLoc, CompressedMoveFlags, ClientRoll, View, /*ClientMovementBase=*/ nullptr, /*ClientBaseBoneName=*/ NAME_None, ClientMovementMode);
}

// ServerMoveDual
void ABoxCharacter::ServerMoveDual_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	GetCharacterMovement()->ServerMoveDual_Implementation(TimeStamp0, InAccel0, PendingFlags, View0, TimeStamp, InAccel, ClientLoc, NewFlags, ClientRoll, View, ClientMovementBase, ClientBaseBoneName, ClientMovementMode);
}

bool ABoxCharacter::ServerMoveDual_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	return GetCharacterMovement()->ServerMoveDual_Validate(TimeStamp0, InAccel0, PendingFlags, View0, TimeStamp, InAccel, ClientLoc, NewFlags, ClientRoll, View, ClientMovementBase, ClientBaseBoneName, ClientMovementMode);
}

// ServerMoveDualNoBase
void ABoxCharacter::ServerMoveDualNoBase_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, uint8 ClientMovementMode)
{
	GetCharacterMovement()->ServerMoveDual_Implementation(TimeStamp0, InAccel0, PendingFlags, View0, TimeStamp, InAccel, ClientLoc, NewFlags, ClientRoll, View, /*ClientMovementBase=*/ nullptr, /*ClientBaseBoneName=*/ NAME_None, ClientMovementMode);
}

bool ABoxCharacter::ServerMoveDualNoBase_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, uint8 ClientMovementMode)
{
	return GetCharacterMovement()->ServerMoveDual_Validate(TimeStamp0, InAccel0, PendingFlags, View0, TimeStamp, InAccel, ClientLoc, NewFlags, ClientRoll, View, /*ClientMovementBase=*/ nullptr, /*ClientBaseBoneName=*/ NAME_None, ClientMovementMode);
}

// ServerMoveDualHybridRootMotion
void ABoxCharacter::ServerMoveDualHybridRootMotion_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	GetCharacterMovement()->ServerMoveDualHybridRootMotion_Implementation(TimeStamp0, InAccel0, PendingFlags, View0, TimeStamp, InAccel, ClientLoc, NewFlags, ClientRoll, View, ClientMovementBase, ClientBaseBoneName, ClientMovementMode);
}

bool ABoxCharacter::ServerMoveDualHybridRootMotion_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	return GetCharacterMovement()->ServerMoveDualHybridRootMotion_Validate(TimeStamp0, InAccel0, PendingFlags, View0, TimeStamp, InAccel, ClientLoc, NewFlags, ClientRoll, View, ClientMovementBase, ClientBaseBoneName, ClientMovementMode);
}

// ServerMoveOld
void ABoxCharacter::ServerMoveOld_Implementation(float OldTimeStamp, FVector_NetQuantize10 OldAccel, uint8 OldMoveFlags)
{
	GetCharacterMovement()->ServerMoveOld_Implementation(OldTimeStamp, OldAccel, OldMoveFlags);
}

bool ABoxCharacter::ServerMoveOld_Validate(float OldTimeStamp, FVector_NetQuantize10 OldAccel, uint8 OldMoveFlags)
{
	return GetCharacterMovement()->ServerMoveOld_Validate(OldTimeStamp, OldAccel, OldMoveFlags);
}

// ClientAckGoodMove
void ABoxCharacter::ClientAckGoodMove_Implementation(float TimeStamp)
{
	GetCharacterMovement()->ClientAckGoodMove_Implementation(TimeStamp);
}

// ClientAdjustPosition
void ABoxCharacter::ClientAdjustPosition_Implementation(float TimeStamp, FVector NewLoc, FVector NewVel, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode)
{
	GetCharacterMovement()->ClientAdjustPosition_Implementation(TimeStamp, NewLoc, NewVel, NewBase, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);
}

// ClientVeryShortAdjustPosition
void ABoxCharacter::ClientVeryShortAdjustPosition_Implementation(float TimeStamp, FVector NewLoc, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode)
{
	GetCharacterMovement()->ClientVeryShortAdjustPosition_Implementation(TimeStamp, NewLoc, NewBase, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);
}

// ClientAdjustRootMotionPosition
void ABoxCharacter::ClientAdjustRootMotionPosition_Implementation(float TimeStamp, float ServerMontageTrackPosition, FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase, FName ServerBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode)
{
	GetCharacterMovement()->ClientAdjustRootMotionPosition_Implementation(TimeStamp, ServerMontageTrackPosition, ServerLoc, ServerRotation, ServerVelZ, ServerBase, ServerBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);
}

// ClientAdjustRootMotionSourcePosition
void ABoxCharacter::ClientAdjustRootMotionSourcePosition_Implementation(float TimeStamp, FRootMotionSourceGroup ServerRootMotion, bool bHasAnimRootMotion, float ServerMontageTrackPosition, FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase, FName ServerBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode)
{
	GetCharacterMovement()->ClientAdjustRootMotionSourcePosition_Implementation(TimeStamp, ServerRootMotion, bHasAnimRootMotion, ServerMontageTrackPosition, ServerLoc, ServerRotation, ServerVelZ, ServerBase, ServerBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);
}

