/* features.h -- what this build of the shell has compiled in.
 *
 * AUROS_WISHES (decision D14: Wishes are NOT in the v0.1 image). 0 here
 * unless build/forge is given a profile that says wishes="on" and passes
 * -DAUROS_WISHES=1. Every way into Wishes that is visible to a person
 * (the "Make a wish" home tile, the "My wishes" row in Settings) is behind
 * it, so a release image has no door to a feature whose engine is not in
 * the image. A tool that compiles shell sources with no flag at all gets
 * 0, the same as a release.
 */
#ifndef AUROS_FEATURES_H
#define AUROS_FEATURES_H

#ifndef AUROS_WISHES
#define AUROS_WISHES 0
#endif

#endif
