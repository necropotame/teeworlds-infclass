#ifndef GAME_SERVER_ENTITIES_LOOPER_WALL_H
#define GAME_SERVER_ENTITIES_LOOPER_WALL_H

#include <game/server/entity.h>

class CLooperWall : public CEntity
{
public:
    CLooperWall(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, int Owner);
    virtual ~CLooperWall();
    
    virtual void Reset();
    virtual void Tick();
    virtual void TickPaused();
    virtual void Snap(int SnappingClient);
    int GetTick() { return m_LifeSpan; }

public:
    int m_Owner;

private:
    vec2 m_Pos2;
    int m_LifeSpan;
    int m_EndPointID;
};

#endif
    
