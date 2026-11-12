#pragma once
#ifndef CATA_SRC_SCENE_DESCRIPTION_H
#define CATA_SRC_SCENE_DESCRIPTION_H

#include <array>
#include <string>
#include <vector>

#include "creature.h"
#include "lightmap.h"

class avatar;

/**
 * Spoken overview of everything the avatar can currently see.
 *
 * A sighted player takes in the whole map window at a glance. These helpers
 * scan the visible part of the current z-level once, collect it into a plain
 * snapshot, and then render that snapshot as short spoken fragments ordered
 * most urgent first: setting, hostiles, other creatures, the shape of the
 * surrounding space, exits, hazards and objects.
 *
 * Only tiles and creatures the avatar can see right now are reported, using
 * the names a sighted player is shown.
 *
 * Gathering and rendering are separate so that the renderer can be unit
 * tested with hand-built snapshots, and so that later features can compare
 * two snapshots instead of rescanning the map. This layer knows nothing about
 * the renderer or the narration backend.
 */

/** Directions are 0-7 clockwise from north; 8 means the avatar's own tile. */
constexpr int scene_direction_here = 8;

struct scene_creature {
    std::string name;
    // Name for two or more of this creature. Same as name for characters.
    std::string plural_name;
    Creature::Attitude attitude = Creature::Attitude::NEUTRAL;
    // False when the avatar cannot judge attitude or awareness.
    bool attitude_known = true;
    bool sees_player = false;
    int distance = 0;
    int direction = scene_direction_here;
};

/** What lies in one of the eight directions from the avatar. */
struct scene_obstacle {
    // True when an impassable tile was found; name and distance describe it.
    bool reached_obstacle = false;
    // When no obstacle was reached: true if the view ended at unseen tiles,
    // false if the way is open as far as the scan looks.
    bool sight_limited = true;
    // Distance to the obstacle, or the number of open tiles seen.
    int distance = 0;
    std::string name;
};

/** A single notable tile, such as a door or a flight of stairs. */
struct scene_feature {
    std::string name;
    int distance = 0;
    int direction = scene_direction_here;
};

/** Several tiles or objects sharing a name. Position is that of the nearest. */
struct scene_object {
    std::string name;
    int count = 0;
    int nearest = 0;
    int direction = scene_direction_here;
};

struct scene_snapshot {
    bool outdoors = false;
    // Overmap terrain name: "house", "road", "forest".
    std::string place;
    // Light on the avatar's own tile.
    lit_level light = lit_level::LIT;
    // Distance to the farthest visible tile.
    int sight_range = 0;
    // True when darkness, not walls, is what limits the view.
    bool sight_reduced = false;
    // Empty when indoors.
    std::string weather;

    // Nearest first.
    std::vector<scene_creature> creatures;
    std::array<scene_obstacle, 8> obstacles;
    // Doors, windows, stairs, ladders and ramps, nearest first.
    std::vector<scene_feature> exits;
    // Dangerous fields and spotted traps, nearest first.
    std::vector<scene_object> hazards;
    std::vector<scene_object> furniture;
    std::vector<scene_object> vehicles;

    int item_tiles = 0;
    int nearest_item_distance = 0;
    int nearest_item_direction = scene_direction_here;
};

/** Scan what the avatar can see on its z-level. Costs no game time. */
scene_snapshot gather_scene( const avatar &ava );

/** Turn a snapshot into speech. */
std::string render_scene( const scene_snapshot &scene );

/** gather_scene followed by render_scene. */
std::string describe_scene( const avatar &ava );

// Helpers exposed for unit tests.
/** 0-7 clockwise from north for an offset, or scene_direction_here for none. */
int scene_direction_index( int dx, int dy );
/** "here", "next to you, east" or "4 east". */
std::string scene_position_phrase( int distance, int direction );

#endif // CATA_SRC_SCENE_DESCRIPTION_H
