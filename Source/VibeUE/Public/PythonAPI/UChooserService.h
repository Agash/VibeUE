// Copyright (c) 2026 Agash. Licensed under the MIT License.
// Contributed to VibeUE (github.com/kevinpbuckley/VibeUE).

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UChooserService.generated.h"

/**
 * Structural snapshot of a ChooserTable asset, returned by GetChooserInfo.
 */
USTRUCT(BlueprintType)
struct FChooserTableInfo
{
	GENERATED_BODY()

	/** Content path of the ChooserTable asset. */
	UPROPERTY(BlueprintReadWrite, Category = "Chooser")
	FString AssetPath;

	/** Class the chooser selects instances/subclasses of (e.g. "AnimSequence"), or empty if unset. */
	UPROPERTY(BlueprintReadWrite, Category = "Chooser")
	FString OutputObjectType;

	/** Whether the result is an object instance or a class: "ObjectResult" or "ClassResult". */
	UPROPERTY(BlueprintReadWrite, Category = "Chooser")
	FString ResultType;

	/** Number of input columns (filters) in the table. */
	UPROPERTY(BlueprintReadWrite, Category = "Chooser")
	int32 NumColumns = 0;

	/** Number of result rows in the table. */
	UPROPERTY(BlueprintReadWrite, Category = "Chooser")
	int32 NumRows = 0;
};

/**
 * Authoring and introspection for Chooser tables (data-driven selection assets that pick an
 * output object/class from input columns). Exposed to Python as unreal.ChooserService and as
 * AI-callable tools. Covers table lifecycle and structure; column/row authoring is added
 * incrementally.
 */
UCLASS(BlueprintType)
class VIBEUE_API UChooserService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Lists all ChooserTable assets under a content directory.
	 * @param DirectoryPath Content root to search (recursive).
	 * @return Asset paths of every ChooserTable found.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static TArray<FString> ListChoosers(const FString& DirectoryPath = TEXT("/Game"));

	/**
	 * Creates a ChooserTable asset that selects objects of a given class.
	 * @param AssetPath Full content path for the new asset (e.g. "/Game/AI/CT_CatIdle").
	 * @param OutputObjectType Class the table outputs, by name or object path (e.g. "AnimSequence" or "/Script/Engine.AnimSequence").
	 * @return True on success. False if the path is invalid, the class can't be resolved, or an asset already exists there.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool CreateChooserTable(const FString& AssetPath, const FString& OutputObjectType);

	/**
	 * Reads structural info about a ChooserTable.
	 * @param AssetPath Content path of the ChooserTable.
	 * @param OutInfo Receives the table's output type, result type, and column/row counts.
	 * @return True if the asset was found and read.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool GetChooserInfo(const FString& AssetPath, FChooserTableInfo& OutInfo);
};
