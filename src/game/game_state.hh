#if !defined(GAME_WORLD_HH)

#include "game/world.hh"

enum {
    PLAYER_INTERACTION_KIND_NONE,
    // Make this separate from destroy not to overcomplicate things trying to use less names
    PLAYER_INTERACTION_KIND_MINE_RESOURCE,
    PLAYER_INTERACTION_KIND_BUILD,
    PLAYER_INTERACTION_KIND_SENTINEL  
};

struct FrameData {
    Vec2 mouse_projection;
    SimEntity *camera_followed_entity;
    SimRegion *sim;
};  

// All game-related data is stored here. Like player resources, debug thigs etc.
struct GameState {
    MemoryArena arena;
    MemoryArena frame_arena;
    
    SimCamera cam;
    EntityID camera_followed_entity_id;
    // Generated world size
    Vec2i min_chunk;
    Vec2i max_chunk;
    
    World *world;
    u32 wood_count;
    u32 gold_count;
    
    EntityID interactable;
    u32 interaction_kind;
    f32 interaction_time;
    f32 interaction_current_time;
    
    bool is_in_building_mode;
    u8 selected_building;
    bool allow_camera_controls;
    
    size_t DEBUG_last_frame_sim_region_entity_count;
};

void game_state_init(GameState *game_state);
void update_and_render(GameState *game_state, Input *input, RendererCommands *commands, Assets *assets);

#define GAME_WORLD_HH 1
#endif