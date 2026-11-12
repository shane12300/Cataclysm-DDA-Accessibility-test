#include "scene_description.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>

#include "avatar.h"
#include "character.h"
#include "coordinates.h"
#include "enums.h"
#include "field.h"
#include "line.h"
#include "map.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "monster.h"
#include "omdata.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "point.h"
#include "string_formatter.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_type.h"

static const trait_id trait_INATTENTIVE( "INATTENTIVE" );

namespace
{

// Tuning. Lists longer than these end with "N more."
constexpr size_t max_exits = 6;
constexpr size_t max_object_groups = 8;
// How far each of the eight direction probes looks before saying "or more".
constexpr int open_distance_cap = 20;
// In dim light, a view this short is reported as "You can see N tiles".
constexpr int reduced_sight_tiles = 8;

struct direction_step {
    int dx;
    int dy;
};

// Index matches the 0-7 direction convention: clockwise from north.
constexpr std::array<direction_step, 8> direction_steps = {{
        { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 },
        { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }
    }
};

std::string direction_word( int direction )
{
    switch( direction ) {
        case 0:
            return _( "north" );
        case 1:
            return _( "northeast" );
        case 2:
            return _( "east" );
        case 3:
            return _( "southeast" );
        case 4:
            return _( "south" );
        case 5:
            return _( "southwest" );
        case 6:
            return _( "west" );
        case 7:
            return _( "northwest" );
        default:
            return _( "here" );
    }
}

std::string join_parts( const std::vector<std::string> &parts, const std::string &sep )
{
    std::string out;
    for( const std::string &part : parts ) {
        if( part.empty() ) {
            continue;
        }
        if( !out.empty() ) {
            out += sep;
        }
        out += part;
    }
    return out;
}

// Record one more tile or object called `name` at the given position.
void note_object( std::map<std::string, scene_object> &objects, const std::string &name,
                  int distance, int direction )
{
    scene_object &entry = objects[name];
    if( entry.count == 0 || distance < entry.nearest ) {
        entry.nearest = distance;
        entry.direction = direction;
    }
    entry.name = name;
    ++entry.count;
}

std::vector<scene_object> nearest_first( const std::map<std::string, scene_object> &objects )
{
    std::vector<scene_object> out;
    out.reserve( objects.size() );
    for( const std::pair<const std::string, scene_object> &entry : objects ) {
        out.push_back( entry.second );
    }
    std::stable_sort( out.begin(), out.end(), []( const scene_object & a, const scene_object & b ) {
        return a.nearest < b.nearest;
    } );
    return out;
}

bool is_exit_tile( const map &here, const tripoint_bub_ms &p )
{
    // Open doors drop the DOOR flag but stay barricadable.
    return here.has_flag( ter_furn_flag::TFLAG_DOOR, p ) ||
           here.has_flag( ter_furn_flag::TFLAG_BARRICADABLE_DOOR, p ) ||
           here.has_flag( ter_furn_flag::TFLAG_BARRICADABLE_DOOR_DAMAGED, p ) ||
           here.has_flag( ter_furn_flag::TFLAG_BARRICADABLE_DOOR_REINFORCED, p ) ||
           here.has_flag( ter_furn_flag::TFLAG_WINDOW, p ) ||
           here.has_flag( ter_furn_flag::TFLAG_BARRICADABLE_WINDOW_CURTAINS, p ) ||
           here.has_flag( ter_furn_flag::TFLAG_GOES_UP, p ) ||
           here.has_flag( ter_furn_flag::TFLAG_GOES_DOWN, p ) ||
           here.has_flag( ter_furn_flag::TFLAG_LADDER, p ) ||
           here.has_flag( ter_furn_flag::TFLAG_RAMP_UP, p ) ||
           here.has_flag( ter_furn_flag::TFLAG_RAMP_DOWN, p );
}

// Stairs are all called "stairs"; say which way they lead.
std::string exit_name( map &here, const tripoint_bub_ms &p )
{
    const std::string name = here.name( p );
    if( here.has_flag( ter_furn_flag::TFLAG_GOES_UP, p ) ||
        here.has_flag( ter_furn_flag::TFLAG_RAMP_UP, p ) ) {
        return string_format( _( "%s leading up" ), name );
    }
    if( here.has_flag( ter_furn_flag::TFLAG_GOES_DOWN, p ) ||
        here.has_flag( ter_furn_flag::TFLAG_RAMP_DOWN, p ) ) {
        return string_format( _( "%s leading down" ), name );
    }
    return name;
}

scene_obstacle probe_direction( map &here, const tripoint_bub_ms &origin,
                                const direction_step &step )
{
    scene_obstacle result;
    for( int distance = 1; distance <= open_distance_cap; ++distance ) {
        const tripoint_bub_ms p = origin + tripoint( step.dx * distance, step.dy * distance, 0 );
        if( !here.inbounds( p ) || !here.pl_sees( p, -1 ) ) {
            result.sight_limited = true;
            result.distance = distance - 1;
            return result;
        }
        if( here.impassable( p ) ) {
            result.reached_obstacle = true;
            result.sight_limited = false;
            result.distance = distance;
            result.name = here.obstacle_name( p );
            return result;
        }
    }
    result.sight_limited = false;
    result.distance = open_distance_cap;
    return result;
}

std::string light_word( lit_level light )
{
    switch( light ) {
        case lit_level::DARK:
            return _( "Dark" );
        case lit_level::LOW:
            return _( "Dim" );
        case lit_level::BRIGHT:
            return _( "Bright" );
        default:
            return _( "Lit" );
    }
}

std::string attitude_word( Creature::Attitude attitude )
{
    switch( attitude ) {
        case Creature::Attitude::HOSTILE:
            return _( "hostile" );
        case Creature::Attitude::FRIENDLY:
            return _( "friendly" );
        default:
            return _( "neutral" );
    }
}

// Creatures of one kind, in one direction, with one attitude.
struct creature_group {
    std::string name;
    std::string plural_name;
    Creature::Attitude attitude = Creature::Attitude::NEUTRAL;
    bool attitude_known = true;
    int direction = scene_direction_here;
    int count = 0;
    int nearest = 0;
    int seeing = 0;
};

// Input is nearest first, so groups come out nearest first as well.
std::vector<creature_group> group_creatures( const std::vector<const scene_creature *> &creatures )
{
    std::vector<creature_group> groups;
    for( const scene_creature *critter : creatures ) {
        const auto found = std::find_if( groups.begin(), groups.end(),
        [&]( const creature_group & group ) {
            return group.name == critter->name && group.direction == critter->direction &&
                   group.attitude == critter->attitude;
        } );
        creature_group *group = nullptr;
        if( found == groups.end() ) {
            groups.emplace_back();
            group = &groups.back();
            group->name = critter->name;
            group->plural_name = critter->plural_name.empty() ? critter->name : critter->plural_name;
            group->attitude = critter->attitude;
            group->attitude_known = critter->attitude_known;
            group->direction = critter->direction;
            group->nearest = critter->distance;
        } else {
            group = &*found;
            group->nearest = std::min( group->nearest, critter->distance );
        }
        ++group->count;
        if( critter->sees_player ) {
            ++group->seeing;
        }
    }
    return groups;
}

// "zombie 5 east, sees you" or "4 zombies north, nearest 7, 3 see you".
std::string creature_group_phrase( const creature_group &group, bool with_awareness,
                                   bool with_attitude )
{
    std::string out;
    if( group.count == 1 ) {
        out = string_format( "%s %s", group.name,
                             scene_position_phrase( group.nearest, group.direction ) );
    } else {
        out = string_format( _( "%1$d %2$s %3$s, nearest %4$d" ), group.count, group.plural_name,
                             direction_word( group.direction ), group.nearest );
    }
    if( with_attitude && group.attitude_known ) {
        out += ", " + attitude_word( group.attitude );
    }
    if( with_awareness && group.attitude_known ) {
        if( group.count == 1 ) {
            out += group.seeing > 0 ? _( ", sees you" ) : _( ", does not see you" );
        } else if( group.seeing == group.count ) {
            out += _( ", all see you" );
        } else if( group.seeing == 0 ) {
            out += _( ", none see you" );
        } else if( group.seeing == 1 ) {
            out += _( ", 1 sees you" );
        } else {
            out += string_format( _( ", %d see you" ), group.seeing );
        }
    }
    return out;
}

std::string obstacle_phrase( const scene_obstacle &obstacle, int direction )
{
    if( obstacle.reached_obstacle ) {
        return string_format( "%s %s", obstacle.name,
                              scene_position_phrase( obstacle.distance, direction ) );
    }
    if( !obstacle.sight_limited ) {
        return string_format( _( "open %1$d or more %2$s" ), obstacle.distance,
                              direction_word( direction ) );
    }
    if( obstacle.distance <= 0 ) {
        return string_format( _( "%s out of sight" ), direction_word( direction ) );
    }
    return string_format( _( "open %1$d %2$s then out of sight" ), obstacle.distance,
                          direction_word( direction ) );
}

// "fire 4 east" or "fire on 3 tiles, nearest 4 east".
std::string hazard_phrase( const scene_object &hazard )
{
    const std::string position = scene_position_phrase( hazard.nearest, hazard.direction );
    if( hazard.count <= 1 ) {
        return string_format( "%s %s", hazard.name, position );
    }
    return string_format( _( "%1$s on %2$d tiles, nearest %3$s" ), hazard.name, hazard.count,
                          position );
}

// "refrigerator 3 north" or "kitchen counter, 2 in view, nearest 3 northeast".
std::string object_phrase( const scene_object &object )
{
    const std::string position = scene_position_phrase( object.nearest, object.direction );
    if( object.count <= 1 ) {
        return string_format( "%s %s", object.name, position );
    }
    return string_format( _( "%1$s, %2$d in view, nearest %3$s" ), object.name, object.count,
                          position );
}

// "Label: a; b; c. 2 more."
std::string list_sentence( const std::string &label, const std::vector<std::string> &entries,
                           size_t cap )
{
    if( entries.empty() ) {
        return std::string();
    }
    const size_t shown = std::min( cap, entries.size() );
    const std::vector<std::string> head( entries.begin(), entries.begin() + shown );
    std::string out = label + ": " + join_parts( head, "; " ) + ".";
    if( entries.size() > shown ) {
        out += " " + string_format( _( "%d more." ), static_cast<int>( entries.size() - shown ) );
    }
    return out;
}

} // namespace

int scene_direction_index( int dx, int dy )
{
    if( dx == 0 && dy == 0 ) {
        return scene_direction_here;
    }
    switch( direction_from( point( dx, dy ) ) ) {
        case direction::NORTH:
            return 0;
        case direction::NORTHEAST:
            return 1;
        case direction::EAST:
            return 2;
        case direction::SOUTHEAST:
            return 3;
        case direction::SOUTH:
            return 4;
        case direction::SOUTHWEST:
            return 5;
        case direction::WEST:
            return 6;
        case direction::NORTHWEST:
            return 7;
        default:
            return scene_direction_here;
    }
}

std::string scene_position_phrase( int distance, int direction )
{
    if( distance <= 0 || direction < 0 || direction >= scene_direction_here ) {
        return _( "here" );
    }
    if( distance == 1 ) {
        return string_format( _( "next to you, %s" ), direction_word( direction ) );
    }
    return string_format( "%d %s", distance, direction_word( direction ) );
}

scene_snapshot gather_scene( const avatar &ava )
{
    map &here = get_map();
    const tripoint_bub_ms origin = ava.pos_bub();
    scene_snapshot scene;

    scene.outdoors = here.is_outside( origin );
    scene.place = overmap_buffer.ter( ava.pos_abs_omt() )->get_name( om_vision_level::full );
    scene.light = here.light_at( origin );
    if( scene.outdoors ) {
        scene.weather = get_weather().weather_id->name.translated();
    }

    // Inattentive characters cannot judge attitude or awareness, matching
    // the look cursor and the creature scanner.
    const bool inattentive = ava.has_trait( trait_INATTENTIVE );
    for( const Creature *critter : ava.get_visible_creatures( MAX_VIEW_DISTANCE ) ) {
        if( critter == nullptr || critter == &ava ) {
            continue;
        }
        const tripoint_bub_ms pos = critter->pos_bub();
        scene_creature entry;
        if( const monster *mon = dynamic_cast<const monster *>( critter ) ) {
            entry.name = mon->name();
            entry.plural_name = mon->name( 2 );
        } else {
            entry.name = critter->get_name();
            entry.plural_name = entry.name;
        }
        entry.attitude_known = !inattentive;
        if( !inattentive ) {
            entry.attitude = critter->attitude_to( ava );
            entry.sees_player = critter->sees( here, ava );
        }
        entry.distance = rl_dist( origin, pos );
        entry.direction = scene_direction_index( pos.x() - origin.x(), pos.y() - origin.y() );
        scene.creatures.push_back( entry );
    }
    std::stable_sort( scene.creatures.begin(), scene.creatures.end(),
    []( const scene_creature & a, const scene_creature & b ) {
        return a.distance < b.distance;
    } );

    for( size_t i = 0; i < direction_steps.size(); ++i ) {
        scene.obstacles[i] = probe_direction( here, origin, direction_steps[i] );
    }

    std::map<std::string, scene_object> hazards;
    std::map<std::string, scene_object> furniture;
    std::map<const vehicle *, scene_object> vehicles;
    bool found_items = false;

    for( int dy = -MAX_VIEW_DISTANCE; dy <= MAX_VIEW_DISTANCE; ++dy ) {
        for( int dx = -MAX_VIEW_DISTANCE; dx <= MAX_VIEW_DISTANCE; ++dx ) {
            if( dx == 0 && dy == 0 ) {
                continue;
            }
            const tripoint_bub_ms p = origin + tripoint( dx, dy, 0 );
            if( !here.inbounds( p ) || !here.pl_sees( p, -1 ) ) {
                continue;
            }
            const int distance = rl_dist( origin, p );
            const int direction = scene_direction_index( dx, dy );
            scene.sight_range = std::max( scene.sight_range, distance );

            if( is_exit_tile( here, p ) ) {
                scene_feature exit;
                exit.name = exit_name( here, p );
                exit.distance = distance;
                exit.direction = direction;
                scene.exits.push_back( exit );
            }

            // A sighted player sees the field drawn on the tile, not every
            // field layered beneath it.
            const field &fields = here.field_at( p );
            if( fields.field_count() > 0 ) {
                const field_entry *shown = fields.find_field( fields.displayed_field_type() );
                if( shown != nullptr && shown->is_dangerous() ) {
                    note_object( hazards, shown->name(), distance, direction );
                }
            }

            const trap &tr = here.tr_at( p );
            if( !tr.is_null() && !tr.is_benign() && tr.can_see( p, ava ) ) {
                note_object( hazards, tr.name(), distance, direction );
            }

            if( here.has_furn( p ) ) {
                note_object( furniture, here.furnname( p ), distance, direction );
            }

            if( const optional_vpart_position vp = here.veh_at( p ) ) {
                const vehicle &veh = vp->vehicle();
                scene_object &entry = vehicles[&veh];
                if( entry.count == 0 || distance < entry.nearest ) {
                    entry.nearest = distance;
                    entry.direction = direction;
                }
                entry.name = veh.name;
                entry.count = 1;
            }

            if( here.sees_some_items( p, ava ) ) {
                ++scene.item_tiles;
                if( !found_items || distance < scene.nearest_item_distance ) {
                    scene.nearest_item_distance = distance;
                    scene.nearest_item_direction = direction;
                    found_items = true;
                }
            }
        }
    }

    std::stable_sort( scene.exits.begin(), scene.exits.end(),
    []( const scene_feature & a, const scene_feature & b ) {
        return a.distance < b.distance;
    } );
    scene.hazards = nearest_first( hazards );
    scene.furniture = nearest_first( furniture );

    // Several vehicles with one name, such as identical appliances, become
    // one entry with a count.
    std::map<std::string, scene_object> vehicles_by_name;
    for( const std::pair<const vehicle *const, scene_object> &entry : vehicles ) {
        note_object( vehicles_by_name, entry.second.name, entry.second.nearest,
                     entry.second.direction );
    }
    scene.vehicles = nearest_first( vehicles_by_name );

    scene.sight_reduced = ( scene.light == lit_level::DARK || scene.light == lit_level::LOW ) &&
                          scene.sight_range <= reduced_sight_tiles;
    return scene;
}

std::string render_scene( const scene_snapshot &scene )
{
    std::vector<std::string> parts;

    // Setting.
    std::string setting = scene.outdoors ? _( "Outdoors" ) : _( "Indoors" );
    if( !scene.place.empty() ) {
        setting += ", " + scene.place;
    }
    parts.push_back( setting + "." );
    parts.push_back( light_word( scene.light ) + "." );
    if( scene.sight_reduced ) {
        parts.push_back( scene.sight_range == 1 ? std::string( _( "You can see 1 tile." ) ) :
                         string_format( _( "You can see %d tiles." ), scene.sight_range ) );
    }
    if( !scene.weather.empty() ) {
        parts.push_back( string_format( _( "Weather: %s." ), scene.weather ) );
    }

    // Creatures.
    std::vector<const scene_creature *> hostiles;
    std::vector<const scene_creature *> others;
    std::vector<const scene_creature *> unknown;
    for( const scene_creature &critter : scene.creatures ) {
        if( !critter.attitude_known ) {
            unknown.push_back( &critter );
        } else if( critter.attitude == Creature::Attitude::HOSTILE ) {
            hostiles.push_back( &critter );
        } else {
            others.push_back( &critter );
        }
    }

    if( !hostiles.empty() ) {
        const std::vector<creature_group> groups = group_creatures( hostiles );
        std::vector<std::string> phrases;
        phrases.reserve( groups.size() );
        for( const creature_group &group : groups ) {
            phrases.push_back( creature_group_phrase( group, true, false ) );
        }
        parts.push_back( list_sentence( _( "Hostiles" ), phrases, phrases.size() ) );
    } else if( unknown.empty() ) {
        parts.emplace_back( _( "No hostiles in view." ) );
    }

    if( !unknown.empty() ) {
        const std::vector<creature_group> groups = group_creatures( unknown );
        std::vector<std::string> phrases;
        phrases.reserve( groups.size() );
        for( const creature_group &group : groups ) {
            phrases.push_back( creature_group_phrase( group, false, false ) );
        }
        parts.push_back( list_sentence( _( "Creatures, attitude unknown" ), phrases,
                                        phrases.size() ) );
    }

    if( !others.empty() ) {
        const std::vector<creature_group> groups = group_creatures( others );
        std::vector<std::string> phrases;
        phrases.reserve( groups.size() );
        for( const creature_group &group : groups ) {
            phrases.push_back( creature_group_phrase( group, false, true ) );
        }
        parts.push_back( list_sentence( _( "Other creatures" ), phrases, phrases.size() ) );
    }

    // Shape of the surrounding space.
    std::vector<std::string> space;
    for( int direction = 0; direction < 8; ++direction ) {
        space.push_back( obstacle_phrase( scene.obstacles[direction], direction ) );
    }
    parts.push_back( uppercase_first_letter( join_parts( space, ", " ) ) + "." );

    // Exits.
    std::vector<std::string> exits;
    exits.reserve( scene.exits.size() );
    for( const scene_feature &exit : scene.exits ) {
        exits.push_back( string_format( "%s %s", exit.name,
                                        scene_position_phrase( exit.distance, exit.direction ) ) );
    }
    if( !exits.empty() ) {
        parts.push_back( list_sentence( _( "Exits" ), exits, max_exits ) );
    } else {
        parts.emplace_back( _( "No exits in view." ) );
    }

    // Hazards.
    std::vector<std::string> hazards;
    hazards.reserve( scene.hazards.size() );
    for( const scene_object &hazard : scene.hazards ) {
        hazards.push_back( hazard_phrase( hazard ) );
    }
    parts.push_back( list_sentence( _( "Hazards" ), hazards, hazards.size() ) );

    // Objects.
    std::vector<std::string> furniture;
    furniture.reserve( scene.furniture.size() );
    for( const scene_object &object : scene.furniture ) {
        furniture.push_back( object_phrase( object ) );
    }
    parts.push_back( list_sentence( _( "Furniture" ), furniture, max_object_groups ) );

    std::vector<std::string> vehicles;
    vehicles.reserve( scene.vehicles.size() );
    for( const scene_object &object : scene.vehicles ) {
        vehicles.push_back( object_phrase( object ) );
    }
    parts.push_back( list_sentence( _( "Vehicles" ), vehicles, max_object_groups ) );

    if( scene.item_tiles == 1 ) {
        parts.push_back( string_format( _( "Items on 1 tile, %s." ),
                                        scene_position_phrase( scene.nearest_item_distance,
                                                scene.nearest_item_direction ) ) );
    } else if( scene.item_tiles > 1 ) {
        parts.push_back( string_format( _( "Items on %1$d tiles, nearest %2$s." ),
                                        scene.item_tiles,
                                        scene_position_phrase( scene.nearest_item_distance,
                                                scene.nearest_item_direction ) ) );
    }

    return remove_color_tags( join_parts( parts, " " ) );
}

std::string describe_scene( const avatar &ava )
{
    return render_scene( gather_scene( ava ) );
}
