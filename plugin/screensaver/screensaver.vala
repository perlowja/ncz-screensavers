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

public class OptionBinding : Object {
    private string hack_id;
    private ScreensaverOption option;
    private Gtk.Widget row;

    public OptionBinding (string hack_id, ScreensaverOption option, Gtk.Widget row) {
        this.hack_id = hack_id;
        this.option = option;
        this.row = row;
    }

    public void refresh (ScreensaverBackend backend) {
        string value = backend.option_value (hack_id, option.name);
        if (row is SwitchRow)
            ((SwitchRow) row).switch_btn.active = value == "true";
        else if (row is SelectionRow)
            ((SelectionRow) row).current_value = value;
        else if (row is SpinRow)
            ((SpinRow) row).spin_btn.value = double.parse (value);
        else if (row is EntryRow)
            ((EntryRow) row).text = value;
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
    private const string[] GPU_IDS = { "auto", "prime", "off" };
    private const string[] GPU_LABELS = { "Automatic (discrete on AC)", "Always the discrete GPU", "Same GPU as the desktop" };
    private const string[] RENDER_IDS = { "auto", "high", "balanced", "fast", "custom" };
    private const string[] RENDER_LABELS = { "Auto (recommended)", "High (native resolution)", "Balanced (75%)", "Fast (50%, 1080p cap)", "Custom" };
    private const string[] POOL_IDS = { "auto", "weak", "mid", "all" };
    private const string[] POOL_LABELS = {
        "Automatic",
        "Only screensavers that run well on weak graphics chips",
        "Also screensavers that need a mid-range GPU",
        "All screensavers"
    };

    private ScreensaverBackend backend;
    private bool refreshing = false;
    private SelectionRow mode_row;
    private SelectionRow hack_row;
    private SpinRow start_row;
    private SpinRow rotate_row;
    private SelectionRow pool_row;
    private PreferencesGroup? flagged_group = null;
    private Box hack_box = new Box (Orientation.VERTICAL, 12);
    private SwitchRow? show_all_row = null;
    private SelectionRow render_row;
    private SpinRow render_height_row;
    private SwitchRow lock_row;
    private SpinRow lock_delay_row;
    private SwitchRow suspend_row;
    private SpinRow display_row;
    private SelectionRow? color_row = null;
    private SelectionRow? gpu_row = null;
    private Gee.ArrayList<OptionBinding> option_bindings = new Gee.ArrayList<OptionBinding> ();
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
        build_render_group ();
        if (backend.color_hack != null && backend.options_for (backend.color_hack).is_empty)
            build_color_group ();
        if (backend.gpu_offload_supported)
            build_gpu_group ();
        append (hack_box);
        build_hack_groups ();
        build_option_groups ();

        backend.flags_changed.connect (rebuild_hack_groups);
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

        pool_row = new SelectionRow ("Screensavers used in random mode", POOL_LABELS, POOL_LABELS[0]);
        pool_row.selected.connect ((label) => {
            if (refreshing)
                return;
            int i = index_of (POOL_LABELS, label);
            if (i >= 0)
                backend.pool_class = POOL_IDS[i];
        });
        group.add_row (pool_row);

        // Read-only: class of the graphics chip driving the display
        if (backend.gpu_class_label != "") {
            var info = new ActionRow ("Graphics chip class", backend.gpu_class_label);
            group.add_row (info);
        }
    }

    private void build_lock_group () {
        var group = new PreferencesGroup ("Lock screen");
        append (group);

        lock_row = new SwitchRow ("Lock after the screensaver");
        lock_row.switch_btn.notify["active"].connect (() => {
            if (!refreshing)
                backend.lock_enabled = lock_row.switch_btn.active;
            lock_delay_row.sensitive = lock_row.switch_btn.active;
        });
        group.add_row (lock_row);

        lock_delay_row = new SpinRow ("Lock after (minutes)", "Minutes after the screensaver starts; 0 locks at once", 0, 240, 1, 1);
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

    private void build_render_group () {
        var group = new PreferencesGroup ("Render quality", "Lower quality renders shader screensavers smaller: less GPU load and less stutter on 4K screens");
        append (group);

        render_row = new SelectionRow ("Render quality", RENDER_LABELS, RENDER_LABELS[0]);
        render_row.selected.connect ((label) => {
            if (refreshing)
                return;
            int i = index_of (RENDER_LABELS, label);
            if (i >= 0)
                backend.render_quality = RENDER_IDS[i];
        });
        group.add_row (render_row);

        render_height_row = new SpinRow ("Maximum render height (advanced)", "Pixels; 0 lets Auto decide (choose High for no cap)", 0, 8192, 10, 0);
        render_height_row.spin_btn.value_changed.connect (() => {
            if (!refreshing)
                backend.max_render_height = (int) render_height_row.spin_btn.value;
        });
        group.add_row (render_height_row);
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

    // The flagged set depends on the GPU offload and pool settings: lay the list out again.
    private void rebuild_hack_groups () {
        Widget? child = hack_box.get_first_child ();
        while (child != null) {
            Widget? next = child.get_next_sibling ();
            hack_box.remove (child);
            child = next;
        }
        pool_rows.clear ();
        show_all_row = null;
        flagged_group = null;
        build_hack_groups ();
        refresh ();
    }

    private void build_hack_groups () {
        var group_order = new Gee.ArrayList<string> ();
        foreach (var hack in backend.hacks ()) {
            if (!group_order.contains (hack.group_name))
                group_order.add (hack.group_name);
        }
        var sorted = new Gee.ArrayList<ScreensaverHack> ();
        sorted.add_all (backend.hacks ());
        sorted.sort ((a, b) => {
            int ga = group_order.index_of (a.group_name);
            int gb = group_order.index_of (b.group_name);
            return ga != gb ? ga - gb : strcmp (a.title.down (), b.title.down ());
        });

        if (backend.has_tiers && backend.has_flags) {
            // Two groups; the flagged one stays hidden until "Show all" is on
            show_all_row = new SwitchRow ("Show all screensavers",
                                          "Also list screensavers that may run poorly on this graphics chip");
            show_all_row.switch_btn.notify["active"].connect (() => {
                if (refreshing)
                    return;
                backend.show_all = show_all_row.switch_btn.active;
                flagged_group.visible = show_all_row.switch_btn.active;
            });
            var switch_group = new PreferencesGroup ();
            switch_group.add_row (show_all_row);
            hack_box.append (switch_group);

            var good = new PreferencesGroup ("Works well on this graphics chip", "Screensavers used in random mode");
            flagged_group = new PreferencesGroup ("May run poorly on this graphics chip", "Screensavers used in random mode");
            hack_box.append (good);
            hack_box.append (flagged_group);
            foreach (var hack in sorted)
                add_hack_row (backend.is_flagged (hack.id) ? flagged_group : good, hack);
            return;
        }

        var by_title = new Gee.HashMap<string, PreferencesGroup> ();
        foreach (var hack in sorted) {
            PreferencesGroup? group = by_title.has_key (hack.group_name) ? by_title[hack.group_name] : null;
            if (group == null) {
                group = new PreferencesGroup (hack.group_name, "Screensavers used in random mode");
                by_title[hack.group_name] = group;
                hack_box.append (group);
            }
            add_hack_row (group, hack);
        }
    }

    private void build_option_groups () {
        foreach (var hack in backend.hacks ()) {
            var options = backend.options_for (hack.id);
            if (options.is_empty)
                continue;
            var by_group = new Gee.HashMap<string, PreferencesGroup> ();
            var order = new Gee.ArrayList<string> ();
            foreach (var option in options) {
                if (!by_group.has_key (option.group_name)) {
                    by_group[option.group_name] = new PreferencesGroup ("%s - %s".printf (hack.title, option.group_name));
                    order.add (option.group_name);
                    append (by_group[option.group_name]);
                }
                by_group[option.group_name].add_row (make_option_row (hack.id, option));
            }
            var actions = new ActionRow ("%s options".printf (hack.title), "Changes apply the next time it starts");
            var reset = new Button.with_label ("Reset to defaults");
            reset.valign = Align.CENTER;
            reset.clicked.connect (() => {
                backend.reset_options (hack.id);
                refresh ();
            });
            var preview = new Button.with_label ("Preview");
            preview.valign = Align.CENTER;
            preview.clicked.connect (() => start_preview (hack.id));
            actions.add_suffix (reset);
            actions.add_suffix (preview);
            by_group[order[order.size - 1]].add_row (actions);
        }
    }

    private Gtk.Widget make_option_row (string hack_id, ScreensaverOption option) {
        string current = backend.option_value (hack_id, option.name);
        switch (option.kind) {
        case "bool":
            var row = new SwitchRow (option.label, option.description, current == "true");
            row.switch_btn.notify["active"].connect (() => {
                if (!refreshing)
                    backend.set_option (hack_id, option.name, row.switch_btn.active ? "true" : "false");
            });
            option_bindings.add (new OptionBinding (hack_id, option, row));
            return row;
        case "enum":
            var row = new SelectionRow (option.label, option.choices, current);
            row.selected.connect ((item) => {
                if (!refreshing)
                    backend.set_option (hack_id, option.name, item);
            });
            option_bindings.add (new OptionBinding (hack_id, option, row));
            return row;
        case "int":
        case "float":
            bool is_float = option.kind == "float";
            double lo = double.parse (option.min_value);
            double hi = double.parse (option.max_value);
            double step = is_float ? ((hi - lo) <= 10 ? 0.05 : 0.5) : 1;
            var row = new SpinRow (option.label, option.description, lo, hi, step, double.parse (current));
            if (is_float)
                row.spin_btn.digits = 2;
            row.spin_btn.value_changed.connect (() => {
                if (refreshing)
                    return;
                double v = row.spin_btn.value;
                backend.set_option (hack_id, option.name, is_float ? "%.2f".printf (v) : "%d".printf ((int) v));
            });
            option_bindings.add (new OptionBinding (hack_id, option, row));
            return row;
        default:
            var row = new EntryRow (option.label);
            row.text = current;
            row.entry_changed.connect (() => {
                if (!refreshing)
                    backend.set_option (hack_id, option.name, row.text);
            });
            option_bindings.add (new OptionBinding (hack_id, option, row));
            return row;
        }
    }

    private void add_hack_row (PreferencesGroup group, ScreensaverHack hack) {
        string id = hack.id;
        string issue = backend.known_issue (id);
        string? subtitle = backend.has_tiers ? hack.group_name : null;
        if (backend.is_flagged (id))
            subtitle = (subtitle != null ? subtitle + " - " : "") + warning_text (id);
        if (issue != "")
            subtitle = (subtitle != null ? subtitle + " - " : "") + "known issue: " + issue;
        var row = new SwitchRow (hack.title, subtitle);
        row.switch_btn.notify["active"].connect (() => {
            if (!refreshing)
                set_hack_enabled (id, row.switch_btn.active);
        });
        var button = new Button.with_label ("Preview");
        button.valign = Align.CENTER;
        button.clicked.connect (() => start_preview (id));
        row.add_suffix (button);
        group.add_row (row);
        pool_rows[id] = row;
    }

    private string warning_text (string id) {
        string expect = backend.expectation (id);
        return expect != "" ? "\u26a0 may stutter - " + expect : "\u26a0 may stutter on this graphics chip";
    }

    // Flagged hacks ask first; without a parent application, preview at once.
    private void start_preview (string id) {
        var window = get_root () as Gtk.Window;
        if (!backend.is_flagged (id) || window == null || window.application == null) {
            backend.preview (id);
            return;
        }
        var dialog = new ConfirmDialog (window.application, "Preview anyway?", "dialog-warning-symbolic",
                                        warning_text (id).replace ("\u26a0 ", "This screensaver may run poorly here: "),
                                        "Preview");
        dialog.transient_for = window;
        dialog.response.connect ((r) => {
            if (r == ConfirmDialog.Response.PRIMARY)
                backend.preview (id);
        });
        dialog.present ();
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
        pool_row.visible = mode != "one" && backend.has_tiers;
        foreach (var hack in backend.hacks ()) {
            if (hack.id == backend.hack_id) {
                foreach (var entry in hack_by_label.entries)
                    if (entry.value == hack.id)
                        hack_row.current_value = entry.key;
            }
        }
        int ri = index_of (RENDER_IDS, backend.render_quality);
        render_row.current_value = RENDER_LABELS[ri >= 0 ? ri : 0];
        render_height_row.spin_btn.value = backend.max_render_height;
        int pi = index_of (POOL_IDS, backend.pool_class);
        pool_row.current_value = POOL_LABELS[pi >= 0 ? pi : 0];
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

        if (show_all_row != null) {
            show_all_row.switch_btn.active = backend.show_all;
            flagged_group.visible = backend.show_all;
        }

        foreach (var binding in option_bindings)
            binding.refresh (backend);

        refreshing = false;
    }
}
