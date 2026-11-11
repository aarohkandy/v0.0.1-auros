# Make an AurOS for one person

You write one small file, run one command, and get back one `.exe` for that
person: her programs, her look, her colours, and the rules you want. No Linux
machine is needed. Everything below is checked against this repository.

## 1. Decide what is different, and write it down

A profile is `profiles/<name>.profile`. The name is lower-case letters, digits
and dashes (1 to 32). Start with `inherit="desktop"` and write only what differs.

| What | Line in the profile | Choices |
|---|---|---|
| Programs | `packages_apps="a b c"` | Ubuntu program names. This list replaces the parent's (desktop's is empty). System parts go in `packages_extra`, things to remove in `packages_exclude`. |
| Look | `shell_archetype="rail"` | `taskbar` (like Windows), `dock` (like a Mac), `rail` (big and simple), `tiles` (one thing at a time). `locked` is the kiosk. |
| Colours | `theme="moss"` | `sandstone` (light), `nocturne` (light writing on dark), `ember`, `moss`, `slate` |
| Language, keyboard | `locale`, `extra_locales`, `keyboard_layout`, `timezone`, `input_method` | See `profiles/multilingual.profile`. The installer asks her too, and her answers win. |
| What she may do | `allow_user_install`, `allow_settings_change`, `allow_theme_change`, `allow_network_change`, `allow_tty` | `"yes"` or `"no"` |
| A kiosk | `kiosk_mode="yes"`, `allowed_apps="..."`, `shell_archetype="locked"` | See `profiles/school-kiosk.profile`. |
| Her account | `default_user="name"` | The one account the OS has (a plain lower-case name). |

Where the installer shows its look page (everything except a kiosk, or a profile
that forbids settings or theme changes), it starts on your look and colours.
Where it does not, the OS keeps your look and she is not asked.

**What you cannot change yet:** the installer's own name and wording (it always
says AurOS); the wallpaper (`wallpaper_style` is read by nothing); which buttons
and programs the first screen shows (only a kiosk's `allowed_apps` and
`blocked_apps` touch it); and `update_channel`, `auto_login`, `enrollment_url`,
which are written down in the image and read by nothing.

A complete example, `profiles/gran.profile` (the games come from Ubuntu):

```
inherit="desktop"
profile_id="gran"
profile_name="AurOS for Gran"
profile_description="Big and simple, with two games."
os_codename="Quarry"
theme="moss"
shell_archetype="rail"
packages_apps="gnome-mahjongg gnome-sudoku"
```

`profile_id` must be the file name. Commit the file. Nothing else is edited:
no list, no code, no workflow.

## 2. Build it

The simplest way, from a checkout with the file committed:

```
git push origin HEAD:refs/heads/makeos/gran
```

The part after the last `/` is the profile. Or, once `make-os.yml` and your
profile are on `main`:

```
gh workflow run make-os.yml -R aarohkandy/v0.0.1-auros -f profile=gran
```

Add `-f windows=false` to skip the Windows journey (the last, longest step).
Watch it with `gh run watch -R aarohkandy/v0.0.1-auros`. If the name is wrong,
or the release already exists, it stops in seconds and says what to do. A rerun
of the same commit needs the old release removed first; the message gives the
command. On a Linux machine instead: `sudo build/make-os gran --base-url
https://github.com/OWNER/REPO/releases/download/TAG/`, then upload the files in
`out/os/gran/` to that release yourself (`docs/HANDOFF-RUNBOOK.md`).

## 3. What the run proves, and what it cannot

It builds the image and the installer, then on that exact image: the installer
installs this profile and no other; the image starts to its own desktop under
UEFI; the look and colours the installer writes are the ones it shows; a whole
install and the way back work over a pretend Windows disk with no memory stick.
Then, on a real Windows 11 in a virtual machine: the website, the download,
Windows' warnings, the installer, the restart, AurOS starting. Steps that cannot
apply (a kiosk has no store) are skipped by a stated rule, and the summary lists
each as ran or SKIPPED with the reason.

It cannot prove anything about a real computer: no real computer has run AurOS
yet. The installer is not signed, so Windows warns before it starts it.

## 4. Where it appears, and what to send

The run publishes a prerelease on the repository's releases page named
`os-gran-<first 7 of the commit>`. It is not the public download and nothing on
the website links to it. The run's summary page ends with text to paste to her:
the address

`https://github.com/aarohkandy/v0.0.1-auros/releases/download/os-gran-1234567/AurOS-Setup-gran.exe`

and the file's SHA-256. She checks it in PowerShell with
`Get-FileHash $HOME\Downloads\AurOS-Setup-gran.exe`; the code must match. Tell her
about the two Windows warnings (Edge: Keep, then Keep anyway; the blue panel:
More info, then Run anyway). Never send her the "Latest" link: that is the
desktop.

## 5. Try it without a computer

Download the pieces and put them back together:

```
gh release download os-gran-1234567 -R aarohkandy/v0.0.1-auros -p 'auros-gran.img.gz.*' -p 'image-gran.sha256'
cat auros-gran.img.gz.* | gunzip > auros-gran.img
shasum -a 256 auros-gran.img      # must equal image-gran.sha256
```

On a Mac (`brew install qemu`; it is slow, a few minutes to the desktop):

```
qemu-img create -f qcow2 -F raw -b "$PWD/auros-gran.img" overlay.qcow2 16G
cp /opt/homebrew/share/qemu/edk2-i386-vars.fd vars.fd
qemu-system-x86_64 -machine q35 -accel tcg,thread=multi -cpu max -smp 4 -m 4G \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd \
  -drive if=pflash,format=raw,unit=1,file=vars.fd \
  -drive file=overlay.qcow2,format=qcow2,if=none,id=d0 -device virtio-blk-pci,drive=d0,bootindex=0 \
  -device VGA,edid=on,xres=1280,yres=800 -display cocoa \
  -device qemu-xhci,id=xhci -device usb-tablet,bus=xhci.0 -device usb-kbd,bus=xhci.0 \
  -nic user,model=virtio-net-pci
```

On an Intel Mac use `/usr/local/share/qemu/` and `-accel hvf`. On Linux with KVM
(`sudo apt install qemu-system-x86 ovmf`), the same, with `-machine q35,accel=kvm
-cpu host`, `-display gtk`, and the firmware from `/usr/share/OVMF/OVMF_CODE_4M.fd`
and a copy of `/usr/share/OVMF/OVMF_VARS_4M.fd`. If it asks whether everything
works, that question belongs to a real install; you can ignore it.

## 6. Updates

Ubuntu's security and bug fixes install by themselves every day, in the
background (`docs/security/UPDATES.md`). That is the same on every profile. Her
programs from Flathub update too, where the profile has Flathub. But AurOS's
update channel (a fix to the desktop, the start-up code, the way back to
Windows) publishes the desktop profile only. In every other image forge writes
`update_channel=off` in `/etc/auros/build-warnings`, and `update_channel="managed"`
in a profile does nothing. So a custom OS keeps the AurOS parts it was built
with. A newer AurOS means you make a new installer and she installs it. There is
no way to update her in place yet. Ubuntu 24.04 is supported until April 2029.
The update tests run on the desktop image, not on yours.

## 7. Taking it off

Settings, then Put Windows back, then Remove AurOS and put Windows back; it copies
her documents into Windows first. A profile with `allow_settings_change="no"` (a
kiosk) has no Settings; its start-up menu, shown for a few seconds each time the
computer starts, has "Put Windows back (remove AurOS)" instead. That route is not
tested for a locked profile.
