#include "api_loader.inl"

TM_LOAD_APIS(load_apis,
    tm_entity_api,
    tm_the_truth_api,
    tm_localizer_api,
    tm_properties_view_api,
    tm_ui_api,
    tm_logger_api,
    tm_transform_component_api,
    tm_the_truth_common_types_api);

#include "interactable_component.h"
#include <foundation/macros.h>
#include <foundation/the_truth.h>
#include <foundation/the_truth_types.h>
#include <foundation/localizer.h>
#include <foundation/allocator.h>
#include <foundation/undo.h>
#include <foundation/log.h>
#include <plugins/entity/entity.h>
#include <plugins/entity/transform_component.h>
#include <plugins/ui/ui.h>
#include <plugins/editor_views/properties.h>
#include <plugins/the_machinery_shared/component_interfaces/editor_ui_interface.h>

#include <foundation/carray.inl>
#include <foundation/rect.inl>
#include <foundation/math.inl>

// ---

enum interactable_component_prop {
    INTERACTABLE_COMPONENT_PROP__DESC, // subobject(TM_TT_TYPE__INTERACTABLE_XXX) -- for example TT_TYPE__INTERACTABLE_LEVER
    INTERACTABLE_COMPONENT_PROP__PLAYER_CAN_ACTIVE, // bool
};

#define TT_TYPE__INTERACTABLE_LEVER "tm_interactable_lever"
#define TT_TYPE_HASH__INTERACTABLE_LEVER TM_STATIC_HASH("tm_interactable_lever", 0xb415dd3c3c35fb79ULL)

enum interactable_lever_prop {
    INTERACTABLE_LEVER_PROP__HANDLE, // reference(entity)
    INTERACTABLE_LEVER_PROP__HANDLE_ROTATE_AXIS, // vec3
    INTERACTABLE_LEVER_PROP__HANDLE_ROTATE_ANGLE, // float
    INTERACTABLE_LEVER_PROP__HANDLE_OPEN_TIME, // float
    INTERACTABLE_LEVER_PROP__TARGET, // reference(entity)
};

#define TT_TYPE__INTERACTABLE_BUTTON "tm_interactable_button"
#define TT_TYPE_HASH__INTERACTABLE_BUTTON TM_STATIC_HASH("tm_interactable_button", 0xb6c62757302df535ULL)

enum interactable_button_prop {
    INTERACTABLE_BUTTON_PROP__BUTTON, // reference(entity)
    INTERACTABLE_BUTTON_PROP__BUTTON_PUSH_AXIS, // vec3
    INTERACTABLE_BUTTON_PROP__BUTTON_PUSH_DISTANCE, // float
    INTERACTABLE_BUTTON_PROP__BUTTON_PUSH_TIME, // float
    INTERACTABLE_BUTTON_PROP__TARGET, // reference(entity)
};

#define TT_TYPE__INTERACTABLE_ROTATING_DOOR "tm_interactable_rotating_door"
#define TT_TYPE_HASH__INTERACTABLE_ROTATING_DOOR TM_STATIC_HASH("tm_interactable_rotating_door", 0x40e43b5a4858aef2ULL)

enum rotating_door_prop {
    ROTATING_DOOR_PROP__PIVOT, // reference(entity)
    ROTATING_DOOR_PROP__PIVOT_ROTATE_AXIS, // vec3
    ROTATING_DOOR_PROP__PIVOT_ROTATE_ANGLE, // float
    ROTATING_DOOR_PROP__PIVOT_ROTATE_TIME, // float
};

enum interactable_type {
    INTERACTABLE_TYPE_LEVER,
    INTERACTABLE_TYPE_BUTTON,
    INTERACTABLE_TYPE_ROTATING_DOOR,
};

enum lever_state {
    LEVER_STATE_CLOSED,
    LEVER_STATE_LEVER_OPENING,
    LEVER_STATE_OPEN,
    LEVER_STATE_LEVER_CLOSING,
};

typedef struct lever_t {
    tm_entity_t handle;
    tm_entity_t target;
    tm_vec4_t handle_closed_rotation;
    tm_vec4_t handle_open_rotation;
    float handle_open_time;
    enum lever_state state;
} lever_t;

enum button_state {
    BUTTON_STATE_NORMAL,
    BUTTON_STATE_BUTTON_PUSHING,
    BUTTON_STATE_PUSHED,
    BUTTON_STATE_BUTTON_UNPUSHING,
};

typedef struct button_t {
    tm_entity_t button;
    tm_entity_t target;
    tm_vec3_t button_normal_position;
    tm_vec3_t button_pushed_position;
    float button_push_time;
    enum button_state state;
} button_t;

enum rotating_door_state {
    ROTATING_DOOR_STATE_CLOSED,
    ROTATING_DOOR_STATE_OPENING,
    ROTATING_DOOR_STATE_OPEN,
    ROTATING_DOOR_STATE_CLOSING,
};

typedef struct rotating_door_t {
    tm_entity_t pivot;
    tm_entity_t target;
    tm_vec4_t pivot_closed_rotation;
    tm_vec4_t pivot_open_rotation;
    float pivot_rotate_time;
    enum rotating_door_state state;
} rotating_door_t;

typedef struct interactable_component_t {
    enum interactable_type type;
    bool player_can_active;
    TM_PAD(3);
    union {
        lever_t lever;
        button_t button;
        rotating_door_t rotating_door;
    };
} interactable_component_t;

// ---

static const char *component_category(void)
{
    return TM_LOCALIZE("Interactables");
}

static tm_ci_editor_ui_i *editor_aspect = &(tm_ci_editor_ui_i){
    .category = component_category
};

typedef struct {
    double start_time;
    tm_entity_t interactable;
} active_interaction_t;

struct tm_interactable_component_manager_o {
    tm_allocator_i allocator;
    tm_entity_context_o *ctx;
    active_interaction_t *active;
    uint32_t interactable_component_type;
    uint32_t transform_component_type;
    tm_transform_component_manager_o *trans_mgr;
};


static void interact(tm_interactable_component_manager_o *mgr, tm_entity_t interactable);
static bool can_interact(tm_interactable_component_manager_o *mgr, tm_entity_t interactable, bool is_player);

static bool update_lever(tm_interactable_component_manager_o *mgr, float dt, double t, active_interaction_t *a, interactable_component_t *c)
{
    bool res = false;
    lever_t *l = &c->lever;
    switch (l->state) {
        case LEVER_STATE_CLOSED: {
            l->state = LEVER_STATE_LEVER_OPENING;
        } break;

        case LEVER_STATE_LEVER_OPENING: {
            const float p = (float)(t - a->start_time)/l->handle_open_time;
            const tm_vec4_t rot = tm_quaternion_nlerp(l->handle_closed_rotation, l->handle_open_rotation, tm_min(p, 1));
            tm_set_local_rotation(mgr->trans_mgr, l->handle, rot);

            if (p >= 1) {
                l->state = LEVER_STATE_OPEN;
                if (can_interact(mgr, l->target, false))
                    interact(mgr, l->target);
                res = true;
            }
        } break;

        case LEVER_STATE_OPEN: {
            l->state = LEVER_STATE_LEVER_CLOSING;
        } break;

        case LEVER_STATE_LEVER_CLOSING: {
            const float p = (float)(t - a->start_time)/l->handle_open_time;
            const tm_vec4_t rot = tm_quaternion_nlerp(l->handle_open_rotation, l->handle_closed_rotation, tm_min(p, 1));
            tm_set_local_rotation(mgr->trans_mgr, l->handle, rot);

            if (p >= 1) {
                l->state = LEVER_STATE_CLOSED;
                if (can_interact(mgr, l->target, false))
                    interact(mgr, l->target);
                res = true;
            }
        } break;
    }

    return res;
}

static bool update_button(tm_interactable_component_manager_o *mgr, float dt, double t, active_interaction_t *a, interactable_component_t *c)
{
    bool res = false;
    button_t *b = &c->button;
    switch (b->state) {
        case BUTTON_STATE_NORMAL: {
            b->state = BUTTON_STATE_BUTTON_PUSHING;
        } break;

        case BUTTON_STATE_BUTTON_PUSHING: {
            const float p = (float)(t - a->start_time)/b->button_push_time;
            const tm_vec3_t pos = tm_vec3_lerp(b->button_normal_position, b->button_pushed_position, tm_min(p, 1));
            tm_set_local_position(mgr->trans_mgr, b->button, pos);

            if (p >= 1) {
                res = true;
                
                if (can_interact(mgr, b->target, false))
                    interact(mgr, b->target);

                b->state = BUTTON_STATE_PUSHED;
            }
        } break;

        case BUTTON_STATE_PUSHED: {
            b->state = BUTTON_STATE_BUTTON_UNPUSHING;
        } break;

        case BUTTON_STATE_BUTTON_UNPUSHING: {
            const float p = (float)(t - a->start_time)/b->button_push_time;
            const tm_vec3_t pos = tm_vec3_lerp(b->button_pushed_position, b->button_normal_position, tm_min(p, 1));
            tm_set_local_position(mgr->trans_mgr, b->button, pos);

            if (p >= 1) {
                res = true;
                
                if (can_interact(mgr, b->target, false))
                    interact(mgr, b->target);

                b->state = BUTTON_STATE_NORMAL;
            }
        } break;
    }

    return res;
}

static bool update_rotating_door(tm_interactable_component_manager_o *mgr, float dt, double t, active_interaction_t *a, interactable_component_t *c)
{
    bool res = false;
    rotating_door_t *s = &c->rotating_door;
    switch (s->state) {
        case ROTATING_DOOR_STATE_CLOSED: {
            s->state = ROTATING_DOOR_STATE_OPENING;
        } break;

        case ROTATING_DOOR_STATE_OPENING: {
            const float p = (float)(t - a->start_time)/s->pivot_rotate_time;
            const tm_vec4_t rot = tm_quaternion_nlerp(s->pivot_closed_rotation, s->pivot_open_rotation, tm_min(p, 1));
            tm_set_local_rotation(mgr->trans_mgr, s->pivot, rot);

            if (p >= 1) {
                res = true;
                s->state = ROTATING_DOOR_STATE_OPEN;
            }
        } break;

        case ROTATING_DOOR_STATE_OPEN: {
            s->state = ROTATING_DOOR_STATE_CLOSING;
        } break;

        case ROTATING_DOOR_STATE_CLOSING: {
            const float p = (float)(t - a->start_time)/s->pivot_rotate_time;
            const tm_vec4_t rot = tm_quaternion_nlerp(s->pivot_open_rotation, s->pivot_closed_rotation, tm_min(p, 1));
            tm_set_local_rotation(mgr->trans_mgr, s->pivot, rot);

            if (p >= 1) {
                res = true;
                s->state = ROTATING_DOOR_STATE_CLOSED;
            }
        } break;
    }

    return res;
}


static void update_active_interactables(tm_interactable_component_manager_o *mgr, float dt, double t)
{
    for (int32_t active_idx = 0; active_idx < (int32_t)tm_carray_size(mgr->active); ++active_idx) {
        active_interaction_t *a = mgr->active + active_idx;
        interactable_component_t *c = tm_entity_api->get_component(mgr->ctx, a->interactable, mgr->interactable_component_type);

        if (!a->start_time)
            a->start_time = t;

        bool res = false;
        switch(c->type) {
            case INTERACTABLE_TYPE_LEVER: {
                res = update_lever(mgr, dt, t, a, c);
            } break;
            case INTERACTABLE_TYPE_BUTTON: {
                res = update_button(mgr, dt, t, a, c);
            } break;
            case INTERACTABLE_TYPE_ROTATING_DOOR: {
                res = update_rotating_door(mgr, dt, t, a, c);
            } break;
        }

        if (res)
            mgr->active[active_idx--] = tm_carray_pop(mgr->active);
    }
}

static void manager_init(tm_interactable_component_manager_o *mgr)
{
    mgr->transform_component_type = tm_entity_api->lookup_component(mgr->ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);
    mgr->trans_mgr = (tm_transform_component_manager_o*)tm_entity_api->component_manager(mgr->ctx, mgr->transform_component_type);
}

static void manager_deinit(tm_interactable_component_manager_o *mgr)
{
    tm_carray_free(mgr->active, &mgr->allocator);
}

static void interact(tm_interactable_component_manager_o *mgr, tm_entity_t interactable)
{
    tm_carray_push(mgr->active, ((active_interaction_t) { .interactable = interactable } ), &mgr->allocator);
}

static bool can_interact(tm_interactable_component_manager_o *mgr, tm_entity_t interactable, bool is_player)
{
    const interactable_component_t *c = tm_entity_api->get_component(mgr->ctx, interactable, mgr->interactable_component_type);

    if (!c)
        return false;

    if (is_player && !c->player_can_active)
        return false;

    // Send is_player false here because this is only a check if this interactable can activate its target.
    switch (c->type) {
        case INTERACTABLE_TYPE_LEVER: {
            if (!can_interact(mgr, c->lever.target, false))
                return false;
        } break;
        case INTERACTABLE_TYPE_BUTTON: {
            if (!can_interact(mgr, c->button.target, false))
                return false;
        } break;
    }

    for (int32_t active_idx = 0; active_idx < (int32_t)tm_carray_size(mgr->active); ++active_idx) {
        active_interaction_t *a = mgr->active + active_idx;

        if (a->interactable.u64 == interactable.u64)
            return false;
    }

    return true;
}

static struct tm_interactable_component_api *tm_interactable_component_api = &(struct tm_interactable_component_api) {
    .can_interact = can_interact,
    .interact = interact,
    .update_active_interactables = update_active_interactables,
};

static float component_properties_ui(struct tm_properties_ui_args_t *args, tm_rect_t item_rect, tm_tt_id_t component_id, uint32_t indent)
{
    tm_the_truth_o *tt = args->tt;
    tm_tt_id_t desc = tm_the_truth_api->get_subobject(tt, tm_tt_read(tt, component_id), INTERACTABLE_COMPONENT_PROP__DESC);
    const uint64_t desc_type = desc.index ? desc.type : 0;
    const uint64_t desc_type_hash = desc_type ? tm_the_truth_api->type_name_hash(tt, desc_type) : 0;

    const uint64_t type_hashes[] = {
        0,
        TT_TYPE_HASH__INTERACTABLE_LEVER,
        TT_TYPE_HASH__INTERACTABLE_BUTTON,
        TT_TYPE_HASH__INTERACTABLE_ROTATING_DOOR,
    };

    const char *type_names[] = {
        TM_LOCALIZE("None"),
        TM_LOCALIZE("Lever"),
        TM_LOCALIZE("Button"),
        TM_LOCALIZE("Rotating Door"),
    };

    const tm_rect_t label_r = tm_rect_split_left(item_rect, args->metrics[TM_PROPERTIES_METRIC_LABEL_WIDTH], args->metrics[TM_PROPERTIES_METRIC_MARGIN], 0);
    const tm_rect_t dropdown_r = tm_rect_split_left(item_rect, args->metrics[TM_PROPERTIES_METRIC_LABEL_WIDTH], args->metrics[TM_PROPERTIES_METRIC_MARGIN], 1);
    
    tm_properties_view_api->ui_label(args, label_r, TM_LOCALIZE("Type"), 0);
    
    const tm_ui_dropdown_t d = {
        .rect = dropdown_r,
        .items = type_names,
        .num_items = TM_ARRAY_COUNT(type_names),
    };

    uint32_t selected_idx = 0;

    for (uint32_t type_idx = 0; type_idx < TM_ARRAY_COUNT(type_hashes); ++type_idx) {
        if (type_hashes[type_idx] == desc_type_hash) {
            selected_idx = type_idx;
            break;
        }
    }

    if (tm_ui_api->dropdown(args->ui, args->uistyle, &d, &selected_idx)) {
        const tm_tt_undo_scope_t undo_scope = tm_the_truth_api->create_undo_scope(tt, TM_LOCALIZE("Change interactable type"));
        tm_the_truth_object_o *component_w = tm_the_truth_api->write(tt, component_id);
         
        if (desc.index) {
            if (tm_the_truth_api->is_alive(tt, desc))
                tm_the_truth_api->destroy_object(tt, desc, undo_scope);
        
            tm_the_truth_api->set_subobject(tt, component_w, INTERACTABLE_COMPONENT_PROP__DESC, 0);
        }

        const uint64_t new_hash = type_hashes[selected_idx];

        if (new_hash) {
            const uint64_t new_type = tm_the_truth_api->optional_object_type_from_name_hash(tt, new_hash);

            if (new_type) {
                desc = tm_the_truth_api->create_object_of_type(tt, new_type, undo_scope);
                tm_the_truth_api->set_subobject_id(tt, component_w, INTERACTABLE_COMPONENT_PROP__DESC, desc, undo_scope);
            }
        }

        tm_the_truth_api->commit(tt, component_w, undo_scope);
        args->undo_stack->add(args->undo_stack->inst, args->tt, undo_scope);
    }

    item_rect.y += item_rect.h + args->metrics[TM_PROPERTIES_METRIC_MARGIN];
    item_rect.y = tm_properties_view_api->ui_object(args, item_rect, desc, indent);
    item_rect.y = tm_properties_view_api->ui_bool(args, item_rect, TM_LOCALIZE("Player Can Activate"), 0, component_id, INTERACTABLE_COMPONENT_PROP__PLAYER_CAN_ACTIVE);
    return item_rect.y;
}

static tm_properties_aspect_i *properties_aspect = &(tm_properties_aspect_i){
    .custom_ui = component_properties_ui,
};


static void component__asset_loaded(tm_component_manager_o *mgr_in, tm_entity_t e, void *data)
{
    tm_interactable_component_manager_o *mgr = (tm_interactable_component_manager_o*)mgr_in;
    interactable_component_t* c = data;
    tm_entity_context_o *ctx = mgr->ctx;
    const tm_tt_id_t entity_asset = tm_entity_api->asset(mgr->ctx, e);
    tm_the_truth_o *tt = tm_entity_api->the_truth(mgr->ctx);
    const uint64_t interactable_tt_type = tm_the_truth_api->object_type_from_name_hash(tt, TM_TT_TYPE_HASH__INTERACTABLE_COMPONENT);
    const tm_tt_id_t asset = tm_the_truth_api->find_subobject_of_type(tt, tm_tt_read(tt, entity_asset), TM_TT_PROP__ENTITY__COMPONENTS, interactable_tt_type);
    const tm_the_truth_object_o* asset_r = tm_tt_read(tt, asset);
    const tm_tt_id_t desc = tm_the_truth_api->get_subobject(tt, asset_r, INTERACTABLE_COMPONENT_PROP__DESC);
    const uint64_t desc_type = desc.type;
    const uint64_t desc_type_hash = tm_the_truth_api->type_name_hash(tt, desc_type);
    const tm_the_truth_object_o *desc_r = tm_tt_read(tt, desc);

    c->player_can_active = tm_the_truth_api->get_bool(tt, asset_r, INTERACTABLE_COMPONENT_PROP__PLAYER_CAN_ACTIVE);

    switch (desc_type_hash) {
        case TT_TYPE_HASH__INTERACTABLE_LEVER: {
            c->type = INTERACTABLE_TYPE_LEVER;
            lever_t *l = &c->lever;
            const tm_tt_id_t handle = tm_the_truth_api->get_reference(tt, desc_r, INTERACTABLE_LEVER_PROP__HANDLE);
            const float handle_rotation_angle = tm_the_truth_api->get_float(tt, desc_r, INTERACTABLE_LEVER_PROP__HANDLE_ROTATE_ANGLE) * (TM_PI/180.f);
            const tm_vec3_t handle_rotation_axis = tm_vec3_normalize(tm_the_truth_common_types_api->get_vec3(tt, desc_r, INTERACTABLE_LEVER_PROP__HANDLE_ROTATE_AXIS));
            if (handle.index) {
                l->handle = tm_entity_api->resolve_asset_reference(ctx, e, handle);
                l->handle_closed_rotation = tm_get_local_rotation(mgr->trans_mgr, l->handle);
                l->handle_open_rotation = tm_quaternion_mul(l->handle_closed_rotation, tm_quaternion_from_rotation(handle_rotation_axis, handle_rotation_angle));
                l->handle_open_time = tm_the_truth_api->get_float(tt, desc_r, INTERACTABLE_LEVER_PROP__HANDLE_OPEN_TIME);
                if (l->handle_open_time < 0.001)
                    l->handle_open_time = 1.0f;
            }
            const tm_tt_id_t target = tm_the_truth_api->get_reference(tt, desc_r, INTERACTABLE_LEVER_PROP__TARGET);
            if (target.index) {
                l->target = tm_entity_api->resolve_asset_reference(ctx, e, target);
            }
        } break;
        case TT_TYPE_HASH__INTERACTABLE_BUTTON: {
            c->type = INTERACTABLE_TYPE_BUTTON;
            button_t *b = &c->button;
            const tm_tt_id_t button = tm_the_truth_api->get_reference(tt, desc_r, INTERACTABLE_BUTTON_PROP__BUTTON);
            const float button_push_distance = tm_the_truth_api->get_float(tt, desc_r, INTERACTABLE_BUTTON_PROP__BUTTON_PUSH_DISTANCE);
            const tm_vec3_t button_push_axis = tm_vec3_normalize(tm_the_truth_common_types_api->get_vec3(tt, desc_r, INTERACTABLE_BUTTON_PROP__BUTTON_PUSH_AXIS));
            if (button.index) {
                b->button = tm_entity_api->resolve_asset_reference(ctx, e, button);
                b->button_normal_position = tm_get_local_position(mgr->trans_mgr, b->button);
                b->button_pushed_position = tm_vec3_add(b->button_normal_position, tm_vec3_mul(button_push_axis, button_push_distance));
                b->button_push_time = tm_the_truth_api->get_float(tt, desc_r, INTERACTABLE_BUTTON_PROP__BUTTON_PUSH_TIME);
                if (b->button_push_time < 0.001)
                    b->button_push_time = 1.0f;
            }
            const tm_tt_id_t target = tm_the_truth_api->get_reference(tt, desc_r, INTERACTABLE_BUTTON_PROP__TARGET);
            if (target.index) {
                b->target = tm_entity_api->resolve_asset_reference(ctx, e, target);
            }
        } break;
        case TT_TYPE_HASH__INTERACTABLE_ROTATING_DOOR: {
            c->type = INTERACTABLE_TYPE_ROTATING_DOOR;
            rotating_door_t *s = &c->rotating_door;
            const tm_tt_id_t pivot = tm_the_truth_api->get_reference(tt, desc_r, ROTATING_DOOR_PROP__PIVOT);
            const float pivot_rotate_angle = tm_the_truth_api->get_float(tt, desc_r, ROTATING_DOOR_PROP__PIVOT_ROTATE_ANGLE) * (TM_PI/180.f); 
            const tm_vec3_t pivot_rotate_axis = tm_vec3_normalize(tm_the_truth_common_types_api->get_vec3(tt, desc_r, ROTATING_DOOR_PROP__PIVOT_ROTATE_AXIS));
            if (pivot.index) {
                s->pivot = tm_entity_api->resolve_asset_reference(ctx, e, pivot);
                s->pivot_closed_rotation = tm_get_local_rotation(mgr->trans_mgr, s->pivot);
                s->pivot_open_rotation = tm_quaternion_mul(s->pivot_closed_rotation, tm_quaternion_from_rotation(pivot_rotate_axis, pivot_rotate_angle));
                s->pivot_rotate_time = tm_the_truth_api->get_float(tt, desc_r, ROTATING_DOOR_PROP__PIVOT_ROTATE_TIME);
                if (s->pivot_rotate_time < 0.001)
                    s->pivot_rotate_time = 1.0f;
            }
        } break;
    }
}

static void component__destroy(tm_component_manager_o *mgr_in)
{
    tm_interactable_component_manager_o *mgr = (tm_interactable_component_manager_o*)mgr_in;
    manager_deinit(mgr);
    tm_allocator_i a = mgr->allocator;
    tm_entity_context_o *ctx = mgr->ctx;
    tm_free(&a, mgr, sizeof(*mgr));
    tm_entity_api->destroy_child_allocator(ctx, &a);
}

static void manager_components_created(tm_component_manager_o *man_in)
{
    tm_interactable_component_manager_o *man = (tm_interactable_component_manager_o*)man_in;
    manager_init(man);
}

static tm_interactable_component_manager_o *component__create(struct tm_entity_context_o* ctx)
{
    tm_allocator_i a;
    tm_entity_api->create_child_allocator(ctx, TM_TT_TYPE__INTERACTABLE_COMPONENT, &a);
    tm_interactable_component_manager_o *m = tm_alloc(&a, sizeof(*m));

    tm_component_i component = {
        .name = TM_TT_TYPE__INTERACTABLE_COMPONENT,
        .bytes = sizeof(struct interactable_component_t),
        .asset_loaded = component__asset_loaded,
        .destroy = component__destroy,
        .manager = (tm_component_manager_o*)m,
        .components_created = manager_components_created,
    };

    const uint32_t interactable_component_type = tm_entity_api->register_component(ctx, &component);

    *m = (tm_interactable_component_manager_o) {
        .allocator = a,
        .ctx = ctx,
        .interactable_component_type = interactable_component_type,
    };

    return m;
}

#define tm_set_api(reg, load, ptr) \
    if (load)                                      \
        reg->set(#ptr, ptr, sizeof(*ptr));         \
    else                                           \
        reg->remove(ptr)


static void create_truth_types(struct tm_the_truth_o* tt)
{
    tm_the_truth_property_definition_t interactable_component_properties[] = {
        [INTERACTABLE_COMPONENT_PROP__DESC] = { "desc", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__ANYTHING },
        [INTERACTABLE_COMPONENT_PROP__PLAYER_CAN_ACTIVE] = { "player_can_active", TM_THE_TRUTH_PROPERTY_TYPE_BOOL },
    };

    const uint64_t interactable_component_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__INTERACTABLE_COMPONENT, interactable_component_properties, TM_ARRAY_COUNT(interactable_component_properties));

    const tm_tt_id_t default_interactable_component = tm_the_truth_api->create_object_of_type(tt, interactable_component_type, TM_TT_NO_UNDO_SCOPE);

    if (default_interactable_component.u64) {
        tm_the_truth_object_o *default_w = tm_the_truth_api->write(tt, default_interactable_component);
        tm_the_truth_api->set_bool(tt, default_w, INTERACTABLE_COMPONENT_PROP__PLAYER_CAN_ACTIVE, true);
        tm_the_truth_api->commit(tt, default_w, TM_TT_NO_UNDO_SCOPE);
        tm_the_truth_api->set_default_object(tt, interactable_component_type, default_interactable_component);
    }

    tm_the_truth_api->set_aspect(tt, interactable_component_type, TM_CI_EDITOR_UI, editor_aspect);
    tm_the_truth_api->set_aspect(tt, interactable_component_type, TM_TT_ASPECT__PROPERTIES, properties_aspect);

    {
        tm_the_truth_property_definition_t lever_properties[] = {
            [INTERACTABLE_LEVER_PROP__HANDLE] = { "handle", TM_THE_TRUTH_PROPERTY_TYPE_REFERENCE, .type_hash = TM_TT_TYPE_HASH__ENTITY },
            [INTERACTABLE_LEVER_PROP__HANDLE_ROTATE_AXIS] = { "handle_rotate_axis", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3, },
            [INTERACTABLE_LEVER_PROP__HANDLE_ROTATE_ANGLE] = { "handle_rotate_angle", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
            [INTERACTABLE_LEVER_PROP__HANDLE_OPEN_TIME] = { "handle_open_time", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
            [INTERACTABLE_LEVER_PROP__TARGET] = { "target", TM_THE_TRUTH_PROPERTY_TYPE_REFERENCE, .type_hash = TM_TT_TYPE_HASH__ENTITY },
        };

        const uint64_t interactable_lever_type = tm_the_truth_api->create_object_type(tt, TT_TYPE__INTERACTABLE_LEVER, lever_properties, TM_ARRAY_COUNT(lever_properties));
        tm_the_truth_api->set_property_aspect(tt, interactable_lever_type, INTERACTABLE_LEVER_PROP__HANDLE, TM_TT_PROP_ASPECT__PROPERTIES__USE_LOCAL_ENTITY_PICKER, (void *)1);
        tm_the_truth_api->set_property_aspect(tt, interactable_lever_type, INTERACTABLE_LEVER_PROP__TARGET, TM_TT_PROP_ASPECT__PROPERTIES__USE_LOCAL_ENTITY_PICKER, (void *)1);
        tm_the_truth_api->set_default_object_to_create_subobjects(tt, interactable_lever_type);
    }

    {
        tm_the_truth_property_definition_t button_properties[] = {
            [INTERACTABLE_BUTTON_PROP__BUTTON] = { "button", TM_THE_TRUTH_PROPERTY_TYPE_REFERENCE, .type_hash = TM_TT_TYPE_HASH__ENTITY },
            [INTERACTABLE_BUTTON_PROP__BUTTON_PUSH_AXIS] = { "button_push_axis", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3, },
            [INTERACTABLE_BUTTON_PROP__BUTTON_PUSH_DISTANCE] = { "button_push_distance", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
            [INTERACTABLE_BUTTON_PROP__BUTTON_PUSH_TIME] = { "button_push_time", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
            [INTERACTABLE_BUTTON_PROP__TARGET] = { "target", TM_THE_TRUTH_PROPERTY_TYPE_REFERENCE, .type_hash = TM_TT_TYPE_HASH__ENTITY },
        };

        const uint64_t interactable_button_type = tm_the_truth_api->create_object_type(tt, TT_TYPE__INTERACTABLE_BUTTON, button_properties, TM_ARRAY_COUNT(button_properties));
        tm_the_truth_api->set_property_aspect(tt, interactable_button_type, INTERACTABLE_BUTTON_PROP__BUTTON, TM_TT_PROP_ASPECT__PROPERTIES__USE_LOCAL_ENTITY_PICKER, (void *)1);
        tm_the_truth_api->set_property_aspect(tt, interactable_button_type, INTERACTABLE_BUTTON_PROP__TARGET, TM_TT_PROP_ASPECT__PROPERTIES__USE_LOCAL_ENTITY_PICKER, (void *)1);
        tm_the_truth_api->set_default_object_to_create_subobjects(tt, interactable_button_type);
    }

    {
        tm_the_truth_property_definition_t rotating_door[] = {
            [ROTATING_DOOR_PROP__PIVOT] = { "pivot", TM_THE_TRUTH_PROPERTY_TYPE_REFERENCE, .type_hash = TM_TT_TYPE_HASH__ENTITY },
            [ROTATING_DOOR_PROP__PIVOT_ROTATE_AXIS] = { "pivot_rotate_axis", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3, },
            [ROTATING_DOOR_PROP__PIVOT_ROTATE_ANGLE] = { "pivot_rotate_angle", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
            [ROTATING_DOOR_PROP__PIVOT_ROTATE_TIME] = { "pivot_rotate_time", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        };

        const uint64_t rotating_door_type = tm_the_truth_api->create_object_type(tt, TT_TYPE__INTERACTABLE_ROTATING_DOOR, rotating_door, TM_ARRAY_COUNT(rotating_door));
        tm_the_truth_api->set_property_aspect(tt, rotating_door_type, ROTATING_DOOR_PROP__PIVOT, TM_TT_PROP_ASPECT__PROPERTIES__USE_LOCAL_ENTITY_PICKER, (void *)1);
        tm_the_truth_api->set_default_object_to_create_subobjects(tt, rotating_door_type);
    }
}

void load_interactable_component(struct tm_api_registry_api* reg, bool load)
{
    load_apis(reg);

    tm_set_api(reg, load, tm_interactable_component_api);
    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, create_truth_types);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, component__create);
}
