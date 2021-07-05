#if !defined(INTERFACE_HH)

#include "lib.hh"

enum {
    INTERFACE_ELEMENT_NONE,   
    INTERFACE_ELEMENT_RECTANGLE,   
    INTERFACE_ELEMENT_TEXT,   
    INTERFACE_ELEMENT_BUTTON,   
    INTERFACE_ELEMENT_IMAGE,   
    INTERFACE_ELEMENT_SENTINEL,   
};

struct InterfaceElement {
    u8 kind;  
    Rect rect;
    Vec4 color;
    const char *text;
    bool is_button_state_saving;
    
    Vec4 color_button_inactive;
    Vec4 color_button_active;
    bool is_button_pressed;
    
    InterfaceElement *next;
};

struct InterfaceStats {
    bool is_mouse_over_element;
    bool interaction_occured;
};

struct Interface {
    u32 DEBUG_elements_count;
    InterfaceElement *first_element;
    InterfaceElement *last_element;
};

struct GameStateInterface {
    Interface inter;
    InterfaceElement *text_for_wood_count;
    InterfaceElement *text_for_gold_count;
    InterfaceElement *button_camera_controls;
    InterfaceElement *button_build_mode;
    InterfaceElement *building_state;
    InterfaceElement *button_selected_building1;
    InterfaceElement *button_selected_building2;
};  

void init_interface_for_game_state(MemoryArena *arena, GameStateInterface *inter, Vec2 winsize);
InterfaceStats interface_update(Interface *inter, InputManager *input);
void interface_render(Interface *inter, RenderGroup *render_group);


#define INTERFACE_HH 1
#endif