#define _POSIX_C_SOURCE 200809L
#include "ncz_harness_cfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NCZ_VERSION
#define NCZ_VERSION "dev"
#endif

static const ncz_opt_def GEN_OPTS[] = {
 {"render-scale", NCZ_OPT_FLOAT, "1", 0.25, 1, "", "", "NCZ_RENDER_SCALE",
  "Render scale", "Fraction of the output resolution the shader renders at; the compositor upscales it. 1 = native.", "Render"},
 {"max-render-height", NCZ_OPT_INT, "0", 0, 8640, "", "", "NCZ_MAX_RENDER_HEIGHT",
  "Max render height", "Cap the render height in pixels (aspect ratio kept); 0 = unlimited. When not set at all, the platform default applies: 1080 on Mali, and on integrated Intel above 1440 lines; unlimited elsewhere.", "Render"},
 {"render-scale-mode", NCZ_OPT_ENUM, "auto", 0, 0, "auto,fixed", "", "NCZ_RENDER_SCALE_MODE",
  "Render scale mode", "auto: step the scale down (1, 0.75, 0.5) if the 95th percentile frame time stays above 40 ms for 3 s, never back up. fixed: keep the configured size.", "Render"},
};
#define GEN_N (sizeof GEN_OPTS / sizeof GEN_OPTS[0])

static ncz_opts g_o;
static int g_ready;
static const ncz_opt_def *g_merged;

const ncz_opts *ncz_harness_opts(void) { return g_ready ? &g_o : NULL; }
double ncz_cfg_render_scale(void) { return g_ready ? ncz_opts_get_float(&g_o, "render-scale") : 1.0; }
int ncz_cfg_max_render_height(int *explicit_set) {
    if (!g_ready) { if (explicit_set) *explicit_set = 0; return 0; }
    if (explicit_set) *explicit_set = ncz_opts_is_set(&g_o, "max-render-height");
    return (int)ncz_opts_get_int(&g_o, "max-render-height");
}
int ncz_cfg_scale_auto(void) { return g_ready ? !strcmp(ncz_opts_get(&g_o, "render-scale-mode"), "auto") : 1; }

static void dump_schema(const ncz_opt_def *defs, size_t n, const char *prefix) {
    for (size_t k = 0; k < n; k++) {
        const ncz_opt_def *d = &defs[k];
        const char *t = d->type == NCZ_OPT_BOOL ? "bool" : d->type == NCZ_OPT_INT ? "int" :
                        d->type == NCZ_OPT_FLOAT ? "float" : d->type == NCZ_OPT_ENUM ? "enum" : "string";
        char mn[32] = "", mx[32] = "", en[128], *q = en;
        if (d->type == NCZ_OPT_INT || d->type == NCZ_OPT_FLOAT) {
            snprintf(mn, sizeof mn, "%g", d->min);
            snprintf(mx, sizeof mx, "%g", d->max);
        }
        for (const char *p = prefix; *p && q < en + 60; p++) *q++ = *p;
        for (const char *p = d->name; *p && q < en + 120; p++)
            *q++ = *p == '-' ? '_' : (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p);
        *q = 0;
        printf("%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", d->name, t, d->def, mn, mx,
               (d->type == NCZ_OPT_ENUM || d->type == NCZ_OPT_LIST) ? d->choices : "",
               d->label, d->desc, d->group, en);
    }
}

int ncz_harness_cfg_init(int argc, char **argv) {
    const ncz_opt_def *hack = NULL;
    size_t hn = 0;
    const char *prefix = "NCZ_", *group = "render";
    if (ncz_hack_options) {
        const char *hp = NULL, *hg = NULL;
        hack = ncz_hack_options(&hn, &hp, &hg);
        if (hp) prefix = hp;
        if (hg) group = hg;
    }
    const char *prog = (argc > 0 && argv[0]) ? argv[0] : "hack";
    /* schema dumps first: --dump-schema (hack options) / --dump-schema-common */
    for (int i = 1; i < argc; i++) {
        if (argv[i] && !strcmp(argv[i], "--dump-schema")) { if (hack) dump_schema(hack, hn, prefix); return 0; }
        if (argv[i] && !strcmp(argv[i], "--dump-schema-common")) { dump_schema(GEN_OPTS, GEN_N, "NCZ_"); return 0; }
        if (argv[i] && !strcmp(argv[i], "--version")) { printf("%s (ncz-screensavers %s)\n", prog, NCZ_VERSION); return 0; }
    }
    ncz_opt_def *m = calloc(GEN_N + hn, sizeof *m);
    if (!m) return -1;
    memcpy(m, GEN_OPTS, sizeof GEN_OPTS);
    if (hack) memcpy(m + GEN_N, hack, hn * sizeof *m);
    g_merged = m;
    if (ncz_opts_init(&g_o, m, GEN_N + hn) != 0) return -1;
    /* generic render options may come from ~/.config/ncz-screensavers/render.conf */
    {
        const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
        char path[512] = "";
        if (x && *x) snprintf(path, sizeof path, "%s/ncz-screensavers/render.conf", x);
        else if (h && *h) snprintf(path, sizeof path, "%s/.config/ncz-screensavers/render.conf", h);
        if (*path) ncz_opts_apply_keyfile(&g_o, path, "render", "render config file");
    }
    int flags = ncz_opts_resolve(&g_o, prefix, group, NULL, argc, argv, prog, NULL);
    g_ready = 1;
    if (flags & 12) {
        fprintf(stderr, "%s: unknown option or argument (see --help)\n", prog);
        return 2;
    }
    if (flags & 1) { ncz_opts_print_help(&g_o, stdout, prefix, prog); return 0; }
    if (flags & 2) { ncz_opts_print_list(&g_o, stdout, prefix); return 0; }
    return -1;
}
