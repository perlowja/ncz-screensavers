using Gtk;
using Singularity;
using Singularity.Widgets;
using Peas;

[ModuleInit]
public void peas_register_types (TypeModule module) {
    var objmodule = module as Peas.ObjectModule;
    objmodule.register_extension_type (typeof (Singularity.Plugin), typeof (ScreensaverPlugin));
}

public class ScreensaverPlugin : Object, Singularity.Plugin {
    private ScreensaverBackend backend = create_screensaver_backend ();

    public void activate (PluginContext context) {
    }

    public void deactivate () {
    }

    public Gtk.Widget? get_settings_widget () {
        if (!backend.is_available ())
            return new ScreensaverUnavailable ();
        return new ScreensaverSettings (backend);
    }
}

public class ScreensaverUnavailable : Gtk.Box {
    public ScreensaverUnavailable () {
        Object (orientation: Orientation.VERTICAL, spacing: 0);
        var page = new StatusPage ();
        page.title = "Screensaver is not installed";
        page.description = "Install the screensaver package to configure it here.";
        page.icon_name = "preferences-desktop-screensaver-symbolic";
        append (page);
    }
}

public class ScreensaverSettings : Gtk.Box {
    private const string[] MODE_IDS = { "off", "one", "random" };
    private const string[] MODE_LABELS = { "Off", "One screensaver", "Random rotation" };
    private const string[] GPU_IDS = { "off", "prime", "auto" };
    private const string[] GPU_LABELS = { "Same GPU as the desktop", "Discrete GPU (PRIME offload)", "Automatic" };

    private ScreensaverBackend backend;
    private bool refreshing = false;
    private SelectionRow mode_row;
    private SelectionRow hack_row;
    private SpinRow start_row;
    private SpinRow rotate_row;
    private SwitchRow lock_row;
    private SpinRow lock_delay_row;
    private SwitchRow suspend_row;
    private SpinRow display_row;
    private SelectionRow? color_row = null;
    private SelectionRow? gpu_row = null;
    private Gee.HashMap<string, SwitchRow> pool_rows = new Gee.HashMap<string, SwitchRow> ();
    private Gee.HashMap<string, string> hack_by_label = new Gee.HashMap<string, string> ();

    public ScreensaverSettings (ScreensaverBackend backend) {
        Object (orientation: Orientation.VERTICAL, spacing: 12);
        this.backend = backend;
        margin_top = 12;
        margin_bottom = 12;
        margin_start = 12;
        margin_end = 12;

        build_screensaver_group ();
        if (backend.lock_supported)
            build_lock_group ();
        build_display_group ();
        if (backend.color_hack != null)
            build_color_group ();
        if (backend.gpu_offload_supported)
            build_gpu_group ();
        build_hack_groups ();

        backend.changed.connect (refresh);
        refresh ();
    }

    private static int minutes (int seconds) {
        return (seconds + 59) / 60;
    }

    private static int index_of (string[] ids, string id) {
        for (int i = 0; i < ids.length; i++)
            if (ids[i] == id)
                return i;
        return -1;
    }

    private void build_screensaver_group () {
        var group = new PreferencesGroup ("Screensaver", "Runs after the desktop has been idle");
        append (group);

        mode_row = new SelectionRow ("Mode", MODE_LABELS, MODE_LABELS[0]);
        mode_row.selected.connect ((label) => {
            if (refreshing)
                return;
            int i = index_of (MODE_LABELS, label);
            if (i >= 0)
                backend.mode = MODE_IDS[i];
        });
        group.add_row (mode_row);

        string[] labels = {};
        foreach (var hack in backend.hacks ()) {
            string label = hack.title;
            if (hack_by_label.has_key (label))
                label = "%s (%s)".printf (hack.title, hack.group_name);
            hack_by_label[label] = hack.id;
            labels += label;
        }
        hack_row = new SelectionRow ("Screensaver", labels, labels.length > 0 ? labels[0] : "");
        hack_row.selected.connect ((label) => {
            if (!refreshing && hack_by_label.has_key (label))
                backend.hack_id = hack_by_label[label];
        });
        group.add_row (hack_row);

        start_row = new SpinRow ("Start after (minutes)", "Idle time before the screensaver starts", 1, 240, 1, 5);
        start_row.spin_btn.value_changed.connect (() => {
            if (!refreshing)
                backend.start_delay = (int) start_row.spin_btn.value * 60;
        });
        group.add_row (start_row);

        rotate_row = new SpinRow ("Change every (minutes)", "How long each screensaver runs in random mode", 1, 240, 1, 10);
        rotate_row.spin_btn.value_changed.connect (() => {
            if (!refreshing)
                backend.rotate_delay = (int) rotate_row.spin_btn.value * 60;
        });
        group.add_row (rotate_row);
    }

    private void build_lock_group () {
        var group = new PreferencesGroup ("Lock screen");
        append (group);

        lock_row = new SwitchRow ("Lock when idle");
        lock_row.switch_btn.notify["active"].connect (() => {
            if (!refreshing)
                backend.lock_enabled = lock_row.switch_btn.active;
            lock_delay_row.sensitive = lock_row.switch_btn.active;
        });
        group.add_row (lock_row);

        lock_delay_row = new SpinRow ("Lock after (minutes)", "Idle time before the screen locks", 1, 240, 1, 5);
        lock_delay_row.spin_btn.value_changed.connect (() => {
            if (!refreshing)
                backend.lock_delay = (int) lock_delay_row.spin_btn.value * 60;
        });
        group.add_row (lock_delay_row);

        suspend_row = new SwitchRow ("Lock on suspend");
        suspend_row.switch_btn.notify["active"].connect (() => {
            if (!refreshing)
                backend.lock_on_suspend = suspend_row.switch_btn.active;
        });
        group.add_row (suspend_row);
    }

    private void build_display_group () {
        var group = new PreferencesGroup ("Display");
        append (group);

        display_row = new SpinRow ("Turn display off after (minutes)", "0 keeps the display on", 0, 240, 1, 0);
        display_row.spin_btn.value_changed.connect (() => {
            if (!refreshing)
                backend.display_off_delay = (int) display_row.spin_btn.value * 60;
        });
        group.add_row (display_row);
    }

    private void build_color_group () {
        var group = new PreferencesGroup ("Color model");
        append (group);

        color_row = new SelectionRow ("Color model", backend.color_labels, backend.color_labels[0]);
        color_row.selected.connect ((label) => {
            if (refreshing)
                return;
            int i = index_of (backend.color_labels, label);
            if (i >= 0)
                backend.color_mode = backend.color_ids[i];
        });
        group.add_row (color_row);

        var preview = new ActionRow ("Preview", "Runs the screensaver for 30 seconds");
        var button = new Button.with_label ("Preview");
        button.valign = Align.CENTER;
        button.clicked.connect (() => backend.preview (backend.color_hack));
        preview.add_suffix (button);
        group.add_row (preview);
    }

    private void build_gpu_group () {
        var group = new PreferencesGroup ("Graphics", "Only the screensaver process moves to the discrete GPU");
        append (group);

        gpu_row = new SelectionRow ("Screensaver GPU", GPU_LABELS, GPU_LABELS[0]);
        gpu_row.selected.connect ((label) => {
            if (refreshing)
                return;
            int i = index_of (GPU_LABELS, label);
            if (i >= 0)
                backend.gpu_offload = GPU_IDS[i];
        });
        group.add_row (gpu_row);
    }

    private void build_hack_groups () {
        var by_group = new Gee.HashMap<string, PreferencesGroup> ();
        foreach (var hack in backend.hacks ()) {
            PreferencesGroup? group = by_group.has_key (hack.group_name) ? by_group[hack.group_name] : null;
            if (group == null) {
                group = new PreferencesGroup (hack.group_name, "Screensavers used in random mode");
                by_group[hack.group_name] = group;
                append (group);
            }

            string id = hack.id;
            var row = new SwitchRow (hack.title);
            row.switch_btn.notify["active"].connect (() => {
                if (!refreshing)
                    set_hack_enabled (id, row.switch_btn.active);
            });
            var button = new Button.with_label ("Preview");
            button.valign = Align.CENTER;
            button.clicked.connect (() => backend.preview (id));
            row.add_suffix (button);
            group.add_row (row);
            pool_rows[id] = row;
        }
    }

    private void set_hack_enabled (string id, bool enabled) {
        var all = new Gee.ArrayList<string> ();
        foreach (var hack in backend.hacks ())
            all.add (hack.id);
        var current = new Gee.ArrayList<string> ();
        foreach (var e in backend.enabled_hacks)
            current.add (e);
        if (current.is_empty)
            current.add_all (all);
        if (enabled && !current.contains (id))
            current.add (id);
        if (!enabled)
            current.remove (id);
        if (current.size == all.size)
            current.clear ();
        backend.enabled_hacks = current.to_array ();
    }

    private void refresh () {
        refreshing = true;

        string mode = backend.mode;
        int mi = index_of (MODE_IDS, mode);
        mode_row.current_value = mi >= 0 ? MODE_LABELS[mi] : MODE_LABELS[2];
        hack_row.visible = mode == "one";
        rotate_row.visible = mode != "one";
        foreach (var hack in backend.hacks ()) {
            if (hack.id == backend.hack_id) {
                foreach (var entry in hack_by_label.entries)
                    if (entry.value == hack.id)
                        hack_row.current_value = entry.key;
            }
        }
        start_row.spin_btn.value = minutes (backend.start_delay);
        rotate_row.spin_btn.value = minutes (backend.rotate_delay);
        if (backend.lock_supported) {
            lock_row.switch_btn.active = backend.lock_enabled;
            lock_delay_row.spin_btn.value = minutes (backend.lock_delay);
            lock_delay_row.sensitive = lock_row.switch_btn.active;
            suspend_row.switch_btn.active = backend.lock_on_suspend;
        }
        display_row.spin_btn.value = minutes (backend.display_off_delay);
        if (color_row != null) {
            int ci = index_of (backend.color_ids, backend.color_mode);
            color_row.current_value = backend.color_labels[ci >= 0 ? ci : 0];
        }
        if (gpu_row != null) {
            int gi = index_of (GPU_IDS, backend.gpu_offload);
            gpu_row.current_value = GPU_LABELS[gi >= 0 ? gi : 0];
        }

        var enabled = new Gee.HashSet<string> ();
        foreach (var e in backend.enabled_hacks)
            enabled.add (e);
        foreach (var entry in pool_rows.entries)
            entry.value.switch_btn.active = enabled.is_empty || enabled.contains (entry.key);

        refreshing = false;
    }
}
