// A green deploy step may not substitute a skipped smoke/log check for proof.
import { pathToFileURL } from "node:url";
export function publicationEvidence(enabled, smokeExit, logsProven) {
  if (typeof enabled !== "boolean" || !Number.isInteger(smokeExit) || smokeExit < 0 || smokeExit > 255 || typeof logsProven !== "boolean") {
    throw new Error("Invalid publication evidence");
  }
  if (enabled && (smokeExit !== 0 || !logsProven)) {
    throw new Error("Enabled publication requires successful live smoke and log assertions; skipped evidence is insufficient");
  }
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    if (!["true", "false"].includes(process.argv[2]) || !["0", "1"].includes(process.argv[4])) throw new Error("Invalid publication evidence flags");
    publicationEvidence(process.argv[2] === "true", Number(process.argv[3]), process.argv[4] === "1");
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
