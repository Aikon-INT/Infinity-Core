/*
 * Copyright (C) 2008-2013 Trinitycore <http://www.trinitycore.org/>
 * Copyright (C) 2009-2014 Infinitycore <http://www.infinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Common.h"
#include "Transport.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Path.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "DBCStores.h"
#include "World.h"
#include "GameObjectAI.h"
#include "MapReference.h"
#include "Player.h"
#include "Cell.h"
#include "CellImpl.h"
void MapManager::LoadTransports()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT guid, entry, name, period, ScriptName FROM transports");

    if (!result)
    {
        IC_LOG_INFO("entities.transport", ">> Loaded 0 transports. DB table `transports` is empty!");
        return;
    }

    uint32 count = 0;

    do
    {

        Field* fields = result->Fetch();
        uint32 lowguid = fields[0].GetUInt32();
        uint32 entry = fields[1].GetUInt32();
        std::string name = fields[2].GetString();
        uint32 period = fields[3].GetUInt32();
        uint32 scriptId = sObjectMgr->GetScriptId(fields[4].GetCString());

        GameObjectTemplate const* goinfo = sObjectMgr->GetGameObjectTemplate(entry);

        if (!goinfo)
        {
            IC_LOG_ERROR("entities.transport", "Transport ID:%u, Name: %s, will not be loaded, gameobject_template missing", entry, name.c_str());
            continue;
        }

        if (goinfo->type != GAMEOBJECT_TYPE_MO_TRANSPORT)
        {
            IC_LOG_ERROR("entities.transport", "Transport ID:%u, Name: %s, will not be loaded, gameobject_template type wrong", entry, name.c_str());
            continue;
        }

        // IC_LOG_INFO("entities.transport", "Loading transport %d between %s, %s", entry, name.c_str(), goinfo->name);

        std::set<uint32> mapsUsed;

        Transport* t = new Transport(period, scriptId);
        if (!t->GenerateWaypoints(goinfo->moTransport.taxiPathId, mapsUsed))
            // skip transports with empty waypoints list
        {
            IC_LOG_ERROR("entities.transport", "Transport (path id %u) path size = 0. Transport ignored, check DBC files or transport GO data0 field.", goinfo->moTransport.taxiPathId);
            delete t;
            continue;
        }

        float x = t->m_WayPoints[0].x;
        float y = t->m_WayPoints[0].y;
        float z = t->m_WayPoints[0].z;
        uint32 mapid = t->m_WayPoints[0].mapid;
        float o = 1.0f;

         // creates the Gameobject
        if (!t->Create(lowguid, entry, mapid, x, y, z, o, 255, 0))
        {
            delete t;
            continue;
        }

        m_Transports.insert(t);

        for (std::set<uint32>::const_iterator i = mapsUsed.begin(); i != mapsUsed.end(); ++i)
            m_TransportsByMap[*i].insert(t);

        //If we someday decide to use the grid to track transports, here:
        t->SetMap(sMapMgr->CreateBaseMap(mapid));
        t->AddToWorld();

        ++count;
    }
    while (result->NextRow());

    // check transport data DB integrity
    result = WorldDatabase.Query("SELECT gameobject.guid, gameobject.id, transports.name FROM gameobject, transports WHERE gameobject.id = transports.entry");
    if (result)                                              // wrong data found
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 guid  = fields[0].GetUInt32();
            uint32 entry = fields[1].GetUInt32();
            std::string name = fields[2].GetString();
            IC_LOG_ERROR("entities.transport", "Transport %u '%s' have record (GUID: %u) in `gameobject`. Transports must not have any records in `gameobject` or its behavior will be unpredictable/bugged.", entry, name.c_str(), guid);
        }
        while (result->NextRow());
    }

    IC_LOG_INFO("entities.transport", ">> Loaded %u transports in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

Transport::Transport(uint32 period, uint32 script) : GameObject(), m_pathTime(0), m_timer(0),
currenttguid(0), m_period(period), ScriptId(script), m_nextNodeTime(0)
{
    m_updateFlag = UPDATEFLAG_TRANSPORT | UPDATEFLAG_LOWGUID | UPDATEFLAG_STATIONARY_POSITION | UPDATEFLAG_ROTATION;
}
/*
Transport::~Transport()
{
    ASSERT(_passengers.empty());
    UnloadStaticPassengers();
}
*/
bool Transport::Create(uint32 guidlow, uint32 entry, uint32 mapid, float x, float y, float z, float ang, uint32 animprogress, uint32 dynflags)
{
    Relocate(x, y, z, ang);

    if (!IsPositionValid())
    {
        IC_LOG_ERROR("entities.transport", "Transport (GUID: %u) not created. Suggested coordinates isn't valid (X: %f Y: %f)",
            guidlow, x, y);
        return false;
    }

    Object::_Create(guidlow, 0, HIGHGUID_MO_TRANSPORT);

    GameObjectTemplate const* goinfo = sObjectMgr->GetGameObjectTemplate(entry);

    if (!goinfo)
    {
        IC_LOG_ERROR("sql.sql", "Transport not created: entry in `gameobject_template` not found, guidlow: %u map: %u  (X: %f Y: %f Z: %f) ang: %f", guidlow, mapid, x, y, z, ang);
        return false;
    }

    m_goInfo = goinfo;

    SetObjectScale(goinfo->size);

    SetUInt32Value(GAMEOBJECT_FACTION, goinfo->faction);
    SetUInt32Value(GAMEOBJECT_FLAGS, goinfo->flags);
    SetUInt32Value(GAMEOBJECT_LEVEL, m_period);
    SetEntry(goinfo->entry);

    SetDisplayId(goinfo->displayId);

    SetGoState(GO_STATE_READY);
    SetGoType(GameobjectTypes(goinfo->type));

    SetGoAnimProgress(animprogress);
    if (dynflags)
        SetUInt32Value(GAMEOBJECT_DYN_FLAGS, MAKE_PAIR32(0, dynflags));

    SetName(goinfo->name);

    SetZoneScript();

    return true;
}

void Transport::Update(uint32 p_diff)
{
    if (!AI())
    {
        if (!AIM_Initialize())
            IC_LOG_ERROR("entities.transport", "Could not initialize GameObjectAI for Transport");
    } else
        AI()->UpdateAI(p_diff);

    if (m_WayPoints.size() <= 1)
        return;

    m_timer = getMSTime() % m_period;
    while (((m_timer - m_curr->first) % m_pathTime) > ((m_next->first - m_curr->first) % m_pathTime))
    {
        //DoEventIfAny(*m_curr, true);

        m_curr = GetNextWayPoint();
        m_next = GetNextWayPoint();

        //DoEventIfAny(*m_curr, false);

        // first check help in case client-server transport coordinates de-synchronization
        if (m_curr->second.mapid != GetMapId() || m_curr->second.teleport)
        {
            TeleportTransport(m_curr->second.mapid, m_curr->second.x, m_curr->second.y, m_curr->second.z);
        }
        else
        {
            Relocate(m_curr->second.x, m_curr->second.y, m_curr->second.z, GetAngle(m_next->second.x, m_next->second.y) + float(M_PI));
            //UpdatePassengerPositions(); // COME BACK MARKER
        }

        sScriptMgr->OnRelocate(this, m_curr->first, m_curr->second.mapid, m_curr->second.x, m_curr->second.y, m_curr->second.z);

        m_nextNodeTime = m_curr->first;

        if (m_curr == m_WayPoints.begin())
            IC_LOG_DEBUG("entities.transport", " ************ BEGIN ************** %s", m_name.c_str());

        IC_LOG_DEBUG("entities.transport", "%s moved to %d %f %f %f %d", m_name.c_str(), m_curr->second.id, m_curr->second.x, m_curr->second.y, m_curr->second.z, m_curr->second.mapid);
    }

    sScriptMgr->OnTransportUpdate(this, p_diff);
}
/*
void Transport::MoveToNextWaypoint()
{
    // Clear events flagging
    _triggeredArrivalEvent = false;
    _triggeredDepartureEvent = false;

    // Set frames
    _currentFrame = _nextFrame++;
    if (_nextFrame == GetKeyFrames().end())
        _nextFrame = GetKeyFrames().begin();
}

float Transport::CalculateSegmentPos(float now)
{
    KeyFrame const& frame = *_currentFrame;
    const float speed = float(m_goInfo->moTransport.moveSpeed);
    const float accel = float(m_goInfo->moTransport.accelRate);
    float timeSinceStop = frame.TimeFrom + (now - (1.0f/IN_MILLISECONDS) * frame.DepartureTime);
    float timeUntilStop = frame.TimeTo - (now - (1.0f/IN_MILLISECONDS) * frame.DepartureTime);
    float segmentPos, dist;
    float accelTime = _transportInfo->accelTime;
    float accelDist = _transportInfo->accelDist;
    // calculate from nearest stop, less confusing calculation...
    if (timeSinceStop < timeUntilStop)
    {
        if (timeSinceStop < accelTime)
            dist = 0.5f * accel * timeSinceStop * timeSinceStop;
        else
            dist = accelDist + (timeSinceStop - accelTime) * speed;
        segmentPos = dist - frame.DistSinceStop;
    }
    else
    {
        if (timeUntilStop < _transportInfo->accelTime)
            dist = 0.5f * accel * timeUntilStop * timeUntilStop;
        else
            dist = accelDist + (timeUntilStop - accelTime) * speed;
        segmentPos = frame.DistUntilStop - dist;
    }

    return segmentPos / frame.NextDistFromPrev;
}
*/

void Transport::TeleportTransport(uint32 newMapid, float x, float y, float z)
{
    Map const* oldMap = GetMap();
    Relocate(x, y, z);

    /*for (PlayerSet::const_iterator itr = m_passengers.begin(); itr != m_passengers.end();)
    {
        Player* player = *itr;
        ++itr;

        if (player->isDead() && !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
            player->ResurrectPlayer(1.0f);

        player->TeleportTo(newMapid, x, y, z, GetOrientation(), TELE_TO_NOT_LEAVE_TRANSPORT);
    }*/

    //we need to create and save new Map object with 'newMapid' because if not done -> lead to invalid Map object reference...
    //player far teleport would try to create same instance, but we need it NOW for transport...

    RemoveFromWorld();
    ResetMap();
    Map* newMap = sMapMgr->CreateBaseMap(newMapid);
    SetMap(newMap);
    ASSERT(GetMap());
    AddToWorld();

    if (oldMap != newMap)
    {
        UpdateForMap(oldMap);
        UpdateForMap(newMap);
    }

    /*for (CreatureSet::iterator itr = m_NPCPassengerSet.begin(); itr != m_NPCPassengerSet.end(); ++itr)
        (*itr)->FarTeleportTo(newMap, x, y, z, (*itr)->GetOrientation());*/
}
/*
void Transport::UpdatePassengerPositions()
{
    for (CreatureSet::iterator itr = m_NPCPassengerSet.begin(); itr != m_NPCPassengerSet.end(); ++itr)
    {
        Creature* npc = *itr;

        float x, y, z, o;
        npc->m_movementInfo.transport.pos.GetPosition(x, y, z, o);
        CalculatePassengerPosition(x, y, z, &o);
        GetMap()->CreatureRelocation(npc, x, y, z, o, false);
        npc->GetTransportHomePosition(x, y, z, o);
        CalculatePassengerPosition(x, y, z, &o);
        npc->SetHomePosition(x, y, z, o);
    }
}

void Transport::DoEventIfAny(KeyFrame const& node, bool departure)
{
    if (uint32 eventid = departure ? node.Node->departureEventID : node.Node->arrivalEventID)
    {
        IC_LOG_DEBUG("maps.script", "Taxi %s event %u of node %u of %s path", departure ? "departure" : "arrival", eventid, node.Node->index, GetName().c_str());
        GetMap()->ScriptsStart(sEventScripts, eventid, this, this);
        EventInform(eventid);
    }
}
*/
void Transport::BuildUpdate(UpdateDataMapType& data_map)
{
    Map::PlayerList const& players = GetMap()->GetPlayers();
    if (players.isEmpty())
        return;

    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        BuildFieldsUpdate(itr->GetSource(), data_map);

    ClearUpdateMask(true);
}
