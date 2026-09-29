public class ScreensaverHack : Object {
    public string id { get; construct; }
    public string title { get; construct; }
    public string group_name { get; construct; }

    public ScreensaverHack (string id, string title, string group_name) {
        Object (id: id, title: title, group_name: group_name);
    }
}

public class ScreensaverOption : Object {
    public string name { get; construct; }
    public string kind { get; construct; }
    public string default_value { get; construct; }
    public string min_value { get; construct; }
    public string max_value { get; construct; }
    public string[] choices { owned get; construct; }
    public string label { get; construct; }
    public string description { get; construct; }
    public string group_name { get; construct; }

    public ScreensaverOption (string name, string kind, string default_value, string min_value, string max_value,
                              string[] choices, string label, string description, string group_name) {
        Object (name: name, kind: kind, default_value: default_value, min_value: min_value, max_value: max_value,
                choices: choices, label: label, description: description, group_name: group_name);
    }
}

public interface ScreensaverBackend : Object {
    public abstract Gee.List<ScreensaverOption> options_for (string hack_id);
    public abstract string option_value (string hack_id, string name);
    public abstract void set_option (string hack_id, string name, string value);
    public abstract void reset_options (string hack_id);
    public abstract bool is_available ();
    public abstract Gee.List<ScreensaverHack> hacks ();
    public abstract string mode { owned get; set; }
    public abstract string hack_id { owned get; set; }
    public abstract string[] enabled_hacks { owned get; set; }
    public abstract bool has_tiers { get; }
    public abstract bool igpu_friendly (string id);
    public abstract string pool_class { owned get; set; }
    public abstract int start_delay { get; set; }
    public abstract int rotate_delay { get; set; }
    public abstract bool lock_supported { get; }
    public abstract bool lock_enabled { get; set; }
    public abstract int lock_delay { get; set; }
    public abstract bool lock_on_suspend { get; set; }
    public abstract int display_off_delay { get; set; }
    public abstract string? color_hack { get; }
    public abstract string[] color_ids { owned get; }
    public abstract string[] color_labels { owned get; }
    public abstract string color_mode { owned get; set; }
    public abstract bool gpu_offload_supported { get; }
    public abstract string gpu_offload { owned get; set; }
    public abstract void preview (string id);
    public abstract void stop_preview ();
    public signal void changed ();
}
