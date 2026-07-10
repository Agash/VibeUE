// Copyright (c) 2026 Agash. Licensed under the MIT License.
// Contributed to VibeUE (github.com/kevinpbuckley/VibeUE).

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UAnimModifierService.generated.h"

/**
 * Applies UE 5.8 Animation Modifiers to AnimSequences — the non-destructive, re-runnable editor
 * operations that bake derived data into a clip (root motion, motion/distance curves, footstep
 * notifies, mirroring, root-bone zeroing, ...). The engine's UAnimationModifier::ApplyToAnimationSequence
 * is not exposed to Python, so this service is the automation entry point, enabling the animation
 * data-prep a motion-matching / distance-matching locomotion pipeline needs.
 *
 * Exposed to Python as unreal.AnimModifierService and as AI-callable tools.
 */
UCLASS(BlueprintType)
class VIBEUE_API UAnimModifierService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Applies an animation modifier of the given class to an AnimSequence, then registers it on the
	 * asset (so it appears in the modifier stack and can be re-applied/reverted). Simple scalar/enum/
	 * name/bool properties can be set from a flat JSON object.
	 * @param AnimPath Content path of the AnimSequence.
	 * @param ModifierClassName Modifier class by name (e.g. "MotionExtractorModifier", "DistanceCurveModifier", "FootstepAnimEventsModifier", "ZeroOutRootBoneModifier").
	 * @param PropsJson Optional flat JSON object of property overrides, e.g. {"BoneName":"pelvis","Axis":"Y","MotionType":"Translation"}.
	 * @return True on success. False if the class can't be resolved, isn't an animation modifier, or the asset isn't found.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|AnimModifier")
	static bool ApplyAnimationModifier(const FString& AnimPath, const FString& ModifierClassName, const FString& PropsJson = TEXT(""));

	/**
	 * Bakes root motion onto an AnimSequence by encoding the trajectory of one or more source bones
	 * (typically the pelvis) into the root bone — the fix for retargeted clips whose forward motion
	 * ended up in the pelvis instead of the root (foot-sliding + snap-back). Uses UEncodeRootBoneModifier
	 * and enables root motion on the clip.
	 * @param AnimPath Content path of the AnimSequence.
	 * @param SourceBone Bone whose motion defines the new root (default "pelvis").
	 * @param OrientationAxis Which local axis of the source bone drives root orientation: "X", "Y", or "Z" (default "Y").
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|AnimModifier")
	static bool EncodeRootMotionFromBone(const FString& AnimPath, const FString& SourceBone = TEXT("pelvis"), const FString& OrientationAxis = TEXT("Y"));

	/**
	 * Lists the animation modifiers currently registered on an AnimSequence, by class name.
	 * @param AnimPath Content path of the AnimSequence.
	 * @return One class name per registered modifier.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|AnimModifier")
	static TArray<FString> ListAppliedModifiers(const FString& AnimPath);

	/**
	 * Saves the AnimSequence asset to disk.
	 * @param AnimPath Content path of the AnimSequence.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|AnimModifier")
	static bool SaveAnimation(const FString& AnimPath);
};
