#include "GameProcess.h"
#include "GameObjects.h"

#include "commands_animation.h"
#include "SafeWrite.h"
#include "utility.h"

BSAnimGroupSequence* __fastcall HandleAnimationChange(AnimData* animData, void* edx, BSAnimGroupSequence* toMorph, UInt32 animGroupId, UInt32 sequenceId)
{
	if (animData && animData->actor)
	{
		auto* weaponInfo = animData->actor->baseProcess->GetWeaponInfo();
		const auto firstPerson = animData == (*g_thePlayer)->firstPersonAnimData;
		
		if (weaponInfo && weaponInfo->weapon)
		{
			if (auto* anim = GetWeaponAnimation(weaponInfo->weapon, animGroupId, firstPerson, animData))
			{
				anim->animGroup->groupID = animGroupId;
				toMorph = anim;
				return GameFuncs::MorphToSequence(animData, toMorph, animGroupId, sequenceId);
			}
		}
		// NPCs animGroupId contains 0x8000 for some reason
		const auto actorAnimGroupId = animGroupId & 0xFFF;
		if (auto* actorAnim = GetActorAnimation(animData->actor, actorAnimGroupId, firstPerson, animData, toMorph ? toMorph->sequenceName : nullptr))
		{
			toMorph = actorAnim;
		}
	}

	return GameFuncs::MorphToSequence(animData, toMorph, animGroupId, sequenceId);
}

void LoadFileAnimPaths();
bool DeferredInitHandler()
{
	static bool called = false;

	if (called)
	{
		LoadFileAnimPaths();
		SafeWriteBuf(0x6EDC35, "\xA0\x3C\x61\x07\x01", 5);
	}

	called = true;
	return *(bool*)0x107613C;
}

void ApplyHooks()
{
	for (UInt32 patchAddr : {0x460B69, 0x460B89, 0x45FC92, 0x45FD16})
	{
		WriteRelCall(patchAddr, UInt32(HandleAnimationChange));
	}

	// add deferred init
	WriteRelCall(0x6EDC35, UInt32(DeferredInitHandler));
}