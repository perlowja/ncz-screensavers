public ScreensaverBackend create_screensaver_backend () {
    return new NczScreensaverBackend ();
}

public class NczScreensaverBackend : Object, ScreensaverBackend {
    private const string SCHEMA = "dev.ncz.screensaver";
    private const string LOCK_SCHEMA = "dev.sinty.lockscreen";
    private const string CATALOG = "/usr/share/ncz-screensavers/hacks.tsv";
    private const string TIERS = "/usr/share/ncz-screensavers/tiers.tsv";
    private const string OPTIONS_DIR = "/usr/share/ncz-screensavers/options";
    private const string LAUNCHER = "ncz-screensaver";
    private const string IDLED = "/usr/libexec/ncz-screensaver-idled";

    private Settings? settings;
    private Settings? lock_settings;
    private Gee.ArrayList<ScreensaverHack> catalog = new Gee.ArrayList<ScreensaverHack> ();
    private Gee.HashSet<string> flagged_ids = new Gee.HashSet<string> ();
    private Gee.HashMap<string, string> expects = new Gee.HashMap<string, string> ();
    private string gpu_class_text = "";
    private bool tiers_loaded = false;
    private Gee.HashMap<string, string> issues = new Gee.HashMap<string, string> ();

    public NczScreensaverBackend () {
        settings = open (SCHEMA);
        lock_settings = open (LOCK_SCHEMA);
        if (settings != null) {
            settings.changed.connect (() => changed ());
            load_catalog ();
            load_tiers ();
            load_launcher_state ();
            load_issues ();
        }
        if (lock_settings != null)
            lock_settings.changed.connect (() => changed ());
    }

    private static Settings? open (string schema) {
        var source = SettingsSchemaSource.get_default ();
        if (source == null || source.lookup (schema, true) == null)
            return null;
        return new Settings (schema);
    }

    private void load_catalog () {
        string data;
        try {
            FileUtils.get_contents (CATALOG, out data);
        } catch (FileError e) {
            return;
        }
        foreach (unowned string line in data.split ("\n")) {
            if (line == "" || line.has_prefix ("#"))
                continue;
            string[] f = line.split ("\t");
            if (f.length >= 3)
                catalog.add (new ScreensaverHack (f[0], f[1], f[2]));
        }
    }

    private void load_issues () {
        string data;
        foreach (unowned string path in new string[] { "/usr/share/ncz-screensavers/broken.tsv", "/usr/share/ncz-screensaver-chooser/broken.tsv" }) {
            try {
                FileUtils.get_contents (path, out data);
            } catch (FileError e) {
                continue;
            }
            foreach (unowned string line in data.split ("
")) {
                string[] f = line.split ("	");
                if (line.has_prefix ("#") || f.length < 3)
                    continue;
                if (f[1] == "broken" || f[1] == "suspect")
                    issues[f[0]] = f[2];
            }
            return;
        }
    }

    public string known_issue (string id) {
        return issues.has_key (id) ? issues[id] : "";
    }

    private void load_tiers () {
        string data;
        try {
            FileUtils.get_contents (TIERS, out data);
        } catch (FileError e) {
            return;
        }
        foreach (unowned string line in data.split ("\n")) {
            if (line == "" || line.has_prefix ("#"))
                continue;
            string[] f = line.split ("\t");
            if (f.length >= 2 && (f[1] == "weak" || f[1] == "mid" || f[1] == "strong" || f[1] == "igpu" || f[1] == "discrete"))
                tiers_loaded = true;
        }
    }

    // Run the launcher and parse its JSON output; null when it cannot run.
    private static Json.Node? launcher_json (string sub) {
        try {
            var proc = new Subprocess (SubprocessFlags.STDOUT_PIPE | SubprocessFlags.STDERR_SILENCE,
                                       LAUNCHER, sub, "--json");
            string? out_text = null;
            proc.communicate_utf8 (null, null, out out_text, null);
            if (!proc.get_successful () || out_text == null)
                return null;
            var parser = new Json.Parser ();
            parser.load_from_data (out_text);
            return parser.get_root ();
        } catch (Error e) {
            return null;
        }
    }

    // Read flagged hacks and the display GPU class from the launcher.
    private void load_launcher_state () {
        string cls = "weak";
        double ms = 0;
        var status = launcher_json ("status");
        if (status != null && status.get_node_type () == Json.NodeType.OBJECT) {
            var o = status.get_object ();
            if (o.has_member ("gpu_class") && !o.get_null_member ("gpu_class"))
                cls = o.get_string_member ("gpu_class");
            if (o.has_member ("gpu_class_score_ms") && !o.get_null_member ("gpu_class_score_ms"))
                ms = o.get_double_member ("gpu_class_score_ms");
        }
        string name = cls == "" ? "Weak" : cls.substring (0, 1).up () + cls.substring (1);
        gpu_class_text = ms > 0 ? "%s (%.1f ms on the calibration test)".printf (name, ms) : name;

        var list = launcher_json ("list");
        if (list == null)
            return;
        Json.Array? items = null;
        if (list.get_node_type () == Json.NodeType.ARRAY)
            items = list.get_array ();
        else if (list.get_node_type () == Json.NodeType.OBJECT && list.get_object ().has_member ("hacks"))
            items = list.get_object ().get_array_member ("hacks");
        if (items == null)
            return;
        items.foreach_element ((arr, i, node) => {
            if (node.get_node_type () != Json.NodeType.OBJECT)
                return;
            var h = node.get_object ();
            if (!h.has_member ("id"))
                return;
            string id = h.get_string_member ("id");
            if (h.has_member ("flagged") && h.get_boolean_member ("flagged"))
                flagged_ids.add (id);
            if (h.has_member ("expect") && !h.get_null_member ("expect"))
                expects[id] = h.get_string_member ("expect");
        });
    }

    public bool is_available () {
        return settings != null
            && !catalog.is_empty
            && Environment.find_program_in_path (LAUNCHER) != null
            && FileUtils.test (IDLED, FileTest.IS_EXECUTABLE);
    }

    public Gee.List<ScreensaverHack> hacks () {
        return catalog;
    }

    public string mode {
        owned get { return settings.get_string ("mode"); }
        set { settings.set_string ("mode", value); }
    }

    public string hack_id {
        owned get { return settings.get_string ("hack-id"); }
        set { settings.set_string ("hack-id", value); }
    }

    public string[] enabled_hacks {
        owned get { return settings.get_strv ("random-hacks"); }
        set { settings.set_strv ("random-hacks", value); }
    }

    public Gee.List<ScreensaverOption> options_for (string hack_id) {
        var list = new Gee.ArrayList<ScreensaverOption> ();
        string short_id = hack_id.has_suffix ("_gles3") ? hack_id.substring (0, hack_id.length - 6) : hack_id;
        string data = null;
        foreach (string stem in new string[] { hack_id, short_id }) {
            try {
                FileUtils.get_contents (Path.build_filename (OPTIONS_DIR, stem + ".tsv"), out data);
                break;
            } catch (FileError e) {
                data = null;
            }
        }
        if (data == null)
            return list;
        foreach (unowned string line in data.split ("\n")) {
            if (line == "" || line.has_prefix ("#"))
                continue;
            string[] f = line.split ("\t");
            if (f.length < 9)
                continue;
            if (f[1] != "bool" && f[1] != "int" && f[1] != "float" && f[1] != "enum" && f[1] != "string")
                continue;
            string[] choices = f[5] == "" ? new string[0] : f[5].split (",");
            list.add (new ScreensaverOption (f[0], f[1], f[2], f[3], f[4], choices,
                                             f[6] == "" ? f[0] : f[6], f[7], f[8] == "" ? "General" : f[8]));
        }
        return list;
    }

    private Variant read_options () {
        return settings.get_value ("hack-options");
    }

    public string option_value (string hack_id, string name) {
        Variant? inner = read_options ().lookup_value (hack_id, new VariantType ("a{ss}"));
        if (inner != null) {
            string? value = null;
            if (inner.lookup ("{ss}", name, out value) && value != null)
                return value;
        }
        foreach (var option in options_for (hack_id))
            if (option.name == name)
                return option.default_value;
        return "";
    }

    public void set_option (string hack_id, string name, string value) {
        var outer = new VariantBuilder (new VariantType ("a{sa{ss}}"));
        Variant all = read_options ();
        bool wrote = false;
        for (size_t i = 0; i < all.n_children (); i++) {
            Variant entry = all.get_child_value (i);
            string id = entry.get_child_value (0).get_string ();
            Variant inner = entry.get_child_value (1);
            var b = new VariantBuilder (new VariantType ("a{ss}"));
            bool seen = false;
            for (size_t j = 0; j < inner.n_children (); j++) {
                Variant kv = inner.get_child_value (j);
                string k = kv.get_child_value (0).get_string ();
                if (id == hack_id && k == name) {
                    b.add ("{ss}", k, value);
                    seen = true;
                } else {
                    b.add ("{ss}", k, kv.get_child_value (1).get_string ());
                }
            }
            if (id == hack_id) {
                if (!seen)
                    b.add ("{ss}", name, value);
                wrote = true;
            }
            outer.add ("{s@a{ss}}", id, b.end ());
        }
        if (!wrote) {
            var b = new VariantBuilder (new VariantType ("a{ss}"));
            b.add ("{ss}", name, value);
            outer.add ("{s@a{ss}}", hack_id, b.end ());
        }
        settings.set_value ("hack-options", outer.end ());
    }

    public void reset_options (string hack_id) {
        var outer = new VariantBuilder (new VariantType ("a{sa{ss}}"));
        Variant all = read_options ();
        for (size_t i = 0; i < all.n_children (); i++) {
            Variant entry = all.get_child_value (i);
            if (entry.get_child_value (0).get_string () != hack_id)
                outer.add ("{s@a{ss}}", entry.get_child_value (0).get_string (), entry.get_child_value (1));
        }
        settings.set_value ("hack-options", outer.end ());
    }

    public bool has_tiers {
        get { return tiers_loaded; }
    }

    public bool has_flags {
        get { return !flagged_ids.is_empty; }
    }

    public bool is_flagged (string id) {
        return flagged_ids.contains (id);
    }

    public string expectation (string id) {
        return expects.has_key (id) ? expects[id] : "";
    }

    public bool show_all {
        get { return settings.get_boolean ("show-all-hacks"); }
        set { settings.set_boolean ("show-all-hacks", value); }
    }

    public string gpu_class_label {
        owned get { return gpu_class_text; }
    }

    public string render_quality {
        owned get {
            string mode = settings.get_string ("render-scale-mode");
            double scale = settings.get_double ("render-scale");
            int height = settings.get_int ("max-render-height");
            if (mode == "auto" && scale == 1.0 && height == 0)
                return "auto";
            if (mode == "fixed" && scale == 1.0 && height == 0)
                return "high";
            if (mode == "fixed" && scale == 0.75 && height == 0)
                return "balanced";
            if (mode == "fixed" && scale == 0.5 && height == 1080)
                return "fast";
            return "custom";
        }
        set {
            switch (value) {
            case "auto": apply_render ("auto", 1.0, 0); break;
            case "high": apply_render ("fixed", 1.0, 0); break;
            case "balanced": apply_render ("fixed", 0.75, 0); break;
            case "fast": apply_render ("fixed", 0.5, 1080); break;
            default: break;
            }
        }
    }

    private void apply_render (string mode, double scale, int height) {
        settings.set_string ("render-scale-mode", mode);
        settings.set_double ("render-scale", scale);
        settings.set_int ("max-render-height", height);
    }

    public int max_render_height {
        get { return settings.get_int ("max-render-height"); }
        set { settings.set_int ("max-render-height", value); }
    }

    public string pool_class {
        owned get {
            string v = settings.get_string ("pool-gpu-class");
            return v == "igpu-only" ? "weak" : v;
        }
        set { settings.set_string ("pool-gpu-class", value); }
    }

    public int start_delay {
        get { return settings.get_int ("hack-idle-delay"); }
        set { settings.set_int ("hack-idle-delay", value); }
    }

    public int rotate_delay {
        get { return settings.get_int ("cycle-delay"); }
        set { settings.set_int ("cycle-delay", value); }
    }

    public bool lock_supported {
        get { return lock_settings != null; }
    }

    public bool lock_enabled {
        get { return lock_settings.get_boolean ("lock-enabled"); }
        set { lock_settings.set_boolean ("lock-enabled", value); }
    }

    public int lock_delay {
        get { return lock_settings.get_int ("idle-delay"); }
        set { lock_settings.set_int ("idle-delay", value); }
    }

    public bool lock_on_suspend {
        get { return lock_settings.get_boolean ("lock-on-suspend"); }
        set { lock_settings.set_boolean ("lock-on-suspend", value); }
    }

    public int display_off_delay {
        get { return settings.get_int ("display-off-delay"); }
        set { settings.set_int ("display-off-delay", value); }
    }

    public string? color_hack {
        get { return "blackhole_gles3"; }
    }

    public string[] color_ids {
        owned get { return { "stylized", "kipthorne", "faithful", "singularity", "slingshot", "whitehole", "eht" }; }
    }

    public string[] color_labels {
        owned get { return { "Stylized", "Kip Thorne (blackbody)", "Faithful (blackbody with Doppler shift)", "Singularity", "Slingshot", "White hole", "Event Horizon Telescope (orange ring)" }; }
    }

    public string color_mode {
        owned get { return settings.get_string ("blackhole-color-mode"); }
        set { settings.set_string ("blackhole-color-mode", value); }
    }

    public bool gpu_offload_supported {
        get {
            string modules;
            try {
                FileUtils.get_contents ("/proc/modules", out modules);
            } catch (FileError e) {
                return false;
            }
            if (!modules.contains ("nvidia "))
                return false;
            var drivers = new Gee.HashSet<string> ();
            for (int i = 0; i < 8; i++) {
                string? target = null;
                try {
                    target = FileUtils.read_link ("/sys/class/drm/card%d/device/driver".printf (i));
                } catch (FileError e) {
                    continue;
                }
                drivers.add (Path.get_basename (target));
            }
            return drivers.size >= 2;
        }
    }

    public string gpu_offload {
        owned get { return settings.get_string ("gpu-offload"); }
        set { settings.set_string ("gpu-offload", value); }
    }

    public void preview (string id) {
        try {
            Process.spawn_async (null, { LAUNCHER, "preview", id, "--seconds", "30" }, null,
                                 SpawnFlags.SEARCH_PATH | SpawnFlags.STDOUT_TO_DEV_NULL | SpawnFlags.STDERR_TO_DEV_NULL,
                                 null, null);
        } catch (SpawnError e) {
            warning ("screensaver preview failed: %s", e.message);
        }
    }

    public void stop_preview () {
        try {
            Process.spawn_async (null, { LAUNCHER, "stop" }, null,
                                 SpawnFlags.SEARCH_PATH | SpawnFlags.STDOUT_TO_DEV_NULL | SpawnFlags.STDERR_TO_DEV_NULL,
                                 null, null);
        } catch (SpawnError e) {
            warning ("screensaver stop failed: %s", e.message);
        }
    }
}
