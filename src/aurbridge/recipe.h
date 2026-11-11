#ifndef AURBRIDGE_RECIPE_H
#define AURBRIDGE_RECIPE_H

#include <stddef.h>

typedef struct {
    const char *base;
    const char *look;
    const char *theme;
    const char *recipe_id;
} ab_baked_recipe;

enum {
    AB_RECIPE_INVALID = -1,
    AB_RECIPE_ABSENT = 0,
    AB_RECIPE_VALID = 1
};

/* A present but malformed generated header is different from no recipe. */
int ab_recipe_status(void);

/* NULL unless the generated recipe is present and valid. Generated values are
 * checked again here so a hand-written or stale header cannot become choices. */
const ab_baked_recipe *ab_recipe_baked(void);

/* The profile this installer installs: the one it was built for
 * (aurbridge-profile.h), desktop when none was named. It is the journal's
 * profile and names the image file. Invalid data returns an empty profile
 * and the wizard refuses to start. */
const char *ab_recipe_image_profile(void);

/* What the look page does for the profile this installer was built for.
 * ab_profile_look_offered(): 0 when the image decides its own look (a kiosk,
 * or a profile that forbids settings or theme changes) and the wizard asks
 * nothing and writes nothing about it. ab_profile_look() / _theme(): the
 * shell and theme the page starts on; "" means the wizard's own default. */
int ab_profile_look_offered(void);
const char *ab_profile_look(void);
const char *ab_profile_theme(void);

/* Given the shell and theme of the card she picked, leave in those buffers
 * what the journal should carry for this profile: nothing at all when the
 * profile does not offer the look, otherwise her shell with the profile's
 * own theme (when it names one). The wizard and the simulation both call
 * this, so the rule is tested where it is written. */
void ab_profile_apply_look(char *shell, size_t shell_n, char *theme, size_t theme_n);

/* A recipe identity describes the resolved triple exactly. If the person
 * changes the look in the wizard, the recipe no longer describes the final
 * choices and this returns an empty string. */
const char *ab_recipe_id_for_selection(const char *profile,
                                       const char *look,
                                       const char *theme);

#endif
