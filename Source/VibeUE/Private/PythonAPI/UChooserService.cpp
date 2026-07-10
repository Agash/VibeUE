// Copyright (c) 2026 Agash. Licensed under the MIT License.
// Contributed to VibeUE (github.com/kevinpbuckley/VibeUE).

#include "PythonAPI/UChooserService.h"

#include "Chooser.h"
#include "ChooserFunctionLibrary.h"
#include "FloatRangeColumn.h"
#include "ObjectChooser_Asset.h"
#include "ChooserPropertyAccess.h"
#include "IChooserColumn.h"
#include "IHasContext.h"
#include "StructUtils/InstancedStruct.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorAssetLibrary.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogChooserService, Log, All);

namespace
{
	/** Resolve a UClass from either a short name ("AnimSequence"), an object path ("/Script/Engine.AnimSequence"), or a Blueprint generated class path ("/Game/AI/BP_Cat.BP_Cat_C"). */
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

	/** Load a ChooserTable by content path, logging a uniform warning on failure. */
	UChooserTable* LoadTable(const FString& AssetPath, const TCHAR* Op)
	{
		UChooserTable* Table = LoadObject<UChooserTable>(nullptr, *AssetPath);
		if (!Table)
		{
			UE_LOG(LogChooserService, Warning, TEXT("%s: ChooserTable not found: %s"), Op, *AssetPath);
		}
		return Table;
	}

	/** Parse the context-direction string; defaults to Read on anything unrecognised. */
	EContextObjectDirection ParseDirection(const FString& In)
	{
		if (In.Equals(TEXT("Write"), ESearchCase::IgnoreCase))
		{
			return EContextObjectDirection::Write;
		}
		if (In.Equals(TEXT("ReadWrite"), ESearchCase::IgnoreCase))
		{
			return EContextObjectDirection::ReadWrite;
		}
		return EContextObjectDirection::Read;
	}

	/** Mark a chooser's package dirty after an authoring edit. */
	void MarkDirty(UChooserTable* Table)
	{
		Table->Modify();
		Table->MarkPackageDirty();
	}
}

// ---- Table lifecycle --------------------------------------------------------------------------

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
	UChooserTable* Table = LoadTable(AssetPath, TEXT("GetChooserInfo"));
	if (!Table)
	{
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

	OutInfo.NumContextParams = Table->ContextData.Num();
	OutInfo.NumColumns = Table->ColumnsStructs.Num();
#if WITH_EDITORONLY_DATA
	OutInfo.NumRows = Table->ResultsStructs.Num();
#endif
	return true;
}

bool UChooserService::DeleteChooserTable(const FString& AssetPath)
{
	if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		UE_LOG(LogChooserService, Warning, TEXT("DeleteChooserTable: No asset at %s"), *AssetPath);
		return false;
	}
	const bool bDeleted = UEditorAssetLibrary::DeleteAsset(AssetPath);
	UE_LOG(LogChooserService, Log, TEXT("DeleteChooserTable: %s -> %s"), *AssetPath, bDeleted ? TEXT("deleted") : TEXT("failed"));
	return bDeleted;
}

bool UChooserService::SetChooserOutputType(const FString& AssetPath, const FString& OutputObjectType, const FString& ResultType)
{
	UChooserTable* Table = LoadTable(AssetPath, TEXT("SetChooserOutputType"));
	if (!Table)
	{
		return false;
	}

	UClass* OutClass = ResolveClassByName(OutputObjectType);
	if (!OutClass)
	{
		UE_LOG(LogChooserService, Warning, TEXT("SetChooserOutputType: Could not resolve output object type: %s"), *OutputObjectType);
		return false;
	}

	EObjectChooserResultType Kind = EObjectChooserResultType::ObjectResult;
	if (ResultType.Equals(TEXT("ClassResult"), ESearchCase::IgnoreCase))
	{
		Kind = EObjectChooserResultType::ClassResult;
	}
	else if (ResultType.Equals(TEXT("NoPrimaryResult"), ESearchCase::IgnoreCase))
	{
		Kind = EObjectChooserResultType::NoPrimaryResult;
	}

	MarkDirty(Table);
	Table->OutputObjectType = OutClass;
	Table->ResultType = Kind;
	return true;
}

// ---- Context parameters -----------------------------------------------------------------------

int32 UChooserService::AddContextObjectParameter(const FString& AssetPath, const FString& ClassName, const FString& Direction)
{
	UChooserTable* Table = LoadTable(AssetPath, TEXT("AddContextObjectParameter"));
	if (!Table)
	{
		return -1;
	}

	UClass* ContextClass = ResolveClassByName(ClassName);
	if (!ContextClass)
	{
		UE_LOG(LogChooserService, Warning, TEXT("AddContextObjectParameter: Could not resolve class: %s"), *ClassName);
		return -1;
	}

	FContextObjectTypeClass Ctx;
	Ctx.Class = ContextClass;
	Ctx.Direction = ParseDirection(Direction);

	MarkDirty(Table);
	const int32 Index = Table->ContextData.Add(FInstancedStruct::Make(Ctx));
	UE_LOG(LogChooserService, Log, TEXT("AddContextObjectParameter: %s [%d] = %s"), *AssetPath, Index, *ContextClass->GetName());
	return Index;
}

TArray<FString> UChooserService::ListContextParameters(const FString& AssetPath)
{
	TArray<FString> Result;
	UChooserTable* Table = LoadTable(AssetPath, TEXT("ListContextParameters"));
	if (!Table)
	{
		return Result;
	}

	for (const FInstancedStruct& Entry : Table->ContextData)
	{
		if (const FContextObjectTypeClass* AsClass = Entry.GetPtr<FContextObjectTypeClass>())
		{
			Result.Add(FString::Printf(TEXT("Class: %s"), AsClass->Class ? *AsClass->Class->GetName() : TEXT("<none>")));
		}
		else if (const FContextObjectTypeStruct* AsStruct = Entry.GetPtr<FContextObjectTypeStruct>())
		{
			Result.Add(FString::Printf(TEXT("Struct: %s"), AsStruct->Struct ? *AsStruct->Struct->GetName() : TEXT("<none>")));
		}
		else
		{
			Result.Add(Entry.GetScriptStruct() ? Entry.GetScriptStruct()->GetName() : TEXT("<invalid>"));
		}
	}
	return Result;
}

bool UChooserService::RemoveContextParameter(const FString& AssetPath, int32 Index)
{
	UChooserTable* Table = LoadTable(AssetPath, TEXT("RemoveContextParameter"));
	if (!Table)
	{
		return false;
	}
	if (!Table->ContextData.IsValidIndex(Index))
	{
		UE_LOG(LogChooserService, Warning, TEXT("RemoveContextParameter: Invalid index %d (%d params)"), Index, Table->ContextData.Num());
		return false;
	}
	MarkDirty(Table);
	Table->ContextData.RemoveAt(Index);
	return true;
}

// ---- Columns ----------------------------------------------------------------------------------

int32 UChooserService::AddFloatRangeColumn(const FString& AssetPath, const FString& PropertyBindingChain, int32 ContextIndex)
{
#if WITH_EDITOR
	UChooserTable* Table = LoadTable(AssetPath, TEXT("AddFloatRangeColumn"));
	if (!Table)
	{
		return -1;
	}

	FFloatRangeColumn Column;

	// Bind the column input to a float property on a context object.
	FFloatContextProperty Binding;
	Binding.Binding.ContextIndex = ContextIndex;
	TArray<FString> Parts;
	PropertyBindingChain.ParseIntoArray(Parts, TEXT("."), /*CullEmpty*/ true);
	for (const FString& Part : Parts)
	{
		Binding.Binding.PropertyBindingChain.Add(FName(*Part));
	}
	Column.InputValue.InitializeAs<FFloatContextProperty>(Binding);

	// Size the new column's cells to the table's current row count.
	Column.RowValues.SetNum(Table->ResultsStructs.Num());

	MarkDirty(Table);
	const int32 Index = Table->ColumnsStructs.Add(FInstancedStruct::Make(Column));
	UE_LOG(LogChooserService, Log, TEXT("AddFloatRangeColumn: %s [%d] bound to '%s' (context %d)"), *AssetPath, Index, *PropertyBindingChain, ContextIndex);
	return Index;
#else
	UE_LOG(LogChooserService, Warning, TEXT("AddFloatRangeColumn: editor-only"));
	return -1;
#endif
}

TArray<FString> UChooserService::ListColumns(const FString& AssetPath)
{
	TArray<FString> Result;
	UChooserTable* Table = LoadTable(AssetPath, TEXT("ListColumns"));
	if (!Table)
	{
		return Result;
	}
	for (const FInstancedStruct& Column : Table->ColumnsStructs)
	{
		Result.Add(Column.GetScriptStruct() ? Column.GetScriptStruct()->GetName() : TEXT("<invalid>"));
	}
	return Result;
}

bool UChooserService::RemoveColumn(const FString& AssetPath, int32 Index)
{
	UChooserTable* Table = LoadTable(AssetPath, TEXT("RemoveColumn"));
	if (!Table)
	{
		return false;
	}
	if (!Table->ColumnsStructs.IsValidIndex(Index))
	{
		UE_LOG(LogChooserService, Warning, TEXT("RemoveColumn: Invalid index %d (%d columns)"), Index, Table->ColumnsStructs.Num());
		return false;
	}
	MarkDirty(Table);
	Table->ColumnsStructs.RemoveAt(Index);
	return true;
}

bool UChooserService::SetFloatRangeCell(const FString& AssetPath, int32 ColumnIndex, int32 RowIndex, float Min, float Max, bool bNoMin, bool bNoMax)
{
	UChooserTable* Table = LoadTable(AssetPath, TEXT("SetFloatRangeCell"));
	if (!Table)
	{
		return false;
	}
	if (!Table->ColumnsStructs.IsValidIndex(ColumnIndex))
	{
		UE_LOG(LogChooserService, Warning, TEXT("SetFloatRangeCell: Invalid column %d"), ColumnIndex);
		return false;
	}

	FFloatRangeColumn* Column = Table->ColumnsStructs[ColumnIndex].GetMutablePtr<FFloatRangeColumn>();
	if (!Column)
	{
		UE_LOG(LogChooserService, Warning, TEXT("SetFloatRangeCell: Column %d is not a Float Range column"), ColumnIndex);
		return false;
	}
	if (RowIndex < 0)
	{
		UE_LOG(LogChooserService, Warning, TEXT("SetFloatRangeCell: Invalid row %d"), RowIndex);
		return false;
	}
	if (!Column->RowValues.IsValidIndex(RowIndex))
	{
		Column->RowValues.SetNum(RowIndex + 1);
	}

	MarkDirty(Table);
	FChooserFloatRangeRowData& Cell = Column->RowValues[RowIndex];
	Cell.Min = Min;
	Cell.Max = Max;
	Cell.bNoMin = bNoMin;
	Cell.bNoMax = bNoMax;
	return true;
}

// ---- Rows -------------------------------------------------------------------------------------

int32 UChooserService::AddRow(const FString& AssetPath)
{
#if WITH_EDITORONLY_DATA
	UChooserTable* Table = LoadTable(AssetPath, TEXT("AddRow"));
	if (!Table)
	{
		return -1;
	}

	MarkDirty(Table);
	const int32 NewIndex = Table->ResultsStructs.Add(FInstancedStruct::Make(FAssetChooser()));

	// Keep the disabled-rows array and every column's cell array in lockstep with the row count.
	const int32 NewCount = Table->ResultsStructs.Num();
	Table->DisabledRows.SetNum(NewCount);
#if WITH_EDITOR
	for (FInstancedStruct& ColumnStruct : Table->ColumnsStructs)
	{
		if (FChooserColumnBase* Column = ColumnStruct.GetMutablePtr<FChooserColumnBase>())
		{
			Column->SetNumRows(NewCount);
		}
	}
#endif
	return NewIndex;
#else
	UE_LOG(LogChooserService, Warning, TEXT("AddRow: editor-only"));
	return -1;
#endif
}

int32 UChooserService::GetRowCount(const FString& AssetPath)
{
	UChooserTable* Table = LoadTable(AssetPath, TEXT("GetRowCount"));
	if (!Table)
	{
		return -1;
	}
#if WITH_EDITORONLY_DATA
	return Table->ResultsStructs.Num();
#else
	return -1;
#endif
}

bool UChooserService::RemoveRow(const FString& AssetPath, int32 RowIndex)
{
#if WITH_EDITORONLY_DATA
	UChooserTable* Table = LoadTable(AssetPath, TEXT("RemoveRow"));
	if (!Table)
	{
		return false;
	}
	if (!Table->ResultsStructs.IsValidIndex(RowIndex))
	{
		UE_LOG(LogChooserService, Warning, TEXT("RemoveRow: Invalid row %d (%d rows)"), RowIndex, Table->ResultsStructs.Num());
		return false;
	}

	MarkDirty(Table);
	Table->ResultsStructs.RemoveAt(RowIndex);
	if (Table->DisabledRows.IsValidIndex(RowIndex))
	{
		Table->DisabledRows.RemoveAt(RowIndex);
	}
#if WITH_EDITOR
	int32 RowIndices[1] = { RowIndex };
	TArrayView<int> View(RowIndices, 1);
	for (FInstancedStruct& ColumnStruct : Table->ColumnsStructs)
	{
		if (FChooserColumnBase* Column = ColumnStruct.GetMutablePtr<FChooserColumnBase>())
		{
			Column->DeleteRows(View);
		}
	}
#endif
	return true;
#else
	UE_LOG(LogChooserService, Warning, TEXT("RemoveRow: editor-only"));
	return false;
#endif
}

bool UChooserService::SetRowResultAsset(const FString& AssetPath, int32 RowIndex, const FString& ResultAssetPath)
{
#if WITH_EDITORONLY_DATA
	UChooserTable* Table = LoadTable(AssetPath, TEXT("SetRowResultAsset"));
	if (!Table)
	{
		return false;
	}
	if (!Table->ResultsStructs.IsValidIndex(RowIndex))
	{
		UE_LOG(LogChooserService, Warning, TEXT("SetRowResultAsset: Invalid row %d (%d rows)"), RowIndex, Table->ResultsStructs.Num());
		return false;
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *ResultAssetPath);
	if (!Asset)
	{
		UE_LOG(LogChooserService, Warning, TEXT("SetRowResultAsset: Could not load asset: %s"), *ResultAssetPath);
		return false;
	}

	FAssetChooser Result;
	Result.Asset = Asset;

	MarkDirty(Table);
	Table->ResultsStructs[RowIndex] = FInstancedStruct::Make(Result);
	return true;
#else
	UE_LOG(LogChooserService, Warning, TEXT("SetRowResultAsset: editor-only"));
	return false;
#endif
}

bool UChooserService::SetRowDisabled(const FString& AssetPath, int32 RowIndex, bool bDisabled)
{
#if WITH_EDITORONLY_DATA
	UChooserTable* Table = LoadTable(AssetPath, TEXT("SetRowDisabled"));
	if (!Table)
	{
		return false;
	}
	if (!Table->ResultsStructs.IsValidIndex(RowIndex))
	{
		UE_LOG(LogChooserService, Warning, TEXT("SetRowDisabled: Invalid row %d (%d rows)"), RowIndex, Table->ResultsStructs.Num());
		return false;
	}
	if (Table->DisabledRows.Num() < Table->ResultsStructs.Num())
	{
		Table->DisabledRows.SetNum(Table->ResultsStructs.Num());
	}
	MarkDirty(Table);
	Table->DisabledRows[RowIndex] = bDisabled;
	return true;
#else
	UE_LOG(LogChooserService, Warning, TEXT("SetRowDisabled: editor-only"));
	return false;
#endif
}

bool UChooserService::SetFallbackResultAsset(const FString& AssetPath, const FString& ResultAssetPath)
{
	UChooserTable* Table = LoadTable(AssetPath, TEXT("SetFallbackResultAsset"));
	if (!Table)
	{
		return false;
	}

	MarkDirty(Table);
	if (ResultAssetPath.IsEmpty())
	{
		Table->FallbackResult.Reset();
		return true;
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *ResultAssetPath);
	if (!Asset)
	{
		UE_LOG(LogChooserService, Warning, TEXT("SetFallbackResultAsset: Could not load asset: %s"), *ResultAssetPath);
		return false;
	}

	FAssetChooser Result;
	Result.Asset = Asset;
	Table->FallbackResult = FInstancedStruct::Make(Result);
	return true;
}

// ---- Compile & save ---------------------------------------------------------------------------

bool UChooserService::CompileChooserTable(const FString& AssetPath)
{
	UChooserTable* Table = LoadTable(AssetPath, TEXT("CompileChooserTable"));
	if (!Table)
	{
		return false;
	}
	Table->Compile(true);
	return true;
}

bool UChooserService::SaveChooserTable(const FString& AssetPath)
{
	UChooserTable* Table = LoadTable(AssetPath, TEXT("SaveChooserTable"));
	if (!Table)
	{
		return false;
	}
	return UEditorAssetLibrary::SaveLoadedAsset(Table, /*bOnlyIfIsDirty*/ false);
}

// ---- Evaluation -------------------------------------------------------------------------------

UObject* UChooserService::EvaluateChooserForObject(const FString& AssetPath, UObject* ContextObject)
{
	UChooserTable* Table = LoadTable(AssetPath, TEXT("EvaluateChooserForObject"));
	if (!Table)
	{
		return nullptr;
	}
	if (!ContextObject)
	{
		UE_LOG(LogChooserService, Warning, TEXT("EvaluateChooserForObject: ContextObject is null"));
		return nullptr;
	}
	return UChooserFunctionLibrary::EvaluateChooser(ContextObject, Table, UObject::StaticClass());
}
