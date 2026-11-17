#pragma once
#ifndef CATA_SRC_VEHICLE_ACCESSIBILITY_H
#define CATA_SRC_VEHICLE_ACCESSIBILITY_H

#include <string>

#include "coordinates.h"
#include "point.h"
#include "units.h"

class Character;
class map;
class vehicle;

/**
 * Spoken equivalents of what a sighted driver reads off the screen.
 *
 * A sighted driver sees the vehicle rotate after each steering key, watches
 * the speed readout in the sidebar, sees how far the vehicle moved each turn
 * and sees the road, walls and creatures ahead. These helpers turn the same
 * information into short sentences. They report what is visible and never
 * steer, brake or otherwise act for the player.
 *
 * Directions use the game's angle convention: 0 degrees points east and
 * angles grow clockwise on screen. Spoken headings use compass azimuth
 * (0 = north) to match the sidebar's azimuth readout.
 */

/** How much driving speech is produced automatically. Cycled with Ctrl+O. */
enum class driving_speech_level : int {
    // Nothing automatic; Ctrl+E and Ctrl+W still work.
    off = 0,
    // Speak heading and target speed after each steering or throttle key.
    controls,
    // Also speak distance moved, speed and road changes after each turn.
    motion,
    // Also speak the nearest obstacle ahead after each turn while moving.
    motion_and_scan,
    num_levels
};

driving_speech_level &driving_speech_setting();
/** Advances to the next level, wrapping around, and returns its spoken name. */
std::string cycle_driving_speech_level();
std::string driving_speech_level_name( driving_speech_level level );

/** The parts of vehicle state that a steering or throttle key can change. */
struct driving_snapshot {
    bool valid = false;
    units::angle face = 0_degrees;
    units::angle turn_dir = 0_degrees;
    units::angle move = 0_degrees;
    int velocity = 0;
    int cruise_velocity = 0;
    bool engine_on = false;
    bool skidding = false;
};

driving_snapshot capture_driving_snapshot( const vehicle &veh );

// ---- Pure wording helpers (no map access; unit tested) ----

/** 16-point compass name, e.g. "east-northeast", for a game-convention angle. */
std::string compass_point_name( units::angle game_direction );
/** Compass azimuth in whole degrees (0 = north, 90 = east). */
int azimuth_degrees( units::angle game_direction );
/** "105 degrees, east-southeast". */
std::string describe_heading( units::angle game_direction );
/** Word for engine strain as the sidebar colours it; empty when unstrained. */
std::string engine_strain_word( float strain );
/**
 * Plain speed sentence from velocities in the vehicle's internal units
 * (mph * 100). Uses the player's configured vehicle speed units.
 * "speed 10, target 25 mph, accelerating".
 */
std::string describe_speed( int velocity, int cruise_velocity );
/**
 * What a steering or throttle key changed: "Turning right to 105 degrees,
 * east-southeast. Target 25 mph, speed 10." Empty if nothing changed.
 */
std::string describe_driving_change( const driving_snapshot &before,
                                     const driving_snapshot &after );
/**
 * Where a mount point sits inside a vehicle's footprint, relative to the
 * vehicle's own front: "front left", "middle", "rear right".
 * Mount x grows toward the front, mount y grows toward the right.
 */
std::string describe_mount_region( const point_rel_ms &mount, const point_rel_ms &mount_min,
                                   const point_rel_ms &mount_max );
/** "6 tiles east-northeast" for a map displacement; empty when zero. */
std::string describe_displacement( const point_rel_ms &delta );

// ---- Map-aware reports ----

/** Ctrl+E: speed, heading, engines, fuel, lights, footprint and damage. */
std::string vehicle_status_summary( map &here, const vehicle &veh, const Character &you );
/**
 * Ctrl+W: what lies in the vehicle's path, nearest first, using only tiles
 * the player can currently see. range <= 0 picks a range from the speed.
 * When brief is true only the road state and the nearest obstacle are given.
 */
std::string vehicle_forward_scan( map &here, const vehicle &veh, const Character &you,
                                  int range = 0, bool brief = false );
/** Road, flat ground or rough ground under a tile, for road-change speech. */
std::string ground_kind( map &here, const tripoint_bub_ms &p );

// ---- Hooks ----

/** Called after a steering or throttle key; speaks the change if enabled. */
void announce_driving_change( const driving_snapshot &before, const vehicle &veh );

/**
 * Per-turn motion speech. begin() is called just before vehicles move, end()
 * just after. Both are cheap no-ops when the player is not driving.
 */
struct driving_motion_tracker {
    bool active = false;
    tripoint_abs_ms start_pos;
    driving_snapshot before;
    std::string ground;
};
driving_motion_tracker begin_driving_motion( map &here, const Character &you );
void end_driving_motion( map &here, const Character &you, const driving_motion_tracker &tracker );

#endif // CATA_SRC_VEHICLE_ACCESSIBILITY_H
