#include "{h_file}"

const char* arbitrary_messages[] = {{
{arbitrary_messages}
}};

const class_t classes[] = {{
{classes}
}};

const turn_threshold_t turn_thresholds[] = {{
{turn_thresholds}
}};

const location_t locations[] = {{
{locations}
}};

const object_t objects[] = {{
{objects}
}};

const obituary_t obituaries[] = {{
{obituaries}
}};

const hint_t hints[] = {{
{hints}
}};

long conditions[] = {{
{conditions}
}};

const motion_t motions[] = {{
{motions}
}};

const action_t actions[] = {{
{actions}
}};

const long tkey[] = {{{tkeys}}};

const travelop_t travel[] = {{
{travel}
}};

const char *ignore = "{ignore}";

const char* get_dungeon_prop(const char *prop_name)
{{
    {getter}
    
    return false;
}}
bool set_dungeon_prop(const char *prop_name, const char *prop_value)
{{
    
}}
/* end */