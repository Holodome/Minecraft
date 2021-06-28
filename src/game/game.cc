#include "game/game.hh"

void game_init(Game *game) {
    logprintln("Game", "Init start");
    game->debug_state = (DebugState *)os_alloc(sizeof(DebugState));
    size_t debug_collate_arena_size = MEGABYTES(256);
    arena_init(&game->debug_state->collate_arena, os_alloc(debug_collate_arena_size), debug_collate_arena_size);
    debug_init(game->debug_state);
    game->is_running = true;   
    
    game->os.init();
    game->os.init_renderer_backend();
    f32 init_start = game->os.get_time();
    size_t renderer_arena_size = MEGABYTES(256);
    arena_init(&game->renderer.arena, os_alloc(renderer_arena_size), renderer_arena_size);
    renderer_init(&game->renderer);
    
    size_t assets_arena_size = MEGABYTES(256);
    arena_init(&game->assets.arena, os_alloc(assets_arena_size), assets_arena_size);
    // @TODO clean
    game->assets.renderer = &game->renderer;
    game->assets.init();
    
    size_t frame_arena_size = MEGABYTES(256);
    arena_init(&game->game_state.frame_arena, os_alloc(frame_arena_size), frame_arena_size);
    size_t world_arena_size = MEGABYTES(512);
    arena_init(&game->game_state.arena, os_alloc(world_arena_size), world_arena_size);
    game_state_init(&game->game_state);
    f32 init_end = game->os.get_time();
    
    size_t dev_ui_arena_size = MEGABYTES(8);
    arena_init(&game->dev_ui.arena, os_alloc(dev_ui_arena_size), dev_ui_arena_size);
    game->dev_ui.assets = &game->assets;
    dev_ui_init(&game->dev_ui, &game->assets);
    logprintln("Game", "Init took %llums", (u64)((init_end - init_start) * 1000));
}

static int records_sort(void *ctx, const void *a, const void *b) {
    DebugFrame *frame = (DebugFrame *)ctx;
    u32 index_a = *(u32 *)a;
    u32 index_b = *(u32 *)b;
    return frame->records[index_a].total_clocks < frame->records[index_b].total_clocks ? 1 : -1;
}

void game_cleanup(Game *game) {
    logprintln("Game", "Cleanup");
    os_free(game->game_state.frame_arena.data);
    os_free(game->game_state.arena.data);
    game->assets.cleanup();
    os_free(game->assets.arena.data);
    renderer_cleanup(&game->renderer);
    os_free(game->renderer.arena.data);
    game->os.cleanup();
}

void game_update_and_render(Game *game) {
    FRAME_MARKER();
    game->os.update_input(&game->input);
#define MIN_DT 0.001f
#define MAX_DT 0.1f
    game->input.dt = Math::clamp(game->input.dt, MIN_DT, MAX_DT);
    
    // Non-game related keybinds
    if (game->input.is_key_pressed(Key::Escape) || game->input.is_quit_requested) {
        game->is_running = false;
    }    
    if (game->input.is_key_pressed(Key::F1)) {
        game->dev_mode = DEV_MODE_NONE;
    }
    if (game->input.is_key_pressed(Key::F2)) {
        game->dev_mode = DEV_MODE_INFO;
    }
    if (game->input.is_key_pressed(Key::F3)) {
        game->dev_mode = DEV_MODE_PROFILER;
    }
    if (game->input.is_key_pressed(Key::F4)) {
        game->debug_state->is_paused = !game->debug_state->is_paused;
    } 
    if (game->input.is_key_pressed(Key::F5)) {
        game->dev_mode = DEV_MODE_MEMORY;
    }
    
    RendererCommands *commands = renderer_begin_frame(&game->renderer, game->input.winsize, Vec4(0.2));
    update_and_render(&game->game_state, &game->input, commands, &game->assets);
    RenderGroup interface_render_group = render_group_begin(commands, &game->assets,
        setup_2d(Mat4x4::ortographic_2d(0, game->input.winsize.x, game->input.winsize.y, 0)));
    
    BEGIN_BLOCK("DevUI");
    game->dev_ui.mouse_d = game->input.mdelta;
    game->dev_ui.mouse_p = game->input.mpos;
    game->dev_ui.is_mouse_pressed = game->input.is_key_held(Key::MouseLeft);
    DevUILayout dev_ui = dev_ui_begin(&game->dev_ui);
    if (game->dev_mode == DEV_MODE_INFO) {
        dev_ui_labelf(&dev_ui, "FPS: %.3f; DT: %ums; D: %llu; E: %llu; S: %llu", 1.0f / game->input.dt, (u32)(game->input.dt * 1000), 
            game->renderer.statistics.draw_call_count, game->game_state.world->entity_count,
            game->game_state.DEBUG_last_frame_sim_region_entity_count);
        Entity *player = get_world_entity(game->game_state.world, game->game_state.camera_followed_entity_id);
        Vec2 player_pos = DEBUG_world_pos_to_p(player->world_pos);
        dev_ui_labelf(&dev_ui, "P: (%.2f %.2f); O: (%.3f %.3f); Chunk: (%d %d)", 
            player_pos.x, player_pos.y,
            player->world_pos.offset.x, player->world_pos.offset.y,
            player->world_pos.chunk.x, player->world_pos.chunk.y);
        dev_ui_labelf(&dev_ui, "Chunks allocated: %llu", game->game_state.world->DEBUG_external_chunks_allocated);
        dev_ui_labelf(&dev_ui, "Wood: %u; Gold: %u", game->game_state.wood_count, game->game_state.gold_count);    
        dev_ui_labelf(&dev_ui, "Building mode: %s", game->game_state.is_in_building_mode ? "true" : "false");
        if (!is_same(game->game_state.interactable, null_id())) {
            SimEntity *interactable = &get_world_entity(game->game_state.world, game->game_state.interactable)->sim;
            if (interactable->world_object_flags & WORLD_OBJECT_FLAG_IS_BUILDING) {
                dev_ui_labelf(&dev_ui, "Building build progress: %.2f", interactable->build_progress);
            }
        }
        if (game->game_state.interaction_kind) {
            dev_ui_labelf(&dev_ui, "I: %u%%", (u32)(game->game_state.interaction_current_time / game->game_state.interaction_time * 100));    
        }
    } else if (game->dev_mode == DEV_MODE_PROFILER) {
        DebugFrame *frame = game->debug_state->frames + (game->debug_state->frame_index ? game->debug_state->frame_index - 1: DEBUG_MAX_FRAME_COUNT - 1);
        // DebugFrame *frame = game->debug_state->frames;
        f32 frame_time = (f32)(frame->end_clock - frame->begin_clock);
        u64 record_count = frame->records_count;
        TempMemory records_sort_temp = temp_memory_begin(&game->debug_state->collate_arena);
        u32 *records_sorted = alloc_arr(&game->debug_state->collate_arena, record_count, u32);
        for (size_t i = 0; i < record_count; ++i) {
            records_sorted[i] = i;
        }
        
        qsort_s(records_sorted, record_count, sizeof(*records_sorted), records_sort, frame);
        dev_ui_labelf(&dev_ui, "Collation: %.2f%%", (f32)frame->collation_clocks / frame_time * 100);    
        for (size_t i = 0; i < frame->records_count; ++i) {
            DebugRecord *record = frame->records + records_sorted[i];
            dev_ui_labelf(&dev_ui, "%2llu %32s %8llu %4u %8llu %.2f%%\n", i, record->name, record->total_clocks, 
                record->times_called, record->total_clocks / (u64)record->times_called, ((f32)record->total_clocks / frame_time * 100));
        }
        temp_memory_end(records_sort_temp);
    } else if (game->dev_mode == DEV_MODE_MEMORY) {
        dev_ui_labelf(&dev_ui, "Debug Arena: %llu/%llu (%.2f%%)", game->debug_state->collate_arena.data_size, game->debug_state->collate_arena.data_capacity,
            game->debug_state->collate_arena.data_size * 100.0f / game->debug_state->collate_arena.data_capacity);
        dev_ui_labelf(&dev_ui, "Renderer Arena: %llu/%llu (%.2f%%)", game->renderer.arena.data_size, game->renderer.arena.data_capacity,
            game->renderer.arena.data_size * 100.0f / game->renderer.arena.data_capacity);
        dev_ui_labelf(&dev_ui, "Assets Arena: %llu/%llu (%.2f%%)", game->assets.arena.data_size, game->assets.arena.data_capacity,
            game->assets.arena.data_size * 100.0f /  game->assets.arena.data_capacity);
        dev_ui_labelf(&dev_ui, "Frame Arena: %llu/%llu (%.2f%%)", game->game_state.frame_arena.data_size, game->game_state.frame_arena.data_capacity,
            game->game_state.frame_arena.data_size * 100.0f / game->game_state.frame_arena.data_capacity);
        dev_ui_labelf(&dev_ui, "Game Arena: %llu/%llu (%.2f%%)", game->game_state.arena.data_size, game->game_state.arena.data_capacity,
            game->game_state.arena.data_size * 100.0f / game->game_state.arena.data_capacity);
    }
    
    dev_ui_end(&dev_ui, &interface_render_group);
    END_BLOCK();
    renderer_end_frame(&game->renderer);
    game->os.update_window();
    debug_frame_end(game->debug_state);
}
