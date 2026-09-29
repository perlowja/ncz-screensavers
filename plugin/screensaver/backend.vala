public class ScreensaverHack : Object {
    public string id { get; construct; }
    public string title { get; construct; }
    public string group_name { get; construct; }

    public ScreensaverHack (string id, string title, string group_name) {
        Object (id: id, title: title, group_name: group_name);
    }
}

public interface ScreensaverBackend : Object {
    public abstract bool is_available ();
    public abstract Gee.List<ScreensaverHack> hacks ();
    public abstract string mode { owned get; set; }
    public abstract string hack_id { owned get; set; }
    public abstract string[] enabled_hacks { owned get; set; }
    public abstract int start_delay { get; set; }
    public abstract int rotate_delay { get; set; }
    public abstract bool lock_supported { get; }
    public abstract bool lock_enabled { get; set; }
    public abstract int lock_delay { get; set; }
    public abstract bool lock_on_suspend { get; set; }
    public abstract int display_off_delay { get; set; }
    public abstract string color_mode { owned get; set; }
    public abstract bool gpu_offload_supported { get; }
    public abstract string gpu_offload { owned get; set; }
    public abstract void preview (string id);
    public abstract void stop_preview ();
    public signal void changed ();
}
