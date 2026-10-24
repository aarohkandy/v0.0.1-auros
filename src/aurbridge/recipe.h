#ifndef AURBRIDGE_RECIPE_H
#define AURBRIDGE_RECIPE_H

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

/* The existing generic installer remains desktop. Invalid data returns an
 * empty profile and the wizard refuses to start. */
const char *ab_recipe_image_profile(void);

/* A recipe identity describes the resolved triple exactly. If the person
 * changes the look in the wizard, the recipe no longer describes the final
 * choices and this returns an empty string. */
const char *ab_recipe_id_for_selection(const char *profile,
                                       const char *look,
                                       const char *theme);

#endif
