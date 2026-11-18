#include <algorithm>
#include <climits>
#include <optional>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "player_helpers.h"
#include "point.h"
#include "sidebar_accessibility.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_accessibility.h"
#include "vpart_position.h"
#include "vpart_range.h"

static const vproto_id vehicle_prototype_motorcycle( "motorcycle" );

static const widget_id widget_vehicle_azimuth_desc_label( "vehicle_azimuth_desc_label" );
static const widget_id widget_vehicle_cruise_desc_label( "vehicle_cruise_desc_label" );

static bool contains( const std::string &haystack, const std::string &needle )
{
    return haystack.find( needle ) != std::string::npos;
}

TEST_CASE( "vehicle headings are spoken as compass points",
           "[vehicle_accessibility][accessibility]" )
{
    // The game's 0 degrees points east; spoken azimuth uses 0 = north.
    CHECK( azimuth_degrees( 0_degrees ) == 90 );
    CHECK( azimuth_degrees( 270_degrees ) == 0 );
    CHECK( azimuth_degrees( -90_degrees ) == 0 );
    CHECK( azimuth_degrees( 90_degrees ) == 180 );
    CHECK( compass_point_name( 0_degrees ) == "east" );
    CHECK( compass_point_name( 270_degrees ) == "north" );
    CHECK( compass_point_name( 180_degrees ) == "west" );
    CHECK( compass_point_name( 15_degrees ) == "east-southeast" );
    CHECK( compass_point_name( -15_degrees ) == "east-northeast" );
    CHECK( compass_point_name( 45_degrees ) == "southeast" );
    CHECK( describe_heading( 15_degrees ) == "105 degrees, east-southeast" );
    CHECK( describe_heading( 270_degrees ) == "0 degrees, north" );
}

TEST_CASE( "vehicle speed and strain are spoken in words",
           "[vehicle_accessibility][accessibility]" )
{
    CHECK( engine_strain_word( 0.0f ).empty() );
    CHECK( engine_strain_word( 0.1f ) == "engine straining" );
    CHECK( engine_strain_word( 0.3f ) == "engine heavily strained" );
    CHECK( engine_strain_word( 0.9f ) == "engine overloaded" );

    CHECK( contains( describe_speed( 0, 0 ), "stopped" ) );
    const std::string speeding_up = describe_speed( 1000, 2500 );
    CHECK( contains( speeding_up, "speed " ) );
    CHECK( contains( speeding_up, "target " ) );
    CHECK( contains( speeding_up, "accelerating" ) );
    // Never the sidebar's "<", which a screen reader reads as "less than".
    CHECK_FALSE( contains( speeding_up, "<" ) );
    CHECK( contains( describe_speed( 2500, 1000 ), "slowing" ) );
    CHECK( contains( describe_speed( 2000, 2000 ), "holding speed" ) );
    CHECK( contains( describe_speed( -500, -500 ), "reversing" ) );
}

TEST_CASE( "steering and throttle changes are described", "[vehicle_accessibility][accessibility]" )
{
    driving_snapshot before;
    before.valid = true;
    before.face = 0_degrees;
    before.turn_dir = 0_degrees;
    before.velocity = 1000;
    before.cruise_velocity = 1000;

    SECTION( "no change says nothing" ) {
        CHECK( describe_driving_change( before, before ).empty() );
    }
    SECTION( "turning clockwise is a right turn" ) {
        driving_snapshot after = before;
        after.turn_dir = 15_degrees;
        CHECK( describe_driving_change( before, after ) ==
               "Turning right to 105 degrees, east-southeast." );
    }
    SECTION( "turning across north is a left turn" ) {
        before.turn_dir = 270_degrees;
        driving_snapshot after = before;
        after.turn_dir = 255_degrees;
        const std::string spoken = describe_driving_change( before, after );
        CHECK( contains( spoken, "Turning left to 345 degrees" ) );
    }
    SECTION( "throttle reports target and current speed" ) {
        driving_snapshot after = before;
        after.cruise_velocity = 1400;
        const std::string spoken = describe_driving_change( before, after );
        CHECK( contains( spoken, "Target " ) );
        CHECK( contains( spoken, ", speed " ) );
    }
    SECTION( "starting to skid is announced" ) {
        driving_snapshot after = before;
        after.skidding = true;
        CHECK( describe_driving_change( before, after ) == "Skidding." );
    }
}

TEST_CASE( "vehicle mount positions use the vehicle's own frame",
           "[vehicle_accessibility][accessibility]" )
{
    const point_rel_ms mn( -2, -1 );
    const point_rel_ms mx( 2, 1 );
    CHECK( describe_mount_region( point_rel_ms( 2, -1 ), mn, mx ) == "front left" );
    CHECK( describe_mount_region( point_rel_ms( -2, 1 ), mn, mx ) == "rear right" );
    CHECK( describe_mount_region( point_rel_ms( 0, 0 ), mn, mx ) == "middle" );
    CHECK( describe_mount_region( point_rel_ms( 0, 1 ), mn, mx ) == "middle right" );
    CHECK( describe_mount_region( point_rel_ms( 2, 0 ), mn, mx ) == "front" );
    CHECK( describe_mount_region( point_rel_ms::zero, point_rel_ms::zero, point_rel_ms::zero ) ==
           "only tile" );
}

TEST_CASE( "map displacement is spoken as distance and direction",
           "[vehicle_accessibility][accessibility]" )
{
    CHECK( describe_displacement( point_rel_ms::zero ).empty() );
    CHECK( describe_displacement( point_rel_ms( 1, 0 ) ) == "1 tile east" );
    CHECK( describe_displacement( point_rel_ms( 3, -3 ) ) == "3 tiles northeast" );
    CHECK( describe_displacement( point_rel_ms( 0, 6 ) ) == "6 tiles south" );
}

TEST_CASE( "driving speech level cycles and wraps", "[vehicle_accessibility][accessibility]" )
{
    driving_speech_level &level = driving_speech_setting();
    const driving_speech_level saved = level;
    level = driving_speech_level::off;
    CHECK( contains( cycle_driving_speech_level(), "steering and speed changes only" ) );
    CHECK( level == driving_speech_level::controls );
    cycle_driving_speech_level();
    cycle_driving_speech_level();
    CHECK( level == driving_speech_level::motion_and_scan );
    CHECK( contains( cycle_driving_speech_level(), "off" ) );
    CHECK( level == driving_speech_level::off );
    level = saved;
}

namespace
{

struct test_vehicle {
    vehicle *veh = nullptr;
    int front_x = INT_MIN;
    int lane_y = 0;
};

// A motorcycle on open pavement at noon, facing east, with the avatar driving.
test_vehicle place_driven_motorcycle()
{
    clear_avatar();
    clear_map();
    clear_vehicles();
    build_test_map( ter_id( "t_pavement" ) );
    set_time_to_day();

    map &here = get_map();
    test_vehicle result;
    const tripoint_bub_ms origin( 60, 60, 0 );
    result.veh = here.add_vehicle( vehicle_prototype_motorcycle, origin, 0_degrees, 100,
                                   veh_spawn_status::UNDAMAGED );
    REQUIRE( result.veh != nullptr );
    vehicle &veh = *result.veh;
    veh.tags.insert( "IN_CONTROL_OVERRIDE" );

    std::optional<tripoint_bub_ms> controls;
    for( const vpart_reference &vpr : veh.get_all_parts() ) {
        const tripoint_bub_ms p = vpr.pos_bub( here );
        result.front_x = std::max( result.front_x, p.x() );
        if( vpr.info().has_flag( "CONTROLS" ) ) {
            controls = p;
        }
    }
    REQUIRE( controls.has_value() );
    result.lane_y = controls->y();
    avatar &ava = get_avatar();
    ava.setpos( here, *controls );
    here.board_vehicle( *controls, &ava );
    ava.controlling_vehicle = true;
    here.invalidate_map_cache( 0 );
    here.build_map_cache( 0, true );
    return result;
}

void leave_vehicle()
{
    map &here = get_map();
    avatar &ava = get_avatar();
    if( ava.in_vehicle ) {
        here.unboard_vehicle( ava.pos_bub( here ) );
    }
    ava.controlling_vehicle = false;
    clear_vehicles();
}

} // namespace

TEST_CASE( "forward scan reports visible obstacles in the vehicle's path",
           "[vehicle_accessibility][accessibility][vehicle]" )
{
    map &here = get_map();
    const test_vehicle tv = place_driven_motorcycle();
    const avatar &ava = get_avatar();

    SECTION( "open road" ) {
        const std::string scan = vehicle_forward_scan( here, *tv.veh, ava, 12 );
        CHECK( contains( scan, "Ahead, toward 90 degrees, east" ) );
        CHECK( contains( scan, "road for at least" ) );
        CHECK( contains( scan, "Path clear for 12 tiles" ) );
    }
    SECTION( "a wall ahead is reported with its distance" ) {
        const tripoint_bub_ms wall( tv.front_x + 6, tv.lane_y, 0 );
        here.ter_set( wall, ter_id( "t_wall" ) );
        here.invalidate_map_cache( 0 );
        here.build_map_cache( 0, true );
        REQUIRE( ava.sees( here, wall ) );
        const std::string scan = vehicle_forward_scan( here, *tv.veh, ava, 12 );
        CAPTURE( scan );
        CHECK( contains( scan, "wall 6 tiles, center" ) );
        CHECK_FALSE( contains( scan, "Path clear" ) );
    }
    SECTION( "the brief form names the nearest creature" ) {
        const tripoint_bub_ms ahead( tv.front_x + 4, tv.lane_y, 0 );
        spawn_test_monster( "mon_zombie", ahead );
        const std::string scan = vehicle_forward_scan( here, *tv.veh, ava, 12, true );
        CAPTURE( scan );
        CHECK( contains( scan, "zombie 4 tiles" ) );
    }
    SECTION( "creatures of the same name on one side are counted" ) {
        spawn_test_monster( "mon_zombie", tripoint_bub_ms( tv.front_x + 4, tv.lane_y, 0 ) );
        spawn_test_monster( "mon_zombie", tripoint_bub_ms( tv.front_x + 7, tv.lane_y, 0 ) );
        const std::string scan = vehicle_forward_scan( here, *tv.veh, ava, 12 );
        CAPTURE( scan );
        CHECK( contains( scan, "zombie 4 tiles, center, plus 1 more further on" ) );
    }
    SECTION( "the path is not called clear past where the view ends" ) {
        const tripoint_bub_ms wall( tv.front_x + 6, tv.lane_y, 0 );
        here.ter_set( wall, ter_id( "t_wall" ) );
        here.invalidate_map_cache( 0 );
        here.build_map_cache( 0, true );
        REQUIRE_FALSE( ava.sees( here, wall + tripoint( 2, 0, 0 ) ) );
        const std::string scan = vehicle_forward_scan( here, *tv.veh, ava, 12 );
        CAPTURE( scan );
        CHECK( contains( scan, "Can't see the whole path beyond 6 tiles" ) );
        CHECK_FALSE( contains( scan, "Path clear" ) );
    }
    SECTION( "ground changes along the lane are reported" ) {
        for( int x = tv.front_x + 5; x <= tv.front_x + 12; ++x ) {
            here.ter_set( tripoint_bub_ms( x, tv.lane_y, 0 ), ter_id( "t_grass" ) );
        }
        here.invalidate_map_cache( 0 );
        here.build_map_cache( 0, true );
        const std::string scan = vehicle_forward_scan( here, *tv.veh, ava, 12 );
        CAPTURE( scan );
        CHECK( contains( scan, "road for " ) );
        CHECK( contains( scan, "then flat ground" ) );
    }
    leave_vehicle();
}

TEST_CASE( "vehicle status summary and sidebar rows speak driving state",
           "[vehicle_accessibility][accessibility][vehicle]" )
{
    map &here = get_map();
    const test_vehicle tv = place_driven_motorcycle();
    avatar &ava = get_avatar();
    tv.veh->velocity = 1200;
    tv.veh->cruise_velocity = 2000;

    const std::string summary = vehicle_status_summary( here, *tv.veh, ava );
    CAPTURE( summary );
    CHECK( contains( summary, "Driving " ) );
    CHECK( contains( summary, "accelerating" ) );
    CHECK( contains( summary, "Facing 90 degrees, east" ) );
    CHECK( contains( summary, "Engines: " ) );
    CHECK( contains( summary, "tiles long" ) );
    CHECK( contains( summary, "On road" ) );

    const std::vector<status_row> speed = accessible_widget_rows( widget_vehicle_cruise_desc_label,
                                          ava, 36 );
    REQUIRE( speed.size() == 1 );
    CHECK( contains( speed[0].value, "accelerating" ) );
    CHECK_FALSE( contains( speed[0].value, "<" ) );
    const std::vector<status_row> heading = accessible_widget_rows(
            widget_vehicle_azimuth_desc_label, ava, 36 );
    REQUIRE( heading.size() == 1 );
    CHECK( heading[0].value == "90 degrees, east" );

    SECTION( "steering feedback reflects a real turn" ) {
        const driving_snapshot before = capture_driving_snapshot( *tv.veh );
        tv.veh->turn( 15_degrees );
        const driving_snapshot after = capture_driving_snapshot( *tv.veh );
        const std::string spoken = describe_driving_change( before, after );
        CHECK( contains( spoken, "Turning right to 105 degrees" ) );
    }
    leave_vehicle();
}
