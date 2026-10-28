# AurOS customization recipe contract

Recipe v1 is the small, non-executable boundary between a website choice and a
future AurOS build service. It selects one of the five existing profiles and,
where policy permits, one of the existing consumer looks and themes. It cannot
name a package, URL, file, shell command, user, service or privilege.

The compiler **does not create an image, modify an installer or apply a recipe**.
Successful output has `delivery.state` set to `validated-not-built` and
`artifact_created` set to `false`. A downloadable JSON recipe is a request, not
proof that a customized installer exists.

## Input

The transport is one UTF-8 JSON document no larger than 16 KiB. The JSON Schema
is [`src/customize/recipe-v1.schema.json`](../src/customize/recipe-v1.schema.json).
The implementation also rejects duplicate fields, non-finite numbers, trailing
documents and unsupported fields at every level.

```json
{
  "schema": "org.auros.recipe",
  "version": 1,
  "base": "desktop",
  "preferences": {
    "look": "taskbar",
    "theme": "sandstone"
  }
}
```

The base is one of `desktop`, `multilingual`, `office`, `revive` or
`school-kiosk`. The public looks are `taskbar`, `dock`, `rail` and `tiles`.
`locked` is the kiosk policy and cannot be requested; `workbench` remains an
internal expert option. Themes are the five checked-in themes: `ember`, `moss`,
`nocturne`, `sandstone` and `slate`.

Both preferences are optional. Omission preserves the profile's default. A
school kiosk always keeps its locked look; asking it to use another look is a
`policy_conflict`. A build-time theme may be chosen for a kiosk, but the
profile's `allow_theme_change=no` policy still prevents changing it at runtime.

Language, text size, customer prose and timestamps are outside recipe v1.
Customer prose may be stored as untrusted request metadata, but it must never be
concatenated into a recipe, profile, command or build log command line.

## Compiler API

From the repository root:

```sh
python3 -m src.customize.recipe catalogue
python3 -m src.customize.recipe compile request.json
python3 -m src.customize.recipe compile < request.json
```

`catalogue` returns stable labels, descriptions, defaults and delivery
requirements for a website. The website should use these IDs exactly. A
successful `compile` prints one compact JSON object. A validation failure prints
one JSON error to standard error and exits 2:

```json
{"error":{"code":"unsupported_value","message":"value is not supported by recipe v1","path":"$.base"},"schema":"org.auros.recipe-error","version":1}
```

The plan contains:

- `requested_preferences`, which preserves whether a field was omitted;
- `resolved_preferences`, which gives the complete look and theme a person can
  review before a build;
- `profile_overrides`, whose only possible internal keys are `shell_archetype`
  and `theme`;
- `recipe_id`, a SHA-256 identity for the canonical validated request; and
- an explicit delivery state and the profile-specific gates still required.

Canonical request bytes are UTF-8 JSON with recursively sorted object keys, no
extra whitespace and ASCII escaping. Input object order therefore has no effect
on `recipe_id`. The ID identifies content; it is not a signature and does not
authenticate who requested it.

`compiler_version` follows semantic versioning and must change whenever accepted
inputs, validation, normalization, defaults or output meaning changes. A plan's
version string is still only a claim by the process that emitted it. The signed
build attestation must also record the exact compiler source commit.

## Required delivery bridge

The existing generic installer does not consume this plan. End-to-end delivery
still needs a trusted service that:

1. authenticates and rate-limits the request;
2. shows separate, high-salience consent before accepting a restrictive profile;
3. runs this validator at the trust boundary;
4. loads the chosen checked-in base profile;
5. applies only the two typed override fields through a purpose-built adapter;
6. runs that profile's build, security and release gates in an isolated worker;
7. binds `recipe_id`, normalized choices, compiler and product source commits,
   and image hash into a signed build attestation; and
8. publishes the signed artifact through an expiring, customer-specific link.

The adapter must not generate, source, `eval`, export or patch a `.profile` file.
AurOS profiles are shell-sourced by `build/forge`; passing request text into that
mechanism would turn data into code. Office and kiosk also require a configured
managed update channel, multilingual requires its input-method integration gate,
and every profile requires its listed release gates. Validation alone never
makes a profile publicly deliverable.

Kiosk consent must say that it automatically signs in and removes settings,
software installation and console access. Office consent must say that it uses
an organization-managed update channel and does not automatically sign in.
Neither should appear as an ordinary home-user variant until its login,
first-boot and update flows pass their release gates.

A recipe also says nothing about whether the target PC is supported. Installer
preflight remains authoritative, must run again on the destination computer and
must refuse unsupported hardware. No recipe field or build service may waive a
preflight refusal.
