#ifndef PRISM_THEME_H
#define PRISM_THEME_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double r;
    double g;
    double b;
} Color;

typedef enum {
    THEME_MIDNIGHT,
    THEME_AURORA,
    THEME_NORD,
    THEME_DRACULA,
    THEME_CARBON,
    THEME_COUNT
} ThemeId;

typedef struct {
    const char *id;
    const char *name;
    Color bg;
    Color bg2;
    Color panel;
    Color panel2;
    Color card;
    Color card2;
    Color border;
    Color border_soft;
    Color text;
    Color dim;
    Color faint;
    Color accent;
    Color accent2;
    Color green;
    Color yellow;
    Color red;
    Color blue;
    Color purple;
    Color cyan;
    Color orange;
    Color grid;
    Color track;
    bool light;
} Theme;

Color color_hex(uint32_t hex);
Color color_mix(Color a, Color b, double t);
Color color_shade(Color c, double factor);
Color color_alpha_blend(Color c, Color bg, double alpha);
void color_css(Color c, char *buf, size_t len);
void color_css_alpha(Color c, double alpha, char *buf, size_t len);

const Theme *theme_base(ThemeId id);
void theme_resolve(Theme *out, ThemeId id, int accent_index);
const char *theme_name(ThemeId id);
int theme_count(void);

int accent_count(void);
const char *accent_name(int index);
Color accent_color(int index);
int accent_default_for_theme(ThemeId id);

char *theme_build_css(const Theme *theme, bool compact, const char *font_family,
                      double font_scale, int quality);

#endif
