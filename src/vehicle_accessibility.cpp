#include "vehicle_accessibility.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "accessibility.h"
#include "cata_utility.h"
#include "character.h"
#include "creature.h"
#include "creature_tracker.h"
#include "item.h"
#include "map.h"
#include "mapdata.h"
#include "string_formatter.h"
#include "tileray.h"
#include "translations.h"
#include "type_id.h"
#include "units_utility.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "vpart_range.h"

static const itype_id fuel_type_animal( "animal" );
static const itype_id fuel_type_battery( "battery" );
static const itype_id fuel_type_muscle( "muscle" );

namespace
{

// Shortest signed difference b - a in degrees, in (-180, 180].
double angle_difference_degrees( units::angle a, units::angle b )
{
    double diff = std::fmod( units::to_degrees( b - a ), 360.0 );
    if( diff <= -180.0 ) {
        diff += 360.0;
    } else if( diff > 180.0 ) {
        diff -= 360.0;
    }
    return diff;
}

int rounded_speed( int internal_velocity )
{
    return static_cast<int>( std::lround( convert_velocity( internal_velocity, VU_VEHICLE ) ) );
}

std::string percent_text( int64_t current, int64_t capacity )
{
    if( capacity <= 0 ) {
        return std::string();
    }
    const int64_t percent = std::clamp<int64_t>( current * 100 / capacity, 0, 100 );
    return string_format( _( "%d percent" ), static_cast<int>( percent ) );
}

// Which part of the vehicle's path a tile is in, from its sideways offset.
std::string side_word( double lateral, double half_width )
{
    const double threshold = std::max( 0.5, half_width * 0.34 );
    if( lateral < -threshold ) {
        return _( "left side" );
    }
    if( lateral > threshold ) {
        return _( "right side" );
    }
    return _( "center" );
}

struct path_hit {
    int distance = 0;
    std::string what;
    std::string side;
    // Lower sorts first at the same distance: creatures before vehicles
    // before terrain. Creatures (priority 0) are also exempt from the cap on
    // how many things are read out, so scenery never hides a moving threat.
    int priority = 0;
    // How many creatures of this name are on this side of the path.
    int count = 1;
};

// Something on this tile that a driver needs to know about, or empty.
// Only called for tiles the player can see.
path_hit classify_path_tile( map &here, const vehicle &own, const Character &you,
                             const tripoint_bub_ms &p )
{
    path_hit hit;
    if( const Creature *critter = get_creature_tracker().creature_at( p, true ) ) {
        if( critter != &you && you.sees( here, *critter ) ) {
            hit.what = critter->disp_name();
            hit.priority = 0;
            return hit;
        }
    }
    if( const optional_vpart_position ovp = here.veh_at( p ) ) {
        if( &ovp->vehicle() != &own ) {
            hit.what = ovp->vehicle().is_appliance() ?
                       string_format( _( "appliance %s" ), ovp->vehicle().name ) :
                       string_format( _( "vehicle %s" ), ovp->vehicle().name );
            hit.priority = 1;
            return hit;
        }
    }
    if( here.has_flag( ter_furn_flag::TFLAG_NO_FLOOR, p ) ) {
        hit.what = _( "drop-off" );
        hit.priority = 2;
        return hit;
    }
    if( here.has_flag( ter_furn_flag::TFLAG_DEEP_WATER, p ) ) {
        hit.what = _( "deep water" );
        hit.priority = 2;
        return hit;
    }
    if( here.impassable( p ) ) {
        hit.what = here.has_furn( p ) ? here.furnname( p ) : here.tername( p );
        hit.priority = 3;
        return hit;
    }
    // Slow, bashable things such as shrubs, fences and rubble.
    if( here.has_furn( p ) && here.move_cost( p, &own ) > 2 ) {
        hit.what = here.furnname( p );
        hit.priority = 4;
        return hit;
    }
    return hit;
}

} // namespace

driving_speech_level &driving_speech_setting()
{
    static driving_speech_level level = driving_speech_level::motion;
    return level;
}

std::string driving_speech_level_name( driving_speech_level level )
{
    switch( level ) {
        case driving_speech_level::off:
            return _( "Driving speech off. Control plus E and Control plus W still work." );
        case driving_speech_level::controls:
            return _( "Driving speech: steering and speed changes only." );
        case driving_speech_level::motion:
            return _( "Driving speech: steering, speed, and movement each turn." );
        case driving_speech_level::motion_and_scan:
            return _( "Driving speech: steering, speed, movement, and nearest obstacle ahead "
                      "each turn." );
        case driving_speech_level::num_levels:
            break;
    }
    return std::string();
}

std::string cycle_driving_speech_level()
{
    driving_speech_level &level = driving_speech_setting();
    const int next = ( static_cast<int>( level ) + 1 ) %
                     static_cast<int>( driving_speech_level::num_levels );
    level = static_cast<driving_speech_level>( next );
    return driving_speech_level_name( level );
}

driving_snapshot capture_driving_snapshot( const vehicle &veh )
{
    driving_snapshot snap;
    snap.valid = true;
    snap.face = veh.face.dir();
    snap.turn_dir = veh.turn_dir;
    snap.move = veh.move.dir();
    snap.velocity = veh.velocity;
    snap.cruise_velocity = veh.cruise_velocity;
    snap.engine_on = veh.engine_on;
    snap.skidding = veh.skidding;
    return snap;
}

int azimuth_degrees( units::angle game_direction )
{
    const long degrees = std::lround( units::to_degrees( game_direction ) + 90.0 );
    return static_cast<int>( ( ( degrees % 360 ) + 360 ) % 360 );
}

std::string compass_point_name( units::angle game_direction )
{
    static const std::array<std::string, 16> names = {{
            translate_marker( "north" ), translate_marker( "north-northeast" ),
            translate_marker( "northeast" ), translate_marker( "east-northeast" ),
            translate_marker( "east" ), translate_marker( "east-southeast" ),
            translate_marker( "southeast" ), translate_marker( "south-southeast" ),
            translate_marker( "south" ), translate_marker( "south-southwest" ),
            translate_marker( "southwest" ), translate_marker( "west-southwest" ),
            translate_marker( "west" ), translate_marker( "west-northwest" ),
            translate_marker( "northwest" ), translate_marker( "north-northwest" )
        }
    };
    const double azimuth = units::to_degrees( game_direction ) + 90.0;
    const long index = std::lround( azimuth / 22.5 );
    return _( names[static_cast<size_t>( ( ( index % 16 ) + 16 ) % 16 )] );
}

std::string describe_heading( units::angle game_direction )
{
    return string_format( _( "%d degrees, %s" ), azimuth_degrees( game_direction ),
                          compass_point_name( game_direction ) );
}

std::string engine_strain_word( float strain )
{
    // Same thresholds as the sidebar's cruise colour.
    if( strain <= 0.0f ) {
        return std::string();
    }
    if( strain <= 0.2f ) {
        return _( "engine straining" );
    }
    if( strain <= 0.4f ) {
        return _( "engine heavily strained" );
    }
    return _( "engine overloaded" );
}

std::string describe_speed( int velocity, int cruise_velocity )
{
    const int current = rounded_speed( velocity );
    const int target = rounded_speed( cruise_velocity );
    const std::string unit = velocity_units( VU_VEHICLE );
    if( current == 0 && target == 0 ) {
        return string_format( _( "stopped, target 0 %s" ), unit );
    }
    std::string trend;
    if( target == current ) {
        trend = _( "holding speed" );
    } else if( std::abs( target ) > std::abs( current ) ) {
        trend = _( "accelerating" );
    } else {
        trend = _( "slowing" );
    }
    if( current < 0 || target < 0 ) {
        return string_format( _( "reversing, speed %d, target %d %s, %s" ), std::abs( current ),
                              std::abs( target ), unit, trend );
    }
    return string_format( _( "speed %d, target %d %s, %s" ), current, target, unit, trend );
}

std::string describe_driving_change( const driving_snapshot &before,
                                     const driving_snapshot &after )
{
    if( !before.valid || !after.valid ) {
        return std::string();
    }
    std::vector<std::string> parts;
    const double turned = angle_difference_degrees( before.turn_dir, after.turn_dir );
    if( std::fabs( turned ) >= 1.0 ) {
        const std::string heading = describe_heading( after.turn_dir );
        parts.push_back( string_format( turned > 0 ? _( "Turning right to %s" ) :
                                        _( "Turning left to %s" ), heading ) );
    }
    if( after.cruise_velocity != before.cruise_velocity ) {
        parts.push_back( string_format( _( "Target %d %s, speed %d" ),
                                        rounded_speed( after.cruise_velocity ),
                                        velocity_units( VU_VEHICLE ),
                                        rounded_speed( after.velocity ) ) );
    }
    if( after.skidding && !before.skidding ) {
        parts.emplace_back( _( "Skidding" ) );
    } else if( before.skidding && !after.skidding ) {
        parts.emplace_back( _( "Control regained" ) );
    }
    if( parts.empty() ) {
        return std::string();
    }
    std::string result;
    for( const std::string &part : parts ) {
        result += part + ". ";
    }
    result.pop_back();
    return result;
}

std::string describe_mount_region( const point_rel_ms &mount, const point_rel_ms &mount_min,
                                   const point_rel_ms &mount_max )
{
    const int length = mount_max.x() - mount_min.x() + 1;
    const int width = mount_max.y() - mount_min.y() + 1;
    std::string lengthwise;
    if( length >= 2 ) {
        if( mount.x() == mount_max.x() ) {
            lengthwise = _( "front" );
        } else if( mount.x() == mount_min.x() ) {
            lengthwise = _( "rear" );
        }
    }
    std::string sideways;
    if( width >= 2 ) {
        if( mount.y() == mount_min.y() ) {
            sideways = _( "left" );
        } else if( mount.y() == mount_max.y() ) {
            sideways = _( "right" );
        }
    }
    if( lengthwise.empty() && sideways.empty() ) {
        return length <= 1 && width <= 1 ? std::string( _( "only tile" ) ) :
               std::string( _( "middle" ) );
    }
    if( lengthwise.empty() ) {
        return string_format( _( "middle %s" ), sideways );
    }
    if( sideways.empty() ) {
        return lengthwise;
    }
    //~ Vehicle footprint region, e.g. "front left"
    return string_format( _( "%1$s %2$s" ), lengthwise, sideways );
}

std::string describe_displacement( const point_rel_ms &delta )
{
    if( delta == point_rel_ms::zero ) {
        return std::string();
    }
    const int tiles = std::max( std::abs( delta.x() ), std::abs( delta.y() ) );
    const units::angle direction = units::atan2( delta.y(), delta.x() );
    return string_format( n_gettext( "%1$d tile %2$s", "%1$d tiles %2$s", tiles ), tiles,
                          compass_point_name( direction ) );
}

std::string ground_kind( map &here, const tripoint_bub_ms &p )
{
    if( !here.inbounds( p ) ) {
        return std::string();
    }
    if( here.has_flag( ter_furn_flag::TFLAG_DEEP_WATER, p ) ) {
        return _( "deep water" );
    }
    if( here.has_flag_ter( ter_furn_flag::TFLAG_ROAD, p ) ) {
        return _( "road" );
    }
    if( here.has_flag_ter( ter_furn_flag::TFLAG_FLAT, p ) ) {
        return _( "flat ground" );
    }
    return _( "rough ground" );
}

std::string vehicle_status_summary( map &here, const vehicle &veh, const Character &you )
{
    std::vector<std::string> out;
    const bool driving = you.controlling_vehicle;
    out.push_back( string_format( driving ? _( "Driving %s" ) : _( "In %s" ), veh.name ) );
    out.push_back( describe_speed( veh.velocity, veh.cruise_velocity ) );
    const std::string strain = engine_strain_word( veh.strain( here ) );
    if( !strain.empty() ) {
        out.push_back( strain );
    }
    out.push_back( string_format( _( "Facing %s" ), describe_heading( veh.face.dir() ) ) );
    if( std::fabs( angle_difference_degrees( veh.face.dir(), veh.turn_dir ) ) >= 1.0 ) {
        out.push_back( string_format( _( "Steering toward %s" ),
                                      describe_heading( veh.turn_dir ) ) );
    }
    if( veh.skidding ) {
        out.push_back( string_format( _( "Skidding, sliding toward %s" ),
                                      compass_point_name( veh.move.dir() ) ) );
    }
    if( veh.is_flying_in_air() ) {
        out.emplace_back( _( "Flying" ) );
    } else if( veh.is_in_water( true ) ) {
        out.emplace_back( _( "Floating in deep water" ) );
    }

    // Engines and the fuel each one draws.
    std::vector<std::string> engines;
    std::set<itype_id> fuels;
    for( const int idx : veh.engines ) {
        const vehicle_part &vp = veh.part( idx );
        std::string state;
        if( vp.is_broken() ) {
            state = _( "broken" );
        } else {
            state = veh.is_engine_on( vp ) ? _( "on" ) : _( "off" );
        }
        engines.push_back( string_format( _( "%1$s %2$s" ), vp.name(), state ) );
        const itype_id fuel = vp.fuel_current();
        if( !fuel.is_null() && fuel != fuel_type_battery && fuel != fuel_type_muscle &&
            fuel != fuel_type_animal && !veh.is_perpetual_type( vp ) ) {
            fuels.insert( fuel );
        }
    }
    if( engines.empty() ) {
        out.emplace_back( _( "No engine" ) );
    } else {
        std::string joined = _( "Engines: " );
        for( size_t i = 0; i < engines.size(); ++i ) {
            joined += ( i ? ", " : "" ) + engines[i];
        }
        out.push_back( joined );
    }
    for( const itype_id &fuel : fuels ) {
        const std::string pct = percent_text( veh.fuel_left( here, fuel ), veh.fuel_capacity( here,
                                              fuel ) );
        if( !pct.empty() ) {
            out.push_back( string_format( _( "%1$s %2$s" ), item::nname( fuel ), pct ) );
        }
    }
    const std::string battery = percent_text( veh.fuel_left( here, fuel_type_battery ),
                                veh.fuel_capacity( here, fuel_type_battery ) );
    out.push_back( battery.empty() ? std::string( _( "No battery" ) ) :
                   string_format( _( "Battery %s" ), battery ) );

    std::vector<std::string> lights;
    if( veh.has_part( "CONE_LIGHT", true ) ) {
        lights.emplace_back( _( "headlights" ) );
    }
    if( veh.has_part( "WIDE_CONE_LIGHT", true ) ) {
        lights.emplace_back( _( "wide headlights" ) );
    }
    if( veh.has_part( "HALF_CIRCLE_LIGHT", true ) || veh.has_part( "CIRCLE_LIGHT", true ) ) {
        lights.emplace_back( _( "area lights" ) );
    }
    if( lights.empty() ) {
        out.emplace_back( _( "Lights off" ) );
    } else {
        std::string joined;
        for( size_t i = 0; i < lights.size(); ++i ) {
            joined += ( i ? ", " : "" ) + lights[i];
        }
        out.push_back( string_format( _( "Lights on: %s" ), joined ) );
    }

    // Footprint, and damage to the parts that keep the vehicle moving.
    point_rel_ms mn( INT_MAX, INT_MAX );
    point_rel_ms mx( INT_MIN, INT_MIN );
    for( const vpart_reference &vpr : veh.get_all_parts() ) {
        const point_rel_ms &m = vpr.part().mount;
        mn.x() = std::min( mn.x(), m.x() );
        mn.y() = std::min( mn.y(), m.y() );
        mx.x() = std::max( mx.x(), m.x() );
        mx.y() = std::max( mx.y(), m.y() );
    }
    if( mn.x() <= mx.x() ) {
        out.push_back( string_format( _( "%1$d tiles long, %2$d wide" ), mx.x() - mn.x() + 1,
                                      mx.y() - mn.y() + 1 ) );
    }
    std::vector<std::string> damage;
    for( const vpart_reference &vpr : veh.get_all_parts() ) {
        const vehicle_part &vp = vpr.part();
        const bool critical = vp.is_wheel() || vp.is_engine() ||
                              vpr.info().has_flag( "STEERABLE" ) ||
                              vpr.info().has_flag( "CONTROLS" );
        if( !critical ) {
            continue;
        }
        const std::string where = describe_mount_region( vp.mount, mn, mx );
        if( vp.is_broken() ) {
            damage.push_back( string_format( _( "%1$s %2$s broken" ), where, vp.name() ) );
        } else if( vp.health_percent() < 0.5 ) {
            damage.push_back( string_format( _( "%1$s %2$s at %3$d percent" ), where, vp.name(),
                                             static_cast<int>( vp.health_percent() * 100 ) ) );
        }
    }
    if( !damage.empty() ) {
        std::string joined = _( "Damaged: " );
        for( size_t i = 0; i < damage.size() && i < 8; ++i ) {
            joined += ( i ? ", " : "" ) + damage[i];
        }
        if( damage.size() > 8 ) {
            joined += string_format( _( ", and %d more" ), static_cast<int>( damage.size() - 8 ) );
        }
        out.push_back( joined );
    }
    const optional_vpart_position under = here.veh_at( you.pos_bub( here ) );
    if( under && &under->vehicle() == &veh ) {
        const std::string ground = ground_kind( here, you.pos_bub( here ) );
        if( !ground.empty() ) {
            out.push_back( string_format( _( "On %s" ), ground ) );
        }
    }

    std::string result;
    for( const std::string &s : out ) {
        result += s + ". ";
    }
    result.pop_back();
    return result;
}

std::string vehicle_forward_scan( map &here, const vehicle &veh, const Character &you,
                                  int range, bool brief )
{
    const bool reversing = veh.velocity < 0;
    units::angle direction = veh.skidding ? veh.move.dir() : veh.face.dir();
    if( reversing && !veh.skidding ) {
        direction += 180_degrees;
    }
    direction = normalize( direction );
    if( range <= 0 ) {
        // Roughly three turns of travel, never less than a short look ahead.
        const float tiles_per_turn = std::abs( veh.velocity ) / vehicles::vmiph_per_tile;
        range = std::clamp( static_cast<int>( std::lround( tiles_per_turn * 3 ) ), 12, 40 );
    }

    std::vector<tripoint_bub_ms> footprint;
    std::set<tripoint_bub_ms> own;
    double cx = 0;
    double cy = 0;
    for( const vpart_reference &vpr : veh.get_all_parts() ) {
        const tripoint_bub_ms p = vpr.pos_bub( here );
        if( own.insert( p ).second ) {
            footprint.push_back( p );
            cx += p.x();
            cy += p.y();
        }
    }
    if( footprint.empty() ) {
        return _( "No vehicle tiles found." );
    }
    cx /= footprint.size();
    cy /= footprint.size();
    const double fx = units::cos( direction );
    const double fy = units::sin( direction );
    // Screen y grows downward, so a clockwise quarter turn is to the right.
    const double rx = -fy;
    const double ry = fx;
    double half_width = 0;
    for( const tripoint_bub_ms &p : footprint ) {
        half_width = std::max( half_width, std::fabs( ( p.x() - cx ) * rx + ( p.y() - cy ) * ry ) );
    }

    // Ground under the driver's lane, followed until it changes.
    const tripoint_bub_ms lane_start = own.count( you.pos_bub( here ) ) ? you.pos_bub( here ) :
                                       footprint.front();
    const std::string start_ground = ground_kind( here, lane_start );
    std::string next_ground;
    int same_ground_tiles = 0;
    bool lane_open = true;

    std::vector<path_hit> hits;
    std::map<std::string, size_t> reported;
    std::set<tripoint_bub_ms> checked;
    // Steps from the vehicle over which every tile of the path was visible.
    // "Clear" is only ever claimed for this stretch.
    int visible_until = 0;
    bool unbroken_view = true;
    bool any_seen = false;
    tileray ray( direction );
    point_rel_ms offset = point_rel_ms::zero;
    for( int step = 1; step <= range; ++step ) {
        ray.advance( 1 );
        offset += point_rel_ms( ray.dx(), ray.dy() );
        bool row_fully_seen = true;
        for( const tripoint_bub_ms &start : footprint ) {
            const tripoint_bub_ms p = start + offset;
            if( own.count( p ) || !checked.insert( p ).second ) {
                continue;
            }
            if( !here.inbounds( p ) || !you.sees( here, p ) ) {
                row_fully_seen = false;
                continue;
            }
            any_seen = true;
            path_hit hit = classify_path_tile( here, veh, you, p );
            if( hit.what.empty() ) {
                continue;
            }
            const double lateral = ( p.x() - cx ) * rx + ( p.y() - cy ) * ry;
            hit.side = side_word( lateral, half_width );
            hit.distance = step;
            // One mention per kind of thing and side; the nearest wins and
            // further creatures of the same name are counted.
            const auto inserted = reported.emplace( hit.what + "|" + hit.side, hits.size() );
            if( inserted.second ) {
                hits.push_back( hit );
            } else if( hit.priority == 0 ) {
                ++hits[inserted.first->second].count;
            }
        }
        const tripoint_bub_ms lane = lane_start + offset;
        if( lane_open ) {
            if( here.inbounds( lane ) && you.sees( here, lane ) ) {
                const std::string g = ground_kind( here, lane );
                if( g == start_ground ) {
                    ++same_ground_tiles;
                } else {
                    next_ground = g;
                    lane_open = false;
                }
            } else {
                lane_open = false;
            }
        }
        if( unbroken_view && row_fully_seen ) {
            visible_until = step;
        } else {
            unbroken_view = false;
        }
    }
    std::stable_sort( hits.begin(), hits.end(), []( const path_hit & a, const path_hit & b ) {
        return a.distance != b.distance ? a.distance < b.distance : a.priority < b.priority;
    } );

    std::vector<std::string> out;
    if( !brief ) {
        std::string heading = reversing && !veh.skidding ? _( "Behind, reversing toward %s" ) :
                              veh.skidding ? _( "Skidding toward %s" ) : _( "Ahead, toward %s" );
        out.push_back( string_format( heading, describe_heading( direction ) ) );
    }
    if( !start_ground.empty() ) {
        if( !next_ground.empty() ) {
            out.push_back( string_format( _( "%1$s for %2$d tiles, then %3$s" ), start_ground,
                                          same_ground_tiles, next_ground ) );
        } else if( !brief ) {
            out.push_back( string_format( _( "%1$s for at least %2$d tiles" ), start_ground,
                                          same_ground_tiles ) );
        }
    }
    // The nearest things of any kind, then every remaining visible creature:
    // scenery in front must not push a creature off the end of the list. The
    // brief form gives the nearest thing and the nearest creature.
    const size_t max_hits = brief ? 1 : 5;
    const size_t max_extra_creatures = brief ? 1 : 5;
    size_t extra_creatures = 0;
    bool creature_spoken = false;
    for( size_t i = 0; i < hits.size(); ++i ) {
        const path_hit &hit = hits[i];
        const bool creature = hit.priority == 0;
        if( i >= max_hits ) {
            if( !creature || extra_creatures >= max_extra_creatures ||
                ( brief && creature_spoken ) ) {
                continue;
            }
            ++extra_creatures;
        }
        creature_spoken = creature_spoken || creature;
        std::string line = string_format( n_gettext( "%1$s %2$d tile, %3$s",
                                          "%1$s %2$d tiles, %3$s", hit.distance ),
                                          hit.what, hit.distance, hit.side );
        if( hit.count > 1 ) {
            line += string_format( _( ", plus %d more further on" ), hit.count - 1 );
        }
        out.push_back( line );
    }
    if( hits.empty() && visible_until > 0 ) {
        out.push_back( string_format( _( "Path clear for %d tiles" ), visible_until ) );
    }
    if( visible_until == 0 ) {
        // Said in the brief form too: silence must not sound like a clear road.
        out.emplace_back( any_seen ? _( "You can't see the whole width of the path ahead" ) :
                          _( "You can't see the path ahead" ) );
    } else if( !brief && visible_until < range ) {
        out.push_back( string_format( _( "Can't see the whole path beyond %d tiles" ),
                                      visible_until ) );
    }
    std::string result;
    for( const std::string &s : out ) {
        result += s + ". ";
    }
    result.pop_back();
    return result;
}

void announce_driving_change( const driving_snapshot &before, const vehicle &veh )
{
    if( driving_speech_setting() == driving_speech_level::off ) {
        return;
    }
    const std::string change = describe_driving_change( before, capture_driving_snapshot( veh ) );
    if( !change.empty() ) {
        // Interrupt: a held key should hear the newest value, not a backlog.
        get_narration_service().announce( { narration_event::movement, change, true } );
    }
}

driving_motion_tracker begin_driving_motion( map &here, const Character &you )
{
    driving_motion_tracker tracker;
    if( driving_speech_setting() < driving_speech_level::motion || !you.controlling_vehicle ) {
        return tracker;
    }
    const optional_vpart_position vp = here.veh_at( you.pos_bub( here ) );
    if( !vp ) {
        return tracker;
    }
    tracker.active = true;
    tracker.start_pos = you.pos_abs();
    tracker.before = capture_driving_snapshot( vp->vehicle() );
    tracker.ground = ground_kind( here, you.pos_bub( here ) );
    return tracker;
}

void end_driving_motion( map &here, const Character &you, const driving_motion_tracker &tracker )
{
    if( !tracker.active || !you.controlling_vehicle ) {
        return;
    }
    const optional_vpart_position vp = here.veh_at( you.pos_bub( here ) );
    if( !vp ) {
        return;
    }
    const vehicle &veh = vp->vehicle();
    const driving_snapshot after = capture_driving_snapshot( veh );
    const tripoint_rel_ms moved = you.pos_abs() - tracker.start_pos;
    const bool was_moving = tracker.before.velocity != 0;
    std::vector<std::string> out;
    const std::string displacement = describe_displacement( moved.xy() );
    if( !displacement.empty() ) {
        out.push_back( displacement );
    }
    if( moved.z() > 0 ) {
        out.emplace_back( _( "up a level" ) );
    } else if( moved.z() < 0 ) {
        out.emplace_back( _( "down a level" ) );
    }
    if( after.velocity == 0 && was_moving ) {
        out.emplace_back( _( "Stopped" ) );
    } else if( after.velocity != 0 ) {
        out.push_back( string_format( _( "%1$d %2$s" ), std::abs( rounded_speed( after.velocity ) ),
                                      velocity_units( VU_VEHICLE ) ) );
    }
    if( after.skidding && !tracker.before.skidding ) {
        out.emplace_back( _( "Skidding" ) );
    }
    const std::string ground = ground_kind( here, you.pos_bub( here ) );
    if( !ground.empty() && ground != tracker.ground && !displacement.empty() ) {
        out.push_back( string_format( _( "now on %s" ), ground ) );
    }
    if( out.empty() ) {
        return;
    }
    if( driving_speech_setting() == driving_speech_level::motion_and_scan && after.velocity != 0 ) {
        out.push_back( vehicle_forward_scan( here, veh, you, 0, true ) );
    }
    std::string result;
    for( const std::string &s : out ) {
        result += s + ". ";
    }
    result.pop_back();
    // Queued, not interrupting: the steering confirmation for the key that
    // ended this turn should still be heard first.
    get_narration_service().announce( { narration_event::movement, result, false } );
}
