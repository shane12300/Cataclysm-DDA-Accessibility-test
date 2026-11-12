#include <algorithm>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "creature.h"
#include "game.h"
#include "lightmap.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "monster.h"
#include "player_helpers.h"
#include "point.h"
#include "scene_description.h"
#include "type_id.h"

static const ter_str_id ter_t_door_c( "t_door_c" );
static const ter_str_id ter_t_wall( "t_wall" );
static const ter_str_id ter_t_window( "t_window" );

static bool contains( const std::string &haystack, const std::string &needle )
{
    return haystack.find( needle ) != std::string::npos;
}

static scene_creature make_creature( const std::string &name, const std::string &plural,
                                     Creature::Attitude attitude, int distance, int direction,
                                     bool sees_player )
{
    scene_creature critter;
    critter.name = name;
    critter.plural_name = plural;
    critter.attitude = attitude;
    critter.distance = distance;
    critter.direction = direction;
    critter.sees_player = sees_player;
    return critter;
}

// A lit room: walls on three sides and open ground to the south.
static scene_snapshot make_room()
{
    scene_snapshot scene;
    scene.outdoors = false;
    scene.place = "house";
    scene.light = lit_level::LIT;
    scene.sight_range = 6;
    for( scene_obstacle &obstacle : scene.obstacles ) {
        obstacle.reached_obstacle = true;
        obstacle.sight_limited = false;
        obstacle.distance = 2;
        obstacle.name = "wall";
    }
    scene.obstacles[4].reached_obstacle = false;
    scene.obstacles[4].distance = 20;
    scene.obstacles[4].name.clear();
    return scene;
}

// Rebuild light and vision caches after editing the map, as vision_test does.
static void refresh_vision( int zlev )
{
    map &here = get_map();
    here.invalidate_visibility_cache();
    here.update_visibility_cache( zlev );
    for( int z = -2; z <= OVERMAP_HEIGHT; z++ ) {
        here.invalidate_map_cache( z );
    }
    here.build_map_cache( zlev );
    here.invalidate_visibility_cache();
    here.update_visibility_cache( zlev );
    here.invalidate_map_cache( zlev );
    here.build_map_cache( zlev );
}

TEST_CASE( "scene positions are spoken relative to the player",
           "[scene_description][accessibility]" )
{
    CHECK( scene_direction_index( 0, -5 ) == 0 );
    CHECK( scene_direction_index( 5, -5 ) == 1 );
    CHECK( scene_direction_index( 5, 0 ) == 2 );
    CHECK( scene_direction_index( 5, 5 ) == 3 );
    CHECK( scene_direction_index( 0, 5 ) == 4 );
    CHECK( scene_direction_index( -5, 5 ) == 5 );
    CHECK( scene_direction_index( -5, 0 ) == 6 );
    CHECK( scene_direction_index( -5, -5 ) == 7 );
    CHECK( scene_direction_index( 0, 0 ) == scene_direction_here );

    CHECK( scene_position_phrase( 4, 2 ) == "4 east" );
    CHECK( scene_position_phrase( 1, 6 ) == "next to you, west" );
    CHECK( scene_position_phrase( 0, scene_direction_here ) == "here" );
}

TEST_CASE( "an empty room is described by setting, space and exits",
           "[scene_description][accessibility]" )
{
    scene_snapshot scene = make_room();
    scene_feature door;
    door.name = "closed wood door";
    door.distance = 6;
    door.direction = 4;
    scene.exits.push_back( door );

    const std::string spoken = render_scene( scene );
    CHECK( contains( spoken, "Indoors, house." ) );
    CHECK( contains( spoken, "Lit." ) );
    CHECK( contains( spoken, "No hostiles in view." ) );
    CHECK( contains( spoken, "Wall 2 north, wall 2 northeast, wall 2 east, wall 2 southeast, "
                     "open 20 or more south, wall 2 southwest, wall 2 west, wall 2 northwest." ) );
    CHECK( contains( spoken, "Exits: closed wood door 6 south." ) );
    // Indoors there is no weather, and empty sections are left out.
    CHECK_FALSE( contains( spoken, "Weather" ) );
    CHECK_FALSE( contains( spoken, "Hazards" ) );
    CHECK_FALSE( contains( spoken, "Furniture" ) );
    CHECK_FALSE( contains( spoken, "Items" ) );

    scene.exits.clear();
    CHECK( contains( render_scene( scene ), "No exits in view." ) );
}

TEST_CASE( "hostiles are grouped by kind and direction", "[scene_description][accessibility]" )
{
    scene_snapshot scene = make_room();
    scene.outdoors = true;
    scene.place = "road";
    scene.weather = "clear";
    using attitude = Creature::Attitude;
    scene.creatures.push_back( make_creature( "zombie dog", "zombie dogs", attitude::HOSTILE,
                               5, 2, true ) );
    scene.creatures.push_back( make_creature( "zombie", "zombies", attitude::HOSTILE, 7, 0, true ) );
    scene.creatures.push_back( make_creature( "zombie", "zombies", attitude::HOSTILE, 8, 0, true ) );
    scene.creatures.push_back( make_creature( "zombie", "zombies", attitude::HOSTILE, 9, 0, false ) );
    scene.creatures.push_back( make_creature( "fat zombie", "fat zombies", attitude::HOSTILE,
                               12, 5, false ) );
    scene.creatures.push_back( make_creature( "boomer", "boomers", attitude::HOSTILE, 15, 6,
                               false ) );
    scene.creatures.push_back( make_creature( "dog", "dogs", attitude::FRIENDLY, 1, 6, true ) );

    const std::string spoken = render_scene( scene );
    CHECK( contains( spoken, "Outdoors, road." ) );
    CHECK( contains( spoken, "Weather: clear." ) );
    CHECK( contains( spoken, "Hostiles: zombie dog 5 east, sees you; "
                     "3 zombies north, nearest 7, 2 see you; "
                     "fat zombie 12 southwest, does not see you; "
                     "boomer 15 west, does not see you." ) );
    CHECK_FALSE( contains( spoken, "No hostiles" ) );
    CHECK( contains( spoken, "Other creatures: dog next to you, west, friendly." ) );
}

TEST_CASE( "attitude is withheld when it cannot be judged", "[scene_description][accessibility]" )
{
    scene_snapshot scene = make_room();
    scene_creature critter = make_creature( "zombie", "zombies", Creature::Attitude::NEUTRAL,
                                            3, 2, false );
    critter.attitude_known = false;
    scene.creatures.push_back( critter );

    const std::string brief = render_scene( scene );
    CHECK( contains( brief, "Creatures, attitude unknown: zombie 3 east." ) );
    CHECK_FALSE( contains( brief, "No hostiles" ) );
    CHECK_FALSE( contains( brief, "see" ) );
}

TEST_CASE( "darkness is reported as a short view", "[scene_description][accessibility]" )
{
    scene_snapshot scene = make_room();
    scene.place = "basement";
    scene.light = lit_level::DARK;
    scene.sight_range = 1;
    scene.sight_reduced = true;
    scene.obstacles[0].distance = 1;
    scene.obstacles[2] = scene_obstacle();
    scene.obstacles[4] = scene_obstacle();
    scene.obstacles[4].distance = 1;
    scene.obstacles[6].distance = 1;

    const std::string brief = render_scene( scene );
    CHECK( contains( brief, "Indoors, basement. Dark. You can see 1 tile." ) );
    CHECK( contains( brief, "Wall next to you, north, wall 2 northeast, east out of sight, "
                     "wall 2 southeast, open 1 south then out of sight, wall 2 southwest, "
                     "wall next to you, west, wall 2 northwest." ) );
}

TEST_CASE( "hazards and objects are counted and placed", "[scene_description][accessibility]" )
{
    scene_snapshot scene = make_room();
    scene_object fire;
    fire.name = "fire";
    fire.count = 3;
    fire.nearest = 4;
    fire.direction = 2;
    scene.hazards.push_back( fire );
    scene_object trap;
    trap.name = "bear trap";
    trap.count = 1;
    trap.nearest = 2;
    trap.direction = 4;
    scene.hazards.push_back( trap );
    scene_object counter;
    counter.name = "kitchen counter";
    counter.count = 2;
    counter.nearest = 3;
    counter.direction = 1;
    scene.furniture.push_back( counter );
    scene_object fridge;
    fridge.name = "refrigerator";
    fridge.count = 1;
    fridge.nearest = 3;
    fridge.direction = 0;
    scene.furniture.push_back( fridge );
    scene.item_tiles = 5;
    scene.nearest_item_distance = 2;
    scene.nearest_item_direction = 2;
    for( int i = 0; i < 8; ++i ) {
        scene_feature window;
        window.name = "window";
        window.distance = 3 + i;
        window.direction = 2;
        scene.exits.push_back( window );
    }

    const std::string spoken = render_scene( scene );
    CHECK( contains( spoken, "Hazards: fire on 3 tiles, nearest 4 east; bear trap 2 south." ) );
    CHECK( contains( spoken, "Exits: window 3 east; window 4 east; window 5 east; window 6 east; "
                     "window 7 east; window 8 east. 2 more." ) );
    CHECK( contains( spoken, "Furniture: kitchen counter, 2 in view, nearest 3 northeast; "
                     "refrigerator 3 north." ) );
    CHECK( contains( spoken, "Items on 5 tiles, nearest 2 east." ) );
}

TEST_CASE( "the scene is gathered from what the avatar can see",
           "[scene_description][accessibility]" )
{
    map &here = get_map();
    avatar &ava = get_avatar();
    clear_avatar();
    clear_map_without_vision();
    set_time( calendar::turn_zero + 12_hours );
    const tripoint_bub_ms origin = ava.pos_bub();

    SECTION( "open ground at noon" ) {
        refresh_vision( origin.z() );
        const scene_snapshot scene = gather_scene( ava );
        CHECK( scene.outdoors );
        CHECK_FALSE( scene.sight_reduced );
        CHECK( scene.creatures.empty() );
        for( const scene_obstacle &obstacle : scene.obstacles ) {
            CHECK_FALSE( obstacle.reached_obstacle );
            CHECK_FALSE( obstacle.sight_limited );
        }
        const std::string brief = render_scene( scene );
        CHECK( contains( brief, "Outdoors" ) );
        CHECK( contains( brief, "No hostiles in view." ) );
    }

    SECTION( "a visible hostile is reported with its position" ) {
        monster &zed = spawn_test_monster( "mon_zombie", origin + tripoint( 3, 0, 0 ) );
        refresh_vision( origin.z() );
        REQUIRE( ava.sees( here, zed ) );
        REQUIRE( zed.attitude_to( ava ) == Creature::Attitude::HOSTILE );
        const scene_snapshot scene = gather_scene( ava );
        REQUIRE( scene.creatures.size() == 1 );
        CHECK( scene.creatures[0].name == "zombie" );
        CHECK( scene.creatures[0].distance == 3 );
        CHECK( scene.creatures[0].direction == 2 );
        CHECK( scene.creatures[0].attitude == Creature::Attitude::HOSTILE );
        CHECK( scene.creatures[0].sees_player == zed.sees( here, ava ) );
        CHECK( contains( render_scene( scene ), "Hostiles: zombie 3 east" ) );
    }

    SECTION( "walls, doors and windows shape the description" ) {
        // A wall two tiles north with a door in it straight ahead and a
        // window just to the side.
        for( int dx = -3; dx <= 3; ++dx ) {
            here.ter_set( origin + tripoint( dx, -2, 0 ), ter_t_wall.id() );
        }
        here.ter_set( origin + tripoint( 0, -2, 0 ), ter_t_door_c.id() );
        here.ter_set( origin + tripoint( 1, -2, 0 ), ter_t_window.id() );
        refresh_vision( origin.z() );

        const scene_snapshot scene = gather_scene( ava );
        CHECK( scene.obstacles[0].reached_obstacle );
        CHECK( scene.obstacles[0].distance == 2 );
        CHECK( scene.obstacles[0].name == "closed wood door" );
        CHECK_FALSE( scene.obstacles[4].reached_obstacle );

        const auto has_exit = [&scene]( const std::string & name ) {
            return std::any_of( scene.exits.begin(), scene.exits.end(),
            [&name]( const scene_feature & exit ) {
                return exit.name == name && exit.distance == 2;
            } );
        };
        CHECK( has_exit( "closed wood door" ) );
        CHECK( has_exit( "window" ) );
        // Nearest first.
        REQUIRE_FALSE( scene.exits.empty() );
        CHECK( scene.exits.front().distance == 2 );

        const std::string brief = render_scene( scene );
        CHECK( contains( brief, "Closed wood door 2 north" ) );
        CHECK( contains( brief, "Exits: " ) );
    }

    SECTION( "a creature behind a wall is not reported" ) {
        for( int dy = -3; dy <= 3; ++dy ) {
            here.ter_set( origin + tripoint( 2, dy, 0 ), ter_t_wall.id() );
        }
        monster &zed = spawn_test_monster( "mon_zombie", origin + tripoint( 4, 0, 0 ) );
        refresh_vision( origin.z() );
        REQUIRE_FALSE( ava.sees( here, zed ) );
        const scene_snapshot scene = gather_scene( ava );
        CHECK( scene.creatures.empty() );
        CHECK( scene.obstacles[2].reached_obstacle );
        CHECK( scene.obstacles[2].distance == 2 );
        CHECK( scene.obstacles[2].name == "wall" );
        CHECK( contains( render_scene( scene ), "No hostiles in view." ) );
    }
}
