/* Copyright (C) 2018, Nikolai Wuttke. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ceiling_sucker.hpp"

#include "base/warnings.hpp"
#include "engine/base_components.hpp"
#include "engine/physical_components.hpp"
#include "engine/sprite_tools.hpp"
#include "game_logic/player.hpp"

RIGEL_DISABLE_WARNINGS
#include <atria/variant/match_boost.hpp>
RIGEL_RESTORE_WARNINGS


namespace rigel { namespace game_logic { namespace behaviors {

namespace {

constexpr int ANIM_SEQUENCE_GRAB_AIR[] = {
  0, 1, 2, 3, 4, 5, 4, 3, 2, 1, 0
};


constexpr int ANIM_SEQUENCE_GRAB_PLAYER[] = {
  5, 9, 8, 7, 6, 0, 6, 0, 6, 0, 6, 0, 6, 0, 6,
  7, 8, 9, 10, 5, 4, 3, 2, 1, 0
};

}


void CeilingSucker::update(
  GlobalDependencies& d,
  const bool isOddFrame,
  const bool isOnScreen,
  entityx::Entity entity
) {
  using namespace ceiling_sucker;
  using engine::toWorldSpace;

  const auto& position = *entity.component<engine::components::WorldPosition>();
  const auto& bbox = *entity.component<engine::components::BoundingBox>();
  const auto& playerPos = d.mpPlayer->position();

  const auto worldBbox = toWorldSpace(bbox, position);

  auto touchingPlayer = [&]() {
    return worldBbox.intersects(d.mpPlayer->worldSpaceHitBox());
  };

  atria::variant::match(mState,
    [&, this](const Ready&) {
      if (
        playerPos.x + 4 >= position.x &&
        position.x + 4 >= playerPos.x
      ) {
        mState = Grabbing{};
        engine::startAnimationSequence(entity, ANIM_SEQUENCE_GRAB_AIR);
      }
    },

    [&, this](Grabbing& state) {
      ++state.mFramesElapsed;
      if (state.mFramesElapsed >= 9) {
        mState = Waiting{};
        return;
      }

      // TODO: Don't grab when player in player ship
      if (
        state.mFramesElapsed == 5 &&
        touchingPlayer() &&
        playerPos.x + 1 >= position.x &&
        position.x + 1 >= playerPos.x
      ) {
        // TODO: Show player for one more frame
        d.mpPlayer->incapacitate();
        mState = HoldingPlayer{};
        engine::startAnimationSequence(entity, ANIM_SEQUENCE_GRAB_PLAYER);
      }
    },

    [&, this](HoldingPlayer& state) {
      ++state.mFramesElapsed;
      if (state.mFramesElapsed == 19) {
        d.mpPlayer->position().x = position.x;
        d.mpPlayer->setFree();
        d.mpPlayer->takeDamage(1);
      }

      if (state.mFramesElapsed >= 24) {
        mState = Waiting{};
      }
    },

    [this](Waiting& state) {
      ++state.mFramesElapsed;
      if (state.mFramesElapsed >= 39) {
        mState = Ready{};
      }
    });
}

}}}
