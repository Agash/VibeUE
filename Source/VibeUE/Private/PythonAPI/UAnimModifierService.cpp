// Copyright (c) 2026 Agash. Licensed under the MIT License.
// Contributed to VibeUE (github.com/kevinpbuckley/VibeUE).

#include "PythonAPI/UAnimModifierService.h"

#include "AnimationModifier.h"
#include "AnimationModifiersAssetUserData.h"
#include "EncodeRootBoneModifier.h"
#include "Animation/AnimSequence.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "EditorAssetLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogAnimModifierService, Log, All);

namespace
{
	UAnimSequence* LoadAnim(const FString& Path, const TCHAR* Op)
	{
		UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *Path);
		if (!Anim)
		{
			UE_LOG(LogAnimModifierService, Warning, TEXT("%s: AnimSequence not found: %s"), Op, *Path);
		}
		return Anim;
	}

	UClass* ResolveModifierClass(const FString& In)
	{
		if (In.IsEmpty())
		{
			return nullptr;
		}
		UClass* Cls = LoadObject<UClass>(nullptr, *In);
		if (!Cls)
		{
			Cls = UClass::TryFindTypeSlow<UClass>(In);
		}
		return (Cls && Cls->IsChildOf(UAnimationModifier::StaticClass()) && !Cls->HasAnyClassFlags(CLASS_Abstract)) ? Cls : nullptr;
	}

	/** Apply a configured modifier instance to an anim and dirty the asset. (UAnimationModifiersAssetUserData's
	 *  AddAnimationModifier is protected, so we bake directly; the derived data persists in the sequence.) */
	bool ApplyModifier(UAnimSequence* Anim, UAnimationModifier* Modifier)
	{
		Anim->Modify();
		Modifier->ApplyToAnimationSequence(Anim);
		Anim->MarkPackageDirty();
		return true;
	}

	/** Set a flat JSON object's fields as simple properties on a UObject. */
	void ImportSimpleProps(UObject* Target, const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Obj.IsValid())
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Obj->Values)
		{
			FProperty* Prop = FindFProperty<FProperty>(Target->GetClass(), *Pair.Key);
			if (!Prop)
			{
				// case-insensitive fallback
				for (TFieldIterator<FProperty> It(Target->GetClass()); It; ++It)
				{
					if (It->GetName().Equals(Pair.Key, ESearchCase::IgnoreCase))
					{
						Prop = *It;
						break;
					}
				}
			}
			if (!Prop)
			{
				UE_LOG(LogAnimModifierService, Warning, TEXT("ApplyAnimationModifier: no property '%s'"), *Pair.Key);
				continue;
			}

			const TSharedPtr<FJsonValue>& Value = Pair.Value;
			FString ValueStr;
			switch (Value->Type)
			{
			case EJson::String:  ValueStr = Value->AsString(); break;
			case EJson::Boolean: ValueStr = Value->AsBool() ? TEXT("true") : TEXT("false"); break;
			case EJson::Number:
			{
				const double D = Value->AsNumber();
				ValueStr = (D == FMath::TruncToDouble(D)) ? FString::Printf(TEXT("%lld"), (int64)D) : FString::SanitizeFloat(D);
				break;
			}
			default: continue;
			}

			void* Addr = Prop->ContainerPtrToValuePtr<void>(Target);
			if (!Prop->ImportText_Direct(*ValueStr, Addr, Target, PPF_None))
			{
				UE_LOG(LogAnimModifierService, Warning, TEXT("ApplyAnimationModifier: failed to set '%s' = %s"), *Pair.Key, *ValueStr);
			}
		}
	}

	EEncodeRootBoneAxis ParseAxis(const FString& In)
	{
		if (In.Equals(TEXT("X"), ESearchCase::IgnoreCase)) return EEncodeRootBoneAxis::X;
		if (In.Equals(TEXT("Z"), ESearchCase::IgnoreCase)) return EEncodeRootBoneAxis::Z;
		return EEncodeRootBoneAxis::Y;
	}
}

bool UAnimModifierService::ApplyAnimationModifier(const FString& AnimPath, const FString& ModifierClassName, const FString& PropsJson)
{
	UAnimSequence* Anim = LoadAnim(AnimPath, TEXT("ApplyAnimationModifier"));
	if (!Anim)
	{
		return false;
	}
	UClass* ModClass = ResolveModifierClass(ModifierClassName);
	if (!ModClass)
	{
		UE_LOG(LogAnimModifierService, Warning, TEXT("ApplyAnimationModifier: '%s' is not a concrete animation modifier class"), *ModifierClassName);
		return false;
	}

	UAnimationModifier* Modifier = NewObject<UAnimationModifier>(Anim, ModClass, NAME_None, RF_Transactional);
	if (!Modifier)
	{
		UE_LOG(LogAnimModifierService, Warning, TEXT("ApplyAnimationModifier: failed to instance modifier"));
		return false;
	}

	if (!PropsJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropsJson);
		if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
		{
			ImportSimpleProps(Modifier, Obj);
		}
		else
		{
			UE_LOG(LogAnimModifierService, Warning, TEXT("ApplyAnimationModifier: PropsJson is not a valid JSON object"));
		}
	}

	ApplyModifier(Anim, Modifier);
	UE_LOG(LogAnimModifierService, Log, TEXT("ApplyAnimationModifier: applied %s to %s"), *ModClass->GetName(), *AnimPath);
	return true;
}

bool UAnimModifierService::EncodeRootMotionFromBone(const FString& AnimPath, const FString& SourceBone, const FString& OrientationAxis)
{
	UAnimSequence* Anim = LoadAnim(AnimPath, TEXT("EncodeRootMotionFromBone"));
	if (!Anim)
	{
		return false;
	}

	UEncodeRootBoneModifier* Modifier = NewObject<UEncodeRootBoneModifier>(Anim, UEncodeRootBoneModifier::StaticClass(), NAME_None, RF_Transactional);

	FEncodeRootBoneWeightedBone PositionBone;
	PositionBone.Bone.BoneName = FName(*SourceBone);
	PositionBone.Weight = 1.f;
	Modifier->WeightedBoneToComputeRootPosition.Add(PositionBone);

	FEncodeRootBoneWeightedBoneAxis OrientationBone;
	OrientationBone.Bone.BoneName = FName(*SourceBone);
	OrientationBone.Weight = 1.f;
	OrientationBone.BoneAxis = ParseAxis(OrientationAxis);
	Modifier->WeightedBoneToComputeRootOrientation.Add(OrientationBone);

	ApplyModifier(Anim, Modifier);

	// Ensure the clip actually drives movement from the freshly-encoded root track.
	Anim->bEnableRootMotion = true;

	UE_LOG(LogAnimModifierService, Log, TEXT("EncodeRootMotionFromBone: %s from bone '%s' (axis %s)"), *AnimPath, *SourceBone, *OrientationAxis);
	return true;
}

TArray<FString> UAnimModifierService::ListAppliedModifiers(const FString& AnimPath)
{
	TArray<FString> Result;
	UAnimSequence* Anim = LoadAnim(AnimPath, TEXT("ListAppliedModifiers"));
	if (!Anim)
	{
		return Result;
	}
	if (const UAnimationModifiersAssetUserData* UserData = Anim->GetAssetUserData<UAnimationModifiersAssetUserData>())
	{
		for (const UAnimationModifier* Modifier : UserData->GetAnimationModifierInstances())
		{
			if (Modifier)
			{
				Result.Add(Modifier->GetClass()->GetName());
			}
		}
	}
	return Result;
}

bool UAnimModifierService::SaveAnimation(const FString& AnimPath)
{
	UAnimSequence* Anim = LoadAnim(AnimPath, TEXT("SaveAnimation"));
	if (!Anim)
	{
		return false;
	}
	return UEditorAssetLibrary::SaveLoadedAsset(Anim, /*bOnlyIfIsDirty*/ false);
}
