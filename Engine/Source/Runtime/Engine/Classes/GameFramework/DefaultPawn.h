// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

//=============================================================================
// DefaultPawns are simple pawns that can fly around the world.
//
//=============================================================================

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Pawn.h"
#include "DefaultPawn.generated.h"

class UInputComponent;
class UPawnMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * DefaultPawn implements a simple Pawn with spherical collision and built-in flying movement.
 * @see UFloatingPawnMovement
 */
UCLASS(config=Game, Blueprintable, BlueprintType)
class ENGINE_API ADefaultPawn : public APawn
{
	GENERATED_UCLASS_BODY()

	// Begin Pawn overrides
	virtual UPawnMovementComponent* GetMovementComponent() const override;
	virtual void SetupPlayerInputComponent(UInputComponent* InInputComponent) override;
	virtual void UpdateNavigationRelevance() override;
	// End Pawn overrides

	/**
	 * Input callback to move forward in local space (or backward if Val is negative).
	 * @param Val Amount of movement in the forward direction (or backward if negative).
	 * @see APawn::AddMovementInput()
	 */
	UFUNCTION(BlueprintCallable, Category="Pawn")
	virtual void MoveForward(float ScaleValue, FVector ActorUpVector, FVector ForwardCam, float AirControl, bool bForce);

	/**
	 * Input callback to strafe right in local space (or left if Val is negative).
	 * @param Val Amount of movement in the right direction (or left if negative).
	 * @see APawn::AddMovementInput()
	 */
	UFUNCTION(BlueprintCallable, Category="Pawn")
	virtual void MoveRight(float Val);

	/**
	 * Input callback to move up in world space (or down if Val is negative).
	 * @param Val Amount of movement in the world up direction (or down if negative).
	 * @see APawn::AddMovementInput()
	 */
	UFUNCTION(BlueprintCallable, Category="Pawn")

		//void MoveForward(float ScaleValue, FVector ActorUpVector, FVector ForwardCam, float AirControl, bool bForce);
	// void MoveForward(float ScaleValue, FVector ActorUpVector, FVector ForwardCam, float AirControl, bool bForce);
	 virtual void MoveUp_World(float Val);

	/**
	 * Called via input to turn at a given rate.
	 * @param Rate	This is a normalized rate, i.e. 1.0 means 100% of desired turn rate
	 */
	UFUNCTION(BlueprintCallable, Category="Pawn")
	void TurnAtRate(float Rate);

	/**
	* Called via input to look up at a given rate (or down if Rate is negative).
	* @param Rate	This is a normalized rate, i.e. 1.0 means 100% of desired turn rate
	*/
	UFUNCTION(BlueprintCallable, Category="Pawn")
	void LookUpAtRate(float Rate);
	
	/** Base turn rate, in deg/sec. Other scaling may affect final turn rate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pawn")
	float BaseTurnRate;

	/** Base lookup rate, in deg/sec. Other scaling may affect final lookup rate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pawn")
	float BaseLookUpRate;

private:
	/** Input callback on pitch (look up) input. */
	UFUNCTION(BlueprintCallable, Category="Pawn", meta=(DeprecatedFunction, DeprecationMessage="Use Pawn.AddControllerPitchInput instead."))
	virtual void LookUp(float Val);

	/** Input callback on yaw (turn) input. */
	UFUNCTION(BlueprintCallable, Category="Pawn", meta=(DeprecatedFunction, DeprecationMessage="Use Pawn.AddControllerYawInput instead."))
	virtual void Turn(float Val);

public:
	/** Name of the MovementComponent.  Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName MovementComponentName;

private:
	/** DefaultPawn movement component */
	UPROPERTY(Category = Pawn, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UPawnMovementComponent* MovementComponent;

public:
	/** Name of the CollisionComponent. */
	static FName CollisionComponentName;

private_subobject:
	/** DefaultPawn collision component */
	DEPRECATED_FORGAME(4.6, "CollisionComponent should not be accessed directly, please use GetCollisionComponent() function instead. CollisionComponent will soon be private and your code will not compile.")
	UPROPERTY(Category = Pawn, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	USphereComponent* CollisionComponent;
public:

	/** Name of the MeshComponent. Use this name if you want to prevent creation of the component (with ObjectInitializer.DoNotCreateDefaultSubobject). */
	static FName MeshComponentName;

private_subobject:
	/** The mesh associated with this Pawn. */
	DEPRECATED_FORGAME(4.6, "MeshComponent should not be accessed directly, please use GetMeshComponent() function instead. MeshComponent will soon be private and your code will not compile.")
	UPROPERTY(Category = Pawn, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* MeshComponent;
public:

	/** If true, adds default input bindings for movement and camera look. */
	UPROPERTY(Category=Pawn, EditAnywhere, BlueprintReadOnly)
	uint32 bAddDefaultMovementBindings:1;

	/** Returns CollisionComponent subobject **/
	USphereComponent* GetCollisionComponent() const;
	/** Returns MeshComponent subobject **/
	UStaticMeshComponent* GetMeshComponent() const;
};

