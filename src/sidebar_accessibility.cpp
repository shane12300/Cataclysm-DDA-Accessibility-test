#include "sidebar_accessibility.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <map>
#include <set>
#include <string_view>
#include <utility>

#include "avatar.h"
#include "bodypart.h"
#include "cata_utility.h"
#include "character.h"
#include "color.h"
#include "coordinates.h"
#include "creature.h"
#include "display.h"
#include "enum_conversions.h"
#include "enums.h"
#include "line.h"
#include "map.h"
#include "map_scale_constants.h"
#include "messages.h"
#include "mtype.h"
#include "npc.h"
#include "omdata.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "panels.h"
#include "point.h"
#include "string_formatter.h"
#include "translations.h"
#include "widget.h"

static const efftype_id effect_bandaged( "bandaged" );
static const efftype_id effect_bite( "bite" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_disinfected( "disinfected" );
static const efftype_id effect_infected( "infected" );

static const flag_id json_flag_SPLINT( "SPLINT" );

namespace
{

std::string join_strings( const std::vector<std::string> &parts, const std::string &sep )
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

std::string with_period( std::string text )
{
    text = trim( text );
    if( text.empty() ) {
        return text;
    }
    const char last = text.back();
    if( last != '.' && last != '!' && last != '?' ) {
        text += '.';
    }
    return text;
}

std::string lowercase_copy( std::string text )
{
    std::transform( text.begin(), text.end(), text.begin(), []( unsigned char c ) {
        return static_cast<char>( std::tolower( c ) );
    } );
    return text;
}

bool is_all_uppercase( const std::string &text )
{
    bool any_letter = false;
    for( const unsigned char c : text ) {
        if( std::isalpha( c ) ) {
            any_letter = true;
            if( std::islower( c ) ) {
                return false;
            }
        }
    }
    return any_letter;
}

// Colour of the first colour tag in a rendered widget string, or c_unset.
nc_color leading_color( const std::string &shown )
{
    const size_t pos = shown.find( "<color_" );
    if( pos == std::string::npos ) {
        return c_unset;
    }
    const color_tag_parse_result parsed = get_color_from_tag(
            std::string_view( shown ).substr( pos ), report_color_error::no );
    return parsed.type == color_tag_parse_result::open_color_tag ? parsed.color : c_unset;
}

// Rendered widget text with colour tags removed and newlines turned into pauses.
std::string plain_text( const std::string &shown )
{
    std::string text = remove_color_tags( shown );
    std::vector<std::string> lines;
    for( const std::string &line : string_split( text, '\n' ) ) {
        const std::string trimmed = trim( line );
        if( !trimmed.empty() ) {
            lines.push_back( trimmed );
        }
    }
    return join_strings( lines, ", " );
}

// Only the red colour family marks a value the glance should mention; yellow
// is spoken as "caution" in the browser but is not an alarm.
bool is_warning_band( const std::string &band )
{
    return band == _( "warning" );
}

// Low and critical are glance-worthy; "moderate" is only spoken in the browser.
bool is_alarming_percent_band( const std::string &band )
{
    return band == _( "critical" ) || band == _( "low" );
}

std::string strip_trailing_colon( std::string text )
{
    text = trim( text );
    while( !text.empty() && ( text.back() == ':' || text.back() == ' ' ) ) {
        text.pop_back();
    }
    return text;
}

const std::array<std::string, 9> &direction_words()
{
    static const std::array<std::string, 9> words = {{
            translate_marker( "North" ), translate_marker( "Northeast" ), translate_marker( "East" ),
            translate_marker( "Southeast" ), translate_marker( "South" ), translate_marker( "Southwest" ),
            translate_marker( "West" ), translate_marker( "Northwest" ), translate_marker( "Nearby" )
        }
    };
    return words;
}

std::string direction_word( int index )
{
    if( index < 0 || index >= static_cast<int>( direction_words().size() ) ) {
        return std::string();
    }
    return _( direction_words()[index] );
}

// What to say for a text widget whose clause text is blank. A sighted player
// sees an empty value next to the label; the blank means "nothing wrong".
std::string blank_word_for_label( const std::string &label )
{
    const std::string key = lowercase_copy( label );
    if( key == "hunger" ) {
        return _( "not hungry" );
    }
    if( key == "thirst" ) {
        return _( "not thirsty" );
    }
    if( key == "rest" || key == "sleepiness" ) {
        return _( "rested" );
    }
    if( key == "pain" ) {
        return _( "no pain" );
    }
    return _( "none" );
}

std::string npc_attitude_word( const npc &n )
{
    switch( n.get_attitude() ) {
        case NPCATT_KILL:
            return _( "hostile" );
        case NPCATT_FOLLOW:
            return _( "follower" );
        default:
            return _( "neutral" );
    }
}

// Names of creatures the compass shows in one direction (0-7) or nearby (8).
std::vector<std::string> compass_direction_names( const avatar &ava, int index )
{
    std::vector<std::string> names;
    if( index < 0 || index > 8 ) {
        return names;
    }
    const monster_visible_info &mv = ava.get_mon_visible();
    for( const npc *n : mv.unique_types[index] ) {
        if( n != nullptr ) {
            names.push_back( string_format( "%s, %s", n->get_name(), npc_attitude_word( *n ) ) );
        }
    }
    for( const std::pair<const mtype *, int> &m : mv.unique_mons[index] ) {
        if( m.first == nullptr ) {
            continue;
        }
        if( m.second > 1 ) {
            names.push_back( string_format( "%d %s", m.second, m.first->nname( m.second ) ) );
        } else {
            names.push_back( m.first->nname() );
        }
    }
    return names;
}

bool compass_direction_dangerous( const avatar &ava, int index )
{
    const monster_visible_info &mv = ava.get_mon_visible();
    return index >= 0 && index < 8 && mv.dangerous[index];
}

std::string monster_danger_word( const mtype &type )
{
    const int difficulty = type.get_total_difficulty();
    if( difficulty >= 30 ) {
        return _( "very dangerous" );
    }
    if( difficulty >= 16 ) {
        return _( "dangerous" );
    }
    if( difficulty >= 8 ) {
        return _( "threat" );
    }
    if( type.agro > 0 ) {
        return _( "aggressive" );
    }
    return std::string();
}

// Spoken form of the compass legend: every visible creature type with counts.
std::string compass_legend_summary( const avatar &ava, bool &abnormal )
{
    const monster_visible_info &mv = ava.get_mon_visible();
    std::vector<std::string> names;
    for( const std::vector<npc *> &npcs : mv.unique_types ) {
        for( const npc *n : npcs ) {
            if( n != nullptr ) {
                names.push_back( string_format( "%s, %s", n->get_name(), npc_attitude_word( *n ) ) );
            }
        }
    }
    std::map<const mtype *, int> counts;
    for( const auto &per_direction : mv.unique_mons ) {
        for( const std::pair<const mtype *, int> &m : per_direction ) {
            if( m.first != nullptr ) {
                counts[m.first] += m.second;
            }
        }
    }
    for( const auto &m : counts ) {
        std::string name = m.second > 1 ? string_format( "%d %s", m.second, m.first->nname( m.second ) ) :
                           m.first->nname();
        const std::string danger = monster_danger_word( *m.first );
        if( m.first->get_total_difficulty() >= 16 ) {
            abnormal = true;
        }
        if( !danger.empty() ) {
            name += ", " + danger;
        }
        names.push_back( name );
    }
    if( names.empty() ) {
        return _( "no creatures in view" );
    }
    return join_strings( names, "; " );
}

// Where the player is on the overmap and what the eight neighbouring overmap
// tiles are. This replaces the mini overmap glyph grid.
std::string overmap_context_summary( const avatar &ava )
{
    const tripoint_abs_omt center = ava.pos_abs_omt();
    std::string out = display::current_position_text( center );

    struct neighbour {
        int dx;
        int dy;
        int direction_index;
    };
    static const std::array<neighbour, 8> neighbours = {{
            { 0, -1, 0 }, { 1, -1, 1 }, { 1, 0, 2 }, { 1, 1, 3 },
            { 0, 1, 4 }, { -1, 1, 5 }, { -1, 0, 6 }, { -1, -1, 7 }
        }
    };
    std::vector<std::string> around;
    for( const neighbour &n : neighbours ) {
        const tripoint_abs_omt p( center.x() + n.dx, center.y() + n.dy, center.z() );
        const om_vision_level seen = overmap_buffer.seen( p );
        const std::string name = seen == om_vision_level::unseen ? std::string( _( "unknown" ) ) :
                                 overmap_buffer.ter( p )->get_name( seen );
        around.push_back( string_format( "%s %s", lowercase_copy( direction_word( n.direction_index ) ),
                                         name ) );
    }
    out += ". " + std::string( _( "Around you" ) ) + ": " + join_strings( around, ", " );

    const tripoint_abs_omt target = ava.get_active_mission_target();
    if( !target.is_invalid() && target != center ) {
        const direction dir = direction_from( center.xy().raw(), target.xy().raw() );
        out += string_format( _( ". Mission target %s, %d overmap tiles away" ),
                              direction_name( dir ), rl_dist( center.xy().raw(), target.xy().raw() ) );
    }
    return out;
}

std::string limb_label( const bodypart_id &bp )
{
    return uppercase_first_letter( body_part_name( bp ) );
}

// One row per main body part for the body graph widgets.
void body_graph_rows( const avatar &ava, bodygraph_var var, std::vector<status_row> &out )
{
    for( const bodypart_id &bp : ava.get_all_body_parts( get_body_part_flags::only_main ) ) {
        if( !ava.has_part( bp, body_part_filter::equivalent ) ) {
            continue;
        }
        status_row row;
        row.label = limb_label( bp );
        const nc_color color = display::get_bodygraph_bp_color( ava, bp, var );
        switch( var ) {
            case bodygraph_var::hp: {
                const int cur = ava.get_part_hp_cur( bp );
                const int max = std::max( 1, ava.get_part_hp_max( bp ) );
                row.value = string_format( _( "%d of %d" ), cur, max );
                row.band = band_for_percent( 100.0 * cur / max );
                row.abnormal = is_alarming_percent_band( row.band );
                const std::string status = describe_bodypart_status( ava, bp );
                if( !status.empty() ) {
                    row.band = join_strings( { row.band, status }, ", " );
                    row.abnormal = true;
                }
                break;
            }
            case bodygraph_var::temp:
                row.value = remove_color_tags( display::temp_text_color( ava, bp.id() ).first );
                row.band = severity_word_for_color( color );
                row.abnormal = is_warning_band( row.band );
                break;
            case bodygraph_var::encumb:
                row.value = string_format( _( "encumbrance %d" ), ava.get_part_encumbrance( bp ) );
                row.band = severity_word_for_color( color );
                row.abnormal = is_warning_band( row.band );
                break;
            case bodygraph_var::status: {
                const std::string status = describe_bodypart_status( ava, bp );
                row.value = status.empty() ? std::string( _( "no wounds" ) ) : status;
                row.abnormal = !status.empty();
                break;
            }
            case bodygraph_var::wet:
                if( color == c_light_gray ) {
                    row.value = _( "dry" );
                } else if( color == c_light_cyan ) {
                    row.value = _( "damp" );
                } else if( color == c_light_blue ) {
                    row.value = _( "wet" );
                } else {
                    row.value = _( "soaked" );
                    row.abnormal = true;
                }
                break;
            case bodygraph_var::last:
                continue;
        }
        out.push_back( row );
    }
}

// A leaf widget whose only job is to show a label (or a coloured limb name)
// for the value widget beside it.
bool is_label_carrier( const widget &w )
{
    return w._var == widget_var::last && w._clauses.empty() && w._bps.empty();
}

bool is_bodypart_status_text( const widget &w )
{
    return w._var == widget_var::last && !w._bps.empty();
}

void leaf_rows( widget &w, const widget_id &wid, const avatar &ava, int width,
                std::vector<status_row> &out )
{
    w.set_default_var_range( ava );
    const bool label_none = w.has_flag( "W_LABEL_NONE" ) || w._label.empty();
    const std::string label = label_none ? std::string() :
                              expand_sidebar_label( w._label.translated(), wid );
    const bool disabled_when_empty = w.has_flag( "W_DISABLED_WHEN_EMPTY" );

    // Compass: both the "N:" label widget and the symbol widget are keyed by
    // direction. Each becomes the same spoken row; duplicates are removed later.
    if( w._direction != cardinal_direction::num_cardinal_directions ) {
        const int index = static_cast<int>( w._direction );
        status_row row;
        row.label = direction_word( index );
        const std::vector<std::string> names = compass_direction_names( ava, index );
        row.value = names.empty() ? std::string( _( "clear" ) ) : join_strings( names, ", " );
        if( compass_direction_dangerous( ava, index ) ) {
            row.band = _( "hostile sees you" );
            row.abnormal = true;
        }
        out.push_back( row );
        return;
    }

    switch( w._var ) {
        case widget_var::compass_legend_text: {
            status_row row;
            row.label = label.empty() ? std::string( _( "Creatures" ) ) : label;
            row.value = compass_legend_summary( ava, row.abnormal );
            out.push_back( row );
            return;
        }
        case widget_var::body_graph:
            body_graph_rows( ava, bodygraph_var::hp, out );
            return;
        case widget_var::body_graph_temp:
            body_graph_rows( ava, bodygraph_var::temp, out );
            return;
        case widget_var::body_graph_encumb:
            body_graph_rows( ava, bodygraph_var::encumb, out );
            return;
        case widget_var::body_graph_status:
            body_graph_rows( ava, bodygraph_var::status, out );
            return;
        case widget_var::body_graph_wet:
            body_graph_rows( ava, bodygraph_var::wet, out );
            return;
        case widget_var::overmap_text: {
            status_row row;
            row.label = label.empty() ? std::string( _( "Overmap" ) ) : label;
            row.value = overmap_context_summary( ava );
            out.push_back( row );
            return;
        }
        case widget_var::sundial_text:
        case widget_var::sundial_time_text: {
            status_row row;
            row.label = label.empty() ? std::string( _( "Time" ) ) : label;
            row.value = display::time_string( ava );
            out.push_back( row );
            return;
        }
        case widget_var::mood_text: {
            // The face glyph means nothing spoken; give the number it encodes.
            status_row row;
            row.label = label.empty() ? std::string( _( "Mood" ) ) : label;
            row.value = string_format( _( "morale %d" ), ava.get_morale_level() );
            row.band = severity_word_for_color( display::morale_face_color( ava ).second );
            row.abnormal = is_warning_band( row.band );
            out.push_back( row );
            return;
        }
        case widget_var::safe_mode_text:
        case widget_var::safe_mode_classic_text: {
            status_row row;
            row.label = label.empty() ? std::string( _( "Safe mode" ) ) : label;
            const std::pair<std::string, nc_color> safe = display::safe_mode_text_color(
                        w._var == widget_var::safe_mode_classic_text );
            row.value = safe.first.empty() ? std::string( _( "Off" ) ) : remove_color_tags( safe.first );
            if( ava.get_mon_visible().has_dangerous_creature_in_proximity ) {
                row.band = _( "hostile in view" );
                row.abnormal = true;
            }
            out.push_back( row );
            return;
        }
        case widget_var::carry_weight_text:
        case widget_var::carry_weight_value:
        case widget_var::carry_weight: {
            status_row row;
            row.label = label.empty() ? std::string( _( "Weight carried" ) ) : label;
            const std::string shown = w.show( ava, width );
            const nc_color color = leading_color( shown );
            row.value = plain_text( shown );
            if( color == c_white_red || color == c_red ) {
                row.band = _( "overloaded" );
                row.abnormal = true;
            } else if( color == c_light_red ) {
                row.band = _( "heavy" );
                row.abnormal = true;
            } else if( color == c_yellow ) {
                row.band = _( "moderate" );
            }
            out.push_back( row );
            return;
        }
        default:
            break;
    }

    if( is_bodypart_status_text( w ) ) {
        // Coloured limb name whose colour encodes bleeding, bites and infection.
        status_row row;
        row.label_only = true;
        row.value = limb_label( w.only_bp() );
        const std::string status = describe_bodypart_status( ava, w.only_bp() );
        row.band = status.empty() ? std::string( _( "no wounds" ) ) : status;
        row.abnormal = !status.empty();
        out.push_back( row );
        return;
    }

    if( is_label_carrier( w ) ) {
        const std::string text = strip_trailing_colon( plain_text( w.show( ava, width ) ) );
        if( text.empty() ) {
            return;
        }
        status_row row;
        row.label_only = true;
        row.value = expand_sidebar_label( text, wid );
        out.push_back( row );
        return;
    }

    if( w._style == "graph" ) {
        status_row row;
        row.label = label;
        const int value = w.get_var_value( ava );
        const bool range_defined = INT_MIN < w._var_min && w._var_max < INT_MAX &&
                                   w._var_max > w._var_min;
        if( w._var == widget_var::bp_hp ) {
            if( row.label.empty() ) {
                row.label = limb_label( w.only_bp() );
            }
            const int max = std::max( 1, w._var_max );
            row.value = string_format( _( "%d of %d" ), value, max );
            row.band = band_for_percent( 100.0 * value / max );
            row.abnormal = is_alarming_percent_band( row.band );
            const std::string status = describe_bodypart_status( ava, w.only_bp() );
            if( !status.empty() ) {
                row.band = join_strings( { row.band, status }, ", " );
                row.abnormal = true;
            }
        } else if( range_defined ) {
            const double percent = 100.0 * ( value - w._var_min ) / ( w._var_max - w._var_min );
            row.value = string_format( _( "%d percent" ), static_cast<int>( percent ) );
            row.band = band_for_percent( percent );
            row.abnormal = is_alarming_percent_band( row.band );
        } else {
            row.value = string_format( "%d", value );
        }
        if( row.label.empty() ) {
            row.label = expand_sidebar_label( io::enum_to_string<widget_var>( w._var ), wid );
        }
        out.push_back( row );
        return;
    }

    if( w._style == "number" && !w.uses_text_function() && w._var != widget_var::last ) {
        status_row row;
        row.label = label;
        const int value = w.get_var_value( ava );
        row.value = plain_text( w.number( value, !w._clauses.empty() ) );
        if( w._var == widget_var::bp_hp ) {
            if( row.label.empty() ) {
                row.label = limb_label( w.only_bp() );
            }
            const int max = std::max( 1, w._var_max );
            row.value = string_format( _( "%d of %d" ), value, max );
            row.band = band_for_percent( 100.0 * value / max );
            row.abnormal = is_alarming_percent_band( row.band );
            const std::string status = describe_bodypart_status( ava, w.only_bp() );
            if( !status.empty() ) {
                row.band = join_strings( { row.band, status }, ", " );
                row.abnormal = true;
            }
        } else {
            row.band = band_for_numeric( w, value );
            row.abnormal = !row.band.empty() && !( row.band == _( "caution" ) );
        }
        if( row.label.empty() ) {
            row.label = expand_sidebar_label( io::enum_to_string<widget_var>( w._var ), wid );
        }
        out.push_back( row );
        return;
    }

    // Everything else: speak what the sidebar prints, minus colour, plus a
    // band word for the colour it was printed in.
    const std::string shown = w.show( ava, width );
    status_row row;
    row.label = label;
    row.value = plain_text( shown );
    if( row.value.empty() ) {
        if( disabled_when_empty ) {
            return;
        }
        row.value = blank_word_for_label( row.label );
    } else {
        row.band = severity_word_for_color( leading_color( shown ) );
        row.abnormal = is_warning_band( row.band );
    }
    if( row.label.empty() && w._var != widget_var::last ) {
        row.label = expand_sidebar_label( io::enum_to_string<widget_var>( w._var ), wid );
    }
    out.push_back( row );
}

// Attach label-only rows to the value row beside them; drop exact repeats.
void merge_label_carriers( std::vector<status_row> &rows )
{
    std::vector<status_row> merged;
    for( size_t i = 0; i < rows.size(); ++i ) {
        status_row &row = rows[i];
        if( !row.label_only ) {
            merged.push_back( row );
            continue;
        }
        if( i + 1 < rows.size() && !rows[i + 1].label_only ) {
            status_row &next = rows[i + 1];
            if( next.label.empty() || next.label == row.value ) {
                next.label = row.value;
                if( row.abnormal && next.band.find( row.band ) == std::string::npos ) {
                    next.band = join_strings( { next.band, row.band }, ", " );
                    next.abnormal = true;
                }
                continue;
            }
        }
        // Nothing to attach to: speak it on its own if it carries a status.
        if( !row.band.empty() ) {
            status_row standalone;
            standalone.label = row.value;
            standalone.value = row.band;
            standalone.abnormal = row.abnormal;
            merged.push_back( standalone );
        }
    }
    rows.clear();
    for( const status_row &row : merged ) {
        if( !rows.empty() && rows.back().label == row.label && rows.back().value == row.value ) {
            if( rows.back().band.empty() ) {
                rows.back().band = row.band;
                rows.back().abnormal = rows.back().abnormal || row.abnormal;
            }
            continue;
        }
        rows.push_back( row );
    }
}

void collect_rows( const widget_id &wid, const avatar &ava, int width,
                   std::vector<status_row> &out )
{
    if( wid.is_null() || !wid.is_valid() ) {
        return;
    }
    widget w = wid.obj();
    if( w._style == "layout" || w._style == "sidebar" ) {
        std::vector<status_row> rows;
        for( const widget_id &child : w.widgets( !w._clauses.empty() ) ) {
            collect_rows( child, ava, width, rows );
        }
        merge_label_carriers( rows );
        out.insert( out.end(), rows.begin(), rows.end() );
        return;
    }
    leaf_rows( w, wid, ava, width, out );
}

} // namespace

std::string status_row::spoken() const
{
    std::string out = label.empty() ? value : label + ": " + value;
    if( !band.empty() ) {
        out += ", " + band;
    }
    return out;
}

std::string band_for_percent( double percent )
{
    if( percent < 12.5 ) {
        return _( "critical" );
    }
    if( percent < 37.5 ) {
        return _( "low" );
    }
    if( percent < 62.5 ) {
        return _( "moderate" );
    }
    return std::string();
}

std::string severity_word_for_color( const nc_color &color )
{
    if( color == c_red || color == c_light_red || color == c_white_red || color == c_red_red ||
        color == c_magenta || color == c_magenta_red || color == c_pink ) {
        return _( "warning" );
    }
    if( color == c_yellow ) {
        return _( "caution" );
    }
    return std::string();
}

std::string band_for_numeric( widget &w, int value )
{
    const bool normal_defined = INT_MIN < w._var_norm.first && w._var_norm.second < INT_MAX;
    if( normal_defined ) {
        if( value < w._var_norm.first ) {
            return _( "below normal" );
        }
        if( value > w._var_norm.second ) {
            return _( "above normal" );
        }
        return std::string();
    }
    return severity_word_for_color( w.value_color( value ) );
}

std::string expand_sidebar_label( const std::string &label, const widget_id &wid )
{
    static const std::map<std::string, std::string> expansions = {
        { "stam", translate_marker( "Stamina" ) },
        { "str", translate_marker( "Strength" ) },
        { "dex", translate_marker( "Dexterity" ) },
        { "int", translate_marker( "Intelligence" ) },
        { "per", translate_marker( "Perception" ) },
        { "head", translate_marker( "Head" ) },
        { "torso", translate_marker( "Torso" ) },
        { "l arm", translate_marker( "Left arm" ) },
        { "r arm", translate_marker( "Right arm" ) },
        { "l leg", translate_marker( "Left leg" ) },
        { "r leg", translate_marker( "Right leg" ) },
        { "safe", translate_marker( "Safe mode" ) },
        { "power", translate_marker( "Bionic power" ) },
        { "pwr", translate_marker( "Bionic power" ) },
        { "heat", translate_marker( "Body heat" ) },
        { "temp", translate_marker( "Temperature" ) },
        { "move", translate_marker( "Move counter" ) },
        { "wt", translate_marker( "Weight" ) },
        { "n", translate_marker( "North" ) },
        { "ne", translate_marker( "Northeast" ) },
        { "e", translate_marker( "East" ) },
        { "se", translate_marker( "Southeast" ) },
        { "s", translate_marker( "South" ) },
        { "sw", translate_marker( "Southwest" ) },
        { "w", translate_marker( "West" ) },
        { "nw", translate_marker( "Northwest" ) },
        // widget_var names used when a widget has no label of its own
        { "bp hp", translate_marker( "Hit points" ) },
        { "stamina", translate_marker( "Stamina" ) },
        { "power percentage", translate_marker( "Bionic power" ) },
        { "weary transition level", translate_marker( "Weariness transition" ) },
        { "move remainder", translate_marker( "Moves left" ) },
        { "move cost", translate_marker( "Move cost" ) },
        { "morale level", translate_marker( "Morale" ) },
        { "weariness level", translate_marker( "Weariness" ) },
    };

    std::string text = strip_trailing_colon( remove_color_tags( label ) );
    std::replace( text.begin(), text.end(), '_', ' ' );
    const std::string key = lowercase_copy( text );
    const auto it = expansions.find( key );
    if( it != expansions.end() ) {
        return _( it->second );
    }
    if( text.empty() && !wid.is_null() ) {
        text = wid.str();
        std::replace( text.begin(), text.end(), '_', ' ' );
    }
    // Remove widget-variable suffixes such as "weather text".
    for( const std::string suffix : { " text", " desc", " num", " label" } ) {
        if( text.size() > suffix.size() &&
            text.compare( text.size() - suffix.size(), suffix.size(), suffix ) == 0 ) {
            text.erase( text.size() - suffix.size() );
        }
    }
    if( is_all_uppercase( text ) ) {
        text = lowercase_copy( text );
    }
    return uppercase_first_letter( text );
}

std::string describe_bodypart_status( const Character &u, const bodypart_id &bp )
{
    std::vector<std::string> parts;
    const int bleed = u.get_effect_int( effect_bleed, bp );
    if( bleed >= 21 ) {
        parts.emplace_back( _( "bleeding heavily" ) );
    } else if( bleed >= 11 ) {
        parts.emplace_back( _( "bleeding" ) );
    } else if( bleed > 0 ) {
        parts.emplace_back( _( "bleeding lightly" ) );
    }
    if( u.has_effect( effect_bite, bp ) ) {
        parts.emplace_back( _( "bitten" ) );
    }
    if( u.has_effect( effect_infected, bp ) ) {
        parts.emplace_back( _( "infected" ) );
    }
    if( u.is_limb_broken( bp ) ) {
        parts.emplace_back( _( "broken" ) );
    }
    if( u.worn_with_flag( json_flag_SPLINT, bp ) ) {
        parts.emplace_back( _( "splinted" ) );
    }
    if( u.has_effect( effect_bandaged, bp ) ) {
        parts.emplace_back( _( "bandaged" ) );
    }
    if( u.has_effect( effect_disinfected, bp ) ) {
        parts.emplace_back( _( "disinfected" ) );
    }
    return join_strings( parts, ", " );
}

std::vector<status_row> accessible_widget_rows( const widget_id &wid, const avatar &ava,
        int width )
{
    std::vector<status_row> rows;
    collect_rows( wid, ava, std::max( width, 16 ), rows );
    // A top-level carrier with nothing beside it is dropped unless it has a status.
    merge_label_carriers( rows );
    return rows;
}

std::vector<status_section> build_status_sections( const avatar &ava )
{
    std::vector<status_section> sections;
    panel_manager &manager = panel_manager::get_manager();
    for( const window_panel &panel : manager.get_current_layout().panels() ) {
        if( !panel.toggle ) {
            continue;
        }
        if( panel.render && !panel.render() ) {
            continue;
        }
        status_section section;
        section.id = panel.get_id();
        section.name = remove_color_tags( panel.get_name() );
        if( section.id == "Map" ) {
            // The minimap is a picture; the Place section already speaks the overmap.
            continue;
        }
        if( section.id == "Log" ) {
            for( const std::pair<std::string, std::string> &message : Messages::recent_messages( 10 ) ) {
                status_row row;
                row.value = remove_color_tags( message.second );
                section.rows.push_back( row );
            }
            if( section.rows.empty() ) {
                status_row row;
                row.value = _( "No recent game messages" );
                section.rows.push_back( row );
            }
        } else if( !panel.get_widget().is_null() ) {
            section.rows = accessible_widget_rows( panel.get_widget(), ava, panel.get_width() );
        } else {
            continue;
        }
        if( section.rows.empty() ) {
            status_row row;
            row.value = _( "Nothing to report" );
            section.rows.push_back( row );
        }
        sections.push_back( section );
    }
    return sections;
}

std::string speak_status_section( const status_section &section )
{
    std::vector<std::string> parts;
    parts.push_back( with_period( section.name ) );
    for( const status_row &row : section.rows ) {
        parts.push_back( with_period( row.spoken() ) );
    }
    return join_strings( parts, " " );
}

int count_hostiles_seeing_player( const avatar &ava )
{
    const map &here = get_map();
    int count = 0;
    for( const Creature *critter : ava.get_visible_creatures( MAX_VIEW_DISTANCE ) ) {
        if( critter != nullptr && critter->attitude_to( ava ) == Creature::Attitude::HOSTILE &&
            critter->sees( here, ava ) ) {
            ++count;
        }
    }
    return count;
}

std::string describe_visible_threats( const avatar &ava )
{
    const monster_visible_info &mv = ava.get_mon_visible();
    std::vector<std::string> parts;
    for( int index = 0; index <= 8; ++index ) {
        const std::vector<std::string> names = compass_direction_names( ava, index );
        if( names.empty() ) {
            continue;
        }
        std::string part = join_strings( names, ", " ) + " " + lowercase_copy( direction_word( index ) );
        if( compass_direction_dangerous( ava, index ) ) {
            part += std::string( ", " ) + _( "hostile sees you" );
        }
        parts.push_back( part );
    }
    std::string out;
    if( mv.has_dangerous_creature_in_proximity ) {
        out += std::string( _( "Danger close." ) ) + " ";
    }
    if( parts.empty() ) {
        out += _( "No creatures in view." );
        return out;
    }
    out += std::string( _( "Creatures" ) ) + ": " + join_strings( parts, "; " ) + ".";
    // The sidebar compass only flags line of sight for creatures past the
    // edge of the map window; a sighted player sees the on-screen ones
    // directly. Check every visible hostile ourselves instead.
    const int hostiles_seeing = count_hostiles_seeing_player( ava );
    if( hostiles_seeing == 1 ) {
        out += std::string( " " ) + _( "One hostile can see you." );
    } else if( hostiles_seeing > 1 ) {
        out += " " + string_format( _( "%d hostiles can see you." ), hostiles_seeing );
    } else {
        out += std::string( " " ) + _( "No hostile can see you." );
    }
    return out;
}

std::string build_status_glance( const avatar &ava )
{
    std::vector<std::string> parts;
    parts.push_back( describe_visible_threats( ava ) );

    std::set<std::string> spoken_labels;
    for( const status_section &section : build_status_sections( ava ) ) {
        if( section.id == "Log" ) {
            continue;
        }
        for( const status_row &row : section.rows ) {
            if( !row.abnormal ) {
                continue;
            }
            if( !row.label.empty() && !spoken_labels.insert( row.label ).second ) {
                continue;
            }
            parts.push_back( with_period( row.spoken() ) );
        }
    }

    // Wounds matter even when the active layout hides the limb widgets.
    for( const bodypart_id &bp : ava.get_all_body_parts( get_body_part_flags::only_main ) ) {
        const std::string status = describe_bodypart_status( ava, bp );
        if( status.empty() ) {
            continue;
        }
        const std::string label = limb_label( bp );
        if( spoken_labels.count( label ) > 0 ) {
            continue;
        }
        spoken_labels.insert( label );
        parts.push_back( with_period( label + " " + status ) );
    }

    if( parts.size() == 1 ) {
        parts.emplace_back( _( "All sidebar values normal." ) );
    }
    return std::string( _( "Status glance." ) ) + " " + join_strings( parts, " " );
}
