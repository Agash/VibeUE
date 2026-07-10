// Copyright (c) 2026 Agash. Licensed under the MIT License.
// Contributed to VibeUE (github.com/kevinpbuckley/VibeUE).

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "USmartObjectService.generated.h"

/**
 * Snapshot of a SmartObjectDefinition asset, returned by GetSmartObjectInfo.
 */
USTRUCT(BlueprintType)
struct FSmartObjectDefinitionInfo
{
	GENERATED_BODY()

	/** Content path of the SmartObjectDefinition asset. */
	UPROPERTY(BlueprintReadWrite, Category = "SmartObject")
	FString AssetPath;

	/** Number of interaction slots defined on the object. */
	UPROPERTY(BlueprintReadWrite, Category = "SmartObject")
	int32 NumSlots = 0;
};

/**
 * Authoring and introspection for UE 5.8 Smart Objects — the data-driven interaction points an AI
 * uses to "use" world objects (a cat sitting on a couch, lying on a bed, taking the performer seat).
 * Covers the SmartObjectDefinition asset (create, save), its interaction slots (add, list, rename,
 * enable/disable, activity-tag CRUD), and attaching a SmartObjectComponent that references a
 * definition onto an actor Blueprint. Exposed to Python as unreal.SmartObjectService and as
 * AI-callable tools.
 *
 * Note: the engine's SmartObjectDefinition exposes no public slot-removal API (slots can be added
 * and edited but not removed programmatically); RemoveSlot is therefore intentionally absent.
 */
UCLASS(BlueprintType)
class VIBEUE_API USmartObjectService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	// ---- Definition asset -------------------------------------------------------------------------

	/**
	 * Lists all SmartObjectDefinition assets under a content directory.
	 * @param DirectoryPath Content root to search (recursive).
	 * @return Asset paths of every SmartObjectDefinition found.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|SmartObject")
	static TArray<FString> ListSmartObjectDefinitions(const FString& DirectoryPath = TEXT("/Game"));

	/**
	 * Creates a new SmartObjectDefinition asset.
	 * @param AssetPath Full content path for the new asset (e.g. "/Game/AI/SOD_Couch").
	 * @return True on success. False if the path is invalid or an asset already exists there.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|SmartObject")
	static bool CreateSmartObjectDefinition(const FString& AssetPath);

	/**
	 * Reads structural info about a SmartObjectDefinition.
	 * @param AssetPath Content path of the SmartObjectDefinition.
	 * @param OutInfo Receives the slot count.
	 * @return True if the asset was found.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|SmartObject")
	static bool GetSmartObjectInfo(const FString& AssetPath, FSmartObjectDefinitionInfo& OutInfo);

	// ---- Slots ------------------------------------------------------------------------------------

	/**
	 * Appends an interaction slot to the definition.
	 * @param AssetPath Content path of the SmartObjectDefinition.
	 * @param SlotName Name for the new slot (e.g. "Sit").
	 * @return Index of the new slot, or -1 on failure.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|SmartObject")
	static int32 AddSlot(const FString& AssetPath, const FString& SlotName);

	/**
	 * Lists the definition's slots as "Name [enabled|disabled] tags=<...>".
	 * @param AssetPath Content path of the SmartObjectDefinition.
	 * @return One entry per slot, in slot-index order.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|SmartObject")
	static TArray<FString> ListSlots(const FString& AssetPath);

	/**
	 * Renames a slot.
	 * @param AssetPath Content path of the SmartObjectDefinition.
	 * @param SlotIndex Slot to rename.
	 * @param SlotName New slot name.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|SmartObject")
	static bool SetSlotName(const FString& AssetPath, int32 SlotIndex, const FString& SlotName);

	/**
	 * Enables or disables a slot (disabled slots are not offered to users).
	 * @param AssetPath Content path of the SmartObjectDefinition.
	 * @param SlotIndex Slot to toggle.
	 * @param bEnabled True to enable.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|SmartObject")
	static bool SetSlotEnabled(const FString& AssetPath, int32 SlotIndex, bool bEnabled);

	/**
	 * Adds an activity gameplay tag to a slot (activity tags describe what the slot affords, e.g.
	 * "SmartObject.Activity.Sit"). The tag must already exist in the project's tag table.
	 * @param AssetPath Content path of the SmartObjectDefinition.
	 * @param SlotIndex Slot to tag.
	 * @param TagName Full gameplay-tag name.
	 * @return True on success. False if the tag is not registered or indices are invalid.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|SmartObject")
	static bool AddSlotActivityTag(const FString& AssetPath, int32 SlotIndex, const FString& TagName);

	/**
	 * Clears all activity tags from a slot.
	 * @param AssetPath Content path of the SmartObjectDefinition.
	 * @param SlotIndex Slot to clear.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|SmartObject")
	static bool ClearSlotActivityTags(const FString& AssetPath, int32 SlotIndex);

	// ---- Component wiring -------------------------------------------------------------------------

	/**
	 * Adds a SmartObjectComponent to an actor Blueprint's construction script (idempotent), turning
	 * that actor into a usable smart object.
	 * @param BlueprintPath Content path of the actor Blueprint (e.g. "/Game/Props/BP_Couch").
	 * @param ComponentName Variable name for the component.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|SmartObject")
	static bool AddSmartObjectComponent(const FString& BlueprintPath, const FString& ComponentName = TEXT("SmartObject"));

	/**
	 * Points a Blueprint's SmartObjectComponent at a SmartObjectDefinition asset.
	 * @param BlueprintPath Content path of the actor Blueprint.
	 * @param ComponentName Variable name of the SmartObjectComponent.
	 * @param DefinitionAssetPath Content path of the SmartObjectDefinition to assign.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|SmartObject")
	static bool SetComponentDefinition(const FString& BlueprintPath, const FString& ComponentName, const FString& DefinitionAssetPath);

	// ---- Save -------------------------------------------------------------------------------------

	/**
	 * Saves the SmartObjectDefinition asset to disk.
	 * @param AssetPath Content path of the SmartObjectDefinition.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|SmartObject")
	static bool SaveSmartObjectDefinition(const FString& AssetPath);
};
