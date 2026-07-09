// Copyright (c) 2026 Agash. Licensed under the MIT License.
// Contributed to VibeUE (github.com/kevinpbuckley/VibeUE).

#include "PythonAPI/UChooserService.h"

#include "Chooser.h"
#include "IHasContext.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogChooserService, Log, All);

namespace
{
	/** Resolve a UClass from either a short name ("AnimSequence") or an object path ("/Script/Engine.AnimSequence"). */
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
}

TArray<FString> UChooserService::ListChoosers(const FString& DirectoryPath)
{
	TArray<FString> Result;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UChooserTable::StaticClass()->GetClassPathName());
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

bool UChooserService::CreateChooserTable(const FString& AssetPath, const FString& OutputObjectType)
{
	if (AssetPath.IsEmpty())
	{
		UE_LOG(LogChooserService, Warning, TEXT("CreateChooserTable: AssetPath is empty"));
		return false;
	}

	FString PackagePath;
	FString AssetName;
	if (!AssetPath.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd) || AssetName.IsEmpty())
	{
		UE_LOG(LogChooserService, Warning, TEXT("CreateChooserTable: Invalid asset path: %s"), *AssetPath);
		return false;
	}

	if (FPackageName::DoesPackageExist(AssetPath))
	{
		UE_LOG(LogChooserService, Warning, TEXT("CreateChooserTable: An asset already exists at %s"), *AssetPath);
		return false;
	}

	UClass* OutClass = ResolveClassByName(OutputObjectType);
	if (!OutClass)
	{
		UE_LOG(LogChooserService, Warning, TEXT("CreateChooserTable: Could not resolve output object type: %s"), *OutputObjectType);
		return false;
	}

	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		UE_LOG(LogChooserService, Warning, TEXT("CreateChooserTable: Failed to create package: %s"), *AssetPath);
		return false;
	}

	UChooserTable* NewTable = NewObject<UChooserTable>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!NewTable)
	{
		UE_LOG(LogChooserService, Warning, TEXT("CreateChooserTable: Failed to create UChooserTable object"));
		return false;
	}

	NewTable->OutputObjectType = OutClass;
	NewTable->ResultType = EObjectChooserResultType::ObjectResult;

	FAssetRegistryModule::AssetCreated(NewTable);
	Package->SetDirtyFlag(true);
	NewTable->Modify();

	UE_LOG(LogChooserService, Log, TEXT("CreateChooserTable: Created ChooserTable at %s (output: %s)"), *AssetPath, *OutClass->GetName());
	return true;
}

bool UChooserService::GetChooserInfo(const FString& AssetPath, FChooserTableInfo& OutInfo)
{
	UChooserTable* Table = LoadObject<UChooserTable>(nullptr, *AssetPath);
	if (!Table)
	{
		UE_LOG(LogChooserService, Warning, TEXT("GetChooserInfo: ChooserTable not found: %s"), *AssetPath);
		return false;
	}

	OutInfo.AssetPath = AssetPath;
	OutInfo.OutputObjectType = Table->OutputObjectType ? Table->OutputObjectType->GetName() : FString();

	switch (Table->ResultType)
	{
	case EObjectChooserResultType::ClassResult:
		OutInfo.ResultType = TEXT("ClassResult");
		break;
	case EObjectChooserResultType::NoPrimaryResult:
		OutInfo.ResultType = TEXT("NoPrimaryResult");
		break;
	default:
		OutInfo.ResultType = TEXT("ObjectResult");
		break;
	}

	OutInfo.NumColumns = Table->ColumnsStructs.Num();
#if WITH_EDITORONLY_DATA
	OutInfo.NumRows = Table->ResultsStructs.Num();
#endif
	return true;
}
