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

ZoneDifficulty* ZoneDifficulty::instance()
{
    static ZoneDifficulty instance;
    return &instance;
}

void ZoneDifficulty::LoadMapDifficultySettings()
{
    if (!sZoneDifficulty->IsEnabled)
    {
        return;
    }

    sZoneDifficulty->Rewards.clear();
    sZoneDifficulty->AllRewards.clear();
    sZoneDifficulty->MythicmodeAI.clear();
    sZoneDifficulty->CreatureOverrides.clear();
    sZoneDifficulty->DailyHeroicQuests.clear();
    sZoneDifficulty->MythicmodeLoot.clear();
    sZoneDifficulty->DisallowedBuffs.clear();
    sZoneDifficulty->SpellNerfOverrides.clear();
    sZoneDifficulty->NerfInfo.clear();

    // 挑战Nerf值，数据库没有数据时的默认值 (index 0xFFFFFFFF)
    NerfInfo[DUEL_INDEX][0].HealingNerfPct = 1;
    NerfInfo[DUEL_INDEX][0].AbsorbNerfPct = 1;
    NerfInfo[DUEL_INDEX][0].MeleeDamageBuffPct = 1;
    NerfInfo[DUEL_INDEX][0].SpellDamageBuffPct = 1;

    // TBC每日英雄地下城任务ID 转 地图ID
    HeroicWLKQuestMapList[601] = 13254; // 艾卓-尼鲁布
    HeroicWLKQuestMapList[619] = 13255; // 安卡赫特：古代王国
    HeroicWLKQuestMapList[600] = 13249; // 达克萨隆要塞
    HeroicWLKQuestMapList[604] = 13250; // 古达克
    HeroicWLKQuestMapList[650] = 14199; // 冠军的试炼
    HeroicWLKQuestMapList[595] = 13251; // 净化斯坦索姆
    HeroicWLKQuestMapList[578] = 13247; // 魔环
    HeroicWLKQuestMapList[576] = 13246; // 魔枢
    HeroicWLKQuestMapList[602] = 13253; // 闪电大厅
    HeroicWLKQuestMapList[599] = 13252; // 岩石大厅
    HeroicWLKQuestMapList[574] = 13245; // 乌特加德城堡
    HeroicWLKQuestMapList[575] = 13248; // 乌特加德之巅
    HeroicWLKQuestMapList[608] = 13256; // 紫罗兰监狱 4415

    // 英雄地下城类别标志位 12
    EncounterCounter[601] = 3; // 艾卓-尼鲁布
    EncounterCounter[619] = 4; // 安卡赫特：古代王国
    EncounterCounter[600] = 3; // 达克萨隆要塞
    EncounterCounter[604] = 4; // 古达克
    EncounterCounter[650] = 3; // 冠军的试炼
    EncounterCounter[595] = 5; // 净化斯坦索姆
    EncounterCounter[578] = 4; // 魔环
    EncounterCounter[576] = 5; // 魔枢
    EncounterCounter[602] = 4; // 闪电大厅
    EncounterCounter[599] = 4; // 岩石大厅
    EncounterCounter[574] = 3; // 乌特加德城堡
    EncounterCounter[575] = 4; // 乌特加德之巅
    EncounterCounter[608] = 3; // 紫罗兰监狱

    // 团队地下城类别标志位 > 12
    EncounterCounter[615] = 1; // 黑曜石圣殿
    EncounterCounter[533] = 20; // 纳克萨玛斯

    // 奖励 Icons
    sZoneDifficulty->ItemIcons[ITEMTYPE_MISC] = "|TInterface\\icons\\inv_misc_cape_17:15|t |TInterface\\icons\\inv_misc_gem_topaz_02:15|t |TInterface\\icons\\inv_jewelry_ring_51naxxramas:15|t ";
    sZoneDifficulty->ItemIcons[ITEMTYPE_CLOTH] = "|TInterface\\icons\\inv_chest_cloth_42:15|t ";
    sZoneDifficulty->ItemIcons[ITEMTYPE_LEATHER] = "|TInterface\\icons\\inv_helmet_41:15|t ";
    sZoneDifficulty->ItemIcons[ITEMTYPE_MAIL] = "|TInterface\\icons\\inv_chest_chain_13:15|t ";
    sZoneDifficulty->ItemIcons[ITEMTYPE_PLATE] = "|TInterface\\icons\\inv_chest_plate12:15|t ";
    sZoneDifficulty->ItemIcons[ITEMTYPE_WEAPONS] = "|TInterface\\icons\\inv_mace_25:15|t |TInterface\\icons\\inv_shield_27:15|t |TInterface\\icons\\inv_weapon_crossbow_04:15|t ";

    if (QueryResult result = WorldDatabase.Query("SELECT * FROM zone_difficulty_info"))
    {
        do
        {
            uint32 mapId = (*result)[0].Get<uint32>();
            uint32 phaseMask = (*result)[1].Get<uint32>();
            ZoneDifficultyNerfData data;
            int8 mode = (*result)[6].Get<int8>();
            if (sZoneDifficulty->HasNormalMode(mode))
            {
                data.HealingNerfPct = (*result)[2].Get<float>();
                data.AbsorbNerfPct = (*result)[3].Get<float>();
                data.MeleeDamageBuffPct = (*result)[4].Get<float>();
                data.SpellDamageBuffPct = (*result)[5].Get<float>();
                data.Enabled = data.Enabled | mode;
                sZoneDifficulty->NerfInfo[mapId][phaseMask] = data;
            }
            //Enable字段为64 并且 挑战模式可用
            if (sZoneDifficulty->HasMythicmode(mode) && sZoneDifficulty->MythicmodeEnable)
            {
                data.HealingNerfPctHard = (*result)[2].Get<float>();
                data.AbsorbNerfPctHard = (*result)[3].Get<float>();
                data.MeleeDamageBuffPctHard = (*result)[4].Get<float>();
                data.SpellDamageBuffPctHard = (*result)[5].Get<float>();
                data.Enabled = data.Enabled | mode;
                sZoneDifficulty->NerfInfo[mapId][phaseMask] = data;
            }
            if ((mode & MODE_HARD) != MODE_HARD && (mode & MODE_NORMAL) != MODE_NORMAL)
            {
                LOG_ERROR("module", "挑战模式: 地图ID{} 使用模式({}) 无效, 已忽略.", mode, mapId);
            }

            // 地图ID不能是海洋（无尽之海）和湿地 并且 地图掩码不等于0
            if (mapId == DUEL_INDEX && phaseMask != 0)
            {
                LOG_ERROR("module", "挑战模式: 挑战地图不能是无尽之海和湿地，即ID不能为2402，且Phasemask字段必须是0.", mapId, phaseMask);
            }

        } while (result->NextRow());
    }
    //哪些法术需要被复写
    if (QueryResult result = WorldDatabase.Query("SELECT * FROM zone_difficulty_spelloverrides"))
    {
        do
        {
            if ((*result)[3].Get<uint8>() > 0)
            {
                sZoneDifficulty->SpellNerfOverrides[(*result)[0].Get<uint32>()][(*result)[1].Get<uint32>()].NerfPct = (*result)[2].Get<float>();
                sZoneDifficulty->SpellNerfOverrides[(*result)[0].Get<uint32>()][(*result)[1].Get<uint32>()].ModeMask = (*result)[3].Get<uint32>();
            }

        } while (result->NextRow());
    }
    //不允许的buff
    if (QueryResult result = WorldDatabase.Query("SELECT * FROM zone_difficulty_disallowed_buffs"))
    {
        do
        {
            std::vector<uint32> debuffs;
            uint32 mapId;
            if ((*result)[2].Get<bool>())
            {
                std::string spellString = (*result)[1].Get<std::string>();
                std::vector<std::string_view> tokens = Acore::Tokenize(spellString, ' ', false);

                mapId = (*result)[0].Get<uint32>();
                for (auto token : tokens)
                {
                    if (token.empty())
                    {
                        continue;
                    }

                    uint32 spell;
                    if ((spell = Acore::StringTo<uint32>(token).value()))
                    {
                        debuffs.push_back(spell);
                    }
                    else
                    {
                        LOG_ERROR("module", "挑战模式: 禁用法术({})无效, 跳过.", spell);
                    }
                }
                sZoneDifficulty->DisallowedBuffs[mapId] = debuffs;
            }
        } while (result->NextRow());
    }
    //查询挑战模式下 哪些生物会被计分
    if (QueryResult result = WorldDatabase.Query("SELECT * FROM zone_difficulty_mythicmode_instance_data"))
    {
        do
        {
            ZoneDifficultyMythicmodeMapData data;
            uint32 MapID = (*result)[0].Get<uint32>();
            data.EncounterEntry = (*result)[1].Get<uint32>();
            data.Override = (*result)[2].Get<uint32>();
            data.RewardType = (*result)[3].Get<uint8>();

            sZoneDifficulty->MythicmodeLoot[MapID].push_back(data);
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: New creature for map {} with entry: {}", MapID, data.EncounterEntry);

            Expansion[MapID] = data.RewardType;

        } while (result->NextRow());
    }
    else
    {
        LOG_ERROR("module", "挑战模式: 查询挑战模式计分生物失败");
    }

    //每日任务表 356TBC日常任务 15678 WLK日常任务
    if (QueryResult result = WorldDatabase.Query("SELECT entry FROM `pool_quest` WHERE `pool_entry`=15678"))
    {
        do
        {
            sZoneDifficulty->DailyHeroicQuests.push_back((*result)[0].Get<uint32>());
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Adding daily heroic quest with id {}.", (*result)[0].Get<uint32>());
        } while (result->NextRow());
    }
    else
    {
        LOG_ERROR("module", "挑战模式: WLK日常任务查询失败");
    }

    //生物生命值系数
    if (QueryResult result = WorldDatabase.Query("SELECT * FROM zone_difficulty_mythicmode_creatureoverrides"))
    {
        do
        {
            uint32 creatureEntry = (*result)[0].Get<uint32>();
            float hpModifier = (*result)[1].Get<float>();
            bool enabled = (*result)[2].Get<bool>();

            if (enabled)
            {
                if (hpModifier != 0)
                {
                    sZoneDifficulty->CreatureOverrides[creatureEntry] = hpModifier;
                }
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: New creature with entry: {} has exception for hp: {}", creatureEntry, hpModifier);
            }
        } while (result->NextRow());
    }
    else
    {
        LOG_ERROR("module", "挑战模式: 没有查询到哪些生物需要被重写生命值系数");
    }

    //生物新的法术AI 比如：永恒时光术师 ，30%的几率释放一个魔法 43242 类似大秘境的ai技能
    if (QueryResult result = WorldDatabase.Query("SELECT * FROM zone_difficulty_mythicmode_ai"))
    {
        do
        {
            bool enabled = (*result)[12].Get<bool>();

            if (enabled)
            {
                uint32 creatureEntry = (*result)[0].Get<uint32>();
                ZoneDifficultyHAI data;
                data.Chance = (*result)[1].Get<uint8>();
                data.Spell = (*result)[2].Get<uint32>();
                data.Spellbp0 = (*result)[3].Get<int32>();
                data.Spellbp1 = (*result)[4].Get<int32>();
                data.Spellbp2 = (*result)[5].Get<int32>();
                data.Target = (*result)[6].Get<uint8>();
                data.TargetArg = (*result)[7].Get<int8>();
                data.TargetArg2 = (*result)[8].Get<uint8>();
                data.Delay = (*result)[9].Get<Milliseconds>();
                data.Cooldown = (*result)[10].Get<Milliseconds>();
                data.Repetitions = (*result)[11].Get<uint8>();
                data.TriggeredCast = (*result)[13].Get<bool>();
                //几率不为0 法术不为0 目标为（施法者、附近的敌人、附近的队友、附近队友、宠物、指定的敌人、施法者的目的地 参照Spell.dbc 90-92列）
                if (data.Chance != 0 && data.Spell != 0 && ((data.Target >= 1 && data.Target <= 6) || data.Target == 18))
                {
                    sZoneDifficulty->MythicmodeAI[creatureEntry].push_back(data);
                    LOG_INFO("module", "挑战模式: 生物{}增加新的法术AI，法术({})", creatureEntry, data.Spell);
                }
                else
                {
                    LOG_ERROR("module", "挑战模式: 生物{}增加新的法术AI({})无效, 法术目标:{}", creatureEntry, data.Spell, data.Target);
                }
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: New creature with entry: {} has exception for hp: {}", creatureEntry, hpModifier);
            }
        } while (result->NextRow());
    }
    else
    {
        LOG_ERROR("module", "挑战模式: 没有获取到新的生物AI");
    }

    //挑战模式奖励
    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Starting load of rewards.");
    if (QueryResult result = WorldDatabase.Query("SELECT ContentType, ItemType, Entry, Price, Enchant, EnchantSlot, Achievement, Enabled FROM zone_difficulty_mythicmode_rewards"))
    {
        /* debug
         * uint32 i = 0;
         * end debug
         */
        do
        {
            /* debug
             * ++i;
             * end debug
             */
            ZoneDifficultyRewardData data;
            uint32 contentType = (*result)[0].Get<uint32>();
            uint32 itemType = (*result)[1].Get<uint32>();
            data.Entry = (*result)[2].Get<uint32>();
            data.Price = (*result)[3].Get<uint32>();
            data.Enchant = (*result)[4].Get<uint32>();
            data.EnchantSlot = (*result)[5].Get<uint8>();
            data.Achievement = (*result)[6].Get<int32>();
            bool enabled = (*result)[7].Get<bool>();

            if (enabled)
            {
                if (data.Achievement >= 0)
                {
                    sZoneDifficulty->Rewards[contentType][itemType].push_back(data);
                    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Loading item with entry {} has enchant {} in slot {}. contentType: {} itemType: {}", data.Entry, data.Enchant, data.EnchantSlot, contentType, itemType);
                }
                else
                {
                    sZoneDifficulty->TierRewards[contentType] = data;
                    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Loading tier reward with entry {} has enchant {} in slot {}. contentType: {} itemType: {}", data.Entry, data.Enchant, data.EnchantSlot, contentType, itemType);
                }
                sZoneDifficulty->AllRewards[contentType].push_back(data);
            }
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Total items in Rewards map: {}.", i);
        } while (result->NextRow());
    }
    else
    {
        LOG_ERROR("module", "挑战模式: 没有找到挑战模式奖励");
    }
}

/**
 *  @brief 从表里面查询保存的挑战模式副本实例
 *
 *  `InstanceID` INT NOT NULL DEFAULT 0,
 *  `MythicmodeOn` TINYINT NOT NULL DEFAULT 0,
 *
 *  排除不在地图IDS中的实例 并且删除已经不存在的实例
 *  白话：更新已经保存的挑战模式副本实例ID，已经更新的副本删除记录
 *  Exclude data not in the IDs stored in GetInstanceIDs() and delete
 *  zone_difficulty_instance_saves for instances that no longer exist.
 */
void ZoneDifficulty::LoadMythicmodeInstanceData()
{
    std::vector<bool> instanceIDs = sMapMgr->GetInstanceIDs();
    /* debugging
    * for (int i = 0; i < int(instanceIDs.size()); i++)
    * {
    *   LOG_INFO("module", "MOD-ZONE-DIFFICULTY: ZoneDifficulty::LoadMythicmodeInstanceData: id {} exists: {}:", i, instanceIDs[i]);
    * }
    * end debugging
    */
    if (QueryResult result = CharacterDatabase.Query("SELECT * FROM zone_difficulty_instance_saves"))
    {
        do
        {
            uint32 InstanceId = (*result)[0].Get<uint32>();
            bool MythicmodeOn = (*result)[1].Get<bool>();

            if (instanceIDs[InstanceId])
            {
                //LOG_INFO("module", "挑战模式: 从数据库加载挑战模式实例，ID：{}， 开启状态:{}", InstanceId, MythicmodeOn);
                sZoneDifficulty->MythicmodeInstanceData[InstanceId] = MythicmodeOn;
            }
            else
            {
                CharacterDatabase.Execute("DELETE FROM zone_difficulty_instance_saves WHERE InstanceID = {}", InstanceId);
            }


        } while (result->NextRow());
    }
}

/**
 *  @brief Loads the score data and encounter logs from the database.
 *  Fetch from zone_difficulty_mythicmode_score.
 * 
 *  从数据库中加载挑战模式分数
 *
 *  `CharacterGuid` INT NOT NULL DEFAULT 0,
 *  `Type` TINYINT NOT NULL DEFAULT 0,
 *  `Score` INT NOT NULL DEFAULT 0,
 *
 *  Fetch from zone_difficulty_encounter_logs.
 *  `InstanceId` INT NOT NULL DEFAULT 0,
 *  `TimestampStart` INT NOT NULL DEFAULT 0,
 *  `TimestampEnd` INT NOT NULL DEFAULT 0,
 *  `Map` INT NOT NULL DEFAULT 0,
 *  `BossId` INT NOT NULL DEFAULT 0,
 *  `PlayerGuid` INT NOT NULL DEFAULT 0,
 *  `Mode` INT NOT NULL DEFAULT 0,
 *
 */
void ZoneDifficulty::LoadMythicmodeScoreData()
{
    if (QueryResult result = CharacterDatabase.Query("SELECT * FROM zone_difficulty_mythicmode_score"))
    {
        do
        {
            uint32 GUID = (*result)[0].Get<uint32>();
            uint8 Type = (*result)[1].Get<uint8>();
            uint32 Score = (*result)[2].Get<uint32>();

            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Loading from DB for player with GUID {}: Type = {}, Score = {}", GUID, Type, Score);
            sZoneDifficulty->MythicmodeScore[GUID][Type] = Score;

        } while (result->NextRow());
    }
    if (QueryResult result = CharacterDatabase.Query("SELECT `Map`, `BossId`, `PlayerGuid` FROM zone_difficulty_encounter_logs WHERE `Mode` = 64"))
    {
        do
        {
            uint32 MapId = (*result)[0].Get<uint32>();
            uint8 BossID = (*result)[1].Get<uint32>();
            uint32 PlayerGuid = (*result)[2].Get<uint32>();

            // Set all BossID which aren't true to false for that mapID
            if (sZoneDifficulty->Logs[PlayerGuid].find(MapId) == sZoneDifficulty->Logs[PlayerGuid].end())
            {
                for (int i = 0; i < sZoneDifficulty->EncounterCounter[MapId]; ++i)
                {
                    //LOG_INFO("module", "挑战模式: 初始化挑战日志，BOSS战斗记录初始化为false, PlayerGuid {} in MapId {} for BossId {}", PlayerGuid, MapId, i);
                    sZoneDifficulty->Logs[PlayerGuid][MapId][i] = false;
                }
            }
            //LOG_INFO("module", "挑战模式: 把挑战模式BOSS的战斗日志设置为true PlayerGuid {} in MapId {} for BossId {}", PlayerGuid, MapId, BossID);
            sZoneDifficulty->Logs[PlayerGuid][MapId][BossID] = true;
        } while (result->NextRow());
    }
}

/**
 *  @brief Sends a whisper to all members of the player's raid in the same instance as the creature.
 *  给所有的玩家悄悄话
 *
 *  @param message The message which should be sent to the <Player>.
 *  @param creature The creature who sends the whisper.
 *  @param player The object of the player, whose whole group should receive the message.
 */
void ZoneDifficulty::SendWhisperToRaid(std::string message, Creature* creature, Player* player)
{
    if (Map* map = creature->GetMap())
    {
        map->DoForAllPlayers([&, player, creature](Player* mapPlayer) {
            if (creature && player)
            {
                if (mapPlayer->IsInSameGroupWith(player))
                {
                    creature->Whisper(message, LANG_UNIVERSAL, mapPlayer);
                }
            }
        });
    }
}

//物品类别转换
std::string ZoneDifficulty::GetItemTypeString(uint32 type)
{
    std::string typestring;
    switch (type)
    {
    case ITEMTYPE_MISC:
        typestring = "背部、手指、颈部和饰品";
        break;
    case ITEMTYPE_CLOTH:
        typestring = "布甲";
        break;
    case ITEMTYPE_LEATHER:
        typestring = "皮甲";
        break;
    case ITEMTYPE_MAIL:
        typestring = "锁甲";
        break;
    case ITEMTYPE_PLATE:
        typestring = "板甲";
        break;
    case ITEMTYPE_WEAPONS:
        typestring = "武器、副手和远程";
        break;
    default:
        LOG_ERROR("module", "MOD-ZONE-DIFFICULTY: Unknown type {} in ZoneDifficulty::GetItemTypeString.", type);
    }
    return typestring;
}

//副本类别转换
std::string ZoneDifficulty::GetContentTypeString(uint32 type)
{
    std::string typestring;
    switch (type)
    {
    case TYPE_VANILLA:
        typestring = "经典旧世地下城";
        break;
    case TYPE_RAID_MC:
        typestring = "熔火之心";
        break;
    case TYPE_RAID_ONY:
        typestring = "奥妮克希亚的巢穴";
        break;
    case TYPE_RAID_BWL:
        typestring = "黑翼之巢";
        break;
    case TYPE_RAID_ZG:
        typestring = "祖尔格拉布";
        break;
    case TYPE_RAID_AQ20:
        typestring = "安其拉废墟";
        break;
    case TYPE_RAID_AQ40:
        typestring = "安其拉神殿";
        break;
    case TYPE_HEROIC_TBC:
        typestring = "燃烧的远征英雄地下城";
        break;
    case TYPE_RAID_T4:
        typestring = "T4团队副本";
        break;
    case TYPE_RAID_T5:
        typestring = "T5团队副本";
        break;
    case TYPE_RAID_T6:
        typestring = "T6团队副本";
        break;
    case TYPE_HEROIC_WOTLK:
        typestring = "巫妖王之怒英雄地下城";
        break;
    case TYPE_RAID_T7:
        typestring = "T7团队副本";
        break;
    case TYPE_RAID_T8:
        typestring = "T8团队副本";
        break;
    case TYPE_RAID_T9:
        typestring = "T9团队副本";
        break;
    case TYPE_RAID_T10:
        typestring = "T10团队副本";
        break;
    default:
        typestring = "-";
    }
    return typestring;
}

/**
 *  @brief 给地下城的所有人积分
 *
 *  @param map The map where the player is currently.
 *  @param type The type of instance the score is awarded for.
 */
void ZoneDifficulty::AddMythicmodeScore(Map* map, uint32 type, uint32 score)
{
    if (!map)
    {
        LOG_ERROR("module", "挑战模式：地图没有找到.");
        return;
    }
    if (type > 255)
    {
        LOG_ERROR("module", "挑战模式：地图类型{}发生错误.地图ID：{}.", type, map->GetInstanceId());
        return;
    }
    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Called AddMythicmodeScore for map id: {} and type: {}", map->GetId(), type);
    Map::PlayerList const& PlayerList = map->GetPlayers();
    for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
    {
        Player* player = i->GetSource();
        if (sZoneDifficulty->MythicmodeScore.find(player->GetGUID().GetCounter()) == sZoneDifficulty->MythicmodeScore.end())
        {
            sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][type] = score;
        }
        else if (sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()].find(type) == sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()].end())
        {
            sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][type] = score;
        }
        else
        {
            sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][type] = sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][type] + score;
        }

        //if (sZoneDifficulty->IsDebugInfoEnabled)
        //{
        //    LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Player {} new score: {}", player->GetName(), sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][type]);
        //}

        std::string typestring = sZoneDifficulty->GetContentTypeString(type);
        ChatHandler(player->GetSession()).PSendSysMessage("你获得[%s]挑战模式积分%i点, 共有积分: %i", typestring, score, sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][type]);
        CharacterDatabase.Execute("REPLACE INTO zone_difficulty_mythicmode_score VALUES({}, {}, {})", player->GetGUID().GetCounter(), type, sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][type]);
    }
}

/**
 * 发物品
*/
void ZoneDifficulty::GiveMythicmodeItem(Map* map, uint32 type)
{
    if (!map)
    {
        LOG_ERROR("module", "挑战模式：地图没有找到.");
        return;
    }
    if (type > 255)
    {
        LOG_ERROR("module", "挑战模式：地图类型{}发生错误.地图ID：{}.", type, map->GetInstanceId());
        return;
    }
    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Called AddMythicmodeScore for map id: {} and type: {}", map->GetId(), type);
    Map::PlayerList const& PlayerList = map->GetPlayers();
    for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
    {
        Player* player = i->GetSource();
        player->AddItem(69999, 4);

        uint32 random = urand(0, 100);
        if(random < 50) {
            return ;
        }

        std::vector<ZoneDifficultyRewardData> rewards = sZoneDifficulty->AllRewards[type];
        int size = int(rewards.size());
        if(size <= 0) {
            return ;
        }
        uint32 roll = urand(0, 1000);
        uint32 Entry = rewards[roll % size].Entry;
        if(Entry) {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(Entry);
            if(player->CanUseItem(proto) != EQUIP_ERR_OK){
                LOG_INFO("module", "目标物品无法使用，放弃掉落");
                return ;
            }
            player->AddItem(Entry, 1);
        }
    }
}

/**
 *  @brief Reduce the score of players when they pay for rewards.
 *  换物品，扣分
 *
 *  @param player The one who pays with their score.
 *  @param type The type of instance the score is deducted for.
 */
void ZoneDifficulty::DeductMythicmodeScore(Player* player, uint32 type, uint32 score)
{
    // NULL check happens in the calling function
    if (sZoneDifficulty->IsDebugInfoEnabled)
    {
        LOG_INFO("module", "挑战模式: 玩家GUID{}在[{}]积分扣除 {}.", player->GetGUID().GetCounter(), type, score);
    }
    sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][type] = sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][type] - score;
    CharacterDatabase.Execute("REPLACE INTO zone_difficulty_mythicmode_score VALUES({}, {}, {})", player->GetGUID().GetCounter(), type, sZoneDifficulty->MythicmodeScore[player->GetGUID().GetCounter()][type]);
}

/**
 * @brief Send and item to the player using the data from sZoneDifficulty->Rewards.
 *  发送兑换的奖励物品
 *
 * @param player The recipient of the mail.
 * @param category The content level e.g. TYPE_HEROIC_TBC.
 * @param itemType The type of the item e.g. ITEMTYPE_CLOTH.
 * @param id the id in the vector.
 */
void ZoneDifficulty::SendItem(Player* player, uint32 category, uint32 itemType, uint32 id)
{
    //Check if a full tier cleareance reward is meant (itemType 99)
    ItemTemplate const* itemTemplate;
    if (itemType == 99)
    {
        itemTemplate = sObjectMgr->GetItemTemplate(sZoneDifficulty->TierRewards[category].Entry);
    }
    else
    {
        itemTemplate = sObjectMgr->GetItemTemplate(sZoneDifficulty->Rewards[category][itemType][id].Entry);
    }

    if (!itemTemplate)
    {
        LOG_ERROR("module", "挑战模式: 物品发送失败 奖励类别 {}, 物品类别 {}, 物品ID {}.", category, itemType, id);
        return;
    }

    ObjectGuid::LowType senderGuid = player->GetGUID().GetCounter();

    // fill mail
    MailDraft draft(REWARD_MAIL_SUBJECT, REWARD_MAIL_BODY);
    MailSender sender(MAIL_NORMAL, senderGuid, MAIL_STATIONERY_GM);
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    if (itemType == 99)
    {
        if (Item* item = Item::CreateItem(sZoneDifficulty->TierRewards[category].Entry, 1, player))
        {
            if (sZoneDifficulty->TierRewards[category].EnchantSlot != 0 && sZoneDifficulty->TierRewards[category].Enchant != 0)
            {
                item->SetEnchantment(EnchantmentSlot(sZoneDifficulty->TierRewards[category].EnchantSlot), sZoneDifficulty->TierRewards[category].Enchant, 0, 0, player->GetGUID());
                player->ApplyEnchantment(item, EnchantmentSlot(sZoneDifficulty->TierRewards[category].EnchantSlot), true, true, true);
            }
            item->SaveToDB(trans); // save for prevent lost at next mail load, if send fail then item will deleted
            draft.AddItem(item);
        }
    }
    else
    {
        if (Item* item = Item::CreateItem(sZoneDifficulty->Rewards[category][itemType][id].Entry, 1, player))
        {
            if (sZoneDifficulty->Rewards[category][itemType][id].EnchantSlot != 0 && sZoneDifficulty->Rewards[category][itemType][id].Enchant != 0)
            {
                item->SetEnchantment(EnchantmentSlot(sZoneDifficulty->Rewards[category][itemType][id].EnchantSlot), sZoneDifficulty->Rewards[category][itemType][id].Enchant, 0, 0, player->GetGUID());
                player->ApplyEnchantment(item, EnchantmentSlot(sZoneDifficulty->Rewards[category][itemType][id].EnchantSlot), true, true, true);
            }
            item->SaveToDB(trans); // save for prevent lost at next mail load, if send fail then item will deleted
            draft.AddItem(item);
        }
    }
    draft.SendMailTo(trans, MailReceiver(player, senderGuid), sender);
    CharacterDatabase.CommitTransaction(trans);
}

/**
 *  @brief Check if the map has assigned any data to tune it.
 *  检查地图是不是挑战模式地图
 *
 *  @param map The ID of the <Map> to check.
 *  @return The result as bool.
 */
bool ZoneDifficulty::IsMythicmodeMap(uint32 mapId)
{
    if (!sZoneDifficulty->MythicmodeEnable)
    {
        return false;
    }
    if (sZoneDifficulty->MythicmodeLoot.find(mapId) == sZoneDifficulty->MythicmodeLoot.end())
    {
        return false;
    }
    return true;
}

/**
 *  @brief 检查目标是玩家、宠物 或者 守卫
 *
 * @param target 目标
 * @return 如果是玩家、宠物、守卫则返回true
 */
bool ZoneDifficulty::IsValidNerfTarget(Unit* target)
{
    return target->IsPlayer() || target->IsPet() || target->IsGuardian() || target->IsNPCBotOrPet();
}

/**
 *  @brief Checks if the element is one of the uint32 values in the vector.
 *  一个奇怪的函数 看不懂
 *
 * @param vec A vector
 * @param element One element which can potentially be part of the values in the vector
 * @return The result as bool
 */
bool ZoneDifficulty::VectorContainsUint32(std::vector<uint32> vec, uint32 element)
{
    return find(vec.begin(), vec.end(), element) != vec.end();
}

/**
 * @brief Checks if the instance and spelloverride have matching modes
 *  检查法术是否需要被跳过Nerf
 *
 * @param instanceId
 * @param spellId
 * @param mapId
 * @return The result as bool
 */
 bool ZoneDifficulty::OverrideModeMatches(uint32 instanceId, uint32 spellId, uint32 mapId)
{
    if ((sZoneDifficulty->HasMythicmode(sZoneDifficulty->SpellNerfOverrides[spellId][mapId].ModeMask) && sZoneDifficulty->MythicmodeInstanceData[instanceId]) ||
        (sZoneDifficulty->HasNormalMode(sZoneDifficulty->SpellNerfOverrides[spellId][mapId].ModeMask) && !sZoneDifficulty->MythicmodeInstanceData[instanceId]))
    {
        return true;
    }
    else
    {
        return false;
    }
}

/**
 *  @brief 检查目标是否在战斗中，并且他们的目标是否是有效对象。nerf需要被显式的被应用
 *
 * @param target 目标
 * @return The result as bool
 */
bool ZoneDifficulty::ShouldNerfInDuels(Unit* target)
{
    //目标所在空间不是战斗区域 
    if (target->GetAreaId() != DUEL_AREA)
    {
        return false;
    }

    if (target->ToTempSummon() && target->ToTempSummon()->GetSummoner())
    {
        target = target->ToTempSummon()->GetSummoner()->ToUnit();
    }

    if (!target->GetAffectingPlayer())
    {
        return false;
    }

    if (!target->GetAffectingPlayer()->duel)
    {
        return false;
    }

    if (target->GetAffectingPlayer()->duel->State != DUEL_STATE_IN_PROGRESS)
    {
        return false;
    }

    if (!target->GetAffectingPlayer()->duel->Opponent)
    {
        return false;
    }

    return true;
}

/**
 *  @brief Find the lowest phase for the target's mapId, which has a db entry for the target's map
 *  and at least partly matches the target's phase.
 *  检查地图的nerf值
 *
 *  `mapId` can be the id of a map or `DUEL_INDEX` to use the duel specific settings.
 *  Return -1 if none found.
 *
 * @param mapId
 * @param phaseMask Bitmask of all phases where the unit is currently visible
 * @return the lowest phase which should be altered for this map and the unit is visible in
 */
int32 ZoneDifficulty::GetLowestMatchingPhase(uint32 mapId, uint32 phaseMask)
{
    // Check if there is an entry for the mapId at all
    if (sZoneDifficulty->NerfInfo.find(mapId) != sZoneDifficulty->NerfInfo.end())
    {

        // Check if 0 is assigned as a phase to cover all phases
        if (sZoneDifficulty->NerfInfo[mapId].find(0) != sZoneDifficulty->NerfInfo[mapId].end())
        {
            return 0;
        }

        // Check all $key in [mapId][$key] if they match the target's visible phases
        for (auto const& [key, value] : sZoneDifficulty->NerfInfo[mapId])
        {
            if (key & phaseMask)
            {
                return key;
            }
        }
    }
    return -1;
}

/**
 *  @brief Store the MythicmodeInstanceData in the database for the given instance id.
 *  zone_difficulty_instance_saves is used to store the data.
 *  保存挑战模式的ID
 *  @param InstanceID INT NOT NULL DEFAULT 0,
 *  @param MythicmodeOn TINYINT NOT NULL DEFAULT 0,
 */
void ZoneDifficulty::SaveMythicmodeInstanceData(uint32 instanceId)
{
    if (sZoneDifficulty->MythicmodeInstanceData.find(instanceId) == sZoneDifficulty->MythicmodeInstanceData.end())
    {
        //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: ZoneDifficulty::SaveMythicmodeInstanceData: InstanceId {} not found in MythicmodeInstanceData.", instanceId);
        return;
    }
    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: ZoneDifficulty::SaveMythicmodeInstanceData: Saving instanceId {} with MythicmodeOn {}", instanceId, sZoneDifficulty->MythicmodeInstanceData[instanceId]);
    CharacterDatabase.Execute("REPLACE INTO zone_difficulty_instance_saves (InstanceID, MythicmodeOn) VALUES ({}, {})", instanceId, sZoneDifficulty->MythicmodeInstanceData[instanceId]);
}

//执行挑战模式 法术AI定义
void ZoneDifficulty::MythicmodeEvent(Unit* unit, uint32 entry, uint32 key)
{
    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: MythicmodeEvent for entry {} with key {}", entry, key);
    if (unit && unit->IsAlive())
    {
        if (!unit->IsInCombat())
        {
            unit->m_Events.CancelEventGroup(EVENT_GROUP);
            return;
        }
        //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: MythicmodeEvent IsInCombat for entry {} with key {}", entry, key);
        // 如果目标正在施法，1秒后重试
        if (unit->HasUnitState(UNIT_STATE_CASTING))
        {
            //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: MythicmodeEvent Re-schedule AI event in 1s because unit is casting for entry {} with key {}", entry, key);
            unit->m_Events.AddEventAtOffset([unit, entry, key]()
                {
                    sZoneDifficulty->MythicmodeEvent(unit, entry, key);
                }, 1s, EVENT_GROUP);
            return;
        }

        //重置计时器
        if (sZoneDifficulty->MythicmodeAI[entry][key].Repetitions == 0)
        {
            unit->m_Events.AddEventAtOffset([unit, entry, key]()
                {
                    sZoneDifficulty->MythicmodeEvent(unit, entry, key);
                }, sZoneDifficulty->MythicmodeAI[entry][key].Cooldown, EVENT_GROUP);
        }

        ZoneDifficultyHAI mythicAI = sZoneDifficulty->MythicmodeAI[entry][key];
        bool has_bp0 = mythicAI.Spellbp0;
        bool has_bp1 = mythicAI.Spellbp1;
        bool has_bp2 = mythicAI.Spellbp2;

        //Multiple targets
        if (mythicAI.Target == TARGET_PLAYER_DISTANCE)
        {
            auto const& threatlist = unit->GetThreatMgr().GetThreatList();

            for (auto itr = threatlist.begin(); itr != threatlist.end(); ++itr)
            {
                Unit* target = (*itr)->getTarget();
                if (!unit->IsWithinDist(target, mythicAI.TargetArg))
                {
                    continue;
                }

                std::string targetName = target ? target->GetName() : "没有找到目标";
                if (!has_bp0 && !has_bp1 && !has_bp2)
                {
                    unit->CastSpell(target, mythicAI.Spell, mythicAI.TriggeredCast);
                    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Creature casting MythicmodeAI spell: {} at target {}", mythicAI.Spell, targetName);
                }
                else
                {
                    unit->CastCustomSpell(target, mythicAI.Spell,
                        has_bp0 ? &mythicAI.Spellbp0 : NULL,
                        has_bp1 ? &mythicAI.Spellbp1 : NULL,
                        has_bp2 ? &mythicAI.Spellbp2 : NULL,
                        mythicAI.TriggeredCast);
                    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Creature casting MythicmodeAI spell: {} at target {} with custom values.", mythicAI.Spell, targetName);
                }
            }
            return;
        }

        // Select target
        Unit* target = nullptr;
        if (mythicAI.Target == TARGET_SELF)
        {
            target = unit;
        }
        else if (mythicAI.Target == TARGET_VICTIM)
        {
            target = unit->GetVictim();
        }
        else
        {
            switch (mythicAI.Target)
            {
                case TARGET_HOSTILE_AGGRO_FROM_TOP:
                {
                    float range = 200.0f;
                    if (mythicAI.TargetArg > 0)
                    {
                        range = mythicAI.TargetArg;
                    }
                    target = unit->GetAI()->SelectTarget(SelectTargetMethod::MaxThreat, mythicAI.TargetArg2, range, true);

                    if (!target)
                    {
                        //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Fall-back to GetVictim()");
                        target = unit->GetVictim();
                    }
                    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Selecting target type TARGET_HOSTILE_AGGRO_FROM_TOP with range TargetArg {} and position on threat-list TargetArg2 {}.", mythicAI.TargetArg, range);
                    break;
                }
                case TARGET_HOSTILE_AGGRO_FROM_BOTTOM:
                {
                    float range = 200.0f;
                    if (mythicAI.TargetArg2 > 0)
                    {
                        range = mythicAI.TargetArg2;
                    }
                    target = unit->GetAI()->SelectTarget(SelectTargetMethod::MinThreat, mythicAI.TargetArg, range, true);

                    if (!target)
                    {
                        //LOG_INFO("module", "Fall-back to GetVictim()");
                        target = unit->GetVictim();
                    }
                    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Selecting target type TARGET_HOSTILE_AGGRO_FROM_TOP with range TargetArg {} and position on threat-list TargetArg2 {}.", mythicAI.TargetArg, range);
                    break;
                }
                case TARGET_HOSTILE_RANDOM:
                {
                    target = unit->GetAI()->SelectTarget(SelectTargetMethod::Random, 0, mythicAI.TargetArg, true);
                    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Selecting target type TARGET_HOSTILE_RANDOM with max range {}.", mythicAI.TargetArg);
                    break;
                    }
                case TARGET_HOSTILE_RANDOM_NOT_TOP:
                {
                    target = unit->GetAI()->SelectTarget(SelectTargetMethod::Random, 0, mythicAI.TargetArg, true, false);
                    //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Selecting target type TARGET_HOSTILE_RANDOM_NOT_TOP with max range {}.", mythicAI.TargetArg);
                    break;
                }
                default:
                {
                    LOG_ERROR("module", "MOD-ZONE-DIFFICULTY: Unknown type for Target: {} in zone_difficulty_mythicmode_ai", mythicAI.Target);
                }
            }
        }

        if (!target && mythicAI.Target != TARGET_NONE)
        {
            Unit* victim = nullptr;
            if (mythicAI.TargetArg > 0)
            {
                if (unit->IsInRange(victim, 0, mythicAI.TargetArg, true))
                {
                    target = victim;
                }
            }
            else if (mythicAI.TargetArg < 0)
            {
                if (unit->IsInRange(victim, mythicAI.TargetArg, 0, true))
                {
                    target = victim;
                }
            }

        }

        if (target || mythicAI.Target == TARGET_NONE)
        {
            std::string targetName = target ? target->GetName() : "没有找到目标";

            if (!has_bp0 && !has_bp1 && !has_bp2)
            {
                unit->CastSpell(target, mythicAI.Spell, mythicAI.TriggeredCast);
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Creature casting MythicmodeAI spell: {} at target {}", mythicAI.Spell, targetName);
            }
            else
            {
                unit->CastCustomSpell(target, mythicAI.Spell,
                    has_bp0 ? &mythicAI.Spellbp0 : NULL,
                    has_bp1 ? &mythicAI.Spellbp1 : NULL,
                    has_bp2 ? &mythicAI.Spellbp2 : NULL,
                    mythicAI.TriggeredCast);
                //LOG_INFO("module", "MOD-ZONE-DIFFICULTY: Creature casting MythicmodeAI spell: {} at target {} with custom values.", mythicAI.Spell, targetName);
            }
        }
        else
        {
            LOG_ERROR("module", "MOD-ZONE-DIFFICULTY: No target could be found for unit with entry {} and harmodeAI key {}.", entry, key);
        }
    }
}

/**
 * 查找奖励类别
*/
bool ZoneDifficulty::HasCompletedFullTier(uint32 category, uint32 playerGuid)
{
    //LOG_INFO("module", "MOD-ZONE-DIFFCULTY: Executing HasCompletedFullTier for category {} playerGUID {}.", category, playerGuid);
    std::vector<uint32> MapList;
    switch (category)
    {
    case TYPE_HEROIC_TBC:
        //585 is Magister's Terrace. Only add when released.
        MapList = { 574, 575, 576, 578, 595, 599, 600, 601, 602, 604, 608, 619, 650 };
        break;
    case TYPE_RAID_T4:
        MapList = { 615, 533};
        break;
    default:
        // LOG_ERROR("module", "挑战模式: 查找的类别不支持 {}", category);
        return false;
        break;
    }

    for (uint32 mapId : MapList)
    {
        //LOG_INFO("module", "MOD-ZONE-DIFFCULTY: Checking HasCompletedFullTier for mapId {}.", mapId);
        if (sZoneDifficulty->EncounterCounter.find(mapId) == sZoneDifficulty->EncounterCounter.end())
        {
            // LOG_ERROR("module", "挑战模式: 查找的类别不支持 {}", mapId);
            return false;
        }
        for (uint8 i = 0; i < sZoneDifficulty->EncounterCounter[mapId]; ++i)
        {
            //LOG_INFO("module", "挑战模式: Checking HasCompletedFullTier for BossId {}: {}.", i, sZoneDifficulty->Logs[playerGuid][mapId][i]);
            //有BOSS没有打 返回false
            if (!sZoneDifficulty->Logs[playerGuid][mapId][i])
            {
                return false;
            }
        }
    }
    return true;
}
