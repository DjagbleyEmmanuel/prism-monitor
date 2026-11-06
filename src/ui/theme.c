#include "ui/theme.h"

#include <stdio.h>
#include <string.h>

static const Theme themes[THEME_COUNT] = {
    {
        .id = "midnight",
        .name = "Midnight",
        .bg = {0.027, 0.039, 0.059},
        .bg2 = {0.043, 0.063, 0.094},
        .panel = {0.059, 0.078, 0.114},
        .panel2 = {0.047, 0.063, 0.094},
        .card = {0.067, 0.094, 0.137},
        .card2 = {0.051, 0.071, 0.106},
        .border = {0.110, 0.149, 0.204},
        .border_soft = {0.086, 0.118, 0.165},
        .text = {0.902, 0.929, 0.965},
        .dim = {0.545, 0.608, 0.690},
        .faint = {0.400, 0.462, 0.545},
        .accent = {0.298, 0.788, 0.941},
        .accent2 = {0.482, 0.380, 1.000},
        .green = {0.239, 0.863, 0.592},
        .yellow = {0.969, 0.788, 0.282},
        .red = {1.000, 0.361, 0.447},
        .blue = {0.302, 0.612, 1.000},
        .purple = {0.694, 0.549, 1.000},
        .cyan = {0.133, 0.827, 0.933},
        .orange = {1.000, 0.624, 0.263},
        .grid = {0.090, 0.118, 0.165},
        .track = {0.118, 0.157, 0.212},
        .light = false,
    },
    {
        .id = "aurora",
        .name = "Aurora",
        .bg = {0.024, 0.055, 0.047},
        .bg2 = {0.035, 0.075, 0.063},
        .panel = {0.047, 0.094, 0.078},
        .panel2 = {0.039, 0.078, 0.067},
        .card = {0.055, 0.110, 0.090},
        .card2 = {0.043, 0.086, 0.071},
        .border = {0.098, 0.180, 0.149},
        .border_soft = {0.078, 0.145, 0.122},
        .text = {0.886, 0.953, 0.929},
        .dim = {0.510, 0.639, 0.588},
        .faint = {0.372, 0.500, 0.452},
        .accent = {0.204, 0.827, 0.600},
        .accent2 = {0.133, 0.827, 0.933},
        .green = {0.204, 0.827, 0.600},
        .yellow = {0.933, 0.839, 0.416},
        .red = {1.000, 0.400, 0.451},
        .blue = {0.357, 0.671, 1.000},
        .purple = {0.655, 0.545, 0.980},
        .cyan = {0.133, 0.827, 0.933},
        .orange = {0.976, 0.671, 0.345},
        .grid = {0.075, 0.137, 0.114},
        .track = {0.102, 0.176, 0.145},
        .light = false,
    },
    {
        .id = "nord",
        .name = "Nord",
        .bg = {0.110, 0.122, 0.153},
        .bg2 = {0.145, 0.165, 0.204},
        .panel = {0.169, 0.192, 0.239},
        .panel2 = {0.157, 0.180, 0.224},
        .card = {0.192, 0.216, 0.267},
        .card2 = {0.165, 0.188, 0.235},
        .border = {0.259, 0.298, 0.361},
        .border_soft = {0.224, 0.259, 0.318},
        .text = {0.925, 0.937, 0.957},
        .dim = {0.647, 0.690, 0.753},
        .faint = {0.490, 0.540, 0.620},
        .accent = {0.533, 0.753, 0.816},
        .accent2 = {0.506, 0.631, 0.757},
        .green = {0.639, 0.745, 0.549},
        .yellow = {0.922, 0.796, 0.545},
        .red = {0.749, 0.380, 0.420},
        .blue = {0.506, 0.631, 0.757},
        .purple = {0.706, 0.557, 0.678},
        .cyan = {0.533, 0.753, 0.816},
        .orange = {0.816, 0.529, 0.439},
        .grid = {0.145, 0.165, 0.204},
        .track = {0.235, 0.271, 0.329},
        .light = false,
    },
    {
        .id = "dracula",
        .name = "Dracula",
        .bg = {0.051, 0.043, 0.078},
        .bg2 = {0.071, 0.059, 0.106},
        .panel = {0.090, 0.075, 0.129},
        .panel2 = {0.102, 0.086, 0.149},
        .card = {0.118, 0.098, 0.173},
        .card2 = {0.094, 0.078, 0.141},
        .border = {0.180, 0.149, 0.259},
        .border_soft = {0.149, 0.122, 0.220},
        .text = {0.969, 0.965, 0.984},
        .dim = {0.694, 0.663, 0.784},
        .faint = {0.520, 0.482, 0.630},
        .accent = {0.741, 0.576, 0.976},
        .accent2 = {1.000, 0.475, 0.776},
        .green = {0.314, 0.980, 0.482},
        .yellow = {0.945, 0.980, 0.549},
        .red = {1.000, 0.333, 0.333},
        .blue = {0.545, 0.914, 0.992},
        .purple = {0.741, 0.576, 0.976},
        .cyan = {0.545, 0.914, 0.992},
        .orange = {1.000, 0.722, 0.424},
        .grid = {0.086, 0.071, 0.125},
        .track = {0.173, 0.145, 0.243},
        .light = false,
    },
    {
        .id = "carbon",
        .name = "Carbon",
        .bg = {0.035, 0.035, 0.039},
        .bg2 = {0.055, 0.055, 0.059},
        .panel = {0.075, 0.075, 0.082},
        .panel2 = {0.067, 0.067, 0.075},
        .card = {0.094, 0.094, 0.102},
        .card2 = {0.078, 0.078, 0.086},
        .border = {0.157, 0.157, 0.169},
        .border_soft = {0.129, 0.129, 0.141},
        .text = {0.945, 0.945, 0.957},
        .dim = {0.612, 0.612, 0.643},
        .faint = {0.462, 0.462, 0.500},
        .accent = {1.000, 0.624, 0.263},
        .accent2 = {1.000, 0.361, 0.447},
        .green = {0.494, 0.906, 0.529},
        .yellow = {0.976, 0.824, 0.318},
        .red = {1.000, 0.361, 0.447},
        .blue = {0.388, 0.694, 1.000},
        .purple = {0.788, 0.620, 0.980},
        .cyan = {0.420, 0.890, 0.918},
        .orange = {1.000, 0.624, 0.263},
        .grid = {0.078, 0.078, 0.086},
        .track = {0.157, 0.157, 0.169},
        .light = false,
    },
};

static const Color accents[] = {
    {0.298, 0.788, 0.941},
    {0.694, 0.549, 1.000},
    {0.239, 0.863, 0.592},
    {0.969, 0.788, 0.282},
    {1.000, 0.361, 0.447},
    {0.302, 0.612, 1.000},
    {1.000, 0.624, 0.263},
    {0.133, 0.827, 0.933},
};

static const char *accent_names[] = {
    "Cyan", "Violet", "Emerald", "Amber", "Rose", "Azure", "Tangerine", "Ice"
};

Color color_hex(uint32_t hex) {
    Color c = {
        ((hex >> 16) & 0xff) / 255.0,
        ((hex >> 8) & 0xff) / 255.0,
        (hex & 0xff) / 255.0
    };
    return c;
}

Color color_mix(Color a, Color b, double t) {
    Color c = {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t
    };
    return c;
}

Color color_shade(Color c, double factor) {
    Color out = {
        c.r * factor,
        c.g * factor,
        c.b * factor
    };
    return out;
}

Color color_alpha_blend(Color c, Color bg, double alpha) {
    return color_mix(bg, c, alpha);
}

void color_css(Color c, char *buf, size_t len) {
    snprintf(buf, len, "#%02x%02x%02x",
             (int)(c.r * 255.0 + 0.5),
             (int)(c.g * 255.0 + 0.5),
             (int)(c.b * 255.0 + 0.5));
}

void color_css_alpha(Color c, double alpha, char *buf, size_t len) {
    snprintf(buf, len, "rgba(%d,%d,%d,%.3f)",
             (int)(c.r * 255.0 + 0.5),
             (int)(c.g * 255.0 + 0.5),
             (int)(c.b * 255.0 + 0.5),
             alpha);
}

const Theme *theme_base(ThemeId id) {
    if (id < 0 || id >= THEME_COUNT) {
        id = THEME_MIDNIGHT;
    }
    return &themes[id];
}

void theme_resolve(Theme *out, ThemeId id, int accent_index) {
    *out = *theme_base(id);
    if (accent_index >= 0 && accent_index < accent_count()) {
        out->accent = accent_color(accent_index);
    }
}

const char *theme_name(ThemeId id) {
    return theme_base(id)->name;
}

int theme_count(void) {
    return THEME_COUNT;
}

int accent_count(void) {
    return (int)(sizeof(accents) / sizeof(accents[0]));
}

const char *accent_name(int index) {
    if (index < 0 || index >= accent_count()) {
        return "Theme";
    }
    return accent_names[index];
}

Color accent_color(int index) {
    if (index < 0 || index >= accent_count()) {
        return accents[0];
    }
    return accents[index];
}

int accent_default_for_theme(ThemeId id) {
    Color accent = theme_base(id)->accent;
    int best = 0;
    double best_distance = 10.0;
    for (int i = 0; i < accent_count(); i++) {
        double dr = accents[i].r - accent.r;
        double dg = accents[i].g - accent.g;
        double db = accents[i].b - accent.b;
        double distance = dr * dr + dg * dg + db * db;
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}


char *theme_build_css(const Theme *t, bool compact, const char *font_family,
                      double font_scale, int quality) {
    GString *css = g_string_new(NULL);
    char text[16], dim[16], faint[16], accent[16], border[16], card[16], panel[16], bg[16], bg2[16], track[16];
    color_css(t->text, text, sizeof(text));
    color_css(t->dim, dim, sizeof(dim));
    color_css(t->faint, faint, sizeof(faint));
    color_css(t->accent, accent, sizeof(accent));
    color_css(t->border, border, sizeof(border));
    color_css(t->card, card, sizeof(card));
    color_css(t->panel, panel, sizeof(panel));
    color_css(t->bg, bg, sizeof(bg));
    color_css(t->bg2, bg2, sizeof(bg2));
    color_css(t->track, track, sizeof(track));

    const char *font = font_family != NULL && font_family[0] != '\0'
        ? font_family : "Ubuntu";
    if (font_scale < 0.75) {
        font_scale = 0.75;
    }
    if (font_scale > 1.5) {
        font_scale = 1.5;
    }
    char *safe_font = g_strescape(font, NULL);
    g_string_append(css, "* { outline: none; }\n");
    g_string_append_printf(css, "window, .background { background-color: %s; color: %s; }\n", bg, text);
    g_string_append_printf(css, "window { font-family: '%s', 'Noto Sans', sans-serif; }\n", safe_font);
    g_string_append_printf(css,
        "label, button, entry, spinbutton, dropdown, popover, .statusbar { font-size: %.1fpx; }\n",
        14.0 * font_scale);
    g_string_append_printf(css,
        "headerbar { background: linear-gradient(180deg, %s, %s);"
        " border-bottom: 1px solid %s; box-shadow: none; min-height: 58px;"
        " padding: 0 12px; color: %s; }\n",
        bg, bg, border, text);
    g_string_append_printf(css,
        "headerbar windowcontrols button, headerbar .titlebutton {"
        " background: transparent; color: %s; border-radius: 8px;"
        " min-width: 28px; min-height: 28px; margin: 0 2px; }\n", dim);
    g_string_append(css,
        "headerbar windowcontrols button:hover, headerbar .titlebutton:hover {"
        " background: rgba(255,255,255,0.08); color: #ffffff; }\n");
    g_string_append(css,
        "headerbar windowcontrols button.close:hover { background: #e0445a; color: #ffffff; }\n");
    g_string_append_printf(css, "label { color: %s; }\n", text);
    g_string_append(css, ".brand { font-family: 'Ubuntu'; font-weight: 800; letter-spacing: 3px; }\n");
    g_string_append(css, ".brand-mark { font-family: 'Ubuntu'; font-weight: 400; }\n");
    g_string_append_printf(css, ".dim { color: %s; }\n", dim);
    g_string_append_printf(css, ".faint { color: %s; }\n", faint);
    g_string_append_printf(css, ".accent { color: %s; }\n", accent);
    g_string_append(css, ".title { font-weight: 700; font-size: 16px; }\n");
    g_string_append(css, ".subtitle { font-size: 11px; }\n");
    g_string_append_printf(css,
        "entry { background: %s; color: %s; border: 1px solid %s;"
        " border-radius: 10px; min-height: 32px; padding: 0 12px;"
        " caret-color: %s; transition: 150ms ease; }\n",
        card, text, border, accent);
    g_string_append_printf(css, "entry:focus-within, entry:focus { border-color: %s; }\n", accent);
    g_string_append_printf(css, "entry placeholder { color: %s; }\n", faint);
    g_string_append_printf(css,
        "button { background: %s; color: %s; border: 1px solid %s;"
        " border-radius: 9px; min-height: 30px; padding: 0 12px;"
        " transition: 120ms ease; }\n", card, text, border);
    g_string_append_printf(css, "button:hover { background: %s; border-color: %s; }\n",
                           panel, border);
    g_string_append_printf(css, "button:active { background: %s; }\n", panel);
    g_string_append_printf(css, "button.flat, menubutton button { background: transparent; border-color: transparent; }\n");
    g_string_append_printf(css, "button.flat:hover, menubutton button:hover { background: %s; }\n", card);
    g_string_append_printf(css, "button.pill { border-radius: 999px; }\n");
    g_string_append_printf(css,
        "menubutton > button { background: transparent; border: none;"
        " min-width: 32px; min-height: 32px; padding: 0; border-radius: 9px; }\n");
    g_string_append_printf(css,
        "menubutton > button:hover { background: %s; }\n", card);
    g_string_append_printf(css,
        "popover { background: transparent; border: none; box-shadow: none; padding: 0; }\n");
    g_string_append_printf(css,
        "popover > contents { background: transparent; border: none; padding: 0; box-shadow: none; }\n");
    g_string_append_printf(css,
        "menu, .menu { background: %s; color: %s;"
        " border: 1px solid %s; border-radius: 12px; padding: 6px;"
        " box-shadow: 0 18px 40px rgba(0,0,0,0.45); }\n", panel, text, border);
    g_string_append_printf(css, "menuitem { padding: 7px 12px; border-radius: 7px; color: %s; }\n", text);
    /* menuitem icons are spaced by GTK internally */
    g_string_append_printf(css, "menuitem:hover { background: %s; }\n", card);
    g_string_append_printf(css,
        ".live-pill { background: rgba(%d,%d,%d,0.12); color: %s;"
        " border: 1px solid rgba(%d,%d,%d,0.24); border-radius: 999px;"
        " padding: 4px 11px; font-size: 11px; font-weight: 700;"
        " letter-spacing: 0.8px; }\n",
        (int)(t->green.r * 255), (int)(t->green.g * 255), (int)(t->green.b * 255),
        text,
        (int)(t->green.r * 255), (int)(t->green.g * 255), (int)(t->green.b * 255));
    g_string_append_printf(css,
        ".statusbar { background: %s; border-top: 1px solid %s; padding: 0 16px; min-height: 28px; }\n",
        bg2, border);
    g_string_append_printf(css,
        ".statusbar label { font-family: 'Ubuntu Mono', monospace; font-size: 10px; color: %s; }\n", dim);
    g_string_append_printf(css,
        "scrollbar { background: transparent; border: none; }\n"
        "scrollbar slider { background: %s; border-radius: 8px; min-width: 7px; min-height: 24px; }\n"
        "scrollbar slider:hover { background: %s; }\n", track, border);
    g_string_append(css, "scrolledwindow { background: transparent; }\n");
    g_string_append_printf(css, "separator { background: %s; min-height: 1px; }\n", border);
    g_string_append_printf(css,
        ".toast { background: %s; color: %s; border: 1px solid %s;"
        " border-radius: 999px; padding: 9px 18px; font-size: 12px;"
        " box-shadow: 0 12px 30px rgba(0,0,0,0.5); }\n", panel, text, border);
    g_string_append_printf(css,
        "switch { background: %s; border: 1px solid %s; border-radius: 999px; }\n", card, border);
    g_string_append_printf(css,
        "switch:checked { background: %s; border-color: %s; }\n", accent, accent);
    g_string_append_printf(css, "switch > slider { background: %s; border-radius: 999px; }\n", text);
    g_string_append_printf(css,
        "scale trough { background: %s; border-radius: 999px; min-height: 5px; }\n", card);
    g_string_append_printf(css,
        "scale highlight { background: %s; border-radius: 999px; }\n", accent);
    g_string_append_printf(css, ".settings-card { background: %s; border: 1px solid %s; border-radius: 14px; padding: 16px; }\n", card, border);
    g_string_append_printf(css,
        ".manage-list { background: transparent; }\n"
        ".manage-list row { background: transparent; border-radius: 9px; }\n"
        ".manage-list row:hover { background: rgba(255,255,255,0.035); }\n"
        ".manage-list row:selected { background: rgba(%d,%d,%d,0.14); }\n"
        ".row-title { font-weight: 600; font-size: 12.5px; }\n"
        ".row-sub { font-size: 10px; color: %s; }\n"
        ".pill { font-size: 9.5px; font-weight: 700; border-radius: 999px;"
        " padding: 2px 9px; background: rgba(255,255,255,0.05);"
        " border: 1px solid rgba(255,255,255,0.08); }\n"
        ".detail-title { font-size: 15px; font-weight: 700; margin-bottom: 4px; }\n"
        ".detail-key { font-size: 10px; color: %s; }\n"
        ".detail-value { font-size: 11px; color: %s; }\n"
        ".detail-note { font-size: 10px; color: %s; }\n",
        (int)(t->accent.r * 255), (int)(t->accent.g * 255), (int)(t->accent.b * 255),
        faint, faint, text, faint);
    g_string_append_printf(css,
        ".state-green { color: rgb(%d,%d,%d); }\n"
        ".state-red { color: rgb(%d,%d,%d); }\n"
        ".state-yellow { color: rgb(%d,%d,%d); }\n"
        ".state-accent { color: %s; }\n"
        ".state-purple { color: rgb(%d,%d,%d); }\n"
        ".state-dim { color: %s; }\n",
        (int)(t->green.r * 255), (int)(t->green.g * 255), (int)(t->green.b * 255),
        (int)(t->red.r * 255), (int)(t->red.g * 255), (int)(t->red.b * 255),
        (int)(t->yellow.r * 255), (int)(t->yellow.g * 255), (int)(t->yellow.b * 255),
        accent,
        (int)(t->purple.r * 255), (int)(t->purple.g * 255), (int)(t->purple.b * 255),
        dim);
    g_string_append_printf(css, ".section-title { font-weight: 700; font-size: 13px; }\n");
    g_string_append_printf(css, ".section-desc { font-size: 11px; }\n");
    g_string_append_printf(css, ".keycap { font-family: 'Ubuntu Mono', monospace; font-size: 11px; font-weight: 700; color: %s; background: %s; border: 1px solid %s; border-radius: 6px; padding: 4px 9px; }\n", accent, card, border);
    g_string_append_printf(css, "dialog, window.dialog { background: %s; }\n", bg);
    g_string_append_printf(css, ".accent-swatch { min-width: 30px; min-height: 30px; padding: 0; border-radius: 15px; border: 2px solid transparent; }\n");
    g_string_append_printf(css, ".accent-swatch:checked { border-color: %s; }\n", text);
    for (int i = 0; i < accent_count(); i++) {
        char hex[16];
        color_css(accent_color(i), hex, sizeof(hex));
        g_string_append_printf(css, ".accent-%d { background: %s; border-color: %s; }\n", i, hex, hex);
    }
    g_string_append_printf(css, ".theme-button { padding: 6px; border-radius: 14px; }\n");
    g_string_append_printf(css, ".theme-button:checked { border-color: %s; background: %s; }\n", accent, panel);
    g_string_append_printf(css, ".process-action { font-size: 11px; font-weight: 600; }\n");
    char red_hex[16];
    color_css(t->red, red_hex, sizeof(red_hex));
    g_string_append_printf(css, ".danger { color: %s; }\n", red_hex);
    if (compact) {
        g_string_append(css, ".dense { font-size: 11px; }\n");
    }
    if (quality == 3) {
        g_string_append(css, ".settings-card, .toast { box-shadow: none; }\n");
        g_string_append(css, "* { transition-duration: 0ms; }\n");
    }
    g_free(safe_font);
    return g_string_free(css, FALSE);
}
