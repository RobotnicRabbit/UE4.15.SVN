// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Components/SceneComponent.h"
#include "EngineDefines.h"
#include "CollisionQueryParams.h"
#include "SkeletalMeshTypes.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimCurveTypes.h"
#include "Components/SkinnedMeshComponent.h"
#include "ClothSimData.h"
#include "SingleAnimationPlayData.h"
#include "Animation/PoseSnapshot.h"
#include "SkeletalMeshComponent.generated.h"

class Error;
class FPhysScene;
class FPrimitiveDrawInterface;
class UAnimInstance;
class UPhysicalMaterial;
class UPhysicsAsset;
class USkeletalMeshComponent;
struct FConstraintInstance;
struct FNavigableGeometryExport;

DECLARE_MULTICAST_DELEGATE(FOnSkelMeshPhysicsCreatedMultiCast);
typedef FOnSkelMeshPhysicsCreatedMultiCast::FDelegate FOnSkelMeshPhysicsCreated;

namespace nvidia
{ 
	namespace apex 
	{
		class ClothingAsset;
		class ClothingActor;
		class ClothingCollision;
	}
}

class FPhysScene;

/** a class to manage an APEX clothing actor */
class FClothingActor
{
public:
	enum TeleportMode
	{
		/** Simulation continues smoothly. This is the most commonly used mode */
		Continuous,
		/**
		 * Transforms the current simulation state from the old global pose to the new global pose.
		 * This will transform positions and velocities and thus keep the simulation state, just translate it to a new pose.
		 */
		Teleport,

		/**
		 * Forces the cloth to the animated position in the next frame.
		 * This can be used to reset it from a bad state or by a teleport where the old state is not important anymore.
		 */
		TeleportAndReset,
	};

	void Clear(bool bReleaseResource = false);

	/** 
	 * to check whether this actor is valid or not 
	 * because clothing asset can be changed by editing 
	 */
	nvidia::apex::ClothingAsset*	ParentClothingAsset;
	/** APEX clothing actor is created from APEX clothing asset for cloth simulation */
	nvidia::apex::ClothingActor*		ApexClothingActor;

	/** The corresponding clothing asset index */
	int32 ParentClothingAssetIndex;

	/** Whether this cloth actor is simulating for the current LOD */
	bool bSimulateForCurrentLOD;
};

//The data that cloth needs for simulation prep in parallel. These properties are accessible via double buffer
struct FClothSimulationContext
{
	FClothSimulationContext();
private:
	/** whether we need to teleport cloth. There are functions which allow you to modify this accordingly. Do not access directly as double buffer strategy relies on internal logic being consistent */
	FClothingActor::TeleportMode ClothTeleportMode;

	TArray<FTransform> BoneTransforms;
	TArray<FClothingActor> ClothingActors;
	TArray<FClothingAssetData> ClothingAssets;	//This is only here because we don't have proper cloth assets and instead embed the data into an array in SkeletalMesh. For now we must copy the data
	TArray<int32> InMasterBoneMap;
	int32 InMasterBoneMapCacheCount;
	bool bUseMasterPose;
	FTransform ComponentToWorld;

	FVector WindDirection;
	float WindAdaption;

	/** Whether we have deferred cloth update transform. If true we will update any cloth collision when appropriate */
	bool bPendingClothUpdateTransform;
	ETeleportType PendingTeleportType;

	friend class USkeletalMeshComponent;
};

struct FAnimationEvaluationContext
{
	// The anim instance we are evaluating
	UAnimInstance* AnimInstance;

	// The SkeletalMesh we are evaluating for
	USkeletalMesh* SkeletalMesh;

	// Double buffer evaluation data
	TArray<FTransform> ComponentSpaceTransforms;
	TArray<FTransform> BoneSpaceTransforms;
	FVector RootBoneTranslation;

	// Double buffer curve data
	FBlendedHeapCurve	Curve;

	// Are we performing interpolation this tick
	bool bDoInterpolation;

	// Are we evaluating this tick
	bool bDoEvaluation;

	// Are we updating the anim instance this tick
	bool bDoUpdate;

	// Are we storing data in cache bones this tick
	bool bDuplicateToCacheBones;

	// duplicate the cache curves
	bool bDuplicateToCacheCurve;

	FAnimationEvaluationContext()
	{
		Clear();
	}

	void Copy(const FAnimationEvaluationContext& Other)
	{
		AnimInstance = Other.AnimInstance;
		SkeletalMesh = Other.SkeletalMesh;
		ComponentSpaceTransforms.Reset();
		ComponentSpaceTransforms.Append(Other.ComponentSpaceTransforms);
		BoneSpaceTransforms.Reset();
		BoneSpaceTransforms.Append(Other.BoneSpaceTransforms);
		RootBoneTranslation = Other.RootBoneTranslation;
		Curve.CopyFrom(Other.Curve);
		bDoInterpolation = Other.bDoInterpolation;
		bDoEvaluation = Other.bDoEvaluation;
		bDoUpdate = Other.bDoUpdate;
		bDuplicateToCacheBones = Other.bDuplicateToCacheBones;
		bDuplicateToCacheCurve = Other.bDuplicateToCacheCurve;
	}

	void Clear()
	{
		AnimInstance = NULL;
		SkeletalMesh = NULL;
	}

};

/**  for storing precomputed cloth morph target data */
struct FClothMorphTargetData
{
	FName MorphTargetName;
	// save a previous weight to compare whether weight was changed or not
	float PrevWeight;
	// an index of clothing assets which this morph target data is used in
	int32 ClothAssetIndex;
	// original positions which only this cloth section is including / extracted from a morph target
	TArray<FVector> OriginPos;
	// delta positions to morph this cloth section
	TArray<FVector> PosDelta;
};

#if WITH_CLOTH_COLLISION_DETECTION

class FClothCollisionPrimitive
{
public:
	enum ClothCollisionPrimType
	{
		SPHERE,
		CAPSULE,
		CONVEX,
		PLANE,		
	};

public:
	// for sphere and convex ( also used in debug draw of capsule )
	FVector Origin;
	// for sphere and capsule
	float	Radius;
	// for capsule ( needs 2 spheres to make an capsule, top end and bottom end )
	FVector SpherePos1;
	FVector SpherePos2;
	// for convex
	TArray<FPlane> ConvexPlanes;

	ClothCollisionPrimType	PrimType;
};

/** Used to define what type of objects cloth will react to. */
struct FApexClothCollisionInfo
{
	enum OverlappedComponentType
	{
		/** Interact with static objects only. */
		OCT_STATIC,
		/** Interact with clothing objects. */
		OCT_CLOTH,
		OCT_MAX
	};

	OverlappedComponentType OverlapCompType;
	/** To verify validation of collision info. */
	uint32 Revision;
	/** ClothingCollisions will be all released when clothing doesn't intersect with this component anymore. */
	TArray<nvidia::apex::ClothingCollision*> ClothingCollisions;
};
#endif // #if WITH_CLOTH_COLLISION_DETECTION

/** This enum defines how you'd like to update bones to physics world.
	If bone is simulating, you don't have to waste time on updating bone transform from kinematic.
	But also sometimes you don't like fixed bones to be updated by animation data. */
UENUM()
namespace EKinematicBonesUpdateToPhysics
{
	enum Type
	{
		/** Update any bones that are not simulating. */
		SkipSimulatingBones,
		/** Skip physics update from kinematic changes. */
		SkipAllBones
	};
}

UENUM()
namespace EAnimationMode
{
	enum Type
	{
		AnimationBlueprint UMETA(DisplayName="Use Animation Blueprint"), 
		AnimationSingleNode UMETA(DisplayName="Use Animation Asset"), 
		// This is custom type, engine leaves AnimInstance as it is
		AnimationCustomMode UMETA(DisplayName = "Use Custom Mode"),
	};
}

UENUM()
namespace EPhysicsTransformUpdateMode
{
	enum Type
	{
		SimulationUpatesComponentTransform,
		ComponentTransformIsKinematic
	};
}


/** Enum for indicating whether kinematic updates can be deferred */
enum class EAllowKinematicDeferral
{
	AllowDeferral,
	DisallowDeferral
};

class USkeletalMeshComponent;

/**
* Tick function that does post physics work on skeletal mesh component. This executes in EndPhysics (after physics is done)
**/
USTRUCT()
struct FSkeletalMeshComponentEndPhysicsTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	 USkeletalMeshComponent*	Target;

	/**
	* Abstract function to execute the tick.
	* @param DeltaTime - frame time to advance, in seconds.
	* @param TickType - kind of tick for this frame.
	* @param CurrentThread - thread we are executing on, useful to pass along as new tasks are created.
	* @param MyCompletionGraphEvent - completion event for this task. Useful for holding the completetion of this task until certain child tasks are complete.
	*/
	virtual void ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	/** Abstract function to describe this tick. Used to print messages about illegal cycles in the dependency graph. */
	virtual FString DiagnosticMessage() override;
};

template<>
struct TStructOpsTypeTraits<FSkeletalMeshComponentEndPhysicsTickFunction> : public TStructOpsTypeTraitsBase
{
	enum
	{
		WithCopy = false
	};
};


/**
* Tick function that prepares and simulates cloth
**/
USTRUCT()
struct FSkeletalMeshComponentClothTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	USkeletalMeshComponent*	Target;

	/**
	* Abstract function to execute the tick.
	* @param DeltaTime - frame time to advance, in seconds.
	* @param TickType - kind of tick for this frame.
	* @param CurrentThread - thread we are executing on, useful to pass along as new tasks are created.
	* @param MyCompletionGraphEvent - completion event for this task. Useful for holding the completetion of this task until certain child tasks are complete.
	*/
	virtual void ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	/** Abstract function to describe this tick. Used to print messages about illegal cycles in the dependency graph. */
	virtual FString DiagnosticMessage() override;
};

template<>
struct TStructOpsTypeTraits<FSkeletalMeshComponentClothTickFunction> : public TStructOpsTypeTraitsBase
{
	enum
	{
		WithCopy = false
	};
};


struct ENGINE_API FClosestPointOnPhysicsAsset
{
	/** The closest point in world space */
	FVector ClosestWorldPosition;

	/** The normal associated with the surface of the closest body */
	FVector Normal;

	/** The name of the bone associated with the closest body */
	FName BoneName;

	/** The distance of the closest point and the original world position. 0 Indicates world position is inside the closest body. */
	float Distance;

	FClosestPointOnPhysicsAsset()
		: ClosestWorldPosition(FVector::ZeroVector)
		, Normal(FVector::ZeroVector)
		, BoneName(NAME_None)
		, Distance(-1.f)
	{
	}
};

/**
 * SkeletalMeshComponent is used to create an instance of an animated SkeletalMesh asset.
 *
 * @see https://docs.unrealengine.com/latest/INT/Engine/Content/Types/SkeletalMeshes/
 * @see USkeletalMesh
 */
UCLASS(ClassGroup=(Rendering, Common), hidecategories=Object, config=Engine, editinlinenew, meta=(BlueprintSpawnableComponent))
class ENGINE_API USkeletalMeshComponent : public USkinnedMeshComponent, public IInterface_CollisionDataProvider
{
	GENERATED_UCLASS_BODY()
	
	/**
	 * Animation 
	 */
	
	/** @Todo anim: Matinee related data start - this needs to be replaced to new system. */
	
	/** @Todo anim: Matinee related data end - this needs to be replaced to new system. */

protected:
	/** Whether to use Animation Blueprint or play Single Animation Asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Animation)
	TEnumAsByte<EAnimationMode::Type>	AnimationMode;

public:
#if WITH_EDITORONLY_DATA
	/** The blueprint for creating an AnimationScript. */
	UPROPERTY()
	class UAnimBlueprint* AnimationBlueprint_DEPRECATED;
#endif

	DEPRECATED(4.11, "This property is deprecated. Please use AnimClass instead")
	UPROPERTY(BlueprintReadOnly, Category = Animation, meta = (DeprecationMessage = "This property is deprecated. Please use AnimClass instead"))
	class UAnimBlueprintGeneratedClass* AnimBlueprintGeneratedClass;

	/* The AnimBlueprint class to use. Use 'SetAnimInstanceClass' to change at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Animation)
	class TSubclassOf<UAnimInstance> AnimClass;

	/** The active animation graph program instance. */
	UPROPERTY(transient)
	UAnimInstance* AnimScriptInstance;

	/** Any running sub anim instances that need to be updates on the game thread */
	UPROPERTY(transient)
	TArray<UAnimInstance*> SubInstances;

	/** An instance created from the PostPhysicsBlueprint property of the skeletal mesh we're using,
	 *  Runs after physics has been blended
	 */
	UPROPERTY(transient)
	UAnimInstance* PostProcessAnimInstance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Animation, meta=(ShowOnlyInnerProperties))
	struct FSingleAnimationPlayData AnimationData;

	/** Temporary array of local-space (relative to parent bone) rotation/translation for each bone. */
	TArray<FTransform> BoneSpaceTransforms;
	
	/** Temporary storage for curves */
	FBlendedHeapCurve AnimCurves;

	// Update Rate

	/** Cached BoneSpaceTransforms for Update Rate optimization. */
	UPROPERTY(Transient)
	TArray<FTransform> CachedBoneSpaceTransforms;

	/** Cached SpaceBases for Update Rate optimization. */
	UPROPERTY(Transient)
	TArray<FTransform> CachedComponentSpaceTransforms;

	/** Cached Curve result for Update Rate optimization */
	FBlendedHeapCurve CachedCurve;

	/** Used to scale speed of all animations on this skeletal mesh. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=Animation)
	float GlobalAnimRateScale;

	/** If true, there is at least one body in the current PhysicsAsset with a valid bone in the current SkeletalMesh */
	UPROPERTY(transient)
	uint32 bHasValidBodies:1;

	/** Indicates that this SkeletalMeshComponent has deferred kinematic bone updates until next physics sim.  */
	uint32 bDeferredKinematicUpdate:1;

	/** If we are running physics, should we update non-simulated bones based on the animation bone positions. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=SkeletalMesh)
	TEnumAsByte<EKinematicBonesUpdateToPhysics::Type> KinematicBonesUpdateType;

	/** Whether physics simulation updates component transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Physics)
	TEnumAsByte<EPhysicsTransformUpdateMode::Type> PhysicsTransformUpdateMode;

	/** Enables blending in of physics bodies whether Simulate or not*/
	UPROPERTY(transient)
	uint32 bBlendPhysics:1;

	/**
	 *  If true, simulate physics for this component on a dedicated server.
	 *  This should be set if simulating physics and replicating with a dedicated server.
	 *	Note: This property cannot be changed at runtime.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = SkeletalMesh)
	uint32 bEnablePhysicsOnDedicatedServer:1;

	/**
	 *	If we should pass joint position to joints each frame, so that they can be used by motorized joints to drive the
	 *	ragdoll based on the animation.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=SkeletalMesh)
	uint32 bUpdateJointsFromAnimation:1;

	/** Disable cloth simulation and play original animation without simulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Clothing)
	uint32 bDisableClothSimulation:1;

	/** can't collide with part of environment if total collision volumes exceed 16 capsules or 32 planes per convex */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Clothing)
	uint32 bCollideWithEnvironment:1;
	/** can't collide with part of attached children if total collision volumes exceed 16 capsules or 32 planes per convex */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Clothing)
	uint32 bCollideWithAttachedChildren:1;
	/**
	 * It's worth trying this option when you feel that the current cloth simulation is unstable.
	 * The scale of the actor is maintained during the simulation. 
	 * It is possible to add the inertia effects to the simulation, through the inertiaScale parameter of the clothing material. 
	 * So with an inertiaScale of 1.0 there should be no visible difference between local space and global space simulation. 
	 * Known issues: - Currently there's simulation issues when this feature is used in 3.x (DE4076) So if localSpaceSim is enabled there's no inertia effect when the global pose of the clothing actor changes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Clothing)
	uint32 bLocalSpaceSimulation : 1;

	/**
	 * cloth morph target option
	 * This option will be applied only before playing because should do pre-calculation to reduce computation time for run-time play
	 * so it's impossible to change this option in run-time
	 */
	UPROPERTY(EditAnywhere, Category = Clothing)
	uint32 bClothMorphTarget : 1;

	/** reset the clothing after moving the clothing position (called teleport) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Clothing)
	uint32 bResetAfterTeleport:1;
	/** 
	 * conduct teleportation if the character's movement is greater than this threshold in 1 frame. 
	 * Zero or negative values will skip the check 
	 * you can also do force teleport manually using ForceNextUpdateTeleport() / ForceNextUpdateTeleportAndReset()
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Clothing)
	float TeleportDistanceThreshold;
	/** 
	 * rotation threshold in degree, ranging from 0 to 180
	 * conduct teleportation if the character's rotation is greater than this threshold in 1 frame. 
	 * Zero or negative values will skip the check 
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Clothing)
	float TeleportRotationThreshold;

	/**
	 * weight to blend between simulated results and key-framed positions
	 * if weight is 1.0, shows only cloth simulation results and 0.0 will show only skinned results
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Clothing)
	float ClothBlendWeight;

	/** Draw the APEX Clothing Normals on clothing sections. */
	uint32 bDisplayClothingNormals:1;

	/** Draw Computed Normal Vectors in Tangent space for clothing section */
	uint32 bDisplayClothingTangents:1;

	/** 
	 * Draw Collision Volumes from apex clothing asset. 
	 * Supports up to 16 capsules / 32 planes per convex and ignored collisions by a max number will be drawn in Dark Gray.
	 */
	uint32 bDisplayClothingCollisionVolumes:1;
	
	/** Draw clothing physical mesh wire frame */
	uint32 	bDisplayClothPhysicalMeshWire:1;	

	/** Draw max distances of clothing simulation vertices */
	uint32 	bDisplayClothMaxDistances:1;

	/** Draw back stops of clothing simulation vertices */
	uint32 	bDisplayClothBackstops:1;

	/** To save previous state */
	uint32 bPrevDisableClothSimulation:1;

	uint32 bDisplayClothFixedVertices:1;
	
	/** Offset of the root bone from the reference pose. Used to offset bounding box. */
	UPROPERTY(transient)
	FVector RootBoneTranslation;

	/** 
	 * Optimization
	 */
	
	/** Skips Ticking and Bone Refresh. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=SkeletalMesh)
	uint32 bNoSkeletonUpdate:1;

	/** pauses this component's animations (doesn't tick them, but still refreshes bones) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=Animation)
	uint32 bPauseAnims:1;

	/** On InitAnim should we set to ref pose (if false use first tick of animation data)*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Animation)
	bool bUseRefPoseOnInitAnim;

	/**
	* Uses skinned data for collision data.
	*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=SkeletalMesh)
	uint32 bEnablePerPolyCollision:1;

	/**
	* Used for per poly collision. In 99% of cases you will be better off using a Physics Asset.
	* This BodySetup is per instance because all modification of vertices is done in place */
	UPROPERTY(transient)
	class UBodySetup * BodySetup;

	void CreateBodySetup();

	virtual void SendRenderDebugPhysics() override;

	/**
	 * Misc 
	 */
	
	/** If true TickPose() will not be called from the Component's TickComponent function.
	* It will instead be called from Autonomous networking updates. See ACharacter. */
	UPROPERTY(Transient)
	uint32 bOnlyAllowAutonomousTickPose : 1;

	/** True if calling TickPose() from Autonomous networking updates. See ACharacter. */
	UPROPERTY(Transient)
	uint32 bIsAutonomousTickPose : 1;

	/** If true, force the mesh into the reference pose - is an optimization. */
	UPROPERTY()
	uint32 bForceRefpose:1;

	/** If bForceRefPose was set last tick. */
	UPROPERTY()
	uint32 bOldForceRefPose:1;

	/** Bool that enables debug drawing of the skeleton before it is passed to the physics. Useful for debugging animation-driven physics. */
	UPROPERTY()
	uint32 bShowPrePhysBones:1;

	/** If false, indicates that on the next call to UpdateSkelPose the RequiredBones array should be recalculated. */
	UPROPERTY(transient)
	uint32 bRequiredBonesUpToDate:1;

	/** If true, AnimTree has been initialised. */
	UPROPERTY(transient)
	uint32 bAnimTreeInitialised:1;

	/** If true, the Location of this Component will be included into its bounds calculation
	* (this can be useful when using SMU_OnlyTickPoseWhenRendered on a character that moves away from the root and no bones are left near the origin of the component) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = SkeletalMesh)
	uint32 bIncludeComponentLocationIntoBounds : 1;

	/** If true, line checks will test against the bounding box of this skeletal mesh component and return a hit if there is a collision. */
	UPROPERTY()
	uint32 bEnableLineCheckWithBounds:1;

protected:
#if WITH_EDITORONLY_DATA
	/** If true, this will Tick until disabled */
	UPROPERTY(AdvancedDisplay, EditInstanceOnly, transient, Category = SkeletalMesh)
	uint32 bUpdateAnimationInEditor : 1;
#endif

public:

	/** Cache AnimCurveUidVersion from Skeleton and this will be used to identify if it needs to be updated */
	UPROPERTY(transient)
	uint16 CachedAnimCurveUidVersion;
	
	/** If bEnableLineCheckWithBounds is true, scale the bounds by this value before doing line check. */
	UPROPERTY()
	FVector LineCheckBoundsScale;

	/** Threshold for physics asset bodies above which we use an aggregate for broadphase collisions */
	int32 RagdollAggregateThreshold;

	/** Notification when constraint is broken. */
	UPROPERTY(BlueprintAssignable)
	FConstraintBrokenSignature OnConstraintBroken;

	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh", meta=(Keywords = "AnimBlueprint"))
	void SetAnimInstanceClass(class UClass* NewClass);

	/** 
	 * Returns the animation instance that is driving the class (if available). This is typically an instance of
	 * the class set as AnimBlueprintGeneratedClass (generated by an animation blueprint)
	 * Since this instance is transient, it is not safe to be used during construction script
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh", meta=(Keywords = "AnimBlueprint", UnsafeDuringActorConstruction = "true"))
	class UAnimInstance * GetAnimInstance() const;

	/**
	 * Returns the active post process instance is one is available. This is set on the mesh that this
	 * component is using, and is evaluated immediately after the main instance.
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh", meta = (Keywords = "AnimBlueprint"))
	UAnimInstance* GetPostProcessInstance() const;

	/** Below are the interface to control animation when animation mode, not blueprint mode **/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation"))
	void SetAnimationMode(EAnimationMode::Type InAnimationMode);
	
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation"))
	EAnimationMode::Type GetAnimationMode() const;

	/* Animation play functions
	 *
	 * These changes status of animation instance, which is transient data, which means it won't serialize with this compoennt
	 * Becuase of that reason, it is not safe to be used during construction script
	 * Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized 
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	void PlayAnimation(class UAnimationAsset* NewAnimToPlay, bool bLooping);

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this compoennt
	* Becuase of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	void SetAnimation(class UAnimationAsset* NewAnimToPlay);

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this compoennt
	* Becuase of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	void Play(bool bLooping);

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this compoennt
	* Becuase of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	void Stop();

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this compoennt
	* Becuase of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	bool IsPlaying() const;

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this compoennt
	* Becuase of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	void SetPosition(float InPos, bool bFireNotifies = true);

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this compoennt
	* Becuase of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	float GetPosition() const;

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this compoennt
	* Becuase of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	void SetPlayRate(float Rate);

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this compoennt
	* Becuase of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	float GetPlayRate() const;

	/**
	 * This overrides current AnimationData parameter in the SkeletalMeshComponent. This will serialize when the component serialize
	 * so it can be used during construction script. However note that this will override current existing data
	 * This can be useful if you'd like to make a blueprint with custom default animation per component
	 * This sets single player mode, which means you can't use AnimBlueprint with it
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation"))
	void OverrideAnimationData(UAnimationAsset* InAnimToPlay, bool bIsLooping = true, bool bIsPlaying = true, float Position = 0.f, float PlayRate = 1.f);

	/**
	 * Set Morph Target with Name and Value(0-1)
	 *
	 * @param bRemoveZeroWeight : Used by editor code when it should stay in the active list with zero weight
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh", meta=(UnsafeDuringActorConstruction="true"))
	void SetMorphTarget(FName MorphTargetName, float Value, bool bRemoveZeroWeight=true);

	/**
	 * Clear all Morph Target that are set to this mesh
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void ClearMorphTargets();

	/**
	 * Get Morph target with given name
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	float GetMorphTarget(FName MorphTargetName) const;

	/**
	 * Takes a snapshot of this skeletal mesh component's pose and saves it to the specified snapshot.
	 * The snapshot is taken at the current LOD, so if for example you took the snapshot at LOD1 
	 * and then used it at LOD0 any bones not in LOD1 will use the reference pose 
	 */
	UFUNCTION(BlueprintCallable, Category = "Pose")
	void SnapshotPose(UPARAM(ref) FPoseSnapshot& Snapshot);

	/**
	 * Get/Set the max distance scale of clothing mesh vertices
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	float GetClothMaxDistanceScale();
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetClothMaxDistanceScale(float Scale);

	/** 
	 * Used to indicate we should force 'teleport' during the next call to UpdateClothState, 
	 * This will transform positions and velocities and thus keep the simulation state, just translate it to a new pose.
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void ForceClothNextUpdateTeleport();
	/** 
	 * Used to indicate we should force 'teleport and reset' during the next call to UpdateClothState.
	 * This can be used to reset it from a bad state or by a teleport where the old state is not important anymore.
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void ForceClothNextUpdateTeleportAndReset();

	/** Stops simulating clothing, but does not show clothing ref pose. Keeps the last known simulation state */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SuspendClothingSimulation();

	/** Resumes a previously suspended clothing simulation, teleporting the clothing on the next tick */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void ResumeClothingSimulation();

	/** Gets whether or not the clothing simulation is currently suspended */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	bool IsClothingSimulationSuspended();

	/**
	 * Reset the teleport mode of a next update to 'Continuous'
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void ResetClothTeleportMode();

	/** 
	 * If this component has a valid MasterPoseComponent then this function makes cloth items on the slave component
	 * take the transforms of the cloth items on the master component instead of simulating separately.
	 * @Note This will FORCE any cloth actor on the master component to simulate in local space. Also
	 * The meshes used in the components must be identical for the cloth to bind correctly
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh", meta=(UnsafeDuringActorConstruction="true"))
	void BindClothToMasterPoseComponent();

	/** 
	 * If this component has a valid MasterPoseComponent and has previously had its cloth bound to the
	 * MCP, this function will unbind the cloth and resume simulation.
	 * @param bRestoreSimulationSpace if true and the master pose cloth was originally simulating in world
	 * space, we will restore this setting. This will cause the master component to reset which may be
	 * undesirable.
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh", meta=(UnsafeDuringActorConstruction="true"))
	void UnbindClothFromMasterPoseComponent(bool bRestoreSimulationSpace = true);

	/**
	* Sets whether or not to force tick component in order to update animation and refresh transform for this component
	* This is supported only in the editor
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh", meta = (DevelopmentOnly, UnsafeDuringActorConstruction = "true"))
	void SetUpdateAnimationInEditor(const bool NewUpdateState);

	/** We detach the Component once we are done playing it.
	 *
	 * @param	ParticleSystemComponent that finished
	 */
	virtual void SkelMeshCompOnParticleSystemFinished( class UParticleSystemComponent* PSC );

	class UAnimSingleNodeInstance * GetSingleNodeInstance() const;
	bool InitializeAnimScriptInstance(bool bForceReinit=true);

	/** @return true if wind is enabled */
	virtual bool IsWindEnabled() const;

#if WITH_EDITOR
	/**
	* Subclasses such as DebugSkelMeshComponent keep track of errors in the anim notifies so they can be displayed to the user. This function adds an error.
	* Errors are added uniquely and only removed when they're cleared by ClearAnimNotifyError.
	*/
	virtual void ReportAnimNotifyError(const FText& Error, UObject* InSourceNotify){}
	
	/**
	* Clears currently stored errors. Call before triggering anim notifies for a particular mesh.
	*/
	virtual void ClearAnimNotifyErrors(UObject* InSourceNotify){}
#endif

protected:

	/** Whether the clothing simulation is suspended (not the same as disabled, we no longer run the sim but keep the last valid sim data around) */
	bool bClothingSimulationSuspended;

public:
	/** Temporary array of bone indices required this frame. Filled in by UpdateSkelPose. */
	TArray<FBoneIndexType> RequiredBones;

	/** Temporary array of bone indices required to populate component space transforms */
	TArray<FBoneIndexType> FillComponentSpaceTransformsRequiredBones;

	/** 
	 *	Index of the 'Root Body', or top body in the asset hierarchy. 
	 *	Filled in by InitInstance, so we don't need to save it.
	 */
	/** To save root body index/bone index consistently **/
	struct 
	{
		int32 BodyIndex;
		FTransform TransformToRoot;
	} RootBodyData;

	/** Set Root Body Index */
	void SetRootBodyIndex(int32 InBodyIndex);
	/** Reset Root Body Index */
	void ResetRootBodyIndex();

	/** Array of FBodyInstance objects, storing per-instance state about about each body. */
	TArray<struct FBodyInstance*> Bodies;

	/** Array of FConstraintInstance structs, storing per-instance state about each constraint. */
	TArray<struct FConstraintInstance*> Constraints;

#if WITH_PHYSX
	/** Physics-engine representation of PxAggregate which contains a physics asset instance with more than numbers of bodies. */
	class physx::PxAggregate* Aggregate;

#endif	//WITH_PHYSX

	FSkeletalMeshComponentClothTickFunction ClothTickFunction;

#if WITH_APEX_CLOTHING

	/** used for pre-computation using TeleportRotationThreshold property */
	float ClothTeleportCosineThresholdInRad;
	/** used for pre-computation using tTeleportDistanceThreshold property */
	float ClothTeleportDistThresholdSquared;

	/** whether we need to teleport cloth. */
	FClothingActor::TeleportMode ClothTeleportMode;

	bool IsClothBoundToMasterComponent() const { return bBindClothToMasterComponent; }

private:

	friend FSkeletalMeshComponentClothTickFunction;

   /** Double buffer for the current cloth simulation context */
	FClothSimulationContext InternalClothSimulationContext;

	/** Whether or not we're taking cloth sim information from our master component */
	bool bBindClothToMasterComponent;
	/** The previous state of the master component simulation coord space, so we can restore on unbind */
	bool bPrevMasterSimulateLocalSpace;

	/** Copies the data from the external cloth simulation context. We copy instead of flipping because the API has to return the full struct to make backwards compat easy*/
	void UpdateClothSimulationContext();

   /** 
	* clothing actors will be created from clothing assets for cloth simulation 
	* 1 actor should correspond to 1 asset
	*/
	TArray<FClothingActor> ClothingActors;

	/** previous root bone matrix to compare the difference and decide to do clothing teleport  */
	FMatrix	PrevRootBoneMatrix;

public:

	const TArray<FClothingActor>& GetClothingActors(){ return ClothingActors; }

	float ClothMaxDistanceScale;
	/** used for checking whether cloth morph target data were pre-computed or not */
	bool bPreparedClothMorphTargets;

	/** precomputed actual cloth morph target data */
	TArray<FClothMorphTargetData> ClothMorphTargets;

	#if WITH_CLOTH_COLLISION_DETECTION
	/** increase every tick to update clothing collision  */
	uint32 ClothingCollisionRevision;

	TArray<nvidia::apex::ClothingCollision*>	ParentCollisions;
	TArray<nvidia::apex::ClothingCollision*>	EnvironmentCollisions;
	TArray<nvidia::apex::ClothingCollision*>	ChildrenCollisions;

	TMap<TWeakObjectPtr<UPrimitiveComponent>, FApexClothCollisionInfo> ClothOverlappedComponentsMap;
	#endif // WITH_CLOTH_COLLISION_DETECTION
#endif // WITH_APEX_CLOTHING

private:

	/** Wrapper that calls our constraint broken delegate */
	void OnConstraintBrokenWrapper(int32 ConstraintIndex);

	/** 
	 * Morph Target Curves. This will override AnimInstance MorphTargetCurves
	 * if same curve is found
	 **/
	TMap<FName, float>	MorphTargetCurves;

	static uint32 GetPhysicsSceneType(const UPhysicsAsset& PhysAsset, const FPhysScene& PhysScene);

public:
	const TMap<FName, float>& GetMorphTargetCurves() const { return MorphTargetCurves;  }
	// 
	// Animation
	//
	virtual void InitAnim(bool bForceReinit);

	/** Tick Animation system */
	void TickAnimation(float DeltaTime, bool bNeedsValidRootMotion);

	/** Tick Clothing Animation , basically this is called inside TickComponent */
	void TickClothing(float DeltaTime, FTickFunction& ThisTickFunction);

	/** Store cloth simulation data into OutClothSimData */
	void GetUpdateClothSimulationData(TMap<int32, FClothSimulData>& OutClothSimData, USkeletalMeshComponent* OverrideLocalRootComponent = nullptr);
	void RemoveAllClothingActors();
	void ReleaseAllClothingResources();

	bool IsValidClothingActor(const FClothingActor& ClothingActor) const;
	/** Draws APEX Clothing simulated normals on cloth meshes **/
	void DrawClothingNormals(FPrimitiveDrawInterface* PDI);
	/** Draws APEX Clothing Graphical Tangents on cloth meshes **/
	void DrawClothingTangents(FPrimitiveDrawInterface* PDI);
	/** Draws internal collision volumes which the character has, colliding with cloth **/
	void DrawClothingCollisionVolumes(FPrimitiveDrawInterface* PDI);
	/** Draws max distances of clothing simulation vertices 
	  * clothing simulation will be disabled and animation will be reset when drawing this option 
	  * because max distances do have meaning only in initial pose
	 **/
	void DrawClothingMaxDistances(FPrimitiveDrawInterface* PDI);
	/** Draws Clothing back stops **/
	void DrawClothingBackstops(FPrimitiveDrawInterface* PDI);
	/** Draws Clothing Physical mesh wire **/
	void DrawClothingPhysicalMeshWire(FPrimitiveDrawInterface* PDI);

	void DrawClothingFixedVertices(FPrimitiveDrawInterface* PDI);

	/** Loads clothing extra infos dynamically just for Previewing in Editor 
	 *  such as MaxDistances, Physical mesh wire
	 **/
	void LoadClothingVisualizationInfo(FClothingAssetData& ClothAssetData);
	void LoadAllClothingVisualizationInfos();

	/** freezing clothing actor now */
	void FreezeClothSection(bool bFreeze);

	/** Changes the value of bNotifyRigidBodyCollision
	* @param bNewNotifyRigidBodyCollision - The value to assign to bNotifyRigidBodyCollision
	*/
	virtual void SetNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision) override;

	/** Changes the value of bNotifyRigidBodyCollision for a given body
	* @param bNewNotifyRigidBodyCollision	The value to assign to bNotifyRigidBodyCollision
	* @param BoneName						Name of the body to turn hit notifies on/off. None implies root body
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	virtual void SetBodyNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision, FName BoneName = NAME_None);

	/** Changes the value of bNotifyRigidBodyCollision on all bodies below a given bone
	* @param bNewNotifyRigidBodyCollision	The value to assign to bNotifyRigidBodyCollision
	* @param BoneName						Name of the body to turn hit notifies on (and below)
	* @param bIncludeSelf					Whether to modify the given body (useful for roots with multiple children)
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	virtual void SetNotifyRigidBodyCollisionBelow(bool bNewNotifyRigidBodyCollision, FName BoneName = NAME_None, bool bIncludeSelf = true);

	/** 
	 * Recalculates the RequiredBones array in this SkeletalMeshComponent based on current SkeletalMesh, LOD and PhysicsAsset.
	 * Is called when bRequiredBonesUpToDate = false
	 *
	 * @param	LODIndex	Index of LOD [0-(MaxLOD-1)]
	 */
	void RecalcRequiredBones(int32 LODIndex);

	/**
	* Recalculates the AnimCurveUids array in RequiredBone of this SkeletalMeshComponent based on current required bone set
	* Is called when Skeleton->IsRequiredCurvesUpToDate() = false
	*/
	void RecalcRequiredCurves();

public:
	//~ Begin UObject Interface.
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FOnSkeletalMeshPropertyChangedMulticaster)
	FOnSkeletalMeshPropertyChangedMulticaster OnSkeletalMeshPropertyChanged;
	typedef FOnSkeletalMeshPropertyChangedMulticaster::FDelegate FOnSkeletalMeshPropertyChanged;

	/** Register / Unregister delegates called when the skeletal mesh property is changed */
	FDelegateHandle RegisterOnSkeletalMeshPropertyChanged(const FOnSkeletalMeshPropertyChanged& Delegate);
	void UnregisterOnSkeletalMeshPropertyChanged(FDelegateHandle Handle);

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Validates the animation asset or blueprint, making sure it is compatible with the current skeleton */
	void ValidateAnimation();

	virtual void LoadedFromAnotherClass(const FName& OldClassName) override;
	virtual void UpdateCollisionProfile() override;
#endif // WITH_EDITOR
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	//~ End UObject Interface.

	//~ Begin UActorComponent Interface.
protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void CreateRenderState_Concurrent() override;
	virtual bool ShouldCreatePhysicsState() const override;
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
public:
	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	//Handle registering our end physics tick function
	void RegisterEndPhysicsTick(bool bRegister);

	//Handle registering our pre cloth tick function
	void RegisterClothTick(bool bRegister);

	//~ End UActorComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual bool IsAnySimulatingPhysics() const override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;
	virtual void UpdateOverlaps(TArray<FOverlapInfo> const* PendingOverlaps=NULL, bool bDoNotifies=true, const TArray<FOverlapInfo>* OverlapsAtEndLocation=NULL) override;
	//~ End USceneComponent Interface.

	//~ Begin UPrimitiveComponent Interface.
protected:
	/**
	 *  Test the collision of the supplied component at the supplied location/rotation, and determine the set of components that it overlaps
	 *  @param  OutOverlaps     Array of overlaps found between this component in specified pose and the world
	 *  @param  World			World to use for overlap test
	 *  @param  Pos             Location of the component's geometry for the test against the world
	 *  @param  Rot             Rotation of the component's geometry for the test against the world
	 *  @param  TestChannel		The 'channel' that this ray is in, used to determine which components to hit
	 *	@param	ObjectQueryParams	List of object types it's looking for. When this enters, we do object query with component shape
	 *  @return TRUE if OutOverlaps contains any blocking results
	 */
	virtual bool ComponentOverlapMultiImpl(TArray<struct FOverlapResult>& OutOverlaps, const class UWorld* InWorld, const FVector& Pos, const FQuat& Rot, ECollisionChannel TestChannel, const struct FComponentQueryParams& Params, const struct FCollisionObjectQueryParams& ObjectQueryParams = FCollisionObjectQueryParams::DefaultObjectQueryParam) const override;
	
	virtual bool ComponentOverlapComponentImpl(class UPrimitiveComponent* PrimComp, const FVector Pos, const FQuat& Quat, const FCollisionQueryParams& Params) override;

	virtual bool MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit = NULL, EMoveComponentFlags MoveFlags = MOVECOMP_NoFlags, ETeleportType Teleport = ETeleportType::None) override;

public:

	virtual class UBodySetup* GetBodySetup() override;
	virtual bool CanEditSimulatePhysics() override;
	virtual FBodyInstance* GetBodyInstance(FName BoneName = NAME_None, bool bGetWelded = true) const override;
	virtual void UpdatePhysicsToRBChannels() override;
	virtual void SetAllPhysicsAngularVelocity(FVector const& NewVel, bool bAddToCurrent = false) override;
	virtual void SetAllPhysicsPosition(FVector NewPos) override;
	virtual void SetAllPhysicsRotation(FRotator NewRot) override;
	virtual void SetAllPhysicsRotation(const FQuat& NewRot) override;
	virtual void WakeAllRigidBodies() override;
	virtual void PutAllRigidBodiesToSleep() override;
	virtual bool IsAnyRigidBodyAwake() override;
	virtual void SetEnableGravity(bool bGravityEnabled);
	virtual void SetUseCustomGravity(bool bCustomGravityEnabled);
	virtual bool IsGravityEnabled() const override;
	virtual bool IsCustomGravityEnabled() const override;
	virtual void OnComponentCollisionSettingsChanged() override;
	virtual void SetPhysMaterialOverride(UPhysicalMaterial* NewPhysMaterial) override;
	virtual bool GetSquaredDistanceToCollision(const FVector& Point, float& OutSquaredDistance, FVector& OutClosestPointOnCollision) const override;

	/**
	 *	Enables or disables gravity for the given bone.
	 *	NAME_None indicates the root body will be edited.
	 *	If the bone name given is otherwise invalid, nothing happens.
	 *
	 *	@param bEnableGravity	Whether gravity should be enabled or disabled.
	 *	@param BoneName			The name of the bone to modify.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics|Components")
	void SetEnableBodyGravity(bool bEnableGravity, FName BoneName);

	/**
	 *	Checks whether or not gravity is enabled on the given bone.
	 *	NAME_None indicates the root body should be queried.
	 *	If the bone name given is otherwise invalid, false is returned.
	 *
	 *	@param BoneName	The name of the bone to check.
	 *	@return True if gravity is enabled on the bone.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics|Components")
	bool IsBodyGravityEnabled(FName BoneName);

	/**
	*	Enables or disables gravity to all bodies below the given bone.
	*   NAME_None indicates all bodies will be edited.
	*	In that case, consider using UPrimitiveComponent::EnableGravity.
	*
	*	@param bEnableGravity	Whether gravity should be enabled or disabled.
	*	@param BoneName			The name of the top most bone.
	*	@param bIncludeSelf		Whether the bone specified should be edited.
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Components")
	void SetEnableGravityOnAllBodiesBelow(bool bEnableGravity, FName BoneName, bool bIncludeSelf=true);

	/** 
	 *	Given a world position, find the closest point on the physics asset. Note that this is independent of collision and welding. This is based purely on animation position
	 *  @param	WorldPosition				The point we want the closest point to (i.e. for all bodies in the physics asset, find the one that has a point closest to WorldPosition)
	 *  @param	ClosestPointOnPhysicsAsset	The data associated with the closest point (position, normal, etc...)
	 *  @param	bApproximate				The closest body is found using bone transform distance instead of body distance. This approximation means the final point is the closest point on a potentially not closest body. This approximation gets worse as the size of Bodies gets bigger.
	 *  @return	true if we found a closest point
	 */
	bool GetClosestPointOnPhysicsAsset(const FVector& WorldPosition, FClosestPointOnPhysicsAsset& ClosestPointOnPhysicsAsset, bool bApproximate) const;

	/** 
	 *	Given a world position, find the closest point on the physics asset. Note that this is independent of collision and welding. This is based purely on animation position
	 *  @param	WorldPosition				The point we want the closest point to (i.e. for all bodies in the physics asset, find the one that has a point closest to WorldPosition)
	 *  @param	ClosestPointOnPhysicsAsset	The data associated with the closest point (position, normal, etc...)
	 *  @return	true if we found a closest point
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh", meta=(DisplayName="GetClosestPointOnPhysicsAsset", Keywords="closest point"))
	bool K2_GetClosestPointOnPhysicsAsset(const FVector& WorldPosition, FVector& ClosestWorldPosition, FVector& Normal, FName& BoneName, float& Distance) const;

	virtual bool LineTraceComponent( FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params ) override;
    virtual bool SweepComponent( FHitResult& OutHit, const FVector Start, const FVector End, const FQuat& ShapRotation, const FCollisionShape& CollisionShape, bool bTraceComplex=false) override;
	virtual bool OverlapComponent(const FVector& Pos, const FQuat& Rot, const FCollisionShape& CollisionShape) override;
	virtual void SetSimulatePhysics(bool bEnabled) override;
	virtual void AddRadialImpulse(FVector Origin, float Radius, float Strength, ERadialImpulseFalloff Falloff, bool bVelChange=false) override;
	virtual void AddRadialForce(FVector Origin, float Radius, float Strength, ERadialImpulseFalloff Falloff, bool bAccelChange=false) override;
	virtual void SetAllPhysicsLinearVelocity(FVector NewVel,bool bAddToCurrent = false) override;
	virtual void SetAllMassScale(float InMassScale = 1.f) override;
	virtual float GetMass() const override;

	/**
	*	Returns the mass (in kg) of the given bone
	*
	*	@param BoneName		Name of the body to return. 'None' indicates root body.
	*	@param bScaleMass	If true, the mass is scaled by the bone's MassScale.
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	float GetBoneMass(FName BoneName = NAME_None, bool bScaleMass = true) const;

	/**
	*	Returns the center of mass of the skeletal mesh, instead of the root body's location
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	FVector GetSkeletalCenterOfMass() const;


	virtual float CalculateMass(FName BoneName = NAME_None) override;
	virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const override;

	/**
	*	Add a force to all rigid bodies below.
	*   This is like a 'thruster'. Good for adding a burst over some (non zero) time. Should be called every frame for the duration of the force.
	*
	*	@param	Force		 Force vector to apply. Magnitude indicates strength of force.
	*	@param	BoneName	 If a SkeletalMeshComponent, name of body to apply force to. 'None' indicates root body.
	*   @param  bAccelChange If true, Force is taken as a change in acceleration instead of a physical force (i.e. mass will have no affect).
	*   @param  bIncludeSelf If false, Force is only applied to bodies below but not given bone name.
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	virtual void AddForceToAllBodiesBelow(FVector Force, FName BoneName = NAME_None, bool bAccelChange = false, bool bIncludeSelf = true);

	/**
	*	Add impulse to all single rigid bodies below. Good for one time instant burst.
	*
	*	@param	Impulse		Magnitude and direction of impulse to apply.
	*	@param	BoneName	If a SkeletalMeshComponent, name of body to apply impulse to. 'None' indicates root body.
	*	@param	bVelChange	If true, the Strength is taken as a change in velocity instead of an impulse (ie. mass will have no affect).
	*	@param bIncludeSelf If false, Force is only applied to bodies below but not given bone name.
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	virtual void AddImpulseToAllBodiesBelow(FVector Impulse, FName BoneName = NAME_None, bool bVelChange = false, bool bIncludeSelf = true);

#if WITH_EDITOR
	virtual bool ComponentIsTouchingSelectionBox(const FBox& InSelBBox, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const override;
	virtual bool ComponentIsTouchingSelectionFrustum(const FConvexVolume& InFrustum, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const override;
#endif
protected:
	virtual FTransform GetComponentTransformFromBodyInstance(FBodyInstance* UseBI) override;
	//~ End UPrimitiveComponent Interface.

public:
	//~ Begin USkinnedMeshComponent Interface
	virtual bool UpdateLODStatus() override;
	virtual void RefreshBoneTransforms( FActorComponentTickFunction* TickFunction = NULL ) override;
	virtual void TickPose(float DeltaTime, bool bNeedsValidRootMotion) override;
	virtual void UpdateSlaveComponent() override;
	virtual bool ShouldUpdateTransform(bool bLODHasChanged) const override;
	virtual bool ShouldTickPose() const override;
	virtual bool AllocateTransformData() override;
	virtual void DeallocateTransformData() override;
	virtual void HideBone( int32 BoneIndex, EPhysBodyOp PhysBodyOption ) override;
	virtual void UnHideBone( int32 BoneIndex ) override;
	virtual void SetPhysicsAsset(class UPhysicsAsset* NewPhysicsAsset,bool bForceReInit = false) override;
	virtual void SetSkeletalMesh(class USkeletalMesh* NewMesh, bool bReinitPose = true) override;
	virtual FVector GetSkinnedVertexPosition(int32 VertexIndex) const override;

	void SetSkeletalMeshWithoutResettingAnimation(class USkeletalMesh* NewMesh);

	virtual bool IsPlayingRootMotion() override;
	virtual bool IsPlayingRootMotionFromEverything() override;
	virtual void FinalizeBoneTransform() override;
	//~ End USkinnedMeshComponent Interface
	/** 
	 *	Iterate over each joint in the physics for this mesh, setting its AngularPositionTarget based on the animation information.
	 */
	void UpdateRBJointMotors();


	/**
	* Runs the animation evaluation for the current pose into the supplied variables
	*
	* @param	InSkeletalMesh			The skeletal mesh we are animating
	* @param	InAnimInstance			The anim instance we are evaluating
	* @param	OutSpaceBases			Component space bone transforms
	* @param	OutBoneSpaceTransforms	Local space bone transforms
	* @param	OutRootBoneTranslation	Calculated root bone translation
	* @param	OutCurves				Blended Curve
	*/
	void PerformAnimationEvaluation(const USkeletalMesh* InSkeletalMesh, UAnimInstance* InAnimInstance, TArray<FTransform>& OutSpaceBases, TArray<FTransform>& OutBoneSpaceTransforms, FVector& OutRootBoneTranslation, FBlendedHeapCurve& OutCurve) const;

	/**
	 * Evaluates the post process instance from the skeletal mesh this component is using.
	 */
	void EvaluatePostProcessMeshInstance(TArray<FTransform>& OutBoneSpaceTransforms, FBlendedHeapCurve& OutCurve, const USkeletalMesh* InSkeletalMesh, FVector& OutRootBoneTranslation) const;

	void PostAnimEvaluation(FAnimationEvaluationContext& EvaluationContext);

	/**
	 * Blend of Physics Bones with PhysicsWeight and Animated Bones with (1-PhysicsWeight)
	 *
	 * @param	RequiredBones	List of bones to be blend
	 */
	void BlendPhysicsBones( TArray<FBoneIndexType>& Bones )
	{
		PerformBlendPhysicsBones(Bones, AnimEvaluationContext.BoneSpaceTransforms);
	}


	/** Take the results of the physics and blend them with the animation state (based on the PhysicsWeight parameter), and update the SpaceBases array. */
	void BlendInPhysics(FTickFunction& ThisTickFunction);	

	/** 
	 * Initialize PhysicsAssetInstance for the physicsAsset 
	 * 
	 * @param	PhysScene	Physics Scene
	 */
	void InitArticulated(FPhysScene* PhysScene);

	/** Turn off all physics and remove the instance. */
	void TermArticulated();

	/** Find the root body index*/
	int32 FindRootBodyIndex() const;


	/** Terminate physics on all bodies below the named bone */
	void TermBodiesBelow(FName ParentBoneName);

	/** Find instance of the constraint that matches the name supplied. */
	FConstraintInstance* FindConstraintInstance(FName ConName);

	/** Utility which returns total mass of all bones below the supplied one in the hierarchy (including this one). */
	float GetTotalMassBelowBone(FName InBoneName);

	/** Set the collision object type on the skeletal mesh */
	virtual void SetCollisionObjectType(ECollisionChannel Channel) override;

	/** Set the movement channel of all bodies */
	void SetAllBodiesCollisionObjectType(ECollisionChannel NewChannel);

	/** Set the rigid body notification state for all bodies. */
	void SetAllBodiesNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision);

	/** Set bSimulatePhysics to true for all bone bodies. Does not change the component bSimulatePhysics flag. */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetAllBodiesSimulatePhysics(bool bNewSimulate);

	/** This is global set up for setting physics blend weight
	 * This does multiple things automatically
	 * If PhysicsBlendWeight == 1.f, it will enable Simulation, and if PhysicsBlendWeight == 0.f, it will disable Simulation. 
	 * Also it will respect each body's setup, so if the body is fixed, it won't simulate. Vice versa
	 * So if you'd like all bodies to change manually, do not use this function, but SetAllBodiesPhysicsBlendWeight
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetPhysicsBlendWeight(float PhysicsBlendWeight);

	/** Disable physics blending of bones **/
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetEnablePhysicsBlending(bool bNewBlendPhysics);

	/** Set all of the bones below passed in bone to be simulated */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetAllBodiesBelowSimulatePhysics(const FName& InBoneName, bool bNewSimulate, bool bIncludeSelf = true );

	/** Allows you to reset bodies Simulate state based on where bUsePhysics is set to true in the BodySetup. */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void ResetAllBodiesSimulatePhysics();

	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetAllBodiesPhysicsBlendWeight(float PhysicsBlendWeight, bool bSkipCustomPhysicsType = false );

	/** Set all of the bones below passed in bone to be simulated */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetAllBodiesBelowPhysicsBlendWeight(const FName& InBoneName, float PhysicsBlendWeight, bool bSkipCustomPhysicsType = false, bool bIncludeSelf = true );

	/** Accumulate AddPhysicsBlendWeight to physics blendweight for all of the bones below passed in bone to be simulated */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void AccumulateAllBodiesBelowPhysicsBlendWeight(const FName& InBoneName, float AddPhysicsBlendWeight, bool bSkipCustomPhysicsType = false );

	/** Enable or Disable AngularPositionDrive. If motor is in SLERP mode it will be turned on if either EnableSwingDrive OR EnableTwistDrive are enabled. In Twist and Swing mode the twist and the swing can be controlled individually.*/
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void SetAllMotorsAngularPositionDrive(bool bEnableSwingDrive, bool bEnableTwistDrive, bool bSkipCustomPhysicsType = false);

	/** Enable or Disable AngularVelocityDrive. If motor is in SLERP mode it will be turned on if either EnableSwingDrive OR EnableTwistDrive are enabled. In Twist and Swing mode the twist and the swing can be controlled individually.*/
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void SetAllMotorsAngularVelocityDrive(bool bEnableSwingDrive, bool bEnableTwistDrive, bool bSkipCustomPhysicsType = false);

	/** Set Angular Drive motors params for all constraint instances */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void SetAllMotorsAngularDriveParams(float InSpring, float InDamping, float InForceLimit, bool bSkipCustomPhysicsType = false);

	/** Sets the constraint profile properties (limits, motors, etc...) to match the constraint profile as defined in the physics asset. If profile name is not found the joint is set to use the default constraint profile.*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Components|PhysicsConstraint")
	void SetConstraintProfile(FName JointName, FName ProfileName, bool bDefaultIfNotFound = false);

	/** Sets the constraint profile properties (limits, motors, etc...) to match the constraint profile as defined in the physics asset for all constraints. If profile name is not found the joint is set to use the default constraint profile.*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Components|PhysicsConstraint")
	void SetConstraintProfileForAll(FName ProfileName, bool bDefaultIfNotFound = false);

	/** Enable or Disable AngularPositionDrive based on a list of bone names */
	void SetNamedMotorsAngularPositionDrive(bool bEnableSwingDrive, bool bEnableTwistDrive, const TArray<FName>& BoneNames, bool bSetOtherBodiesToComplement = false);

	/** Enable or Disable AngularVelocityDrive based on a list of bone names */
	void SetNamedMotorsAngularVelocityDrive(bool bEnableSwingDrive, bool bEnableTwistDrive, const TArray<FName>& BoneNames, bool bSetOtherBodiesToComplement = false);

	void GetWeldedBodies(TArray<FBodyInstance*> & OutWeldedBodies, TArray<FName> & OutChildrenLabels, bool bIncludingAutoWeld = false) override;

	/** Iterates over all bodies below and executes Func. Returns number of bodies found */
	int32 ForEachBodyBelow(FName BoneName, bool bIncludeSelf, bool bSkipCustomType, TFunctionRef<void(FBodyInstance*)> Func);

	/** 
	 * Change whether to force mesh into ref pose (and use cheaper vertex shader) 
	 *
	 * @param	bNewForceRefPose	true if it would like to force ref pose
	 */
	void SetForceRefPose(bool bNewForceRefPose);
	
	/** Update bHasValidBodies flag */
	void UpdateHasValidBodies();

	/** Update the bone mapping on each body instance. This is useful when changing skeletal mesh without recreating bodies */
	void UpdateBoneBodyMapping();
	
	/** 
	 * Initialize SkelControls
	 */
	void InitSkelControls();
	
	/**
	 * Find Constraint Index from the name
	 * 
	 * @param	ConstraintName	Joint Name of constraint to look for
	 * @return	Constraint Index
	 */
	int32	FindConstraintIndex(FName ConstraintName);
	
	/**
	 * Find Constraint Name from index
	 * 
	 * @param	ConstraintIndex	Index of constraint to look for
	 * @return	Constraint Joint Name
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Components|PhysicsConstraint")
	FName	FindConstraintBoneName(int32 ConstraintIndex);

	/** 
	 *	Iterate over each physics body in the physics for this mesh, and for each 'kinematic' (ie fixed or default if owner isn't simulating) one, update its
	 *	transform based on the animated transform.
	 *	@param	InComponentSpaceTransforms	Array of bone transforms in component space
	 *	@param	Teleport					Whether movement is a 'teleport' (ie infers no physics velocity, but moves simulating bodies) or not
	 *	@param	bNeedsSkinning				Whether we may need  to send new triangle data for per-poly skeletal mesh collision
	 *	@perem	AllowDeferral				Whether we can defer actual update of bodies (if 'physics only' collision)
	 */
	void UpdateKinematicBonesToAnim(const TArray<FTransform>& InComponentSpaceTransforms, ETeleportType Teleport, bool bNeedsSkinning, EAllowKinematicDeferral DeferralAllowed = EAllowKinematicDeferral::AllowDeferral);

	/**
	 * Look up all bodies for broken constraints.
	 * Makes sure child bodies of a broken constraints are not fixed and using bone springs, and child joints not motorized.
	 */
	void UpdateMeshForBrokenConstraints();
	
	/**
	 * Notifier when look at control goes beyond of limit - candidate for delegate
	 */
	virtual void NotifySkelControlBeyondLimit(class USkelControlLookAt* LookAt);

	/** 
	 * Break a constraint off a Gore mesh. 
	 * 
	 * @param	Impulse	vector of impulse
	 * @param	HitLocation	location of the hit
	 * @param	InBoneName	Name of bone to break constraint for
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh", meta = (Keywords = "Constraint"))
	void BreakConstraint(FVector Impulse, FVector HitLocation, FName InBoneName);

	/** Sets the Angular Motion Ranges for a named bone
	*  @param InBoneName  Name of bone to adjust constraint ranges for
	*  @param Swing1LimitAngle	 Size of limit in degrees, 0 means locked, 180 means free
	*  @param TwistLimitAngle	 Size of limit in degrees, 0 means locked, 180 means free
	*  @param Swing2LimitAngle	 Size of limit in degrees, 0 means locked, 180 means free
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void  SetAngularLimits(FName InBoneName,float Swing1LimitAngle, float TwistLimitAngle, float Swing2LimitAngle);

	/** Gets the current Angular state for a named bone constraint 
	*  @param InBoneName  Name of bone to get constraint ranges for
	*  @param Swing1Angle current angular state of the constraint
	*  @param TwistAngle  current angular state of the constraint
	*  @param Swing2Angle current angular state of the constraint
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void GetCurrentJointAngles(FName InBoneName,float& Swing1Angle, float& TwistAngle, float& Swing2Angle) ;


	/** iterates through all bodies in our PhysicsAsset and returns the location of the closest bone associated
	 * with a body that has collision enabled.
	 * @param TestLocation - location to check against
	 * @return location of closest colliding rigidbody, or TestLocation if there were no bodies to test
	 */
	FVector GetClosestCollidingRigidBodyLocation(const FVector& TestLocation) const;
	
	/** Set physics transforms for all bodies */
	void ApplyDeltaToAllPhysicsTransforms(const FVector& DeltaLocation, const FQuat& DeltaRotation);

#if WITH_APEX_CLOTHING
	/** 
	* APEX clothing actor is created from APEX clothing asset for cloth simulation 
	* create only if became invalid
	* BlendedData : added for cloth morph target but not used commonly
	*/
	bool CreateClothingActor(int32 AssetIndex, nvidia::apex::ClothingAsset* ClothingAsset, TArray<FVector>* BlendedDelta = NULL);
	/** should call this method if occurred any changes in clothing assets */
	void RecreateClothingActors();
	/** add bounding box for cloth */
	void AddClothingBounds(FBoxSphereBounds& InOutBounds, const FTransform& LocalToWorld) const;
	/** changes clothing LODs, if clothing LOD is disabled or LODIndex is greater than apex clothing LODs, simulation will be disabled */
	void SetClothingLOD(int32 LODIndex);
	/** check whether clothing teleport is needed or not to avoid a weird simulation result */
	virtual void CheckClothTeleport();
	/** 
	* methods for cloth morph targets 
	*/
	/** pre-compute morph target data for clothing */
	void PrepareClothMorphTargets();
	/** change morph target mapping when active morph target is changed */
	void ChangeClothMorphTargetMapping(FClothMorphTargetData& MorphData, FName CurrentActivatedMorphName);
	/** update active morph target's blending data when morph weight is changed */
	void UpdateClothMorphTarget();

	/** 
	 * Updates all clothing animation states including ComponentToWorld-related states. Triggers the simulation tasks
	 */
	void UpdateClothStateAndSimulate(float DeltaTime, FTickFunction& ThisTickFunction);
	/** 
	 * Updates cloth collision outside the cloth asset (environment collision, child collision, etc...)
	 * Should be called when scene changes or world position changes
	 */
	void UpdateClothTransform(ETeleportType TeleportType);

	/** only check whether there are valid clothing actors or not */
	bool HasValidClothingActors() const;

	/** get root bone matrix by the root bone index which Apex cloth asset is holding */
	void GetClothRootBoneMatrix(int32 AssetIndex, FMatrix& OutRootBoneMatrix) const;

	/** if the vertex index is valid for simulated vertices, returns the position in world space */
	bool GetClothSimulatedPosition(int32 AssetIndex, int32 VertexIndex, FVector& OutSimulPos) const;

#if WITH_CLOTH_COLLISION_DETECTION

	/** draws currently intersected collisions */
	void DrawDebugClothCollisions();
	/** draws a convex from planes for debug info */
	void DrawDebugConvexFromPlanes(FClothCollisionPrimitive& CollisionPrimitive, FColor& Color, bool bDrawWithPlanes=true);
	void ReleaseClothingCollision(nvidia::apex::ClothingCollision* Collision);
	/** create new collisions when newly added  */
	FApexClothCollisionInfo* CreateNewClothingCollsions(UPrimitiveComponent* PrimitiveComponent);

	void RemoveAllOverlappedComponentMap();
	/** for non-static collisions which need to be updated every tick */ 
	void UpdateOverlappedComponent(UPrimitiveComponent* PrimComp, FApexClothCollisionInfo* Info);

	void ReleaseAllParentCollisions();
	void ReleaseAllChildrenCollisions();

	void ProcessClothCollisionWithEnvironment();
	/** copy parent's cloth collisions to attached children, where parent means this component */
	void CopyClothCollisionsToChildren();
	/** copy children's cloth collisions to parent, where parent means this component */
	void CopyChildrenClothCollisionsToParent();

	/**
  	 * Get collision data from a static mesh only for collision with clothes.
	 * Returns false when failed to get cloth collision data
	*/
	bool GetClothCollisionDataFromStaticMesh(UPrimitiveComponent* PrimComp, TArray<FClothCollisionPrimitive>& ClothCollisionPrimitives);
	/** find if this component has collisions for clothing and return the results calculated by bone transforms */
	void FindClothCollisions(TArray<FApexClothCollisionVolumeData>& OutCollisions);
	/** create Apex clothing collisions from input collision info and add them to clothing actors */
	void CreateInternalClothCollisions(TArray<FApexClothCollisionVolumeData>& InCollisions, TArray<nvidia::apex::ClothingCollision*>& OutCollisions);

#endif // WITH_CLOTH_COLLISION_DETECTION


#endif// #if WITH_APEX_CLOTHING
	bool IsAnimBlueprintInstanced() const;

	/** Debug render skeletons bones to supplied canvas */
	void DebugDrawBones(class UCanvas* Canvas, bool bSimpleBones) const;

protected:
	bool NeedToSpawnAnimScriptInstance() const;
	bool NeedToSpawnPostPhysicsInstance() const;

private:

	FSkeletalMeshComponentEndPhysicsTickFunction EndPhysicsTickFunction;
	friend struct FSkeletalMeshComponentEndPhysicsTickFunction;

	/** Update systems after physics sim is done */
	void EndPhysicsTickComponent(FSkeletalMeshComponentEndPhysicsTickFunction& ThisTickFunction);

	/** Evaluate Anim System **/
	void EvaluateAnimation(const USkeletalMesh* InSkeletalMesh, UAnimInstance* InAnimInstance, TArray<FTransform>& OutBoneSpaceTransforms, FVector& OutRootBoneTranslation, FBlendedHeapCurve& OutCurve) const;

	/**
	* Take the BoneSpaceTransforms array (translation vector, rotation quaternion and scale vector) and update the array of component-space bone transformation matrices (ComponentSpaceTransforms).
	* It will work down hierarchy multiplying the component-space transform of the parent by the relative transform of the child.
	* This code also applies any per-bone rotators etc. as part of the composition process
	*/
	void FillComponentSpaceTransforms(const USkeletalMesh* InSkeletalMesh, const TArray<FTransform>& InBoneSpaceTransforms, TArray<FTransform>& OutComponentSpaceTransforms) const;

	void RenderAxisGizmo(const FTransform& Transform, class UCanvas* Canvas) const;

	bool ShouldBlendPhysicsBones() const;
	bool DoAnyPhysicsBodiesHaveWeight() const;

	void ClearAnimScriptInstance();
	virtual void RefreshMorphTargets() override;

#if WITH_APEX_CLOTHING
	void GetWindForCloth_GameThread(FVector& WindVector, float& WindAdaption) const;
	static void ApplyWindForCloth_Concurrent(nvidia::apex::ClothingActor& ClothingActor, const FVector& WindVector, float WindAdaption);
#endif
	
	//Data for parallel evaluation of animation
	FAnimationEvaluationContext AnimEvaluationContext;

	// Reference to our current parallel animation evaluation task (if there is one)
	FGraphEventRef				ParallelAnimationEvaluationTask;

	// Reference to our current blend physics task (if there is one)
	FGraphEventRef				ParallelBlendPhysicsCompletionTask;

public:
	// Parallel evaluation wrappers
	void ParallelAnimationEvaluation();
	void CompleteParallelAnimationEvaluation(bool bDoPostAnimEvaluation);


	// Returns whether we are currently trying to run a parallel animation evaluation task
	bool IsRunningParallelEvaluation() const { return IsValidRef(ParallelAnimationEvaluationTask); }

	// Management function for if we want to do an evaluation but may already be running one
	// bBlockOnTask - if true and we are currently performing parallel eval we wait for it to finish
	// bPerformPostAnimEvaluation - if true and we are currently performing parallel eval we call PostAnimEvaluation too
	// return true if parallel task was running.
	bool HandleExistingParallelEvaluationTask(bool bBlockOnTask, bool bPerformPostAnimEvaluation);

	friend class FSkeletalMeshComponentDetails;

	/** Apply animation curves to this component */
	void ApplyAnimationCurvesToComponent(const TMap<FName, float>* InMaterialParameterCurves, const TMap<FName, float>* InAnimationMorphCurves);
	
protected:

	// Returns whether we need to run the Cloth Tick or not
	virtual bool ShouldRunClothTick() const;

	// Returns whether we're able to run a simulation (ignoring the suspend flag)
	bool CanSimulateClothing() const;

private:
	/** Override USkinnedMeshComponent */
	virtual void AddSlavePoseComponent(USkinnedMeshComponent* SkinnedMeshComponent) override;

	// Returns whether we need to run the Pre Cloth Tick or not
	bool ShouldRunEndPhysicsTick() const;

	// Handles registering/unregistering the pre cloth tick as it is needed
	void UpdateEndPhysicsTickRegisteredState();

	// Handles registering/unregistering the cloth tick as it is needed
	void UpdateClothTickRegisteredState();

	// Handles registering/unregistering the 'during animation' tick as it is needed
	void UpdateDuringAnimationTickRegisteredState();

	friend class FParallelBlendPhysicsTask;
	
	//wrapper for parallel blend physics
	void ParallelBlendPhysics() { PerformBlendPhysicsBones(RequiredBones, AnimEvaluationContext.BoneSpaceTransforms); }

	void PerformBlendPhysicsBones(const TArray<FBoneIndexType>& InRequiredBones, TArray<FTransform>& InBoneSpaceTransforms);

	friend class FParallelClothTask;
	// This is the parallel function that updates the cloth data and runs the simulation. This is safe to call from worker threads.
	static void ParallelEvaluateCloth(float DeltaTime, const FClothingActor& ClothingActor, const FClothSimulationContext& ClothSimulationContext);

	friend class FParallelBlendPhysicsCompletionTask;
	void CompleteParallelBlendPhysics();
	void PostBlendPhysics();

#if WITH_APEX_CLOTHING
	/** See UpdateClothTransform for documentation. */
	void UpdateClothTransformImp();
#endif

	friend class FTickClothingTask;

	// these are deprecated variables from removing SingleAnimSkeletalComponent
	// remove if this version goes away : VER_UE4_REMOVE_SINGLENODEINSTANCE
	// deprecated variable to be re-save
	UPROPERTY()
	class UAnimSequence* SequenceToPlay_DEPRECATED;

	// The default sequence to play on this skeletal mesh
	UPROPERTY()
	class UAnimationAsset* AnimToPlay_DEPRECATED;

	// Default setting for looping for SequenceToPlay. This is not current state of looping.
	UPROPERTY()
	uint32 bDefaultLooping_DEPRECATED:1;

	// Default setting for playing for SequenceToPlay. This is not current state of playing.
	UPROPERTY()
	uint32 bDefaultPlaying_DEPRECATED:1;

	// Default setting for position of SequenceToPlay to play. 
	UPROPERTY()
	float DefaultPosition_DEPRECATED;

	// Default setting for playrate of SequenceToPlay to play. 
	UPROPERTY()
	float DefaultPlayRate_DEPRECATED;
	

	/*
	 * Update MorphTargetCurves from mesh - these are not animation curves, but SetMorphTarget and similar functions that can set to this mesh component
	 */
	void UpdateMorphTargetOverrideCurves();
	/*
	 * Reset MorphTarget Curves - Reset all morphtarget curves
	 */
	void ResetMorphTargetCurves();

public:
	/** Keep track of when animation has been ticked to ensure it is ticked only once per frame. */
	UPROPERTY(Transient)
	float LastPoseTickTime;

	/** Checked whether we have already ticked the pose this frame */
	bool PoseTickedThisFrame() const;

	/** Take extracted RootMotion and convert it from local space to world space. */
	FTransform ConvertLocalRootMotionToWorld(const FTransform& InTransform);

	FRootMotionMovementParams ConsumeRootMotion();

private:

#if WITH_EDITOR
	/** This is required for recording animations, so save for editor only */
	/** Temporary array of curve arrays that are active on this component - keeps same buffer index as SpaceBases - Please check SkinnedMeshComponent*/
	FBlendedHeapCurve	CurvesArray[2];
public: 
	/** Access Curve Array for reading */
	const FBlendedHeapCurve& GetAnimationCurves() const { return CurvesArray[CurrentReadComponentTransforms]; }

	/** Get Access to the current editable Curve Array - uses same buffer as space bases*/
	FBlendedHeapCurve& GetEditableAnimationCurves() { return CurvesArray[CurrentEditableComponentTransforms]; }
	const FBlendedHeapCurve& GetEditableAnimationCurves() const { return CurvesArray[CurrentEditableComponentTransforms]; }
#endif 

public:
	/** Skeletal mesh component should not be able to have its mobility set to static */
	virtual const bool CanHaveStaticMobility() const override { return false; }

public:
	/** Register/Unregister for physics state creation callback */
	FDelegateHandle RegisterOnPhysicsCreatedDelegate(const FOnSkelMeshPhysicsCreated& Delegate);
	void UnregisterOnPhysicsCreatedDelegate(const FDelegateHandle& DelegateHandle);

private:

	/** Multicaster fired when this component creates physics state (in case external objects rely on physics state)*/
	FOnSkelMeshPhysicsCreatedMultiCast OnSkelMeshPhysicsCreated;

	/** Mark current anim UID version to up-to-date. Called when it's recalcualted */
	void MarkRequiredCurveUpToDate();
	
	/* This will check if the required curves are up-to-date by checking version number with skeleton. 
	 * Skeleton's curve list changes whenever newer is added or deleted. 
	 * This still has to happen in editor as well as in game as 
	 * There is no guarantee of Skeleton having all curves as we've seen over and over again. 
	 * Cooking does not guarantee skeleton containing all names
	 */
	bool AreRequiredCurvesUpToDate() const;
};
