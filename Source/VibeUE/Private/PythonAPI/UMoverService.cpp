// Copyright (c) 2026 Agash. Licensed under the MIT License.
// Contributed to VibeUE (github.com/kevinpbuckley/VibeUE).

#include "PythonAPI/UMoverService.h"

#include "MoverComponent.h"
#include "MovementMode.h"
#include "MovementModeTransition.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EditorAssetLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogMoverService, Log, All);

namespace
{
	/** Resolve a UClass from a short name ("WalkingMode") or object path ("/Script/Mover.WalkingMode"). */
	UClass* ResolveClassByName(const FString& In)
	{
		if (In.IsEmpty())
		{
			return nullptr;
		}
		if (UClass* Loaded = LoadObject<UClass>(nullptr, *In))
		{
			return Loaded;
		}
		return UClass::TryFindTypeSlow<UClass>(In);
	}

	UBlueprint* LoadBlueprint(const FString& Path, const TCHAR* Op)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path);
		if (!Blueprint)
		{
			UE_LOG(LogMoverService, Warning, TEXT("%s: Blueprint not found: %s"), Op, *Path);
		}
		return Blueprint;
	}

	/** Find a Mover component template on a Blueprint's construction script by SCS variable name. */
	UMoverComponent* FindMoverTemplate(UBlueprint* Blueprint, const FString& ComponentName, const TCHAR* Op)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript)
		{
			UE_LOG(LogMoverService, Warning, TEXT("%s: Blueprint has no construction script"), Op);
			return nullptr;
		}
		const FName Wanted(*ComponentName);
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName() == Wanted)
			{
				UMoverComponent* Mover = Cast<UMoverComponent>(Node->ComponentTemplate);
				if (!Mover)
				{
					UE_LOG(LogMoverService, Warning, TEXT("%s: Component '%s' is not a Mover component"), Op, *ComponentName);
				}
				return Mover;
			}
		}
		UE_LOG(LogMoverService, Warning, TEXT("%s: Component '%s' not found on %s"), Op, *ComponentName, *Blueprint->GetName());
		return nullptr;
	}
}

// ---- Component --------------------------------------------------------------------------------

bool UMoverService::AddMoverComponent(const FString& BlueprintPath, const FString& ComponentName, const FString& ComponentClass)
{
	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath, TEXT("AddMoverComponent"));
	if (!Blueprint)
	{
		return false;
	}
	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS)
	{
		UE_LOG(LogMoverService, Warning, TEXT("AddMoverComponent: Blueprint has no construction script: %s"), *BlueprintPath);
		return false;
	}

	const FName Wanted(*ComponentName);
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (Node && Node->GetVariableName() == Wanted)
		{
			if (Cast<UMoverComponent>(Node->ComponentTemplate))
			{
				UE_LOG(LogMoverService, Log, TEXT("AddMoverComponent: '%s' already exists on %s"), *ComponentName, *BlueprintPath);
				return true;
			}
			UE_LOG(LogMoverService, Warning, TEXT("AddMoverComponent: '%s' exists but is not a Mover component"), *ComponentName);
			return false;
		}
	}

	UClass* CompClass = ResolveClassByName(ComponentClass);
	if (!CompClass || !CompClass->IsChildOf(UMoverComponent::StaticClass()) || CompClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogMoverService, Warning, TEXT("AddMoverComponent: '%s' is not a concrete Mover component class"), *ComponentClass);
		return false;
	}

	USCS_Node* NewNode = SCS->CreateNode(CompClass, Wanted);
	if (!NewNode)
	{
		UE_LOG(LogMoverService, Warning, TEXT("AddMoverComponent: Failed to create SCS node"));
		return false;
	}
	SCS->AddNode(NewNode);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	UE_LOG(LogMoverService, Log, TEXT("AddMoverComponent: Added '%s' (%s) to %s"), *ComponentName, *CompClass->GetName(), *BlueprintPath);
	return true;
}

bool UMoverService::GetMoverInfo(const FString& BlueprintPath, const FString& ComponentName, FMoverComponentInfo& OutInfo)
{
	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath, TEXT("GetMoverInfo"));
	if (!Blueprint)
	{
		return false;
	}
	UMoverComponent* Mover = FindMoverTemplate(Blueprint, ComponentName, TEXT("GetMoverInfo"));
	if (!Mover)
	{
		return false;
	}

	OutInfo.ComponentName = ComponentName;
	OutInfo.ComponentClass = Mover->GetClass()->GetName();
	OutInfo.NumModes = Mover->MovementModes.Num();
	OutInfo.StartingMode = Mover->StartingMovementMode.ToString();
	OutInfo.NumTransitions = Mover->Transitions.Num();
	return true;
}

// ---- Movement modes ---------------------------------------------------------------------------

TArray<FString> UMoverService::ListMovementModes(const FString& BlueprintPath, const FString& ComponentName)
{
	TArray<FString> Result;
	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath, TEXT("ListMovementModes"));
	if (!Blueprint)
	{
		return Result;
	}
	UMoverComponent* Mover = FindMoverTemplate(Blueprint, ComponentName, TEXT("ListMovementModes"));
	if (!Mover)
	{
		return Result;
	}
	for (const TPair<FName, TObjectPtr<UBaseMovementMode>>& Pair : Mover->MovementModes)
	{
		const FString ClassName = Pair.Value ? Pair.Value->GetClass()->GetName() : TEXT("<null>");
		Result.Add(FString::Printf(TEXT("%s = %s"), *Pair.Key.ToString(), *ClassName));
	}
	return Result;
}

bool UMoverService::AddMovementMode(const FString& BlueprintPath, const FString& ComponentName, const FString& ModeName, const FString& ModeClass)
{
	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath, TEXT("AddMovementMode"));
	if (!Blueprint)
	{
		return false;
	}
	UMoverComponent* Mover = FindMoverTemplate(Blueprint, ComponentName, TEXT("AddMovementMode"));
	if (!Mover)
	{
		return false;
	}
	if (ModeName.IsEmpty())
	{
		UE_LOG(LogMoverService, Warning, TEXT("AddMovementMode: ModeName is empty"));
		return false;
	}

	UClass* Resolved = ResolveClassByName(ModeClass);
	if (!Resolved || !Resolved->IsChildOf(UBaseMovementMode::StaticClass()) || Resolved->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogMoverService, Warning, TEXT("AddMovementMode: '%s' is not a concrete movement-mode class"), *ModeClass);
		return false;
	}

	UBaseMovementMode* Mode = NewObject<UBaseMovementMode>(Mover, Resolved, NAME_None, RF_Transactional);
	if (!Mode)
	{
		UE_LOG(LogMoverService, Warning, TEXT("AddMovementMode: Failed to instance mode"));
		return false;
	}

	Mover->Modify();
	Mover->MovementModes.Add(FName(*ModeName), Mode);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	UE_LOG(LogMoverService, Log, TEXT("AddMovementMode: %s -> %s (%s)"), *ComponentName, *ModeName, *Resolved->GetName());
	return true;
}

bool UMoverService::RemoveMovementMode(const FString& BlueprintPath, const FString& ComponentName, const FString& ModeName)
{
	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath, TEXT("RemoveMovementMode"));
	if (!Blueprint)
	{
		return false;
	}
	UMoverComponent* Mover = FindMoverTemplate(Blueprint, ComponentName, TEXT("RemoveMovementMode"));
	if (!Mover)
	{
		return false;
	}
	Mover->Modify();
	const int32 Removed = Mover->MovementModes.Remove(FName(*ModeName));
	if (Removed == 0)
	{
		UE_LOG(LogMoverService, Warning, TEXT("RemoveMovementMode: No mode named '%s'"), *ModeName);
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UMoverService::SetStartingMovementMode(const FString& BlueprintPath, const FString& ComponentName, const FString& ModeName)
{
	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath, TEXT("SetStartingMovementMode"));
	if (!Blueprint)
	{
		return false;
	}
	UMoverComponent* Mover = FindMoverTemplate(Blueprint, ComponentName, TEXT("SetStartingMovementMode"));
	if (!Mover)
	{
		return false;
	}
	const FName ModeFName(*ModeName);
	if (!ModeFName.IsNone() && !Mover->MovementModes.Contains(ModeFName))
	{
		UE_LOG(LogMoverService, Warning, TEXT("SetStartingMovementMode: '%s' is not a configured mode"), *ModeName);
		return false;
	}
	Mover->Modify();
	Mover->StartingMovementMode = ModeFName;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

// ---- Transitions ------------------------------------------------------------------------------

TArray<FString> UMoverService::ListTransitions(const FString& BlueprintPath, const FString& ComponentName)
{
	TArray<FString> Result;
	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath, TEXT("ListTransitions"));
	if (!Blueprint)
	{
		return Result;
	}
	UMoverComponent* Mover = FindMoverTemplate(Blueprint, ComponentName, TEXT("ListTransitions"));
	if (!Mover)
	{
		return Result;
	}
	for (const TObjectPtr<UBaseMovementModeTransition>& Transition : Mover->Transitions)
	{
		Result.Add(Transition ? Transition->GetClass()->GetName() : TEXT("<null>"));
	}
	return Result;
}

int32 UMoverService::AddTransition(const FString& BlueprintPath, const FString& ComponentName, const FString& TransitionClass)
{
	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath, TEXT("AddTransition"));
	if (!Blueprint)
	{
		return -1;
	}
	UMoverComponent* Mover = FindMoverTemplate(Blueprint, ComponentName, TEXT("AddTransition"));
	if (!Mover)
	{
		return -1;
	}

	UClass* Resolved = ResolveClassByName(TransitionClass);
	if (!Resolved || !Resolved->IsChildOf(UBaseMovementModeTransition::StaticClass()) || Resolved->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogMoverService, Warning, TEXT("AddTransition: '%s' is not a concrete transition class"), *TransitionClass);
		return -1;
	}

	UBaseMovementModeTransition* Transition = NewObject<UBaseMovementModeTransition>(Mover, Resolved, NAME_None, RF_Transactional);
	if (!Transition)
	{
		UE_LOG(LogMoverService, Warning, TEXT("AddTransition: Failed to instance transition"));
		return -1;
	}

	Mover->Modify();
	const int32 Index = Mover->Transitions.Add(Transition);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	UE_LOG(LogMoverService, Log, TEXT("AddTransition: %s [%d] = %s"), *ComponentName, Index, *Resolved->GetName());
	return Index;
}

bool UMoverService::RemoveTransition(const FString& BlueprintPath, const FString& ComponentName, int32 Index)
{
	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath, TEXT("RemoveTransition"));
	if (!Blueprint)
	{
		return false;
	}
	UMoverComponent* Mover = FindMoverTemplate(Blueprint, ComponentName, TEXT("RemoveTransition"));
	if (!Mover)
	{
		return false;
	}
	if (!Mover->Transitions.IsValidIndex(Index))
	{
		UE_LOG(LogMoverService, Warning, TEXT("RemoveTransition: Invalid index %d (%d transitions)"), Index, Mover->Transitions.Num());
		return false;
	}
	Mover->Modify();
	Mover->Transitions.RemoveAt(Index);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

// ---- Compile & save ---------------------------------------------------------------------------

bool UMoverService::CompileMoverBlueprint(const FString& BlueprintPath)
{
	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath, TEXT("CompileMoverBlueprint"));
	if (!Blueprint)
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return true;
}

bool UMoverService::SaveMoverBlueprint(const FString& BlueprintPath)
{
	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath, TEXT("SaveMoverBlueprint"));
	if (!Blueprint)
	{
		return false;
	}
	return UEditorAssetLibrary::SaveLoadedAsset(Blueprint, /*bOnlyIfIsDirty*/ false);
}
