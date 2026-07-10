// Copyright (c) 2026 Agash. Licensed under the MIT License.
// Contributed to VibeUE (github.com/kevinpbuckley/VibeUE).

#include "PythonAPI/UPoseSearchService.h"

#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchRole.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorAssetLibrary.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_MotionMatching.h"
#include "AnimGraphNode_PoseSearchHistoryCollector.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogPoseSearchService, Log, All);

namespace
{
	/** Create a package for a new asset, validating the path is free. Returns nullptr + logs on failure. */
	UPackage* MakeAssetPackage(const FString& AssetPath, FString& OutAssetName, const TCHAR* Op)
	{
		if (AssetPath.IsEmpty())
		{
			UE_LOG(LogPoseSearchService, Warning, TEXT("%s: AssetPath is empty"), Op);
			return nullptr;
		}
		FString PackagePath;
		if (!AssetPath.Split(TEXT("/"), &PackagePath, &OutAssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd) || OutAssetName.IsEmpty())
		{
			UE_LOG(LogPoseSearchService, Warning, TEXT("%s: invalid asset path: %s"), Op, *AssetPath);
			return nullptr;
		}
		if (FPackageName::DoesPackageExist(AssetPath))
		{
			UE_LOG(LogPoseSearchService, Warning, TEXT("%s: an asset already exists at %s"), Op, *AssetPath);
			return nullptr;
		}
		return CreatePackage(*AssetPath);
	}
}

// ---- Schema -----------------------------------------------------------------------------------

bool UPoseSearchService::CreateSchema(const FString& AssetPath, const FString& SkeletonPath)
{
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		UE_LOG(LogPoseSearchService, Warning, TEXT("CreateSchema: skeleton not found: %s"), *SkeletonPath);
		return false;
	}

	FString AssetName;
	UPackage* Package = MakeAssetPackage(AssetPath, AssetName, TEXT("CreateSchema"));
	if (!Package)
	{
		return false;
	}

	UPoseSearchSchema* Schema = NewObject<UPoseSearchSchema>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	Schema->AddSkeleton(Skeleton);
	Schema->AddDefaultChannels();

	FAssetRegistryModule::AssetCreated(Schema);
	Package->SetDirtyFlag(true);
	Schema->Modify();
	UE_LOG(LogPoseSearchService, Log, TEXT("CreateSchema: created %s for skeleton %s"), *AssetPath, *Skeleton->GetName());
	return true;
}

// ---- Database ---------------------------------------------------------------------------------

bool UPoseSearchService::CreateDatabase(const FString& AssetPath, const FString& SchemaPath)
{
	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
	if (!Schema)
	{
		UE_LOG(LogPoseSearchService, Warning, TEXT("CreateDatabase: schema not found: %s"), *SchemaPath);
		return false;
	}

	FString AssetName;
	UPackage* Package = MakeAssetPackage(AssetPath, AssetName, TEXT("CreateDatabase"));
	if (!Package)
	{
		return false;
	}

	UPoseSearchDatabase* Database = NewObject<UPoseSearchDatabase>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	Database->Schema = Schema;

	FAssetRegistryModule::AssetCreated(Database);
	Package->SetDirtyFlag(true);
	Database->Modify();
	UE_LOG(LogPoseSearchService, Log, TEXT("CreateDatabase: created %s (schema %s)"), *AssetPath, *Schema->GetName());
	return true;
}

bool UPoseSearchService::AddSequenceToDatabase(const FString& DatabasePath, const FString& AnimPath)
{
	UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *DatabasePath);
	if (!Database)
	{
		UE_LOG(LogPoseSearchService, Warning, TEXT("AddSequenceToDatabase: database not found: %s"), *DatabasePath);
		return false;
	}
	UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *AnimPath);
	if (!Anim)
	{
		UE_LOG(LogPoseSearchService, Warning, TEXT("AddSequenceToDatabase: anim not found: %s"), *AnimPath);
		return false;
	}

	FPoseSearchDatabaseAnimationAsset Entry;
	Entry.AnimAsset = Anim;

	Database->Modify();
	Database->AddAnimationAsset(Entry);
	Database->MarkPackageDirty();
	UE_LOG(LogPoseSearchService, Log, TEXT("AddSequenceToDatabase: %s += %s"), *DatabasePath, *Anim->GetName());
	return true;
}

int32 UPoseSearchService::AddSequencesFromDirectory(const FString& DatabasePath, const FString& DirectoryPath, const FString& NameContains, const FString& ExcludeContains)
{
	UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *DatabasePath);
	if (!Database)
	{
		UE_LOG(LogPoseSearchService, Warning, TEXT("AddSequencesFromDirectory: database not found: %s"), *DatabasePath);
		return 0;
	}

	USkeleton* SchemaSkeleton = nullptr;
	if (Database->Schema)
	{
		SchemaSkeleton = Database->Schema->GetSkeleton(UE::PoseSearch::DefaultRole);
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter Filter;
	Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(FName(*DirectoryPath));

	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	int32 Added = 0;
	Database->Modify();
	for (const FAssetData& AssetData : Assets)
	{
		const FString Name = AssetData.AssetName.ToString();
		if (!NameContains.IsEmpty() && !Name.Contains(NameContains))
		{
			continue;
		}
		if (!ExcludeContains.IsEmpty() && Name.Contains(ExcludeContains))
		{
			continue;
		}
		UAnimSequence* Anim = Cast<UAnimSequence>(AssetData.GetAsset());
		if (!Anim)
		{
			continue;
		}
		if (SchemaSkeleton && Anim->GetSkeleton() != SchemaSkeleton)
		{
			continue;
		}
		FPoseSearchDatabaseAnimationAsset Entry;
		Entry.AnimAsset = Anim;
		Database->AddAnimationAsset(Entry);
		++Added;
	}
	if (Added > 0)
	{
		Database->MarkPackageDirty();
	}
	UE_LOG(LogPoseSearchService, Log, TEXT("AddSequencesFromDirectory: %s += %d clips from %s"), *DatabasePath, Added, *DirectoryPath);
	return Added;
}

bool UPoseSearchService::GetDatabaseInfo(const FString& DatabasePath, FPoseSearchDatabaseInfo& OutInfo)
{
	UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *DatabasePath);
	if (!Database)
	{
		UE_LOG(LogPoseSearchService, Warning, TEXT("GetDatabaseInfo: database not found: %s"), *DatabasePath);
		return false;
	}
	OutInfo.AssetPath = DatabasePath;
	OutInfo.SchemaPath = Database->Schema ? Database->Schema->GetPathName() : FString();
	OutInfo.NumAnimationAssets = Database->GetNumAnimationAssets();
	return true;
}

TArray<FString> UPoseSearchService::ListDatabaseAssets(const FString& DatabasePath)
{
	TArray<FString> Result;
	UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *DatabasePath);
	if (!Database)
	{
		return Result;
	}
	const int32 Num = Database->GetNumAnimationAssets();
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const UObject* Asset = Database->GetAnimationAsset(Index);
		Result.Add(Asset ? Asset->GetName() : TEXT("<null>"));
	}
	return Result;
}

// ---- Anim graph nodes -------------------------------------------------------------------------

namespace
{
	UEdGraph* FindAnimGraphByName(UAnimBlueprint* AnimBlueprint, const FString& GraphName)
	{
		TArray<UEdGraph*> AllGraphs;
		AnimBlueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph && Graph->GetName() == GraphName)
			{
				return Graph;
			}
		}
		return nullptr;
	}
}

FString UPoseSearchService::AddMotionMatchingNode(const FString& AnimBlueprintPath, const FString& GraphName, const FString& DatabasePath, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogPoseSearchService, Warning, TEXT("AddMotionMatchingNode: AnimBlueprint not found: %s"), *AnimBlueprintPath);
		return FString();
	}
	UEdGraph* TargetGraph = FindAnimGraphByName(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogPoseSearchService, Warning, TEXT("AddMotionMatchingNode: graph '%s' not found"), *GraphName);
		return FString();
	}

	FGraphNodeCreator<UAnimGraphNode_MotionMatching> NodeCreator(*TargetGraph);
	UAnimGraphNode_MotionMatching* NewNode = NodeCreator.CreateNode();
	NewNode->NodePosX = static_cast<int32>(PosX);
	NewNode->NodePosY = static_cast<int32>(PosY);

	if (!DatabasePath.IsEmpty())
	{
		if (UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *DatabasePath))
		{
			// UAnimGraphNode_MotionMatching::Node is private but reflected — set Node.Database via FProperty.
			if (FStructProperty* NodeProp = FindFProperty<FStructProperty>(NewNode->GetClass(), TEXT("Node")))
			{
				void* NodePtr = NodeProp->ContainerPtrToValuePtr<void>(NewNode);
				if (FObjectPropertyBase* DbProp = FindFProperty<FObjectPropertyBase>(NodeProp->Struct, TEXT("Database")))
				{
					DbProp->SetObjectPropertyValue(DbProp->ContainerPtrToValuePtr<void>(NodePtr), Database);
				}
			}
		}
		else
		{
			UE_LOG(LogPoseSearchService, Warning, TEXT("AddMotionMatchingNode: database not found: %s"), *DatabasePath);
		}
	}

	NodeCreator.Finalize();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogPoseSearchService, Log, TEXT("AddMotionMatchingNode: added to %s/%s (db %s)"), *AnimBlueprintPath, *GraphName, *DatabasePath);
	return NewNode->NodeGuid.ToString();
}

FString UPoseSearchService::AddPoseHistoryNode(const FString& AnimBlueprintPath, const FString& GraphName, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogPoseSearchService, Warning, TEXT("AddPoseHistoryNode: AnimBlueprint not found: %s"), *AnimBlueprintPath);
		return FString();
	}
	UEdGraph* TargetGraph = FindAnimGraphByName(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogPoseSearchService, Warning, TEXT("AddPoseHistoryNode: graph '%s' not found"), *GraphName);
		return FString();
	}

	FGraphNodeCreator<UAnimGraphNode_PoseSearchHistoryCollector> NodeCreator(*TargetGraph);
	UAnimGraphNode_PoseSearchHistoryCollector* NewNode = NodeCreator.CreateNode();
	NewNode->NodePosX = static_cast<int32>(PosX);
	NewNode->NodePosY = static_cast<int32>(PosY);
	NodeCreator.Finalize();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogPoseSearchService, Log, TEXT("AddPoseHistoryNode: added to %s/%s"), *AnimBlueprintPath, *GraphName);
	return NewNode->NodeGuid.ToString();
}

// ---- Save -------------------------------------------------------------------------------------

bool UPoseSearchService::SavePoseSearchAsset(const FString& AssetPath)
{
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		UE_LOG(LogPoseSearchService, Warning, TEXT("SavePoseSearchAsset: not found: %s"), *AssetPath);
		return false;
	}
	return UEditorAssetLibrary::SaveLoadedAsset(Asset, /*bOnlyIfIsDirty*/ false);
}
