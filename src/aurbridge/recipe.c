#include "recipe.h"
#include "profile_image.h"

#include <stddef.h>
#include <string.h>

#if defined(__has_include)
# if __has_include("aurbridge-recipe.h")
#  include "aurbridge-recipe.h"
# endif
#endif

/* WHICH PROFILE THIS INSTALLER INSTALLS, fixed when it was built
 * (tools/customize/installer_profile.py; build/aurbridge writes the header on
 * every build). Absent, it is the desktop installer exactly as before. */
#if defined(__has_include)
# if __has_include("aurbridge-profile.h")
#  include "aurbridge-profile.h"
# endif
#endif
#ifndef AUROS_PROFILE_ID
#define AUROS_PROFILE_ID "desktop"
#endif
#ifndef AUROS_PROFILE_LOOK
#define AUROS_PROFILE_LOOK ""
#endif
#ifndef AUROS_PROFILE_THEME
#define AUROS_PROFILE_THEME ""
#endif
#ifndef AUROS_PROFILE_LOOK_OFFERED
#define AUROS_PROFILE_LOOK_OFFERED 1
#endif

static int profile_baked_ok(void)
{
    static const char *const looks[] = { "taskbar", "dock", "rail", "tiles" };
    const char *id = AUROS_PROFILE_ID, *look = AUROS_PROFILE_LOOK,
               *theme = AUROS_PROFILE_THEME;
    size_t i, n = strlen(theme);
    int look_ok = look[0] == 0;
    if (!ab_profile_name_ok(id)) return 0;
    for (i = 0; i < sizeof looks / sizeof looks[0]; i++)
        if (strcmp(look, looks[i]) == 0) look_ok = 1;
    if (!look_ok || n > 32) return 0;
    for (i = 0; i < n; i++)
        if (!((theme[i] >= 'a' && theme[i] <= 'z') ||
              (theme[i] >= '0' && theme[i] <= '9') || theme[i] == '-'))
            return 0;
    return AUROS_PROFILE_LOOK_OFFERED == 0 || AUROS_PROFILE_LOOK_OFFERED == 1;
}

#ifndef AUROS_RECIPE_PRESENT
#define AUROS_RECIPE_PRESENT 0
#endif
#ifndef AUROS_RECIPE_BASE
#define AUROS_RECIPE_BASE ""
#endif
#ifndef AUROS_RECIPE_LOOK
#define AUROS_RECIPE_LOOK ""
#endif
#ifndef AUROS_RECIPE_THEME
#define AUROS_RECIPE_THEME ""
#endif
#ifndef AUROS_RECIPE_ID
#define AUROS_RECIPE_ID ""
#endif

#if AUROS_RECIPE_PRESENT
static const ab_baked_recipe baked_recipe = {
    AUROS_RECIPE_BASE, AUROS_RECIPE_LOOK, AUROS_RECIPE_THEME, AUROS_RECIPE_ID
};

static int one_of(const char *value, const char *const *allowed, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++)
        if (strcmp(value, allowed[i]) == 0) return 1;
    return 0;
}

static int valid_identity(const char *value)
{
    static const char prefix[] = "sha256:";
    size_t i;
    if (strncmp(value, prefix, sizeof prefix - 1) != 0 ||
        strlen(value) != (sizeof prefix - 1) + 64)
        return 0;
    for (i = sizeof prefix - 1; value[i]; i++)
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f')))
            return 0;
    return 1;
}
#endif

int ab_recipe_status(void)
{
    /* A malformed profile block is as bad as a malformed recipe: the wizard
     * refuses to start rather than install something it cannot name. */
    if (!profile_baked_ok()) return AB_RECIPE_INVALID;
#if AUROS_RECIPE_PRESENT
    static const char *const looks[] = { "dock", "rail", "taskbar", "tiles" };
    static const char *const themes[] = {
        "ember", "moss", "nocturne", "sandstone", "slate"
    };
    /* Packaging currently enables desktop only. Keep this check aligned with
     * tools/customize/installer_recipe.py's delivery allowlist. A recipe is
     * also only ever for the profile this installer was built for. */
    if (strcmp(baked_recipe.base, "desktop") != 0 ||
        strcmp(baked_recipe.base, AUROS_PROFILE_ID) != 0 ||
        !one_of(baked_recipe.look, looks, sizeof looks / sizeof looks[0]) ||
        !one_of(baked_recipe.theme, themes, sizeof themes / sizeof themes[0]) ||
        !valid_identity(baked_recipe.recipe_id))
        return AB_RECIPE_INVALID;
    return AB_RECIPE_VALID;
#else
    return AB_RECIPE_ABSENT;
#endif
}

const ab_baked_recipe *ab_recipe_baked(void)
{
#if AUROS_RECIPE_PRESENT
    return ab_recipe_status() == AB_RECIPE_VALID ? &baked_recipe : NULL;
#else
    return NULL;
#endif
}

const char *ab_recipe_image_profile(void)
{
#if AUROS_RECIPE_PRESENT
    return ab_recipe_status() == AB_RECIPE_VALID ? baked_recipe.base : "";
#else
    return profile_baked_ok() ? AUROS_PROFILE_ID : "";
#endif
}

int ab_profile_look_offered(void)
{
    /* A personalized (recipe) installer always shows the look it was made
     * with; its profile is desktop, which always offers the choice. */
    return profile_baked_ok() && AUROS_PROFILE_LOOK_OFFERED;
}

const char *ab_profile_look(void)
{
    return profile_baked_ok() ? AUROS_PROFILE_LOOK : "";
}

const char *ab_profile_theme(void)
{
    return profile_baked_ok() ? AUROS_PROFILE_THEME : "";
}

void ab_profile_apply_look(char *shell, size_t shell_n, char *theme, size_t theme_n)
{
    const char *t = ab_profile_theme();
    size_t n = strlen(t);
    if (!ab_profile_look_offered()) {
        /* The image decides its own look; the journal says nothing, and
         * first boot (rootfs/usr/lib/auros/choices.sh) leaves it alone. */
        if (shell_n) shell[0] = 0;
        if (theme_n) theme[0] = 0;
        return;
    }
    if (n && theme_n > n) memcpy(theme, t, n + 1);
}

const char *ab_recipe_id_for_selection(const char *profile,
                                       const char *look,
                                       const char *theme)
{
    const ab_baked_recipe *recipe = ab_recipe_baked();
    if (!recipe || !profile || !look || !theme ||
        strcmp(recipe->base, profile) != 0 ||
        strcmp(recipe->look, look) != 0 ||
        strcmp(recipe->theme, theme) != 0)
        return "";
    return recipe->recipe_id;
}
