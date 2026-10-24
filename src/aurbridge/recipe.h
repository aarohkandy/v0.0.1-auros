#ifndef AURBRIDGE_RECIPE_H
#define AURBRIDGE_RECIPE_H

typedef struct {
    const char *base;
    const char *look;
    const char *theme;
    const char *recipe_id;
} ab_baked_recipe;

/* NULL for a generic installer. Generated values are checked again here so a
 * hand-written or stale header cannot turn arbitrary strings into choices. */
const ab_baked_recipe *ab_recipe_baked(void);

/* The existing developer/generic installer remains a desktop installer. */
const char *ab_recipe_image_profile(void);

/* A recipe identity describes the resolved triple exactly. If the person
 * changes the look in the wizard, the recipe no longer describes the final
 * choices and this returns an empty string. */
const char *ab_recipe_id_for_selection(const char *profile,
                                       const char *look,
                                       const char *theme);

#endif
