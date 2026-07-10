// Copyright (c) 2026 Agash. Licensed under the MIT License.
// Contributed to VibeUE (github.com/kevinpbuckley/VibeUE).

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UMoverService.generated.h"

/**
 * Snapshot of a Mover component's configuration on a Blueprint, returned by GetMoverInfo.
 */
USTRUCT(BlueprintType)
struct FMoverComponentInfo
{
	GENERATED_BODY()

	/** SCS variable name of the Mover component on the Blueprint. */
	UPROPERTY(BlueprintReadWrite, Category = "Mover")
	FString ComponentName;

	/** Concrete component class (e.g. "CharacterMoverComponent"). */
	UPROPERTY(BlueprintReadWrite, Category = "Mover")
	FString ComponentClass;

	/** Number of named movement modes configured on the component. */
	UPROPERTY(BlueprintReadWrite, Category = "Mover")
	int32 NumModes = 0;

	/** Name of the mode the simulation starts in (a key into the modes map). */
	UPROPERTY(BlueprintReadWrite, Category = "Mover")
	FString StartingMode;

	/** Number of mode-transition objects on the component. */
	UPROPERTY(BlueprintReadWrite, Category = "Mover")
	int32 NumTransitions = 0;
};

/**
 * Authoring and introspection for the UE 5.8 experimental Mover / Chaos Mover movement system.
 * Operates on a Mover component *template* inside a Blueprint (so configuration persists and
 * compiles into the asset, unlike the runtime UMoverComponent API), covering: adding the Mover
 * component to a pawn Blueprint; the named movement-modes map (add/remove/list) with the starting
 * mode; and the mode-transition list (add/remove/list); plus compile + save.
 *
 * Movement-mode and transition classes are resolved by name, so both the base Mover set
 * (WalkingMode / FallingMode / FlyingMode / SwimmingMode / NavWalkingMode) and the Chaos Mover set
 * (ChaosWalkingMode / ChaosFallingMode / ...) are usable when their plugins are enabled.
 * Exposed to Python as unreal.MoverService and as AI-callable tools.
 */
UCLASS(BlueprintType)
class VIBEUE_API UMoverService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	// ---- Component --------------------------------------------------------------------------------

	/**
	 * Adds a Mover component to a pawn/actor Blueprint's construction script (idempotent — returns
	 * true if a Mover component of that name already exists).
	 * @param BlueprintPath Content path of the Blueprint (e.g. "/Game/AI/BP_Cat").
	 * @param ComponentName Variable name for the component.
	 * @param ComponentClass Mover component class by name (e.g. "CharacterMoverComponent" or "MoverComponent").
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Mover")
	static bool AddMoverComponent(const FString& BlueprintPath, const FString& ComponentName = TEXT("MoverComponent"), const FString& ComponentClass = TEXT("CharacterMoverComponent"));

	/**
	 * Reads the Mover component's configuration.
	 * @param BlueprintPath Content path of the Blueprint.
	 * @param ComponentName Variable name of the Mover component.
	 * @param OutInfo Receives the component class, mode count, starting mode, and transition count.
	 * @return True if the component was found.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Mover")
	static bool GetMoverInfo(const FString& BlueprintPath, const FString& ComponentName, FMoverComponentInfo& OutInfo);

	// ---- Movement modes ---------------------------------------------------------------------------

	/**
	 * Lists the component's named movement modes as "Name = ClassName".
	 * @param BlueprintPath Content path of the Blueprint.
	 * @param ComponentName Variable name of the Mover component.
	 * @return One entry per configured mode.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Mover")
	static TArray<FString> ListMovementModes(const FString& BlueprintPath, const FString& ComponentName);

	/**
	 * Adds (or replaces) a named movement mode, instancing the given mode class as a subobject of
	 * the component template.
	 * @param BlueprintPath Content path of the Blueprint.
	 * @param ComponentName Variable name of the Mover component.
	 * @param ModeName Key under which to register the mode (e.g. "Walking").
	 * @param ModeClass Movement-mode class by name (e.g. "WalkingMode", "ChaosWalkingMode").
	 * @return True on success. False if the component/class can't be resolved or the class is abstract or not a movement mode.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Mover")
	static bool AddMovementMode(const FString& BlueprintPath, const FString& ComponentName, const FString& ModeName, const FString& ModeClass);

	/**
	 * Removes a named movement mode.
	 * @param BlueprintPath Content path of the Blueprint.
	 * @param ComponentName Variable name of the Mover component.
	 * @param ModeName Key of the mode to remove.
	 * @return True if a mode was removed.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Mover")
	static bool RemoveMovementMode(const FString& BlueprintPath, const FString& ComponentName, const FString& ModeName);

	/**
	 * Sets the mode the simulation starts in. The name must be an existing key in the modes map.
	 * @param BlueprintPath Content path of the Blueprint.
	 * @param ComponentName Variable name of the Mover component.
	 * @param ModeName Key of the starting mode.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Mover")
	static bool SetStartingMovementMode(const FString& BlueprintPath, const FString& ComponentName, const FString& ModeName);

	// ---- Transitions ------------------------------------------------------------------------------

	/**
	 * Lists the component's mode transitions as their class names, in evaluation order.
	 * @param BlueprintPath Content path of the Blueprint.
	 * @param ComponentName Variable name of the Mover component.
	 * @return One class name per transition.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Mover")
	static TArray<FString> ListTransitions(const FString& BlueprintPath, const FString& ComponentName);

	/**
	 * Appends a mode transition, instancing the given transition class as a subobject of the
	 * component template.
	 * @param BlueprintPath Content path of the Blueprint.
	 * @param ComponentName Variable name of the Mover component.
	 * @param TransitionClass Transition class by name (a UBaseMovementModeTransition subclass).
	 * @return Index of the new transition, or -1 on failure.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Mover")
	static int32 AddTransition(const FString& BlueprintPath, const FString& ComponentName, const FString& TransitionClass);

	/**
	 * Removes a transition by index.
	 * @param BlueprintPath Content path of the Blueprint.
	 * @param ComponentName Variable name of the Mover component.
	 * @param Index Transition index.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Mover")
	static bool RemoveTransition(const FString& BlueprintPath, const FString& ComponentName, int32 Index);

	// ---- Compile & save ---------------------------------------------------------------------------

	/**
	 * Compiles the Blueprint so template edits propagate to the generated class.
	 * @param BlueprintPath Content path of the Blueprint.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Mover")
	static bool CompileMoverBlueprint(const FString& BlueprintPath);

	/**
	 * Saves the Blueprint asset to disk.
	 * @param BlueprintPath Content path of the Blueprint.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Mover")
	static bool SaveMoverBlueprint(const FString& BlueprintPath);
};
