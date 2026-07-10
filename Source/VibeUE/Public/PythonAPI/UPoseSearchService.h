// Copyright (c) 2026 Agash. Licensed under the MIT License.
// Contributed to VibeUE (github.com/kevinpbuckley/VibeUE).

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UPoseSearchService.generated.h"

/**
 * Snapshot of a PoseSearch (Motion Matching) database, returned by GetDatabaseInfo.
 */
USTRUCT(BlueprintType)
struct FPoseSearchDatabaseInfo
{
	GENERATED_BODY()

	/** Content path of the database. */
	UPROPERTY(BlueprintReadWrite, Category = "PoseSearch")
	FString AssetPath;

	/** Schema the database uses. */
	UPROPERTY(BlueprintReadWrite, Category = "PoseSearch")
	FString SchemaPath;

	/** Number of animation entries in the database. */
	UPROPERTY(BlueprintReadWrite, Category = "PoseSearch")
	int32 NumAnimationAssets = 0;
};

/**
 * Authoring for UE 5.8 Motion Matching (PoseSearch) — the pose database + schema a Motion Matching
 * anim node searches every tick to pick the best clip+frame from a whole animation set (replacing
 * blendspaces/state machines for locomotion). No Epic toolset exists for this. Exposed to Python as
 * unreal.PoseSearchService and as AI-callable tools.
 *
 * Workflow: CreateSchema (skeleton + default trajectory/pose channels) -> CreateDatabase (references
 * the schema) -> add root-motion animation entries -> the search index builds automatically on use.
 */
UCLASS(BlueprintType)
class VIBEUE_API UPoseSearchService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	// ---- Schema -----------------------------------------------------------------------------------

	/**
	 * Creates a PoseSearch schema for a skeleton, with Epic's default feature channels (trajectory +
	 * pose), matching what the schema factory produces.
	 * @param AssetPath Full content path for the new schema (e.g. "/Game/AI/PSS_Cat").
	 * @param SkeletonPath Content path of the skeleton the database's animations use.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|PoseSearch")
	static bool CreateSchema(const FString& AssetPath, const FString& SkeletonPath);

	// ---- Database ---------------------------------------------------------------------------------

	/**
	 * Creates a PoseSearch database that references a schema.
	 * @param AssetPath Full content path for the new database (e.g. "/Game/AI/PSD_CatLoco").
	 * @param SchemaPath Content path of the schema created with CreateSchema.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|PoseSearch")
	static bool CreateDatabase(const FString& AssetPath, const FString& SchemaPath);

	/**
	 * Adds one AnimSequence as an entry in the database.
	 * @param DatabasePath Content path of the database.
	 * @param AnimPath Content path of the AnimSequence (should have root motion for locomotion matching).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|PoseSearch")
	static bool AddSequenceToDatabase(const FString& DatabasePath, const FString& AnimPath);

	/**
	 * Adds every AnimSequence in a content directory to the database, filtered by name and (by
	 * default) excluding in-place "-IP" twins. Skips clips whose skeleton doesn't match the schema.
	 * @param DatabasePath Content path of the database.
	 * @param DirectoryPath Content directory to scan (recursive).
	 * @param NameContains Only add clips whose name contains this substring (empty = any).
	 * @param ExcludeContains Skip clips whose name contains this substring (default "-IP").
	 * @return Number of clips added.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|PoseSearch")
	static int32 AddSequencesFromDirectory(const FString& DatabasePath, const FString& DirectoryPath, const FString& NameContains = TEXT(""), const FString& ExcludeContains = TEXT("-IP"));

	/**
	 * Reads structural info about a database.
	 * @param DatabasePath Content path of the database.
	 * @param OutInfo Receives the schema path and entry count.
	 * @return True if the asset was found.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|PoseSearch")
	static bool GetDatabaseInfo(const FString& DatabasePath, FPoseSearchDatabaseInfo& OutInfo);

	/**
	 * Lists the animation entries in the database, by asset name.
	 * @param DatabasePath Content path of the database.
	 * @return One name per entry.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|PoseSearch")
	static TArray<FString> ListDatabaseAssets(const FString& DatabasePath);

	// ---- Anim graph nodes -------------------------------------------------------------------------

	/**
	 * Adds a Motion Matching anim node to a graph in an Animation Blueprint, set to search a database.
	 * Wire its Pose input to a Pose History node and connect its result toward the output pose.
	 * @param AnimBlueprintPath Content path of the Animation Blueprint.
	 * @param GraphName Name of the graph to add the node to (e.g. "AnimGraph").
	 * @param DatabasePath Content path of the PoseSearch database the node searches.
	 * @param PosX Node X position in the graph editor.
	 * @param PosY Node Y position in the graph editor.
	 * @return The new node's GUID string, or empty on failure.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|PoseSearch")
	static FString AddMotionMatchingNode(const FString& AnimBlueprintPath, const FString& GraphName, const FString& DatabasePath, float PosX = 0.f, float PosY = 0.f);

	/**
	 * Adds a Pose History (pose search history collector) anim node to a graph — records recent pose
	 * and trajectory so a Motion Matching node can build its query. Insert it as a pass-through before
	 * the Motion Matching node.
	 * @param AnimBlueprintPath Content path of the Animation Blueprint.
	 * @param GraphName Name of the graph to add the node to.
	 * @param PosX Node X position in the graph editor.
	 * @param PosY Node Y position in the graph editor.
	 * @return The new node's GUID string, or empty on failure.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|PoseSearch")
	static FString AddPoseHistoryNode(const FString& AnimBlueprintPath, const FString& GraphName, float PosX = 0.f, float PosY = 0.f);

	/**
	 * One-call Motion Matching locomotion setup for a graph: creates a Motion Matching node (searching
	 * the database) and a Pose History node (with bGenerateTrajectory=true, so no external trajectory
	 * component is needed), then wires Motion Matching -> Pose History -> the graph's output pose,
	 * replacing whatever previously fed the output. Compile the Blueprint afterwards.
	 * @param AnimBlueprintPath Content path of the Animation Blueprint.
	 * @param GraphName Name of the graph to build in (e.g. "AnimGraph").
	 * @param DatabasePath Content path of the PoseSearch database.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|PoseSearch")
	static bool SetupMotionMatchingLocomotion(const FString& AnimBlueprintPath, const FString& GraphName, const FString& DatabasePath);

	// ---- Save -------------------------------------------------------------------------------------

	/**
	 * Saves a PoseSearch schema or database asset to disk.
	 * @param AssetPath Content path of the schema or database.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|PoseSearch")
	static bool SavePoseSearchAsset(const FString& AssetPath);
};
