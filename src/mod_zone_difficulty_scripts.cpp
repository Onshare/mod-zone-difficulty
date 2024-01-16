/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "Config.h"
#include "Chat.h"
#include "GameTime.h"
#include "ItemTemplate.h"
#include "MapMgr.h"
#include "Pet.h"
#include "Player.h"
#include "PoolMgr.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "StringConvert.h"
#include "TaskScheduler.h"
#include "Tokenize.h"
#include "Unit.h"
#include "ZoneDifficulty.h"

class mod_zone_difficulty_unitscript : public UnitScript
{
public:
    mod_zone_difficulty_unitscript() : UnitScript("mod_zone_difficulty_unitscript") { }

    //修改buff及光环性法术
    void OnAuraApply(Unit* target, Aura* aura) override
    {
        if (!sZoneDifficulty->IsEnabled)
        {
            return;
        }
        if (!sZoneDifficulty->MythicmodeInNormalDungeons && !target->GetMap()->IsRaidOrHeroicDungeon())
        {
            return;
        }
        //玩家、宠物、目标 需要被nerf
        if (sZoneDifficulty->IsValidNerfTarget(target))
        {
            uint32 mapId = target->GetMapId();
            bool nerfInDuel = sZoneDifficulty->ShouldNerfInDuels(target);

            //Check if the map of the target is subject of a nerf at all OR if the target is subject of a nerf in a duel
            if (sZoneDifficulty->NerfInfo.find(mapId) != sZoneDifficulty->NerfInfo.end() || nerfInDuel)
            {
                if (SpellInfo const* spellInfo = aura->GetSpellInfo())
                {
                    // 跳过不手削弱影响的法术（比如药剂）
                    if (spellInfo->HasAttribute(SPELL_ATTR0_NO_IMMUNITIES))
                    {
                        return;
                    }

                    if (spellInfo->HasAura(SPELL_AURA_SCHOOL_ABSORB))
                    {
                        std::list<AuraEffect*> AuraEffectList = target->GetAuraEffectsByType(SPELL_AURA_SCHOOL_ABSORB);

                        for (AuraEffect* eff : AuraEffectList)
                        {
                            if ((eff->GetAuraType() != SPELL_AURA_SCHOOL_ABSORB) || (eff->GetSpellInfo()->Id != spellInfo->Id))
                            {
                                continue;
                            }

                            if (sZoneDifficulty->IsDebugInfoEnabled && target)
                            {
                                if (Player* player = target->ToPlayer()) // Pointless check? Perhaps.
                                {
                                    if (player->GetSession())
                                    {
                                        ChatHandler(player->GetSession()).PSendSysMessage("法术: %s (%u) 基础值: %i", spellInfo->SpellName[player->GetSession()->GetSessionDbcLocale()], spellInfo->Id, eff->GetAmount());
                                    }
                                }
                            }

                            int32 absorb = eff->GetAmount();
                            uint32 phaseMask = target->GetPhaseMask();
                            int matchingPhase = sZoneDifficulty->GetLowestMatchingPhase(mapId, phaseMask);
                            int8 mode = sZoneDifficulty->NerfInfo[mapId][matchingPhase].Enabled;
                            if (matchingPhase != -1)
                            {
                                Map* map = target->GetMap();
                                if (sZoneDifficulty->HasNormalMode(mode))
                                {
                                    absorb = eff->GetAmount() * sZoneDifficulty->NerfInfo[mapId][matchingPhase].AbsorbNerfPct;
                                }
                                if (sZoneDifficulty->HasMythicmode(mode) && sZoneDifficulty->MythicmodeInstanceData[target->GetMap()->GetInstanceId()])
                                {
                                    if (map->IsRaid() ||
                                        (map->IsHeroic() && map->IsDungeon()))
                                    {
                                        absorb = eff->GetAmount() * sZoneDifficulty->NerfInfo[mapId][matchingPhase].AbsorbNerfPctHard;
                                    }
                                }
                            }
                            else if (sZoneDifficulty->NerfInfo[DUEL_INDEX][0].Enabled > 0 && nerfInDuel)
                            {
                                absorb = eff->GetAmount() * sZoneDifficulty->NerfInfo[DUEL_INDEX][0].AbsorbNerfPct;
                            }

                            //This check must be last and override duel and map adjustments
                            if (sZoneDifficulty->SpellNerfOverrides.find(spellInfo->Id) != sZoneDifficulty->SpellNerfOverrides.end())
                            {
                                if (sZoneDifficulty->SpellNerfOverrides[spellInfo->Id].find(mapId) != sZoneDifficulty->SpellNerfOverrides[spellInfo->Id].end())
                                {
                                    // Check if the mode of instance and SpellNerfOverride match 
                                    if (sZoneDifficulty->OverrideModeMatches(target->GetMap()->GetInstanceId(), spellInfo->Id, mapId))
                                    {
                                        absorb = eff->GetAmount() * sZoneDifficulty->SpellNerfOverrides[spellInfo->Id][mapId].NerfPct;
                                    }
                                }
                                else if (sZoneDifficulty->SpellNerfOverrides[spellInfo->Id].find(0) != sZoneDifficulty->SpellNerfOverrides[spellInfo->Id].end())
                                {
                                    if (sZoneDifficulty->OverrideModeMatches(target->GetMap()->GetInstanceId(), spellInfo->Id, mapId))
                                    {
                                        absorb = eff->GetAmount() * sZoneDifficulty->SpellNerfOverrides[spellInfo->Id][0].NerfPct;
                                    }
                                }
                            }

                            eff->SetAmount(absorb);

                            if (sZoneDifficulty->IsDebugInfoEnabled && target)
                            {
                                if (Player* player = target->ToPlayer()) // Pointless check? Perhaps.
                                {
                                    if (player->GetSession())
                                    {
                                        ChatHandler(player->GetSession()).PSendSysMessage("法术: %s (%u) 修正值: %i", spellInfo->SpellName[player->GetSession()->GetSessionDbcLocale()], spellInfo->Id, eff->GetAmount());
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    //修改治疗技能
    void ModifyHealReceived(Unit* target, Unit* /*healer*/, uint32& heal, SpellInfo const* spellInfo) override
    {
        if (!sZoneDifficulty->IsEnabled)
        {
            return;
        }
        if (!sZoneDifficulty->MythicmodeInNormalDungeons && !target->GetMap()->IsRaidOrHeroicDungeon())
        {
            return;
        }

        if (sZoneDifficulty->IsValidNerfTarget(target))
        {
            if (spellInfo)
            {
                if (spellInfo->HasEffect(SPELL_EFFECT_HEALTH_LEECH))
                {
                    return;
                }
                for (auto const& eff : spellInfo->GetEffects())
                {
                    if (eff.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH)
                    {
                        return;
                    }
                }
                // Skip spells not affected by vulnerability (potions) and bandages
                if (spellInfo->HasAttribute(SPELL_ATTR0_NO_IMMUNITIES) || spellInfo->Mechanic == MECHANIC_BANDAGE)
                {
                    return;
                }
            }

            uint32 mapId = target->GetMapId();
            bool nerfInDuel = sZoneDifficulty->ShouldNerfInDuels(target);
            //Check if the map of the target is subject of a nerf at all OR if the target is subject of a nerf in a duel
            if (sZoneDifficulty->NerfInfo.find(mapId) != sZoneDifficulty->NerfInfo.end() || sZoneDifficulty->ShouldNerfInDuels(target))
            {
                //This check must be first and skip the rest to override everything else.
                if (spellInfo)
                {
                    if (sZoneDifficulty->SpellNerfOverrides.find(spellInfo->Id) != sZoneDifficulty->SpellNerfOverrides.end())
                    {
                        if (sZoneDifficulty->SpellNerfOverrides[spellInfo->Id].find(mapId) != sZoneDifficulty->SpellNerfOverrides[spellInfo->Id].end())
                        {
                            if (sZoneDifficulty->OverrideModeMatches(target->GetMap()->GetInstanceId(), spellInfo->Id, mapId))
                            {
                                heal = heal * sZoneDifficulty->SpellNerfOverrides[spellInfo->Id][mapId].NerfPct;
                                return;
                            }
                        }
                        if (sZoneDifficulty->SpellNerfOverrides[spellInfo->Id].find(0) != sZoneDifficulty->SpellNerfOverrides[spellInfo->Id].end())
                        {
                            if (sZoneDifficulty->OverrideModeMatches(target->GetMap()->GetInstanceId(), spellInfo->Id, mapId))
                            {
                                heal = heal * sZoneDifficulty->SpellNerfOverrides[spellInfo->Id][0].NerfPct;
                                return;
                            }
                        }
                    }
                }

                uint32 phaseMask = target->GetPhaseMask();
                int matchingPhase = sZoneDifficulty->GetLowestMatchingPhase(mapId, phaseMask);
                int8 mode = sZoneDifficulty->NerfInfo[mapId][matchingPhase].Enabled;
                if (matchingPhase != -1)
                {
                    Map* map = target->GetMap();
                    if (sZoneDifficulty->HasNormalMode(mode))
                    {
                        heal = heal * sZoneDifficulty->NerfInfo[mapId][matchingPhase].HealingNerfPct;
                    }
                    if (sZoneDifficulty->HasMythicmode(mode) && sZoneDifficulty->MythicmodeInstanceData[map->GetInstanceId()])
                    {
                        if (map->IsRaid() ||
                            (map->IsHeroic() && map->IsDungeon()))
                        {
                            heal = heal * sZoneDifficulty->NerfInfo[mapId][matchingPhase].HealingNerfPctHard;
                        }
                    }
                }
                else if (sZoneDifficulty->NerfInfo[DUEL_INDEX][0].Enabled > 0 && nerfInDuel)
                {
                    heal = heal * sZoneDifficulty->NerfInfo[DUEL_INDEX][0].HealingNerfPct;
                }
            }
        }
    }

    //修改Dot类法术
    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo) override
    {
        if (!sZoneDifficulty->IsEnabled)
        {
            return;
        }
        if (!sZoneDifficulty->MythicmodeInNormalDungeons && !target->GetMap()->IsRaidOrHeroicDungeon())
        {
            return;
        }

        bool isDot = false;

        if (spellInfo)
        {
            for (auto const& eff : spellInfo->GetEffects())
            {
                if (eff.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE || eff.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT)
                {
                    isDot = true;
                }
            }
        }

        if (!isDot)
        {
            return;
        }

        // Disclaimer: also affects disables boss adds buff.
        if (sConfigMgr->GetOption<bool>("ModZoneDifficulty.SpellBuff.OnlyBosses", false))
        {
            if (attacker->ToCreature() && !attacker->ToCreature()->IsDungeonBoss())
            {
                return;
            }
        }

        if (sZoneDifficulty->IsValidNerfTarget(target))
        {
            uint32 mapId = target->GetMapId();
            uint32 phaseMask = target->GetPhaseMask();
            int32 matchingPhase = sZoneDifficulty->GetLowestMatchingPhase(mapId, phaseMask);

            if (sZoneDifficulty->IsDebugInfoEnabled && attacker)
            {
                if (Player* player = attacker->ToPlayer())
                {
                    if (player->GetSession())
                    {
                        ChatHandler(player->GetSession()).PSendSysMessage("周期性法术被更改. 基础值: %i", damage);
                    }
                }
            }

            if (sZoneDifficulty->NerfInfo.find(mapId) != sZoneDifficulty->NerfInfo.end() && matchingPhase != -1)
            {
                int8 mode = sZoneDifficulty->NerfInfo[mapId][matchingPhase].Enabled;
                Map* map = target->GetMap();
                if (sZoneDifficulty->HasNormalMode(mode))
                {
                    damage = damage * sZoneDifficulty->NerfInfo[mapId][matchingPhase].SpellDamageBuffPct;
                }
                if (sZoneDifficulty->HasMythicmode(mode) && sZoneDifficulty->MythicmodeInstanceData[map->GetInstanceId()])
                {
                    if (map->IsRaid() ||
                        (map->IsHeroic() && map->IsDungeon()))
                    {
                        damage = damage * sZoneDifficulty->NerfInfo[mapId][matchingPhase].SpellDamageBuffPctHard;
                    }
                }
            }
            else if (sZoneDifficulty->ShouldNerfInDuels(target))
            {
                if (sZoneDifficulty->NerfInfo[DUEL_INDEX][0].Enabled > 0)
                {
                    damage = damage * sZoneDifficulty->NerfInfo[DUEL_INDEX][0].SpellDamageBuffPct;
                }
            }

            if (sZoneDifficulty->IsDebugInfoEnabled && attacker)
            {
                if (Player* player = attacker->ToPlayer())
                {
                    if (player->GetSession())
                    {
                        ChatHandler(player->GetSession()).PSendSysMessage("周期性法术被更改. 修正值: %i", damage);
                    }
                }
            }
        }
    }

    //修改伤害性法术
    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        if (!sZoneDifficulty->IsEnabled)
        {
            return;
        }
        if (!sZoneDifficulty->MythicmodeInNormalDungeons && !target->GetMap()->IsRaidOrHeroicDungeon())
        {
            return;
        }

        // Disclaimer: also affects disables boss adds buff.
        if (sConfigMgr->GetOption<bool>("ModZoneDifficulty.SpellBuff.OnlyBosses", false))
        {
            if (attacker->ToCreature() && !attacker->ToCreature()->IsDungeonBoss())
            {
                return;
            }
        }

        if (sZoneDifficulty->IsValidNerfTarget(target))
        {
            uint32 mapId = target->GetMapId();
            uint32 phaseMask = target->GetPhaseMask();
            int32 matchingPhase = sZoneDifficulty->GetLowestMatchingPhase(mapId, phaseMask);
            if (spellInfo)
            {
                //This check must be first and skip the rest to override everything else.
                if (sZoneDifficulty->SpellNerfOverrides.find(spellInfo->Id) != sZoneDifficulty->SpellNerfOverrides.end())
                {
                    if (sZoneDifficulty->SpellNerfOverrides[spellInfo->Id].find(mapId) != sZoneDifficulty->SpellNerfOverrides[spellInfo->Id].end())
                    {
                        if (sZoneDifficulty->OverrideModeMatches(target->GetMap()->GetInstanceId(), spellInfo->Id, mapId))
                        {
                            damage = damage * sZoneDifficulty->SpellNerfOverrides[spellInfo->Id][mapId].NerfPct;
                            return;
                        }
                    }
                    else if (sZoneDifficulty->SpellNerfOverrides[spellInfo->Id].find(0) != sZoneDifficulty->SpellNerfOverrides[spellInfo->Id].end())
                    {
                        if (sZoneDifficulty->OverrideModeMatches(target->GetMap()->GetInstanceId(), spellInfo->Id, mapId))
                        {
                            damage = damage * sZoneDifficulty->SpellNerfOverrides[spellInfo->Id][0].NerfPct;
                            return;
                        }
                    }
                }

                if (sZoneDifficulty->IsDebugInfoEnabled && target)
                {
                    if (Player* player = target->ToPlayer()) // Pointless check? Perhaps.
                    {
                        if (player->GetSession())
                        {
                            ChatHandler(player->GetSession()).PSendSysMessage("法术: %s (%u) 基础值: %i (%f 正常模式)", spellInfo->SpellName[player->GetSession()->GetSessionDbcLocale()], spellInfo->Id, damage, sZoneDifficulty->NerfInfo[mapId][matchingPhase].SpellDamageBuffPct);
                            ChatHandler(player->GetSession()).PSendSysMessage("法术: %s (%u) 基础值: %i (%f 挑战模式)", spellInfo->SpellName[player->GetSession()->GetSessionDbcLocale()], spellInfo->Id, damage, sZoneDifficulty->NerfInfo[mapId][matchingPhase].SpellDamageBuffPctHard);
                        }
                    }
                }
            }

            if (sZoneDifficulty->NerfInfo.find(mapId) != sZoneDifficulty->NerfInfo.end() && matchingPhase != -1)
            {
                int8 mode = sZoneDifficulty->NerfInfo[mapId][matchingPhase].Enabled;
                Map* map = target->GetMap();
                if (sZoneDifficulty->HasNormalMode(mode))
                {
                    damage = damage * sZoneDifficulty->NerfInfo[mapId][matchingPhase].SpellDamageBuffPct;
                }
                if (sZoneDifficulty->HasMythicmode(mode) && sZoneDifficulty->MythicmodeInstanceData[map->GetInstanceId()])
                {
                    if (map->IsRaid() ||
                        (map->IsHeroic() && map->IsDungeon()))
                    {
                        damage = damage * sZoneDifficulty->NerfInfo[mapId][matchingPhase].SpellDamageBuffPctHard;
                    }
                }
            }
            else if (sZoneDifficulty->ShouldNerfInDuels(target))
            {
                if (sZoneDifficulty->NerfInfo[DUEL_INDEX][0].Enabled > 0)
                {
                    damage = damage * sZoneDifficulty->NerfInfo[DUEL_INDEX][0].SpellDamageBuffPct;
                }
            }

            if (sZoneDifficulty->IsDebugInfoEnabled && target)
            {
                if (Player* player = target->ToPlayer()) // Pointless check? Perhaps.
                {
                    if (player->GetSession())
                    {
                        ChatHandler(player->GetSession()).PSendSysMessage("法术: %s (%u) 修正值: %i", spellInfo->SpellName[player->GetSession()->GetSessionDbcLocale()], spellInfo->Id, damage);
                    }
                }
            }
        }
    }

    //修改近战伤害
    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        if (!sZoneDifficulty->IsEnabled)
        {
            return;
        }
        if (!sZoneDifficulty->MythicmodeInNormalDungeons && !target->GetMap()->IsRaidOrHeroicDungeon())
        {
            return;
        }

        // Disclaimer: also affects disables boss adds buff.
        if (sConfigMgr->GetOption<bool>("ModZoneDifficulty.MeleeBuff.OnlyBosses", false))
        {
            if (attacker->ToCreature() && !attacker->ToCreature()->IsDungeonBoss())
            {
                return;
            }
        }

        if (sZoneDifficulty->IsValidNerfTarget(target))
        {
            uint32 mapId = target->GetMapId();
            uint32 phaseMask = target->GetPhaseMask();
            int matchingPhase = sZoneDifficulty->GetLowestMatchingPhase(mapId, phaseMask);
            if (sZoneDifficulty->NerfInfo.find(mapId) != sZoneDifficulty->NerfInfo.end() && matchingPhase != -1)
            {
                int8 mode = sZoneDifficulty->NerfInfo[mapId][matchingPhase].Enabled;
                Map* map = target->GetMap();
                if (sZoneDifficulty->HasNormalMode(mode))
                {
                    damage = damage * sZoneDifficulty->NerfInfo[mapId][matchingPhase].MeleeDamageBuffPct;
                }
                if (sZoneDifficulty->HasMythicmode(mode) && sZoneDifficulty->MythicmodeInstanceData[target->GetMap()->GetInstanceId()])
                {
                    if (map->IsRaid() ||
                        (map->IsHeroic() && map->IsDungeon()))
                    {
                        damage = damage * sZoneDifficulty->NerfInfo[mapId][matchingPhase].MeleeDamageBuffPctHard;
                    }
                }
            }
            else if (sZoneDifficulty->ShouldNerfInDuels(target))
            {
                if (sZoneDifficulty->NerfInfo[DUEL_INDEX][0].Enabled > 0)
                {
                    damage = damage * sZoneDifficulty->NerfInfo[DUEL_INDEX][0].MeleeDamageBuffPct;
                }
            }
        }
    }

    /**
     *  @brief 当单位进入战斗，检查是否激活挑战模式，并且是否分类了ai。如果依旧激活了，则使用挑战模式内置法术AI
     */
    void OnUnitEnterCombat(Unit* unit, Unit* /*victim*/) override
    {
        //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: OnUnitEnterCombat for unit {}", unit->GetEntry());
        //当前地图实例不是挑战模式
        if (sZoneDifficulty->MythicmodeInstanceData.find(unit->GetInstanceId()) == sZoneDifficulty->MythicmodeInstanceData.end())
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Instance is not in mythic mode.");
            return;
        }
        //当前地图实例未激活挑战模式
        if (!sZoneDifficulty->MythicmodeInstanceData[unit->GetInstanceId()])
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: InstanceId not found in mythic mode list.");
            return;
        }

        if (Creature* creature = unit->ToCreature())
        {
            //是一个触发器非怪物 不执行法术AI
            if (creature->IsTrigger())
            {
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Creature is a trigger.");
                return;
            }
        }

        uint32 entry = unit->GetEntry();
        //没有该生物的AI模型
        if (sZoneDifficulty->MythicmodeAI.find(222000) == sZoneDifficulty->MythicmodeAI.end())
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: No HarmodeAI found for creature with entry {}", entry);
            return;
        }

        unit->m_Events.CancelEventGroup(EVENT_GROUP);
        //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: OnUnitEnterCombat checks passed for unit {}", unit->GetEntry());

        uint32 i = 0;
        //执行法术AI
        for (ZoneDifficultyHAI& data : sZoneDifficulty->MythicmodeAI[222000])
        {
            if (data.Chance == 100 || data.Chance >= urand(1, 100))
            {
                unit->m_Events.AddEventAtOffset([unit, entry, i]()
                    {
                        sZoneDifficulty->MythicmodeEvent(unit, entry, i);
                    }, data.Delay, EVENT_GROUP);
            }
            ++i;
        }
    }
};

class mod_zone_difficulty_playerscript : public PlayerScript
{
public:
    mod_zone_difficulty_playerscript() : PlayerScript("mod_zone_difficulty_playerscript") { }

    //移除挑战模式不允许的buff，在zone_difficulty_disallowed_buffs表里面
    void OnMapChanged(Player* player) override
    {
        uint32 mapId = player->GetMapId();
        if (sZoneDifficulty->DisallowedBuffs.find(mapId) != sZoneDifficulty->DisallowedBuffs.end())
        {
            for (auto aura : sZoneDifficulty->DisallowedBuffs[mapId])
            {
                player->RemoveAura(aura);
            }
        }
    }
};

class mod_zone_difficulty_petscript : public PetScript
{
public:
    mod_zone_difficulty_petscript() : PetScript("mod_zone_difficulty_petscript") { }

    //移除宠物身上不允许的buff,位置同上
    void OnPetAddToWorld(Pet* pet) override
    {
        uint32 mapId = pet->GetMapId();
        if (sZoneDifficulty->DisallowedBuffs.find(mapId) != sZoneDifficulty->DisallowedBuffs.end())
        {
            pet->m_Events.AddEventAtOffset([mapId, pet]()
                {
                    for (uint32 aura : sZoneDifficulty->DisallowedBuffs[mapId])
                    {
                        pet->RemoveAurasDueToSpell(aura);
                    }
                }, 2s);
        }
    }
};

class mod_zone_difficulty_worldscript : public WorldScript
{
public:
    mod_zone_difficulty_worldscript() : WorldScript("mod_zone_difficulty_worldscript") { }

    //加载配置文件
    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sZoneDifficulty->IsEnabled = sConfigMgr->GetOption<bool>("ModZoneDifficulty.Enable", false);
        sZoneDifficulty->IsDebugInfoEnabled = sConfigMgr->GetOption<bool>("ModZoneDifficulty.DebugInfo", false);
        sZoneDifficulty->MythicmodeHpModifier = sConfigMgr->GetOption<float>("ModZoneDifficulty.Mythicmode.HpModifier", 2);
        sZoneDifficulty->MythicmodeEnable = sConfigMgr->GetOption<bool>("ModZoneDifficulty.Mythicmode.Enable", false);
        sZoneDifficulty->MythicmodeInNormalDungeons = sConfigMgr->GetOption<bool>("ModZoneDifficulty.Mythicmode.InNormalDungeons", false);
        sZoneDifficulty->LoadMapDifficultySettings();
    }

    void OnStartup() override
    {
        sZoneDifficulty->LoadMythicmodeInstanceData();
        sZoneDifficulty->LoadMythicmodeScoreData();
    }
};

class mod_zone_difficulty_globalscript : public GlobalScript
{
public:
    mod_zone_difficulty_globalscript() : GlobalScript("mod_zone_difficulty_globalscript") { }

    //记录挑战模式下BOSS战斗战斗日志, 记录位置zone_difficulty_encounter_logs
    void OnBeforeSetBossState(uint32 id, EncounterState newState, EncounterState oldState, Map* instance) override
    {
        if (!sZoneDifficulty->MythicmodeEnable)
        {
            return;
        }
        if (sZoneDifficulty->IsDebugInfoEnabled)
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: OnBeforeSetBossState: bossId = {}, newState = {}, oldState = {}, MapId = {}, InstanceId = {}", id, newState, oldState, instance->GetId(), instance->GetInstanceId());
        }
        uint32 instanceId = instance->GetInstanceId();
        //如果当前的地图实例ID不是挑战模式地图 或者 普通5人本没有被启用并且不是团队或英雄地下城
        if (!sZoneDifficulty->IsMythicmodeMap(instance->GetId()) ||
            (!sZoneDifficulty->MythicmodeInNormalDungeons && !instance->IsRaidOrHeroicDungeon()))
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: OnBeforeSetBossState: Instance not handled because there is no Mythicmode loot data for map id: {}", instance->GetId());
            return;
        }
        if (oldState != IN_PROGRESS && newState == IN_PROGRESS)
        {
            if (sZoneDifficulty->MythicmodeInstanceData[instanceId])
            {
                sZoneDifficulty->EncountersInProgress[instanceId] = GameTime::GetGameTime().count();
            }
        }
        else if (oldState == IN_PROGRESS && newState == DONE)
        {
            if (sZoneDifficulty->MythicmodeInstanceData[instanceId])
            {
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Mythicmode is on.");
                if (sZoneDifficulty->EncountersInProgress.find(instanceId) != sZoneDifficulty->EncountersInProgress.end() && sZoneDifficulty->EncountersInProgress[instanceId] != 0)
                {
                    Map::PlayerList const& PlayerList = instance->GetPlayers();
                    for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
                    {
                        Player* player = i->GetSource();
                        if (!player->IsGameMaster() && !player->IsDeveloper())
                        {
                            CharacterDatabase.Execute(
                                "REPLACE INTO `zone_difficulty_encounter_logs` VALUES({}, {}, {}, {}, {}, {}, {})",
                                instanceId, sZoneDifficulty->EncountersInProgress[instanceId], GameTime::GetGameTime().count(), instance->GetId(), id, player->GetGUID().GetCounter(), 64);
                        }
                    }
                }
            }
        }
    }

    //副本被重置，删除挑战模式记录
    void OnInstanceIdRemoved(uint32 instanceId) override
    {
        //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: OnInstanceIdRemoved: instanceId = {}", instanceId);
        if (sZoneDifficulty->MythicmodeInstanceData.find(instanceId) != sZoneDifficulty->MythicmodeInstanceData.end())
        {
            sZoneDifficulty->MythicmodeInstanceData.erase(instanceId);
        }

        CharacterDatabase.Execute("DELETE FROM zone_difficulty_instance_saves WHERE InstanceID = {};", instanceId);
    }

    //记录战斗之后，写入个人得分 遍历zone_difficulty_mythicmode_instance_data表 SourceEntry存在且Overide不为1则记一分
    void OnAfterUpdateEncounterState(Map* map, EncounterCreditType /*type*/, uint32 /*creditEntry*/, Unit* source, Difficulty /*difficulty_fixed*/, DungeonEncounterList const* /*encounters*/, uint32 /*dungeonCompleted*/, bool /*updated*/) override
    {
        if (!sZoneDifficulty->MythicmodeEnable)
        {
            return;
        }
        if (!source)
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: source is a nullptr in OnAfterUpdateEncounterState");
            return;
        }
        if (sZoneDifficulty->MythicmodeInstanceData.find(map->GetInstanceId()) != sZoneDifficulty->MythicmodeInstanceData.end())
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Encounter completed. Map relevant. Checking for source: {}", source->GetEntry());
            // Give additional loot, if the encounter was in Mythicmode.
            if (sZoneDifficulty->MythicmodeInstanceData[map->GetInstanceId()])
            {
                uint32 mapId = map->GetId();
                uint32 score = 0;
                if (!sZoneDifficulty->IsMythicmodeMap(mapId) ||
                    (!sZoneDifficulty->MythicmodeInNormalDungeons && !map->IsRaidOrHeroicDungeon()))
                {
                    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: No additional loot stored in map with id {}.", map->GetInstanceId());
                    return;
                }

                bool SourceAwardsMythicmodeLoot = false;
                //iterate over all listed creature entries for that map id and see, if the encounter should yield Mythicmode loot and if there is an override to the default behaviour
                for (auto value : sZoneDifficulty->MythicmodeLoot[mapId])
                {
                    //LOG_INFO("module", "DbEntry : {}, sourceEntry:{}", value.EncounterEntry, source->GetEntry());
                    if (value.EncounterEntry == source->GetEntry())
                    {
                        SourceAwardsMythicmodeLoot = true;
                        if (!(value.Override & 1))
                        {
                            score = 1;
                        }
                        break;
                    }
                }

                if (!SourceAwardsMythicmodeLoot)
                {
                    return;
                }

                if (map->IsHeroic() && map->IsNonRaidDungeon())
                {
                    sZoneDifficulty->AddMythicmodeScore(map, sZoneDifficulty->Expansion[mapId], score);
                    sZoneDifficulty->GiveMythicmodeItem(map, sZoneDifficulty->Expansion[mapId]);
                }
                else if (map->IsRaid())
                {
                    sZoneDifficulty->AddMythicmodeScore(map, sZoneDifficulty->Expansion[mapId], score);
                }
                /* debug
                 * else
                 * {
                 *   LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Map with id {} is not a raid or a dungeon. Mythicmode loot not granted.", map->GetInstanceId());
                 * }
                 */
            }
        }
    }
};

//*****************************************以下为奖励代码，不看
class mod_zone_difficulty_rewardnpc : public CreatureScript
{
public:
    mod_zone_difficulty_rewardnpc() : CreatureScript("mod_zone_difficulty_rewardnpc") { }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        if (sZoneDifficulty->IsDebugInfoEnabled)
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: OnGossipSelectRewardNpc action: {}", action);
        }
        ClearGossipMenuFor(player);
        uint32 npcText = 0;
        //Safety measure. There's a way for action 0 to happen even though it's not provided in the gossip menu.
        if (action == 0)
        {
            CloseGossipMenuFor(player);
            return true;
        }

        if (action == 999998)//不需要这件物品 关闭弹窗
        {
            CloseGossipMenuFor(player);
            return true;
        }

        if (action == 999999) //查看积分
        {
            npcText = NPC_TEXT_SCORE;
            for (int i = 1; i <= 16; ++i)
            {
                std::string whisper;
                whisper.append("你拥有积分：");
                if (sZoneDifficulty->MythicmodeScore.find(player->GetGUID().GetCounter()) == sZoneDifficulty->MythicmodeScore.end())
                {
                    continue;
                }
                else if (sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()].find(i) == sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()].end())
                {
                    continue;
                }
                else
                {
                    whisper.append(std::to_string(sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][i])).append("点");
                }
                whisper.append("（");
                whisper.append(sZoneDifficulty->GetContentTypeString(i));
                whisper.append("）");
                creature->Whisper(whisper, LANG_UNIVERSAL, player);
            }
            CloseGossipMenuFor(player);
            return true;
        }

        // full tier clearance rewards: confirmation
        if (action > 99001000) // Tier类物品
        {

            uint32 category = action - 99001000;

            // Check (again) if the player has enough score in the respective category.
            uint32 availableScore = 0;
            if (sZoneDifficulty->MythicmodeScore.find(player->GetGUID().GetCounter()) != sZoneDifficulty->MythicmodeScore.end())
            {
                if (sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()].find(category) != sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()].end())
                {
                    availableScore = sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][category];
                }
            }
            if (availableScore < sZoneDifficulty->TierRewards[category].Price)
            {
                CloseGossipMenuFor(player);
                return true;
            }

            // Check (again) if the player has the neccesary achievement
            if (!sZoneDifficulty->HasCompletedFullTier(category, player->GetGUID().GetCounter()))
            {
                CloseGossipMenuFor(player);
                return true;
            }

            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Sending full tier clearance reward for category {}", category);
            sZoneDifficulty->DeductMythicmodeScore(player, category, sZoneDifficulty->TierRewards[category].Price);
            sZoneDifficulty->SendItem(player, category, 99, 0);

            return true;
        }

        // full tier clearance rewards: selection
        if (action > 99000000) //Tier类物品奖励
        {
            uint32 category = action - 99000000;
            if (sZoneDifficulty->HasCompletedFullTier(category, player->GetGUID().GetCounter()))
            {
                // Check if the player has enough score in the respective category.
                uint32 availableScore = 0;
                if (sZoneDifficulty->MythicmodeScore.find(player->GetGUID().GetCounter()) != sZoneDifficulty->MythicmodeScore.end())
                {
                    if (sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()].find(category) != sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()].end())
                    {
                        availableScore = sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][category];
                    }
                }

                if (availableScore < sZoneDifficulty->TierRewards[category].Price)
                {
                    npcText = NPC_TEXT_DENIED;
                    SendGossipMenuFor(player, npcText, creature);
                    std::string whisper;
                    whisper.append("对不起，旅行者。该奖励价值： ");
                    whisper.append(std::to_string(sZoneDifficulty->TierRewards[category].Price));
                    whisper.append("点积分,但是你只有");
                    whisper.append(std::to_string(sZoneDifficulty->MythicmodeScore[category][player->GetGUID().GetCounter()]));
                    whisper.append("点积分（");
                    whisper.append(sZoneDifficulty->GetContentTypeString(category));
                    whisper.append("）");
                    creature->Whisper(whisper, LANG_UNIVERSAL, player);
                    return true;
                }
                npcText = NPC_TEXT_CONFIRM;
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(sZoneDifficulty->TierRewards[category].Entry);
                std::string gossip;
                std::string name = proto->Name1;
                if (ItemLocale const* leftIl = sObjectMgr->GetItemLocale(sZoneDifficulty->TierRewards[category].Entry))
                {
                    ObjectMgr::GetLocaleString(leftIl->Name, player->GetSession()->GetSessionDbcLocale(), name);
                }
                gossip.append("好的, ").append(name).append(" 是我想要的东西.");
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "不!", GOSSIP_SENDER_MAIN, 999998);
                AddGossipItemFor(player, GOSSIP_ICON_VENDOR, gossip, GOSSIP_SENDER_MAIN, 99001000 + category);
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: AddingGossipItem with action {}", 99001000 + category);
                SendGossipMenuFor(player, npcText, creature);
                return true;
            }
            return true;
        }

        // player has selected a content type
        else if (action < 100)
        {
            npcText = NPC_TEXT_CATEGORY;
            if (sZoneDifficulty->HasCompletedFullTier(action, player->GetGUID().GetCounter()))
            {
                std::string gossip = "我想要兑换挑战模式奖励";
                gossip.append(sZoneDifficulty->GetContentTypeString(action));
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, gossip, GOSSIP_SENDER_MAIN, 99000000 + action);
            }

            uint32 i = 1;
            for (auto& itemType : sZoneDifficulty->Rewards[action])
            {
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: typedata.first is {}", (itemType.first + (action * 100)));
                std::string gossip;
                std::string typestring = sZoneDifficulty->GetItemTypeString(itemType.first);
                if (sZoneDifficulty->ItemIcons.find(i) != sZoneDifficulty->ItemIcons.end())
                {
                    gossip.append(sZoneDifficulty->ItemIcons[i]);
                }
                gossip.append("我要兑换").append(typestring).append("类物品.");
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, gossip, GOSSIP_SENDER_MAIN, itemType.first + (action * 100)); // HWLK/HTBC 1201 1202 801 802
                ++i;
            }
        }
        else if (action < 10000) //最多可以99类
        {
            npcText = NPC_TEXT_ITEM;
            uint32 category = action / 100; //分类 TWLK-12 T10-14 HTBC-8 取首部两位 最大99
            uint32 counter = action % 100; //类别 饰品、布甲、皮甲..  0 1 2 3... 取末尾两位 最大99
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Building gossip with category {} and counter {}", category, counter);

            for (size_t i = 0; i < sZoneDifficulty->Rewards[category][counter].size(); ++i)
            {
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Adding gossip option for entry {}", sZoneDifficulty->Rewards[category][counter][i].Entry);
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(sZoneDifficulty->Rewards[category][counter][i].Entry);
                std::string gossip;
                std::string name = proto->Name1;
                if (ItemLocale const* leftIl = sObjectMgr->GetItemLocale(sZoneDifficulty->Rewards[category][counter][i].Entry))
                {
                    ObjectMgr::GetLocaleString(leftIl->Name, player->GetSession()->GetSessionDbcLocale(), name);
                }
                gossip.append(name);
                gossip.append("[").append(std::to_string(proto->ItemLevel)).append("]");
                if((proto->Flags2 & ITEM_FLAGS_EXTRA_HORDE_ONLY) == ITEM_FLAGS_EXTRA_HORDE_ONLY) {
                    gossip.append("(仅限部落使用)");
                } else if((proto->Flags2 & ITEM_FLAGS_EXTRA_ALLIANCE_ONLY) == ITEM_FLAGS_EXTRA_ALLIANCE_ONLY) {
                    gossip.append("(仅限联盟使用)");
                }
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, gossip, GOSSIP_SENDER_MAIN, (10000 * category) + (100 * counter) + i);
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: AddingGossipItem with action {}", (1000 * category) + (100 * counter) + i);
            }
        }
        else if (action < 1000000) //最大值990000 + 9900 + 99 = 99 99 99
        {
            uint32 category = action / 10000; // 取首部两位 最大99
            uint32 itemType = (action % 10000) / 100; //取中间两位数字 最大99
            uint32 counter = action % 100; //取末尾两位 最大99
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Handling item with category {}, itemType {}, counter {}, action {}", category, itemType, counter, action);

            // Check if the player has enough score in the respective category.
            uint32 availableScore = 0;
            if (sZoneDifficulty->MythicmodeScore.find(player->GetGUID().GetCounter()) != sZoneDifficulty->MythicmodeScore.end())
            {
                if (sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()].find(category) != sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()].end())
                {
                    availableScore = sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][category];
                }
            }

            if (availableScore < sZoneDifficulty->Rewards[category][itemType][counter].Price)
            {
                npcText = NPC_TEXT_DENIED;
                SendGossipMenuFor(player, npcText, creature);
                std::string whisper;
                whisper.append("对不起，旅行者。该奖励需要：");
                whisper.append(std::to_string(sZoneDifficulty->Rewards[category][itemType][counter].Price));
                whisper.append("点积分，但是你只有");
                whisper.append(std::to_string(sZoneDifficulty->MythicmodeScore[category][player->GetGUID().GetCounter()]));
                whisper.append("点积分（");
                whisper.append(sZoneDifficulty->GetContentTypeString(category));
                whisper.append("）");
                creature->Whisper(whisper, LANG_UNIVERSAL, player);
                return true;
            }

            npcText = NPC_TEXT_CONFIRM;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(sZoneDifficulty->Rewards[category][itemType][counter].Entry);
            if(player->CanUseItem(proto) != EQUIP_ERR_OK){
                creature->Whisper("你不能使用这件物品.", LANG_UNIVERSAL, player);
                CloseGossipMenuFor(player);
                return true;
            }
            std::string gossip;
            std::string name = proto->Name1;
            if (ItemLocale const* leftIl = sObjectMgr->GetItemLocale(sZoneDifficulty->Rewards[category][itemType][counter].Entry))
            {
                ObjectMgr::GetLocaleString(leftIl->Name, player->GetSession()->GetSessionDbcLocale(), name);
            }
            gossip.append("好的, [").append(name).append("]是我需要的物品.");
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "我不需要这件物品!", GOSSIP_SENDER_MAIN, 999998);
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, gossip, GOSSIP_SENDER_MAIN, 1000000 + (10000 * category) + (100 * itemType) + counter);
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: AddingGossipItem with action {}", 100000 + (1000 * category) + (100 * itemType) + counter);
        }
        else if (action > 1000000) //最大值 1 99 99 99
        {
            npcText = NPC_TEXT_GRANT;
            uint32 flag = action - 1000000; // 99 99 99
            uint32 category = flag / 10000;
            uint32 itemType = (flag % 10000) / 100;
            uint32 counter = action % 100;

            // Check (again) if the player has enough score in the respective category.
            uint32 availableScore = 0;
            if (sZoneDifficulty->MythicmodeScore.find(player->GetGUID().GetCounter()) != sZoneDifficulty->MythicmodeScore.end())
            {
                if (sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()].find(category) != sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()].end())
                {
                    availableScore = sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][category];
                }
            }
            if (availableScore < sZoneDifficulty->Rewards[category][itemType][counter].Price)
            {
                return true;
            }

            // Check if the player has the neccesary achievement
            if (sZoneDifficulty->Rewards[category][itemType][counter].Achievement != 0)
            {
                if (!player->HasAchieved(sZoneDifficulty->Rewards[category][itemType][counter].Achievement))
                {
                    std::string gossip = "你需要完成所有巫妖王之怒英雄地下城才能兑换该物品。";
                    creature->Whisper(gossip, LANG_UNIVERSAL, player);
                    /* debug
                     * LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Player missing achiement with ID {} to obtain item with category {}, itemType {}, counter {}",
                     *    sZoneDifficulty->Rewards[category][itemType][counter].Achievement, category, itemType, counter);
                     * end debug
                     */
                    CloseGossipMenuFor(player);
                    return true;
                }
            }

            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Sending item with category {}, itemType {}, counter {}", category, itemType, counter);
            sZoneDifficulty->DeductMythicmodeScore(player, category, sZoneDifficulty->Rewards[category][itemType][counter].Price);
            sZoneDifficulty->SendItem(player, category, itemType, counter);
        }

        SendGossipMenuFor(player, npcText, creature);
        return true;
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: OnGossipHelloRewardNpc");
        uint32 npcText = NPC_TEXT_OFFER;
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "我能看看我的积分点数吗?", GOSSIP_SENDER_MAIN, 999999);

        for (auto& typedata : sZoneDifficulty->Rewards)
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: typedata.first is {}", typedata.first);
            if (typedata.first != 0)
            {
                std::string gossip;
                std::string typestring = sZoneDifficulty->GetContentTypeString(typedata.first);
                gossip.append("我想兑换").append(typestring).append("奖励.");
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: typestring is: {} action is: {}", typestring, typedata.first);
                // typedata.first is the ContentType
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, gossip, GOSSIP_SENDER_MAIN, typedata.first);
            }
        }

        SendGossipMenuFor(player, npcText, creature);
        return true;
    }
};

class mod_zone_difficulty_dungeonmaster : public CreatureScript
{
public:
    mod_zone_difficulty_dungeonmaster() : CreatureScript("mod_zone_difficulty_dungeonmaster") { }

    struct mod_zone_difficulty_dungeonmasterAI : public ScriptedAI
    {
        mod_zone_difficulty_dungeonmasterAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: mod_zone_difficulty_dungeonmasterAI: Reset happens.");
            if (me->GetMap() && me->GetMap()->IsHeroic() && !me->GetMap()->IsRaid())
            {
                if (!sZoneDifficulty->MythicmodeEnable)
                {
                    return;
                }
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: We're inside a heroic 5man now.");
                //todo: add the list for the wotlk heroic dungeons quests
                for (auto& quest : sZoneDifficulty->DailyHeroicQuests)
                {
                    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Checking quest {} and MapId {}", quest, me->GetMapId());
                    if (sPoolMgr->IsSpawnedObject<Quest>(quest))
                    {
                        if (sZoneDifficulty->HeroicWLKQuestMapList[me->GetMapId()] == quest)
                        {
                            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: mod_zone_difficulty_dungeonmasterAI: Quest with id {} is active.", quest);
                            me->SetPhaseMask(1, true);

                            _scheduler.Schedule(8s, [this](TaskContext /*context*/)
                                {
                                    me->Yell("嗨~你好，冒险者！想要进行更高难度的挑战吗？和我交谈一下吧。", LANG_UNIVERSAL);
                                });
                            _scheduler.Schedule(55s, [this](TaskContext /*context*/)
                                {
                                    me->Yell("非常感谢你的关注，冒险者。但我现在要去为其他冒险者服务了，再见!", LANG_UNIVERSAL);
                                });
                            _scheduler.Schedule(60s, [this](TaskContext /*context*/)
                                {
                                    me->DespawnOrUnsummon();
                                });
                            break;
                        }
                    }
                }
            }
        }

        void UpdateAI(uint32 diff) override
        {
            _scheduler.Update(diff);
        }

    private:
        TaskScheduler _scheduler;
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new mod_zone_difficulty_dungeonmasterAI(creature);
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        uint32 instanceId = player->GetMap()->GetInstanceId();
        if (action == 100)
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Try turn on");
            bool canTurnOn = true;

            // Forbid turning Mythicmode on ...
            // ...if a single encounter was completed on normal mode
            if (sZoneDifficulty->MythicmodeInstanceData.find(instanceId) != sZoneDifficulty->MythicmodeInstanceData.end())
            {
                if (player->GetInstanceScript()->GetBossState(0) == DONE)
                {
                    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Mythicmode is not Possible for instanceId {}", instanceId);
                    canTurnOn = false;
                    creature->Whisper("冒险者，你已经完成了一次挑战模式冒险。暂时不能参加新的挑战了.", LANG_UNIVERSAL, player);
                    sZoneDifficulty->SaveMythicmodeInstanceData(instanceId);
                }
            }
            // ... if there is an encounter in progress
            if (player->GetInstanceScript()->IsEncounterInProgress())
            {
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: IsEncounterInProgress");
                canTurnOn = false;
                creature->Whisper("冒险者，你有正在进行中的挑战模式冒险，暂时不能参加新的挑战了.", LANG_UNIVERSAL, player);
            }

            if (player->IsGameMaster())
            {
                LOG_ERROR("module", "MOD-ZONE-DIFFICULTY: GM {} has allowed Mythicmode for instance {}", player->GetName(), instanceId);
                canTurnOn = true;
            }

            if (canTurnOn)
            {
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Turn on Mythicmode for id {}", instanceId);
                sZoneDifficulty->MythicmodeInstanceData[instanceId] = true;
                sZoneDifficulty->SaveMythicmodeInstanceData(instanceId);
                sZoneDifficulty->SendWhisperToRaid("已开启地下城挑战模式！", creature, player);
            }

            CloseGossipMenuFor(player);
        }
        else if (action == 101)
        {
            if (player->GetInstanceScript()->IsEncounterInProgress())
            {
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: IsEncounterInProgress");
                creature->Whisper("冒险者，你有正在进行中的挑战模式地下城，暂时不能参加新的挑战了.", LANG_UNIVERSAL, player);
                CloseGossipMenuFor(player);
            }
            if (player->GetInstanceScript()->GetBossState(0) != DONE)
            {
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Turn off Mythicmode for id {}", instanceId);
                sZoneDifficulty->MythicmodeInstanceData[instanceId] = false;
                sZoneDifficulty->SaveMythicmodeInstanceData(instanceId);
                sZoneDifficulty->SendWhisperToRaid("已经切换到正常冒险模式", creature, player);
                CloseGossipMenuFor(player);
            }
            else
            {
                ClearGossipMenuFor(player);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "是的，虽然这将无法再次返回挑战模式，但我依旧要切换到正常模式的冒险。", GOSSIP_SENDER_MAIN, 102);
                SendGossipMenuFor(player, NPC_TEXT_LEADER_FINAL, creature);
            }
        }
        else if (action == 102)
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Turn off Mythicmode for id {}", instanceId);
            sZoneDifficulty->MythicmodeInstanceData[instanceId] = false;
            sZoneDifficulty->SaveMythicmodeInstanceData(instanceId);
            sZoneDifficulty->SendWhisperToRaid("已经切换到正常冒险模式", creature, player);
            CloseGossipMenuFor(player);
        }

        return true;
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: OnGossipHelloChromie");
        Group* group = player->GetGroup();
        if (group && group->IsLfgRandomInstance() && !player->GetMap()->IsRaid())
        {
            creature->Whisper("很抱歉，冒险者。你不能在这里接受挑战。请选择一个特定的地下城之后在和我对话吧。", LANG_UNIVERSAL, player);
            return true;
        }
        if (!group && !player->IsGameMaster())
        {
            creature->Whisper("很抱歉，冒险者。请多带几个朋友一起来挑战吧!", LANG_UNIVERSAL, player);
            return true;
        }
        uint32 npcText = NPC_TEXT_OTHER;
        //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: OnGossipHello Has Group");
        if (player->IsGameMaster() || (group && group->IsLeader(player->GetGUID())))
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: OnGossipHello Is Leader");
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "我希望挑战更高难度的地下城模式（挑战模式）。", GOSSIP_SENDER_MAIN, 100);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "我要进行正常难度的地下城模式（正常模式）", GOSSIP_SENDER_MAIN, 101);

            if (sZoneDifficulty->MythicmodeInstanceData[player->GetMap()->GetInstanceId()])
            {
                npcText = NPC_TEXT_LEADER_HARD;
            }
            else
            {
                npcText = NPC_TEXT_LEADER_NORMAL;
            }
        }
        else
        {
            creature->Whisper("请让你的队长来选择挑战的地下城模式吧。", LANG_UNIVERSAL, player);
        }
        SendGossipMenuFor(player, npcText, creature);
        return true;
    }
};

class mod_zone_difficulty_allcreaturescript : public AllCreatureScript
{
public:
    mod_zone_difficulty_allcreaturescript() : AllCreatureScript("mod_zone_difficulty_allcreaturescript") { }

    void OnAllCreatureUpdate(Creature* creature, uint32 /*diff*/) override
    {
        if (!sZoneDifficulty->MythicmodeEnable)
        {
            return;
        }
        // Heavily inspired by https://github.com/azerothcore/mod-autobalance/blob/1d82080237e62376b9a030502264c90b5b8f272b/src/AutoBalance.cpp
        Map* map = creature->GetMap();
        if (!creature || !map)
        {
            return;
        }

        if (!map->IsRaid() &&
            (!(map->IsHeroic() && map->IsDungeon())))
        {
            return;
        }

        uint32 mapId = creature->GetMapId();
        if (sZoneDifficulty->NerfInfo.find(mapId) == sZoneDifficulty->NerfInfo.end())
        {
            return;
        }

        if ((creature->IsHunterPet() || creature->IsPet() || creature->IsSummon()) && creature->IsControlledByPlayer())
        {
            return;
        }

        if(creature -> IsNPCBotOrPet()) {
            return ;
        }

        if (!creature->IsAlive())
        {
            return;
        }

        CreatureTemplate const* creatureTemplate = creature->GetCreatureTemplate();
        //skip critters and special creatures (spell summons etc.) in instances
        if (creatureTemplate->maxlevel <= 1)
        {
            return;
        }

        CreatureBaseStats const* origCreatureStats = sObjectMgr->GetCreatureBaseStats(creature->GetLevel(), creatureTemplate->unit_class);
        uint32 baseHealth = origCreatureStats->GenerateHealth(creatureTemplate);
        uint32 newHp;
        uint32 entry = creature->GetEntry();
        if (sZoneDifficulty->CreatureOverrides.find(entry) == sZoneDifficulty->CreatureOverrides.end())
        {
            // if (creature->IsDungeonBoss())
            // {
            //     return;
            // }
            newHp = round(baseHealth * sZoneDifficulty->MythicmodeHpModifier);
        }
        else
        {
            newHp = round(baseHealth * (sZoneDifficulty->MythicmodeHpModifier) * (sZoneDifficulty->CreatureOverrides[entry]));
        }

        uint32 phaseMask = creature->GetPhaseMask();
        int matchingPhase = sZoneDifficulty->GetLowestMatchingPhase(creature->GetMapId(), phaseMask);
        int8 mode = sZoneDifficulty->NerfInfo[mapId][matchingPhase].Enabled;
        if (matchingPhase != -1)
        {
            if (sZoneDifficulty->HasMythicmode(mode) && sZoneDifficulty->MythicmodeInstanceData[creature->GetMap()->GetInstanceId()])
            {
                if (creature->GetMaxHealth() == newHp)
                {
                    return;
                }
                //if (sZoneDifficulty->IsDebugInfoEnabled)
                //{
                    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Modify creature hp for Mythic Mode: {} to {}", baseHealth, newHp);
                //}
                bool hpIsFull = false;
                if (creature->GetHealthPct() >= 100)
                {
                    hpIsFull = true;
                }
                creature->SetMaxHealth(newHp);
                creature->SetCreateHealth(newHp);
                creature->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, (float)newHp);
                if (hpIsFull)
                {
                    creature->SetHealth(newHp);
                }
                creature->UpdateAllStats();
                creature->ResetPlayerDamageReq();
                return;
            }

            if (sZoneDifficulty->MythicmodeInstanceData[creature->GetMap()->GetInstanceId()] == false)
            {
                if (creature->GetMaxHealth() == newHp)
                {
                    //if (sZoneDifficulty->IsDebugInfoEnabled)
                    //{
                    //    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Modify creature hp for normal mode: {} to {}", baseHealth, baseHealth);
                    //}
                    bool hpIsFull = false;
                    if (creature->GetHealthPct() >= 100)
                    {
                        hpIsFull = true;
                    }
                    creature->SetMaxHealth(baseHealth);
                    creature->SetCreateHealth(baseHealth);
                    creature->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, (float)baseHealth);
                    if (hpIsFull)
                    {
                        creature->SetHealth(baseHealth);
                    }
                    creature->UpdateAllStats();
                    creature->ResetPlayerDamageReq();
                    return;
                }
            }
        }
    }
};

// Add all scripts in one
void AddModZoneDifficultyScripts()
{
    new mod_zone_difficulty_unitscript();
    new mod_zone_difficulty_playerscript();
    new mod_zone_difficulty_petscript();
    new mod_zone_difficulty_worldscript();
    new mod_zone_difficulty_globalscript();
    new mod_zone_difficulty_rewardnpc();
    new mod_zone_difficulty_dungeonmaster();
    new mod_zone_difficulty_allcreaturescript();
}
