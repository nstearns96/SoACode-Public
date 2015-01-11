///
/// GameSystemFactories.h
/// Seed of Andromeda
///
/// Created by Benjamin Arnold on 11 Jan 2015
/// Copyright 2014 Regrowth Studios
/// All Rights Reserved
///
/// Summary:
/// Factory methods for GameSystem entities
///

#pragma once

#ifndef GameSystemFactories_h__
#define GameSystemFactories_h__

#include <Vorb/Entity.h>

class GameSystem;

namespace GameSystemFactories {
    extern vcore::EntityID createPlayer(OUT GameSystem* gameSystem, const f64v3& spacePosition,
                                      const f64q& orientation, float massKg, const f64v3& initialVel);
   
    extern void destroyPlayer(OUT GameSystem* gameSystem, vcore::EntityID playerEntity);
}

#endif // GameSystemFactories_h__
