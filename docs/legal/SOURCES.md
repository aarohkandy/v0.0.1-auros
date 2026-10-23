# The source code we owe, and how it is published

Prepared by the AurOS agents without a lawyer (decision D15). Not legal advice.
The review behind it: `COMPLIANCE.md` LIC-1 and LIC-3.

## Why

AurOS hands a person an image of hundreds of programs. The Linux kernel, the
boot loader, bash and many more are under the GNU General Public Licence
(GPL); glibc and others under the LGPL; Firefox under the Mozilla Public
Licence. Those licences make *us*, the people who hand over the programs,
responsible for giving the matching source code (or a written offer of it) to
everyone who gets a copy. Pointing at Ubuntu's archive is not enough: old
versions leave it, and the GPL asks for the source from the same place as the
binary.

## What we do

For every AurOS release `TAG` (say `v0.1.0-618726b`) the workflow
`.github/workflows/sources.yml` makes a second release, `sources-TAG`
(a prerelease), with:

| File | Content |
|---|---|
| `sources-TAG-<n>of<N>.tar` | The Ubuntu source packages (`.dsc`, original tarball, Debian changes) of every program in the image, in the exact versions. Parts stay under 1.9 GiB (GitHub's limit is 2 GiB); a program's files are never split between parts. |
| `SOURCES.txt` | Each source package, its version, its part, the sha256 of its `.dsc`, which binary packages came from it; the address and sha256 of Firefox's source at Mozilla; AurOS's own source archive. |
| `PACKAGES.txt` | Every installed binary package of the image and the source it came from. |
| `SHA256SUMS` | Every file above. |
| `auros-source-<commit>.tar.gz` | `git archive` of the exact commit the image was built from (MIT). |

The release notes carry the written offer (`rootfs/usr/share/doc/auros/SOURCE-OFFER`,
also in the image, on the website's Licences page and, in a short form, in the
notes of the AurOS release itself).

## How a release is made (`build/sources`)

1. The package list is the **dpkg database of the rootfs the image was made
   from** (the `auros-rootfs` artifact of the build run): not a guess from the
   profile. `${source:Package}` and `${source:Version}` are worked out as dpkg
   does it (a binNMU's `Source: foo (1.0-1)` header wins over its own version).
2. Each source package is fetched from Launchpad's permanent per-file URLs
   (`launchpad.net/ubuntu/+archive/primary/+sourcefiles/<source>/<version>/<file>`;
   superseded versions stay there), then from `archive.ubuntu.com`'s pool. The
   `.dsc` is read; **every file it names must download and hash to the sha256
   it gives**, or the build stops and names the package.
3. Firefox: Mozilla's `.deb` has no source package; MPL-2.0 lets an unchanged
   binary point to the maker's source. `SOURCES.txt` has the address and
   Mozilla's own sha256; the tool checks the address answers.
4. Things the Windows installer links statically from the **build machine**
   (`init` and `aurscreen` use the runner's glibc) are added from the build
   host's package list (`--extra`).
5. The result is verified as a whole (`build/sources verify`) before it is
   published, then published as a draft, filled, and only then made a
   prerelease. A second job downloads the public release again and verifies it
   against the image's own package list. Every Monday a job re-checks every
   `sources-*` release against GitHub's digests and fails if anything is gone.

`python3 -I tools/legal/sourcestest.py` is the test (a pretend Launchpad on
127.0.0.1, 34 cases, each of which breaks something and requires the tool to
say so; `--mutants` breaks the tool itself and requires the suite to go red).

## Rules

- `sources-*` releases are **never deleted** (`docs/OWNER-SETUP.md` section 9).
  `profiles.yml` prunes only `profiles-*`; nothing else may prune `sources-*`.
- A release is not announced, linked from the website or promoted to "latest"
  without its `sources-TAG` release (stream E's `promote` job needs the
  `sources` workflow).
- Updates a computer later takes from Ubuntu or Mozilla come with source from
  them; the AurOS update channel (`apt`) carries only AurOS's own MIT code.

## Redoing one by hand

`git commit --allow-empty -m "sources of an existing release [sources TAG RUN-ID]"` and push
(RUN-ID is the build run whose rootfs artifact made that release; artifacts live
14 days). An existing published `sources-TAG` is only verified, never rewritten.

## Not covered, said plainly

- The `.dsc` files carry Ubuntu uploaders' signatures but we do not check
  them; we check the files against the sha256 in the `.dsc` we recorded.
  Authenticity of Ubuntu's source is Canonical's.
- If a source package has left Launchpad's storage entirely, the build fails
  and says so; nothing here can produce source we cannot download.
- The version of the glibc of the machine that built the installer's
  statically linked parts is read where the sources job runs unless stream E
  records it in `build-info.txt` (hand-off in `docs/handoff/streams/LEGAL.md`).
