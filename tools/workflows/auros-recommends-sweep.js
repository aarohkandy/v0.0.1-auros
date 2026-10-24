export const meta = {
  name: 'auros-recommends-sweep',
  description: 'The image builds without Recommends; find every dropped Recommends that breaks something she would notice (like the SVG icon loader), adversarially judged',
  phases: [
    { title: 'Map', detail: 'shipped package list and every Recommends not installed' },
    { title: 'Judge', detail: 'one judge per area: which drops break something she sees' },
    { title: 'Refute', detail: 'a skeptic per area' },
  ],
}

const REPO = '/Users/aaroh/Desktop/auros v0.0.1/v0.0.1-auros'
const GIT = `git -C "${REPO}"`
const H = 'ssh -o ConnectTimeout=10 -i $HOME/.ssh/homebase_ed25519 azureuser@100.108.213.121'
const CTX = `AurOS is an Ubuntu 24.04 (noble) based desktop for a great-grandmother, built by build/forge with apt --no-install-recommends and package lists in profiles/ (desktop profile) and build/forge. Stream C just found that adwaita-icon-theme only RECOMMENDS librsvg2-common (the gdk-pixbuf SVG loader), so every symbolic icon in GNOME Software drew the image-missing glyph on the shipped image (fixed in 14b3d2a). Find every other case like it. Checkout ${REPO} (path has a space; quote it). READ-ONLY there: \`${GIT} fetch -q origin\`, \`${GIT} show origin/main:<path>\`, \`${GIT} grep -n <pat> origin/main -- build profiles rootfs\`. Published image manifests may exist as release assets or build artifacts: \`gh release list -R aarohkandy/v0.0.1-auros\`, \`gh release view <tag> -R aarohkandy/v0.0.1-auros\`, \`gh run download <id> -R aarohkandy/v0.0.1-auros -n <artifact> -D <your own new dir under /private/tmp/claude-501/-Users-aaroh-Desktop-auros-v0-0-1/914a7092-8dd8-4369-bb66-bf5c4e2ea1f2/scratchpad/recsweep/>\`. For apt metadata use the owner's Linux server homebase as a good guest: \`${H} '<cmd>'\`; work ONLY in ~/work/auros/recsweep/, run \`docker run --rm --memory 4g ubuntu:24.04 bash -c '...'\` (apt-get update inside the container, then apt-cache depends/show/rdepends) with nice; never sudo, never touch other containers or services, delete what you create when done. Never edit, commit or push the repository.`

const MAPS = { type: 'object', properties: {
  package_list_source: { type: 'string' },
  installed_count: { type: 'number' },
  dropped: { type: 'array', items: { type: 'object', properties: { pkg: { type: 'string' }, recommended_by: { type: 'array', items: { type: 'string' } }, what_it_is: { type: 'string' } }, required: ['pkg', 'recommended_by'] } } },
  required: ['package_list_source', 'dropped'] }
const J = { type: 'object', properties: { findings: { type: 'array', items: { type: 'object', properties: {
  pkg: { type: 'string' }, recommended_by: { type: 'string' }, what_breaks: { type: 'string' }, how_she_notices: { type: 'string' },
  severity: { type: 'string', enum: ['blocker', 'high', 'medium', 'low'] }, evidence: { type: 'string' }, fix: { type: 'string' }, cost_mb: { type: 'string' } },
  required: ['pkg', 'what_breaks', 'severity', 'evidence', 'fix'] } } }, required: ['findings'] }
const V = { type: 'object', properties: { verdicts: { type: 'array', items: { type: 'object', properties: { pkg: { type: 'string' }, refuted: { type: 'boolean' }, reason: { type: 'string' } }, required: ['pkg', 'refuted', 'reason'] } } }, required: ['verdicts'] }

phase('Map')
const map = await agent(`${CTX}\n\nProduce the exact list of packages installed in the shipped desktop image (best source: a published manifest; else resolve the profile + forge package lists with apt in the container, without recommends). Then list EVERY package that some installed package Recommends but which is NOT installed (apt-cache depends --recurse is not needed; for each installed package, its Recommends lines; alternatives A|B count as satisfied if any is installed). Say what each dropped package is in a few words. Also note packages build/forge explicitly purges or pins out (those are deliberate).`, { label: 'map', phase: 'Map', schema: MAPS })
if (!map || !map.dropped) return { error: 'map failed', map }
log(`${map.dropped.length} dropped Recommends from ${map.package_list_source}`)

const AREAS = [
  { key: 'graphics-icons-fonts-themes', q: 'pixbuf/image loaders, icon themes, fonts and emoji, cursor themes, GTK/Qt platform themes and portals for appearance' },
  { key: 'media-sound-camera', q: 'GStreamer plugins, codecs, pipewire/wireplumber pieces, ALSA/SOF firmware, camera (libcamera, v4l), media players' },
  { key: 'printing-scanning-devices', q: 'CUPS filters, drivers, ipp-usb, sane backends, bluetooth, udisks/gvfs backends (MTP phones, cameras, SMB shares, trash), power (upower), sensors' },
  { key: 'apps-flatpak-portals-network', q: 'GNOME Software plugins, flatpak, xdg-desktop-portal backends, PackageKit, NetworkManager plugins (Wi-Fi, VPN, mobile), wpa, firmware, Firefox, LibreOffice, ca-certificates' },
  { key: 'accessibility-language-input', q: 'at-spi, screen reader, speech, on-screen keyboard, input methods, spell/hyphenation dictionaries, locales/langpacks, man/help viewers, and anything aurshell, aurwl, Xwayland or the Wishes voice path rely on' },
]
const res = await pipeline(AREAS,
  a => agent(`${CTX}\n\nArea: ${a.key} (${a.q}). From the dropped Recommends below, pick those in your area and decide for each whether its absence breaks or degrades something she would NOTICE in normal use (a missing icon, no sound, a program that will not open a file type, a printer not found, a phone not showing up, a broken spelling check). Verify with evidence: what code path loads it (pixbuf loader cache, gst-inspect registry, portal config, GNOME Software plugin dir), and whether AurOS installs an equivalent another way (grep build/ profiles/ rootfs/). Ignore pure docs/dev packages. Include size cost.\n\nDROPPED:\n${JSON.stringify(map.dropped).slice(0, 80000)}`, { label: `judge:${a.key}`, phase: 'Judge', schema: J }),
  (j, a) => (!j || !j.findings.length) ? { area: a.key, findings: [], verdicts: [] } :
    agent(`${CTX}\n\nYou are a SKEPTIC. For each finding try to REFUTE it: is the package really absent from the shipped image, is the feature really broken without it (or does something else provide it), would she ever hit it? Default refuted=true if unconfirmed.\n\n${JSON.stringify(j.findings)}`, { label: `refute:${a.key}`, phase: 'Refute', schema: V })
      .then(v => ({ area: a.key, findings: j.findings, verdicts: v ? v.verdicts : [] })))
return { source: map.package_list_source, installed: map.installed_count, dropped_count: map.dropped.length, results: res.filter(Boolean) }