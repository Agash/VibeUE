// Copyright (c) 2026 Agash. Licensed under the MIT License.
// Contributed to VibeUE (github.com/kevinpbuckley/VibeUE).

#include "PythonAPI/USmartObjectService.h"

#include "SmartObjectDefinition.h"
#include "SmartObjectComponent.h"
#include "GameplayTagsManager.h"
#include "GameplayTagContainer.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "EditorAssetLibrary.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogSmartObjectService, Log, All);

namespace
{
	USmartObjectDefinition* LoadDefinition(const FString& AssetPath, const TCHAR* Op)
	{
		USmartObjectDefinition* Def = LoadObject<USmartObjectDefinition>(nullptr, *AssetPath);
		if (!Def)
		{
			UE_LOG(LogSmartObjectService, Warning, TEXT("%s: SmartObjectDefinition not found: %s"), Op, *AssetPath);
		}
		return Def;
	}

	void MarkDirty(UObject* Obj)
	{
		Obj->Modify();
		Obj->MarkPackageDirty();
	}
}

// ---- Definition asset -------------------------------------------------------------------------

TArray<FString> USmartObjectService::ListSmartObjectDefinitions(const FString& DirectoryPath)
{
	TArray<FString> Result;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(USmartObjectDefinition::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(FName(*DirectoryPath));

	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);
	for (const FAssetData& Asset : Assets)
	{
		Result.Add(Asset.GetObjectPathString());
	}
	return Result;
}

bool USmartObjectService::CreateSmartObjectDefinition(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("CreateSmartObjectDefinition: AssetPath is empty"));
		return false;
	}

	FString PackagePath;
	FString AssetName;
	if (!AssetPath.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd) || AssetName.IsEmpty())
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("CreateSmartObjectDefinition: Invalid asset path: %s"), *AssetPath);
		return false;
	}

	if (FPackageName::DoesPackageExist(AssetPath))
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("CreateSmartObjectDefinition: An asset already exists at %s"), *AssetPath);
		return false;
	}

	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("CreateSmartObjectDefinition: Failed to create package: %s"), *AssetPath);
		return false;
	}

	USmartObjectDefinition* NewDef = NewObject<USmartObjectDefinition>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!NewDef)
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("CreateSmartObjectDefinition: Failed to create USmartObjectDefinition"));
		return false;
	}

	FAssetRegistryModule::AssetCreated(NewDef);
	Package->SetDirtyFlag(true);
	NewDef->Modify();

	UE_LOG(LogSmartObjectService, Log, TEXT("CreateSmartObjectDefinition: Created at %s"), *AssetPath);
	return true;
}

bool USmartObjectService::GetSmartObjectInfo(const FString& AssetPath, FSmartObjectDefinitionInfo& OutInfo)
{
	USmartObjectDefinition* Def = LoadDefinition(AssetPath, TEXT("GetSmartObjectInfo"));
	if (!Def)
	{
		return false;
	}
	OutInfo.AssetPath = AssetPath;
	OutInfo.NumSlots = Def->GetSlots().Num();
	return true;
}

// ---- Slots ------------------------------------------------------------------------------------

int32 USmartObjectService::AddSlot(const FString& AssetPath, const FString& SlotName)
{
	USmartObjectDefinition* Def = LoadDefinition(AssetPath, TEXT("AddSlot"));
	if (!Def)
	{
		return -1;
	}
	MarkDirty(Def);
	FSmartObjectSlotDefinition& Slot = Def->DebugAddSlot();
	Slot.Name = FName(*SlotName);
	const int32 Index = Def->GetSlots().Num() - 1;
	UE_LOG(LogSmartObjectService, Log, TEXT("AddSlot: %s [%d] = %s"), *AssetPath, Index, *SlotName);
	return Index;
}

TArray<FString> USmartObjectService::ListSlots(const FString& AssetPath)
{
	TArray<FString> Result;
	USmartObjectDefinition* Def = LoadDefinition(AssetPath, TEXT("ListSlots"));
	if (!Def)
	{
		return Result;
	}
	for (const FSmartObjectSlotDefinition& Slot : Def->GetSlots())
	{
		const FString Tags = Slot.ActivityTags.IsEmpty() ? TEXT("-") : Slot.ActivityTags.ToStringSimple();
		Result.Add(FString::Printf(TEXT("%s [%s] tags=%s"), *Slot.Name.ToString(), Slot.bEnabled ? TEXT("enabled") : TEXT("disabled"), *Tags));
	}
	return Result;
}

bool USmartObjectService::SetSlotName(const FString& AssetPath, int32 SlotIndex, const FString& SlotName)
{
	USmartObjectDefinition* Def = LoadDefinition(AssetPath, TEXT("SetSlotName"));
	if (!Def)
	{
		return false;
	}
	TArrayView<FSmartObjectSlotDefinition> Slots = Def->GetMutableSlots();
	if (!Slots.IsValidIndex(SlotIndex))
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("SetSlotName: Invalid slot %d (%d slots)"), SlotIndex, Slots.Num());
		return false;
	}
	MarkDirty(Def);
	Slots[SlotIndex].Name = FName(*SlotName);
	return true;
}

bool USmartObjectService::SetSlotEnabled(const FString& AssetPath, int32 SlotIndex, bool bEnabled)
{
	USmartObjectDefinition* Def = LoadDefinition(AssetPath, TEXT("SetSlotEnabled"));
	if (!Def)
	{
		return false;
	}
	TArrayView<FSmartObjectSlotDefinition> Slots = Def->GetMutableSlots();
	if (!Slots.IsValidIndex(SlotIndex))
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("SetSlotEnabled: Invalid slot %d (%d slots)"), SlotIndex, Slots.Num());
		return false;
	}
	MarkDirty(Def);
	Slots[SlotIndex].bEnabled = bEnabled;
	return true;
}

bool USmartObjectService::AddSlotActivityTag(const FString& AssetPath, int32 SlotIndex, const FString& TagName)
{
	USmartObjectDefinition* Def = LoadDefinition(AssetPath, TEXT("AddSlotActivityTag"));
	if (!Def)
	{
		return false;
	}
	TArrayView<FSmartObjectSlotDefinition> Slots = Def->GetMutableSlots();
	if (!Slots.IsValidIndex(SlotIndex))
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("AddSlotActivityTag: Invalid slot %d (%d slots)"), SlotIndex, Slots.Num());
		return false;
	}

	const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagName), /*ErrorIfNotFound*/ false);
	if (!Tag.IsValid())
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("AddSlotActivityTag: Gameplay tag not registered: %s"), *TagName);
		return false;
	}

	MarkDirty(Def);
	Slots[SlotIndex].ActivityTags.AddTag(Tag);
	return true;
}

bool USmartObjectService::ClearSlotActivityTags(const FString& AssetPath, int32 SlotIndex)
{
	USmartObjectDefinition* Def = LoadDefinition(AssetPath, TEXT("ClearSlotActivityTags"));
	if (!Def)
	{
		return false;
	}
	TArrayView<FSmartObjectSlotDefinition> Slots = Def->GetMutableSlots();
	if (!Slots.IsValidIndex(SlotIndex))
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("ClearSlotActivityTags: Invalid slot %d (%d slots)"), SlotIndex, Slots.Num());
		return false;
	}
	MarkDirty(Def);
	Slots[SlotIndex].ActivityTags.Reset();
	return true;
}

// ---- Component wiring -------------------------------------------------------------------------

bool USmartObjectService::AddSmartObjectComponent(const FString& BlueprintPath, const FString& ComponentName)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("AddSmartObjectComponent: Blueprint not found: %s"), *BlueprintPath);
		return false;
	}
	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS)
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("AddSmartObjectComponent: Blueprint has no construction script: %s"), *BlueprintPath);
		return false;
	}

	const FName Wanted(*ComponentName);
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (Node && Node->GetVariableName() == Wanted)
		{
			if (Cast<USmartObjectComponent>(Node->ComponentTemplate))
			{
				UE_LOG(LogSmartObjectService, Log, TEXT("AddSmartObjectComponent: '%s' already exists on %s"), *ComponentName, *BlueprintPath);
				return true;
			}
			UE_LOG(LogSmartObjectService, Warning, TEXT("AddSmartObjectComponent: '%s' exists but is not a SmartObjectComponent"), *ComponentName);
			return false;
		}
	}

	USCS_Node* NewNode = SCS->CreateNode(USmartObjectComponent::StaticClass(), Wanted);
	if (!NewNode)
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("AddSmartObjectComponent: Failed to create SCS node"));
		return false;
	}
	SCS->AddNode(NewNode);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	UE_LOG(LogSmartObjectService, Log, TEXT("AddSmartObjectComponent: Added '%s' to %s"), *ComponentName, *BlueprintPath);
	return true;
}

bool USmartObjectService::SetComponentDefinition(const FString& BlueprintPath, const FString& ComponentName, const FString& DefinitionAssetPath)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		UE_LOG(LogSmartObjectService, Warning, TEXT("SetComponentDefinition: Blueprint not found: %s"), *BlueprintPath);
		return false;
	}

	USmartObjectDefinition* Def = LoadDefinition(DefinitionAssetPath, TEXT("SetComponentDefinition"));
	if (!Def)
	{
		return false;
	}

	const FName Wanted(*ComponentName);
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName() == Wanted)
		{
			USmartObjectComponent* Comp = Cast<USmartObjectComponent>(Node->ComponentTemplate);
			if (!Comp)
			{
				UE_LOG(LogSmartObjectService, Warning, TEXT("SetComponentDefinition: '%s' is not a SmartObjectComponent"), *ComponentName);
				return false;
			}
			Comp->Modify();
			Comp->SetDefinition(Def);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return true;
		}
	}
	UE_LOG(LogSmartObjectService, Warning, TEXT("SetComponentDefinition: Component '%s' not found on %s"), *ComponentName, *BlueprintPath);
	return false;
}

// ---- Save -------------------------------------------------------------------------------------

bool USmartObjectService::SaveSmartObjectDefinition(const FString& AssetPath)
{
	USmartObjectDefinition* Def = LoadDefinition(AssetPath, TEXT("SaveSmartObjectDefinition"));
	if (!Def)
	{
		return false;
	}
	return UEditorAssetLibrary::SaveLoadedAsset(Def, /*bOnlyIfIsDirty*/ false);
}
