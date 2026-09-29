public ScreensaverBackend create_screensaver_backend () {
    return new NczScreensaverBackend ();
}

public class NczScreensaverBackend : Object, ScreensaverBackend {
    private const string SCHEMA = "dev.ncz.screensaver";
    private const string LOCK_SCHEMA = "dev.sinty.lockscreen";
    private const string CATALOG = "/usr/share/ncz-screensavers/hacks.tsv";
    private const string LAUNCHER = "ncz-screensaver";
    private const string IDLED = "/usr/libexec/ncz-screensaver-idled";

    private Settings? settings;
    private Settings? lock_settings;
    private Gee.ArrayList<ScreensaverHack> catalog = new Gee.ArrayList<ScreensaverHack> ();

    public NczScreensaverBackend () {
        settings = open (SCHEMA);
        lock_settings = open (LOCK_SCHEMA);
        if (settings != null) {
            settings.changed.connect (() => changed ());
            load_catalog ();
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
        owned get { return { "stylized", "kipthorne", "faithful" }; }
    }

    public string[] color_labels {
        owned get { return { "Stylized", "Kip Thorne (blackbody)", "Faithful (blackbody with Doppler shift)" }; }
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
