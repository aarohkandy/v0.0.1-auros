# Extending AurOS without another OS fork

The five profiles share the same maintained operating system, installer and
desktop. Add behavior at the narrowest existing layer. A new behavior should
not copy the image builder or create another distribution.

## Choose the boundary

| Request | Existing boundary | Required implementation |
| --- | --- | --- |
| Choose a base, look or theme | `src/customize/recipe.py` | A validated recipe, then a trusted build/delivery adapter |
| Change a desktop preference | `src/aurshell/` | A typed setting with persistence, policy checks and reversal |
| React to time, apps, battery or sensors | `src/aurwish/` | A validated trigger/action document and runtime support |
| Add hardware support | Device service and capability discovery | A bounded adapter; advertise only capabilities actually present |
| Add software or privileged services | Reviewed profile/build code | Source review, provenance, image tests and a new release |

The recipe contract is deliberately small. Arbitrary commands, paths, package
URLs and executable code must not be added as convenient recipe fields. New
capabilities require explicit versioned fields and tests. A model proposes data;
trusted code decides whether the whole request is supported.

## Example: brighter when pointing north

This is already represented by the Wishes v1 schema:

```json
{
  "v": 1,
  "when": {
    "type": "sensor",
    "sensor": "compass",
    "heading": "north",
    "tolerance_deg": 20
  },
  "do": [{"type": "brightness", "set_percent": 100}]
}
```

This is an engine example, not an install recipe or a claim that the shipping
image supports it. `profiles/desktop.profile` keeps Wishes off under decision
D14. Most ordinary computers do not expose every sensor; capability detection,
not a model's guess, decides availability.

The existing path is:

1. `docs/wish-v1.schema.json` describes the document. Cloud validation in
   `cloud/src/validate.js` checks allowed actions and triggers.
2. `src/aurwish/wish.c` checks again on the device, including actual compass
   availability, heading, tolerance and action limits. It constructs the
   confirmation sentence from validated fields.
3. `src/aurwish/sensors.c` talks to iio-sensor-proxy through D-Bus. Sensors are
   claimed only while needed, with bounded reads and retry behavior.
4. `engine_compass` in `src/aurwish/engine.c` evaluates the trigger. The brightness
   action uses typed callbacks and applies its safety floor.
5. `src/aurwish/main.c` connects those callbacks to the existing power controls.

For this example, do not invent another sensor daemon or let generated code
write arbitrary device files. Complete and verify the existing feature's
packaging, configuration, consent and hardware path before enabling it.

## Adding a trigger or action

Keep schema, parser, capability advertisement, confirmation wording and runtime
implementation together. Update the shared JSON schema, the enums and typed
structures in `wish.h`, parsing and serialization in `wish.c`, engine dispatch,
and the relevant device or shell adapter. Cloud output must remain a subset of
the capability list sent by the device.

For shell actions, return success only after applying the change; retain the
previous value for reversal where appropriate. Do not add an action to the
advertised supported set while the shell merely recognizes its name. As of this
checkpoint, `src/aurshell/wish.c` explicitly refuses wallpaper, night light
and pointer size controls it cannot apply. Its do-not-disturb adapter validates
the event and acknowledges the engine's already-applied suppression of Wishes
interruptions. This does not control other applications' notifications, and
Wishes remains disabled pending its activation and runtime gates.

Preserve kiosk and organization policy. Preserve bounded input, timeouts,
resource limits, privacy boundaries and missing-device refusals. Do not turn a
missing capability into a successful default or silently discard part of a
multi-part request.

## Evidence required for an extension

- Parser/schema tests: accepted document, unknown/duplicate/extra fields,
  missing hardware, invalid ranges and unsupported action combinations.
- Engine tests: trigger boundaries, repeated readings, disconnect/reconnect,
  timeouts, reversal and failure of the target control.
- Adapter tests: exercise the actual device/shell adapter with a fake service
  or controlled device, including denied access and malformed replies.
- Mutation tests: removing the important check must cause a named failure.
- Image journey: use the exact built image, demonstrate confirmation, execution,
  cancellation, persistence and recovery. A parser unit test is not this proof.

Existing entry points include `tools/wishtest.sh --mutants`,
`tools/wishuitest.sh`, `tools/wishe2e.sh` and `tools/wishesoff.sh`. Read each
script's environment requirements before running. Host-only tests skip Linux
integration; report those skips. Changes to feature activation additionally need
the signed cloud configuration, voice dependencies where promised, and an
enabled-image test. Keep the default-off release gate until those are ready.
