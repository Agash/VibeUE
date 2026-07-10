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

	/** Whether the result is an object instance or a class: "ObjectResult", "ClassResult", or "NoPrimaryResult". */
	UPROPERTY(BlueprintReadWrite, Category = "Chooser")
	FString ResultType;

	/** Number of context parameter bindings (the objects/structs the chooser reads inputs from). */
	UPROPERTY(BlueprintReadWrite, Category = "Chooser")
	int32 NumContextParams = 0;

	/** Number of input columns (filters) in the table. */
	UPROPERTY(BlueprintReadWrite, Category = "Chooser")
	int32 NumColumns = 0;

	/** Number of result rows in the table. */
	UPROPERTY(BlueprintReadWrite, Category = "Chooser")
	int32 NumRows = 0;
};

/**
 * Authoring and introspection for Chooser tables — the data-driven selection assets (UE 5.8
 * experimental Chooser plugin) that filter a set of result rows by input columns and return an
 * output object/class. Exposed to Python as unreal.ChooserService and as AI-callable tools.
 *
 * Covers the full table lifecycle: create/delete, output-type config, context-parameter bindings,
 * Float Range columns (the range-filter column, bound to a context property), result rows mapping
 * to output assets, per-row enable/disable, a fallback result, plus compile + save. Row management
 * is generic (it drives every column's editor row API in lockstep), so adding further column types
 * later only needs a new AddXColumn entry point.
 */
UCLASS(BlueprintType)
class VIBEUE_API UChooserService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	// ---- Table lifecycle ------------------------------------------------------------------------

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
	 * @param OutInfo Receives the table's output type, result type, and context/column/row counts.
	 * @return True if the asset was found and read.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool GetChooserInfo(const FString& AssetPath, FChooserTableInfo& OutInfo);

	/**
	 * Deletes a ChooserTable asset from the content browser.
	 * @param AssetPath Content path of the ChooserTable.
	 * @return True if it existed and was deleted.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool DeleteChooserTable(const FString& AssetPath);

	/**
	 * Sets the table's output class and result kind.
	 * @param AssetPath Content path of the ChooserTable.
	 * @param OutputObjectType Class the table outputs, by name or object path.
	 * @param ResultType One of "ObjectResult", "ClassResult", "NoPrimaryResult".
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool SetChooserOutputType(const FString& AssetPath, const FString& OutputObjectType, const FString& ResultType = TEXT("ObjectResult"));

	// ---- Context parameters (input sources) -----------------------------------------------------

	/**
	 * Adds a class context parameter — the object the chooser reads input properties from at runtime
	 * (e.g. the pawn or anim instance that owns a "Restlessness" float). Column bindings reference
	 * a context parameter by its index.
	 * @param AssetPath Content path of the ChooserTable.
	 * @param ClassName Context object class, by name or object path (e.g. "/Game/AI/BP_Cat.BP_Cat_C" or "Pawn").
	 * @param Direction "Read", "Write", or "ReadWrite".
	 * @return Index of the new context parameter, or -1 on failure.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static int32 AddContextObjectParameter(const FString& AssetPath, const FString& ClassName, const FString& Direction = TEXT("Read"));

	/**
	 * Lists the table's context parameters as human-readable descriptors ("Class: Pawn", "Struct: Vector").
	 * @param AssetPath Content path of the ChooserTable.
	 * @return One descriptor per context parameter, in binding-index order.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static TArray<FString> ListContextParameters(const FString& AssetPath);

	/**
	 * Removes a context parameter by index.
	 * @param AssetPath Content path of the ChooserTable.
	 * @param Index Context-parameter index (as returned by AddContextObjectParameter / ListContextParameters).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool RemoveContextParameter(const FString& AssetPath, int32 Index);

	// ---- Columns (filters) ----------------------------------------------------------------------

	/**
	 * Adds a Float Range column bound to a float context property. Each row then filters on whether
	 * the bound value falls inside the row's [Min, Max] range (set via SetFloatRangeCell). The new
	 * column is sized to the table's current row count.
	 * @param AssetPath Content path of the ChooserTable.
	 * @param PropertyBindingChain Dot-separated property path on the context object (e.g. "Restlessness" or "MovementComponent.Velocity.X").
	 * @param ContextIndex Index of the context parameter this binding reads from (default 0).
	 * @return Index of the new column, or -1 on failure.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static int32 AddFloatRangeColumn(const FString& AssetPath, const FString& PropertyBindingChain, int32 ContextIndex = 0);

	/**
	 * Lists the table's columns as their struct display names (e.g. "FloatRangeColumn").
	 * @param AssetPath Content path of the ChooserTable.
	 * @return One name per column, in column-index order.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static TArray<FString> ListColumns(const FString& AssetPath);

	/**
	 * Removes a column by index (drops its per-row cell data with it).
	 * @param AssetPath Content path of the ChooserTable.
	 * @param Index Column index.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool RemoveColumn(const FString& AssetPath, int32 Index);

	/**
	 * Sets the [Min, Max] cell of a Float Range column for one row.
	 * @param AssetPath Content path of the ChooserTable.
	 * @param ColumnIndex Index of a Float Range column.
	 * @param RowIndex Row whose cell to set.
	 * @param Min Range minimum (ignored when bNoMin).
	 * @param Max Range maximum (ignored when bNoMax).
	 * @param bNoMin Treat the minimum as unbounded (-infinity).
	 * @param bNoMax Treat the maximum as unbounded (+infinity).
	 * @return True on success. False if the column isn't a Float Range column or indices are invalid.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool SetFloatRangeCell(const FString& AssetPath, int32 ColumnIndex, int32 RowIndex, float Min, float Max, bool bNoMin = false, bool bNoMax = false);

	// ---- Rows (results) -------------------------------------------------------------------------

	/**
	 * Appends a result row, extending every column's cell array in lockstep. Assign the row's output
	 * with SetRowResultAsset and its filter cells with the per-column setters.
	 * @param AssetPath Content path of the ChooserTable.
	 * @return Index of the new row, or -1 on failure.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static int32 AddRow(const FString& AssetPath);

	/**
	 * Returns the number of result rows in the table.
	 * @param AssetPath Content path of the ChooserTable.
	 * @return Row count, or -1 if the asset was not found.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static int32 GetRowCount(const FString& AssetPath);

	/**
	 * Removes a row and its cells from every column.
	 * @param AssetPath Content path of the ChooserTable.
	 * @param RowIndex Row to remove.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool RemoveRow(const FString& AssetPath, int32 RowIndex);

	/**
	 * Sets a row's result to a hard reference to an asset (an FAssetChooser result).
	 * @param AssetPath Content path of the ChooserTable.
	 * @param RowIndex Row to set.
	 * @param ResultAssetPath Content path of the asset the row selects (e.g. an AnimSequence).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool SetRowResultAsset(const FString& AssetPath, int32 RowIndex, const FString& ResultAssetPath);

	/**
	 * Enables or disables a row (disabled rows are skipped during evaluation).
	 * @param AssetPath Content path of the ChooserTable.
	 * @param RowIndex Row to toggle.
	 * @param bDisabled True to disable the row.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool SetRowDisabled(const FString& AssetPath, int32 RowIndex, bool bDisabled);

	/**
	 * Sets the fallback result — used when no row passes all filters. Pass an empty path to clear it
	 * (the chooser then returns null in that case).
	 * @param AssetPath Content path of the ChooserTable.
	 * @param ResultAssetPath Content path of the fallback asset, or empty to clear.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool SetFallbackResultAsset(const FString& AssetPath, const FString& ResultAssetPath);

	// ---- Compile & save -------------------------------------------------------------------------

	/**
	 * Recompiles the table's bindings/columns so it can be evaluated in-memory without a reload.
	 * @param AssetPath Content path of the ChooserTable.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool CompileChooserTable(const FString& AssetPath);

	/**
	 * Saves the ChooserTable asset to disk.
	 * @param AssetPath Content path of the ChooserTable.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static bool SaveChooserTable(const FString& AssetPath);

	// ---- Evaluation (verify authored choosers) --------------------------------------------------

	/**
	 * Evaluates the chooser against a context object and returns the selected object (or null). The
	 * table reads its column inputs (e.g. a bound "Restlessness" float) from ContextObject, whose
	 * class must match a context parameter added via AddContextObjectParameter. Intended for
	 * verifying an authored chooser without entering PIE — pass a spawned actor or a class default
	 * object, having set the driving property on it first.
	 * @param AssetPath Content path of the ChooserTable.
	 * @param ContextObject The object the chooser reads its inputs from.
	 * @return The selected object, or null if nothing matched and no fallback is set.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Chooser")
	static UObject* EvaluateChooserForObject(const FString& AssetPath, UObject* ContextObject);
};
