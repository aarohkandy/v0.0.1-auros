/* fuzz_textinput.c -- the compositor's zwp_text_input_v3 against programs that lie.
 *
 * Stream W1's src/aurwl/textinput.c is reached by every program she runs
 * once Wishes are on, and it keeps pointers (the focused surface, each
 * program's text inputs) across the lifetime of programs it does not
 * control. This is fuzz_aurwl.c -- the generic protocol-driven fuzzer: two
 * programs, requests built from the generated signatures in hostile order,
 * focus moves, windows closing, programs killed -- with the text-input
 * manager added to the registry, so enable / disable / set_content_type /
 * set_surrounding_text / commit / destroy and get_text_input arrive in every
 * order, with the keyboard focus moving underneath them. The matcher
 * (wishhook.c) is linked in, so every key the shell sends is also pushed
 * through it while a text input is in any state.
 *
 * At the end of every input both programs disconnect and the text input
 * module must be empty: no text input, no remembered focus.
 *
 * Seeds are fuzz_aurwl's corpus; discoveries go in corpus/textinput.
 */
#define _GNU_SOURCE
#define FUZZ_WITH_TEXTINPUT 1
#include <wayland-server.h>
#include "text-input-server.h"
#include "../../src/aurwl/textinput.h"
#include "fuzz_aurwl.c"
#include "../../src/aurwl/textinput.c"
#include "../../src/aurwl/wishhook.c"

/* Words are registered from the start, so every key the shell sends is also
 * run through the matcher, which asks the text input state each time. */
__attribute__((constructor)) static void register_words(void)
{
    const char *w[] = { "potato", "qwer" };
    wishhook_set(w, 2, NULL, 0);
}
