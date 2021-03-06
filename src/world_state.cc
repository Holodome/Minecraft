#include "world_state.hh"


inline WorldObjectSpec get_spec_for_type(WorldState *world_state, u32 type) {
    assert(type < WORLD_OBJECT_KIND_SENTINEL);
    return world_state->world_object_specs[type];
}

static EntityID add_player(SimRegion *sim, vec2 pos) {
    Entity *entity = create_new_entity(sim, pos);
    entity->kind = ENTITY_KIND_PLAYER;
    entity->flags = ENTITY_FLAG_IS_ANCHOR;
    return entity->id;
}

static EntityID add_tree(SimRegion *sim, vec2 pos, u32 kind) {
    pos.x = Floor(pos.x) + 0.5f;
    pos.y = Floor(pos.y) + 0.5f;
    Entity *entity = create_new_entity(sim, pos);
    entity->kind = ENTITY_KIND_WORLD_OBJECT;
    entity->flags = ENTITY_FLAG_HAS_WORLD_PLACEMENT;
    entity->world_object_kind = kind;
    entity->resource_interactions_left = 1;
    return entity->id;
}

static EntityID add_pawn(SimRegion *sim, vec2 pos) {
    Entity *entity = create_new_entity(sim, pos);
    entity->kind = ENTITY_KIND_PAWN;
    return entity->id;
}

inline WorldObjectSpec tree_spec(u32 resource_gain) {
    WorldObjectSpec spec = {};
    spec.type = WORLD_OBJECT_TYPE_RESOURCE;
    spec.resource_kind = RESOURCE_KIND_WOOD;
    spec.default_resource_interactions = resource_gain;
    spec.resource_gain = 10;
    return spec;
}

inline WorldObjectSpec building_spec() {
    WorldObjectSpec spec = {};
    spec.type = WORLD_OBJECT_TYPE_BUILDING;
    return spec;
}

void world_state_init(WorldState *world_state, MemoryArena *arena, MemoryArena *frame_arena) {    
    world_state->arena = arena;
    world_state->frame_arena = frame_arena;
    // @TODO initialize world properly
    world_state->world = alloc_struct(arena, World);
    world_state->world->arena = arena;
    world_state->world->max_entity_id = 1;
    // Set spec settings
    world_state->world_object_specs[WORLD_OBJECT_KIND_TREE_FOREST] = tree_spec(4);
    world_state->world_object_specs[WORLD_OBJECT_KIND_TREE_JUNGLE] = tree_spec(2);
    world_state->world_object_specs[WORLD_OBJECT_KIND_TREE_DESERT] = tree_spec(1);
    world_state->world_object_specs[WORLD_OBJECT_KIND_BUILDING1] = building_spec();
    world_state->world_object_specs[WORLD_OBJECT_KIND_BUILDING2] = building_spec();
    init_order_system(&world_state->order_system, world_state->arena);
    init_particle_system(&world_state->particle_system, world_state->arena);
    world_state->particle_system.emitter.spec.p = Vec3(0);
    world_state->particle_system.emitter.spec.spawn_rate = 10;
    
    // Generate world
    Entropy gen_entropy { 123456789 };
    SimRegion *creation_sim = alloc_struct(frame_arena, SimRegion);
    begin_sim(creation_sim, frame_arena, world_state->world, 100, 100, 25);
    vec2 player_pos = Vec2(0);
    world_state->camera_followed_entity = add_player(creation_sim, player_pos);    
    world_state->pawns[world_state->pawn_count++] = add_pawn(creation_sim, Vec2(5, 5));
    world_state->pawns[world_state->pawn_count++] = add_pawn(creation_sim, Vec2(-5, 5));
    world_state->pawns[world_state->pawn_count++] = add_pawn(creation_sim, Vec2(5, -5));
    world_state->pawns[world_state->pawn_count++] = add_pawn(creation_sim, Vec2(-5, -5));
    world_state->pawns[world_state->pawn_count++] = add_pawn(creation_sim, Vec2(15, 15));
    world_state->pawns[world_state->pawn_count++] = add_pawn(creation_sim, Vec2(-15, 15));
    world_state->pawns[world_state->pawn_count++] = add_pawn(creation_sim, Vec2(15, -15));
    world_state->pawns[world_state->pawn_count++] = add_pawn(creation_sim, Vec2(-15, -15));
    for (u32 i = 0; i < 1000; ++i) {
        for (;;) {
            f32 x = random_bilateral(&gen_entropy) * CHUNK_SIZE * 15;
            f32 y = random_bilateral(&gen_entropy) * CHUNK_SIZE * 15;
            if (!is_cell_occupied(creation_sim, Floor(x), Floor(y))) {
                add_tree(creation_sim, Vec2(x, y), WORLD_OBJECT_KIND_TREE_FOREST);
                break;
            }
        }
    }
    end_sim(creation_sim, world_state);
}

static vec3 uv_to_world(Mat4x4 projection, Mat4x4 view, vec2 uv) {
    f32 x = uv.x;
    f32 y = uv.y;
    vec3 ray_dc = Vec3(x, y, 1.0f);
    vec4 ray_clip = Vec4(ray_dc.xy, -1.0f, 1.0f);
    vec4 ray_eye = Mat4x4::inverse(projection) * ray_clip;
    ray_eye.z = -1.0f;
    ray_eye.w = 0.0f;
    vec3 ray_world = normalize((Mat4x4::inverse(view) * ray_eye).xyz);
    return ray_world;
}

static void init_particles_for_interaction(WorldState *world_state, SimRegion *sim, InputManager *input, Interaction *interaction) {
    if (interaction->kind == INTERACTION_KIND_MINE_RESOURCE) {
        Entity *interactable = get_entity_by_id(sim, interaction->entity);
        assert(interactable);
        // interaction->particle_emitter = add_particle_emitter(&world_state->particle_system, PARTICLE_EMITTER_KIND_FOUNTAIN, vec4(1, 0, 0, 1));
    } else {
        NOT_IMPLEMENTED;
    }
}

static void update_interaction(WorldState *world_state, SimRegion *sim, Entity *entity, InputManager *input) {
    assert(IS_NOT_NULL(entity->order));
    Order *order = get_order_by_id(&world_state->order_system, entity->order);
    assert(order);
    
    if (entity->interaction.kind) {
        entity->interaction.current_time += input->platform->frame_dt;
        if (entity->interaction.current_time > entity->interaction.time) {
            Entity *interactable = get_entity_by_id(sim, entity->interaction.entity);
            assert(interactable);
            
            if (entity->interaction.kind == INTERACTION_KIND_MINE_RESOURCE) {
                assert(interactable->kind == ENTITY_KIND_WORLD_OBJECT);
                assert(interactable->resource_interactions_left > 0);
                interactable->resource_interactions_left -= 1;
                WorldObjectSpec interactable_spec = get_spec_for_type(world_state, interactable->world_object_kind);
                assert(interactable_spec.type == WORLD_OBJECT_TYPE_RESOURCE);
                if (interactable_spec.resource_kind == RESOURCE_KIND_WOOD) {
                    world_state->wood_count += interactable_spec.resource_gain;
                } else {
                    NOT_IMPLEMENTED;
                } 
                
                if (interactable->resource_interactions_left == 0){
                    interactable->flags |= ENTITY_FLAG_IS_DELETED;
                    disband_order(&world_state->order_system, entity->order);
                    entity->order = {};
                    // delete_particle_emitter(&world_state->particle_system, entity->interaction.particle_emitter);
                    entity->interaction = {};
                }
            } else {
                NOT_IMPLEMENTED;
            }
        }
    } else {
        EntityID order_entity_id = order->destination_id;
        Entity *dest_entity = get_entity_by_id(sim, order_entity_id);
        assert(dest_entity->kind == ENTITY_KIND_WORLD_OBJECT);
        assert(length_sq(dest_entity->p - entity->p) < DISTANCE_TO_INTERACT_SQ);
        WorldObjectSpec dest_spec = get_spec_for_type(world_state, dest_entity->world_object_kind);
        // Get ineraction settings from order and whatever
        u32 interaction_kind = 0;
        f32 interaction_time = 0.0f;
        bool cancel_interaction = false;
        if (order->kind == ORDER_CHOP) {
            if (dest_spec.type == WORLD_OBJECT_TYPE_RESOURCE) {
                interaction_kind = INTERACTION_KIND_MINE_RESOURCE;
                assert(dest_entity->resource_interactions_left > 0);
                if (dest_spec.resource_kind == RESOURCE_KIND_WOOD) {
                    interaction_time = 1.0f;
                } else {
                    NOT_IMPLEMENTED;
                }
            } else {
                NOT_IMPLEMENTED;
            }
        } else {
            NOT_IMPLEMENTED;
        }
        
        if (!cancel_interaction) {
            assert(interaction_kind);
            entity->interaction.kind = interaction_kind;
            entity->interaction.entity = order_entity_id;
            entity->interaction.time = interaction_time;
            entity->interaction.current_time = 0.0f;
            init_particles_for_interaction(world_state, sim, input, &entity->interaction);
        }
    }
}

void update_game(WorldState *world_state, SimRegion *sim, InputManager *input) {
    TIMED_FUNCTION();
    if (is_key_held(input, KEY_Z)) {
        f32 x_view_coef = 1.0f * input->platform->frame_dt;
        f32 y_view_coef = 0.6f * input->platform->frame_dt;
        f32 x_angle_change = input->platform->mdelta.x * x_view_coef;
        f32 y_angle_change = input->platform->mdelta.y * y_view_coef;
        world_state->cam.yaw += x_angle_change;
        world_state->cam.pitch += y_angle_change;
#define MIN_CAM_PITCH (HALF_PI * 0.1f)
#define MAX_CAM_PITCH (HALF_PI * 0.9f)
        f32 delta_zoom = input->platform->mwheel;
        world_state->cam.distance_from_player -= delta_zoom;
    }
    world_state->cam.yaw = unwind_rad(world_state->cam.yaw);
    world_state->cam.pitch = Clamp(world_state->cam.pitch, MIN_CAM_PITCH, MAX_CAM_PITCH);
    world_state->cam.distance_from_player = Clamp(world_state->cam.distance_from_player, 0.5f, 1000);
    
    Entity *camera_controlled_entity = get_entity_by_id(sim, world_state->camera_followed_entity);
    assert(camera_controlled_entity);
    // Calculate player movement
    vec2 player_delta = Vec2(0);
    f32 move_coef = 16.0f * input->platform->frame_dt;
    f32 z_speed = 0;
    if (is_key_held(input, KEY_W)) {
        z_speed = move_coef;
    } else if (is_key_held(input, KEY_S)) {
        z_speed = -move_coef;
    }
    player_delta.x += z_speed *  Sin(world_state->cam.yaw);
    player_delta.y += z_speed * -Cos(world_state->cam.yaw);
    
    f32 x_speed = 0;
    if (is_key_held(input, KEY_D)) {
        x_speed = move_coef;
    } else if (is_key_held(input, KEY_A)) {
        x_speed = -move_coef;
    }
    player_delta.x += x_speed * Cos(world_state->cam.yaw);
    player_delta.y += x_speed * Sin(world_state->cam.yaw);     
    vec2 new_p = camera_controlled_entity->p + player_delta;
    change_entity_position(sim, camera_controlled_entity, new_p);
    {DEBUG_VALUE_BLOCK("Player")
            DEBUG_VALUE(camera_controlled_entity->p, "Position");
        DEBUG_VALUE(sim->center_chunk_x, "Center chunk x");
        DEBUG_VALUE(sim->center_chunk_y, "Center chunk y");
    }
    
    vec3 center_pos = xz(camera_controlled_entity->p);
    f32 horiz_distance = world_state->cam.distance_from_player * Cos(world_state->cam.pitch);
    f32 vert_distance = world_state->cam.distance_from_player * Sin(world_state->cam.pitch);
    f32 offsetx = horiz_distance * Sin(-world_state->cam.yaw);
    f32 offsetz = horiz_distance * Cos(-world_state->cam.yaw);
    vec3 cam_p;
    cam_p.x = offsetx + center_pos.x;
    cam_p.z = offsetz + center_pos.z;
    cam_p.y = vert_distance;
    world_state->cam_p = cam_p;
    
#define CAMERA_FOV rad(60)
#define CAMERA_NEAR_PLANE 0.001f
#define CAMERA_FAR_PLANE  10000.0f
    f32 aspect_ratio = input->platform->display_size.x / input->platform->display_size.y;
    world_state->projection = Mat4x4::perspective(CAMERA_FOV, aspect_ratio, CAMERA_NEAR_PLANE, CAMERA_FAR_PLANE);
    world_state->view = Mat4x4::identity() * Mat4x4::rotation(world_state->cam.pitch, Vec3(1, 0, 0)) * Mat4x4::rotation(world_state->cam.yaw, Vec3(0, 1, 0))
        * Mat4x4::translate(-cam_p);
    world_state->mvp = world_state->projection * world_state->view;
    
    vec2 mouse_uv = Vec2((2.0f * input->platform->mpos.x) / input->platform->display_size.x - 1.0f,
                         1.0f - (2.0f * input->platform->mpos.y) / input->platform->display_size.y);
    vec3 ray_dir = uv_to_world(world_state->projection, world_state->view, mouse_uv);
    f32 t = 0;
    ray_intersect_plane(Vec3(0, 1, 0), 0, cam_p, ray_dir, &t);
    vec3 mouse_point_xyz = cam_p + ray_dir * t;
    vec2 mouse_point = Vec2(mouse_point_xyz.x, mouse_point_xyz.z);
    DEBUG_VALUE(mouse_point, "Mouse point");
    DEBUG_VALUE(world_state->cam_p, "Cam p");
    world_state->mouse_projection = mouse_point;
    // Find entity to be selected with mouse
    world_state->mouse_selected_entity = {};
    f32 min_distance = F32_INFINITY;
    ITERATE(iter, iterate_entities(sim, iter_radius(world_state->mouse_projection, DISTANCE_TO_MOUSE_SELECT))) {
        Entity *entity = get_entity_by_id(sim, *iter.ptr);
        f32 distance_to_mouse_sq = length_sq(world_state->mouse_projection - entity->p);
        if (distance_to_mouse_sq < DISTANCE_TO_MOUSE_SELECT_SQ && distance_to_mouse_sq < min_distance) {
            world_state->mouse_selected_entity = entity->id;
            min_distance = distance_to_mouse_sq;
        }
    }
    // Add selected entity to job queue if it fits
    if (is_key_pressed(input, KEY_MOUSE_LEFT)) {
        if (IS_NOT_NULL(world_state->mouse_selected_entity)) {
            Entity *entity = get_entity_by_id(sim, world_state->mouse_selected_entity);
            if (entity->kind == ENTITY_KIND_WORLD_OBJECT) {
                WorldObjectSpec spec = get_spec_for_type(world_state, entity->world_object_kind);
                if (spec.type == WORLD_OBJECT_TYPE_RESOURCE) {
                    Order order = {};
                    order.kind = ORDER_CHOP;
                    order.destination_id = world_state->mouse_selected_entity;
                    try_to_add_order(&world_state->order_system, order);
                }
            }
            
        }
    }
    
    vec2 player_pos = camera_controlled_entity->p;
    for (u32 pawn_idx = 0; 
         pawn_idx < world_state->pawn_count;
         ++pawn_idx) {
        EntityID pawn_id = world_state->pawns[pawn_idx];
        Entity *entity = get_entity_by_id(sim, pawn_id);
        // @TODO maybe we want all pawns to be made anchors with small radius 
        if (entity) {
            if (IS_NULL(entity->order)) {
                OrderID order_id = get_pending_order_id(&world_state->order_system);
                if (IS_NOT_NULL(order_id)) {
                    entity->order = order_id;
                    set_order_assigned(&world_state->order_system, order_id);
                }
            }
            
            if (IS_NOT_NULL(entity->order)) {
                Order *order = get_order_by_id(&world_state->order_system, entity->order);
                if (order->kind == ORDER_CHOP) {
                    Entity *to_chop = get_entity_by_id(sim, order->destination_id);
                    assert(to_chop); 
                    // @TODO what do we do if entity is outside of sim region - 
                    // set new state for order like out of bounds and request new one
                    vec2 delta = to_chop->p - entity->p;
                    if (length_sq(delta) > DISTANCE_TO_INTERACT_SQ) {
                        vec2 delta_p = normalize(delta) * PAWN_SPEED * input->platform->frame_dt;
                        vec2 new_pawn_p = entity->p + delta_p;
                        change_entity_position(sim, entity, new_pawn_p);
                    } else {
                        update_interaction(world_state, sim, entity, input);
                        // to_chop->flags |= ENTITY_FLAG_IS_DELETED;
                        // disband_order(&world_state->order_system, entity->order);
                        // entity->order = {};
                    }
                }
            } else { 
                vec2 delta = player_pos - entity->p;
                if (length_sq(delta) > PAWN_DISTANCE_TO_PLAYER_SQ) {
                    vec2 delta_p = normalize(delta) * PAWN_SPEED * input->platform->frame_dt;
                    vec2 new_pawn_p = entity->p + delta_p;
                    change_entity_position(sim, entity, new_pawn_p);
                }
            }
        }
    }
    // Assign job to pawn if 
}

void render_game(WorldState *world_state, SimRegion *sim, RendererCommands *commands, Assets *assets, InputManager *input) {
    RendererSetup setup = setup_3d(world_state->view, world_state->projection);
    set_setup(commands, &setup);
    begin_depth_peel(commands);
    RenderGroup render_group = create_render_group(commands, assets);
    // 
    // Ground
    // 
    u32 chunk_count = get_chunk_count_for_radius(sim->chunk_radius);
    AssetID ground_tex = assets_get_first_of_type(assets, ASSET_TYPE_GRASS);
    BEGIN_BLOCK("Ground render");
    for (u32 i = 0; i < chunk_count; ++i) {
        SimRegionChunk *chunk = sim->chunks + i;
#define WORLD_EPSILON 0.01f
        vec3 p[4] = {
            Vec3(chunk->chunk_x, 0, chunk->chunk_y) * CHUNK_SIZE,
            Vec3(chunk->chunk_x, 0, chunk->chunk_y + 1) * CHUNK_SIZE,
            Vec3(chunk->chunk_x + 1, 0, chunk->chunk_y) * CHUNK_SIZE,
            Vec3(chunk->chunk_x + 1, 0, chunk->chunk_y + 1) * CHUNK_SIZE,
        };
        vec3 p1[4];
        memcpy(p1, p, sizeof(p));
        p1[0].y = WORLD_EPSILON;
        p1[1].y = WORLD_EPSILON;
        p1[2].y = WORLD_EPSILON;
        p1[3].y = WORLD_EPSILON;
        push_quad(&render_group, p, ground_tex);
        if (world_state->draw_frames) {
            DEBUG_push_quad_outline(&render_group, p1);
        }
    }
    vec2 mouse_cell_pos = Vec2(Floor(world_state->mouse_projection.x), Floor(world_state->mouse_projection.y));
    DEBUG_VALUE(mouse_cell_pos, "Selected cell");
    DEBUG_VALUE(is_cell_occupied(sim, mouse_cell_pos.x, mouse_cell_pos.y), "Is occupied");
    if (world_state->draw_frames) {
#define MOUSE_CELL_RAD 10
        for (i32 dy = -MOUSE_CELL_RAD / 2; dy <= MOUSE_CELL_RAD / 2; ++dy) {
            for (i32 dx = -MOUSE_CELL_RAD / 2; dx <= MOUSE_CELL_RAD / 2; ++dx) {
                bool is_occupied = is_cell_occupied(sim, mouse_cell_pos.x + dx, mouse_cell_pos.y + dy);
                if (!is_occupied) {
                    vec3 v[4] = {
                        Vec3(mouse_cell_pos.x + dx * CELL_SIZE, WORLD_EPSILON, mouse_cell_pos.y + dy * CELL_SIZE),
                        Vec3(mouse_cell_pos.x + dx * CELL_SIZE, WORLD_EPSILON, mouse_cell_pos.y + dy * CELL_SIZE + CELL_SIZE),
                        Vec3(mouse_cell_pos.x + dx * CELL_SIZE + CELL_SIZE, WORLD_EPSILON, mouse_cell_pos.y + dy * CELL_SIZE),
                        Vec3(mouse_cell_pos.x + dx * CELL_SIZE + CELL_SIZE, WORLD_EPSILON, mouse_cell_pos.y + dy * CELL_SIZE + CELL_SIZE),
                    };
                    DEBUG_push_quad_outline(&render_group, v);
                } else {
                    vec3 v[4] = {
                        Vec3(mouse_cell_pos.x + dx * CELL_SIZE, WORLD_EPSILON * 5, mouse_cell_pos.y + dy * CELL_SIZE),
                        Vec3(mouse_cell_pos.x + dx * CELL_SIZE, WORLD_EPSILON * 5, mouse_cell_pos.y + dy * CELL_SIZE + CELL_SIZE),
                        Vec3(mouse_cell_pos.x + dx * CELL_SIZE + CELL_SIZE, WORLD_EPSILON * 5, mouse_cell_pos.y + dy * CELL_SIZE),
                        Vec3(mouse_cell_pos.x + dx * CELL_SIZE + CELL_SIZE, WORLD_EPSILON * 5, mouse_cell_pos.y + dy * CELL_SIZE + CELL_SIZE),
                    };
                    DEBUG_push_quad_outline(&render_group, v, RED);
                }
            }
        }
    }
    
    if (IS_NOT_NULL(world_state->mouse_selected_entity)) {
        Entity *entity = get_entity_by_id(sim, world_state->mouse_selected_entity);
        assert(entity);
        vec2 half_size = Vec2(1, 1) * 0.5f;
        vec3 v[4];
        v[0] = xz(entity->p + Vec2(-half_size.x, -half_size.y), WORLD_EPSILON);
        v[1] = xz(entity->p + Vec2(-half_size.x, half_size.y),  WORLD_EPSILON);
        v[2] = xz(entity->p + Vec2(half_size.x, -half_size.y),  WORLD_EPSILON);
        v[3] = xz(entity->p + Vec2(half_size.x, half_size.y),   WORLD_EPSILON);
        AssetID select_tex_id = assets_get_first_of_type(assets, ASSET_TYPE_ADDITIONAL);
        push_quad(&render_group, v, select_tex_id);
    }
    END_BLOCK();
    
    //
    // Entities
    //
    BEGIN_BLOCK("Render entities");
    SortEntry *sort_a = alloc_arr(world_state->frame_arena, sim->entity_count, SortEntry);
    SortEntry *sort_b = alloc_arr(world_state->frame_arena, sim->entity_count, SortEntry);
    vec3 cam_z = world_state->mvp.get_z();
    for (size_t entity_idx = 0; entity_idx < sim->entity_count; ++entity_idx) {
        sort_a[entity_idx].sort_key = dot(cam_z, xz(sim->entities[entity_idx].p) - world_state->cam_p);
        sort_a[entity_idx].sort_index = entity_idx;
    }
    radix_sort(sort_a, sort_b, sim->entity_count);
    vec3 cam_x = world_state->mvp.get_x();
    vec3 cam_y = world_state->mvp.get_y();
    for (size_t sorted_idx = 0; sorted_idx < sim->entity_count; ++sorted_idx) {
        Entity *entity = sim->entities + sort_a[sim->entity_count - sorted_idx - 1].sort_index;
        AssetID texture_id;
        switch (entity->kind) {
            case ENTITY_KIND_PLAYER: {
                texture_id = assets_get_first_of_type(assets, ASSET_TYPE_PLAYER);
            } break;
            case ENTITY_KIND_WORLD_OBJECT: {
                AssetTagList match_tags = {};
                AssetTagList weight_tags = {};
                match_tags.tags[ASSET_TAG_WORLD_OBJECT_KIND] = entity->world_object_kind;
                weight_tags.tags[ASSET_TAG_WORLD_OBJECT_KIND] = 1.0f;
                texture_id = assets_get_closest_match(assets, ASSET_TYPE_WORLD_OBJECT, &weight_tags, &match_tags);
            } break;
            case ENTITY_KIND_PAWN: {
                texture_id = assets_get_first_of_type(assets, ASSET_TYPE_PAWN);
            } break;
            INVALID_DEFAULT_CASE;
        }
        vec3 v[4];
        get_billboard_positions(xz(entity->p), cam_x, cam_y, 1.5f, 1.5f, v);
        push_quad(&render_group, v, texture_id);
    }
    END_BLOCK();
    
    //
    // Particles
    //
    update_and_render_particles(&world_state->particle_system, &render_group, input->platform->frame_dt);
    end_depth_peel(commands);
}

void update_and_render_world_state(WorldState *world_state, InputManager *input, RendererCommands *commands, Assets *assets) {
    
    u32 sim_region_count = world_state->anchor_count;
    // Zero anchor count so it can be set again from different sim regions
    world_state->anchor_count = 0;
    SimRegion *sim_regions = alloc_arr(world_state->frame_arena, sim_region_count, SimRegion);
    u32 total_sim_entities = 0;
    u32 total_sim_chunks = 0;
    for (u32 anchor_idx = 0; anchor_idx < sim_region_count; ++anchor_idx) {
        Anchor *anchor = world_state->anchors + anchor_idx;
        SimRegion *sim = sim_regions + anchor_idx;
        begin_sim(sim, world_state->frame_arena, world_state->world, anchor->chunk_x, anchor->chunk_y, anchor->radius);
        total_sim_entities += sim->entity_count;
        total_sim_chunks += sim->chunks_count;
        update_game(world_state, sim, input);
        render_game(world_state, sim, commands, assets, input);
        end_sim(sim, world_state);
    }
    {DEBUG_VALUE_BLOCK("World")
            DEBUG_SWITCH(&world_state->draw_frames, "Frames");
        DEBUG_VALUE(world_state->anchor_count, "Anchor count");
        DEBUG_VALUE(world_state->world->chunks_allocated, "Chunks allocated");
        DEBUG_VALUE(world_state->world->entity_blocks_allocated, "Entity blocks allocated");
        DEBUG_VALUE(world_state->world->entity_ids_allocated, "Entity ids allocated");
        DEBUG_VALUE(total_sim_entities, "Total sim entities");
        DEBUG_VALUE(total_sim_chunks, "Total sim chunks");
        DEBUG_VALUE(world_state->order_system.orders_allocated, "Orders allocated");
        DEBUG_VALUE(world_state->mouse_selected_entity.value, "Mouse select entity");
        DEBUG_VALUE(world_state->wood_count, "Wood count");
    }
}
