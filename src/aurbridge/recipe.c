#include "recipe.h"

#include <stddef.h>
#include <string.h>

#if defined(__has_include)
# if __has_include("aurbridge-recipe.h")
#  include "aurbridge-recipe.h"
# endif
#endif

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

const ab_baked_recipe *ab_recipe_baked(void)
{
#if AUROS_RECIPE_PRESENT
    static const char *const looks[] = { "dock", "rail", "taskbar", "tiles" };
    static const char *const themes[] = {
        "ember", "moss", "nocturne", "sandstone", "slate"
    };
    static const ab_baked_recipe recipe = {
        AUROS_RECIPE_BASE, AUROS_RECIPE_LOOK, AUROS_RECIPE_THEME, AUROS_RECIPE_ID
    };
    /* Packaging currently enables desktop only. Keep this check aligned with
     * tools/customize/installer_recipe.py's delivery allowlist. */
    if (strcmp(recipe.base, "desktop") != 0 ||
        !one_of(recipe.look, looks, sizeof looks / sizeof looks[0]) ||
        !one_of(recipe.theme, themes, sizeof themes / sizeof themes[0]) ||
        !valid_identity(recipe.recipe_id))
        return NULL;
    return &recipe;
#else
    return NULL;
#endif
}

const char *ab_recipe_image_profile(void)
{
    const ab_baked_recipe *recipe = ab_recipe_baked();
    return recipe ? recipe->base : "desktop";
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
