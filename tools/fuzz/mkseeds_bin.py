#!/usr/bin/env python3
"""mkseeds_bin.py -- seed inputs for fuzz_inflate, fuzz_gpt, fuzz_journal, fuzz_aurwl.

Python standard library only. The output is committed under
tools/fuzz/corpus/<target>/, so this runs only when the seeds change:

    python3 tools/fuzz/mkseeds_bin.py
"""
import gzip
import json
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def out(target, name, data):
    d = os.path.join(HERE, "corpus", target)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "wb") as f:
        f.write(data)


# ── inflate: [piece size][gzip member] ────────────────────────────
def gz_raw(data, level=6, strategy=zlib.Z_DEFAULT_STRATEGY):
    c = zlib.compressobj(level, zlib.DEFLATED, 31, 9, strategy)
    return c.compress(data) + c.flush()


def inflate_seeds():
    text = (b"AurOS keeps Windows as it was. " * 40) + bytes(range(256)) * 3
    out("inflate", "stored", b"\x00" + gzip.compress(text, compresslevel=0, mtime=0))
    out("inflate", "fixed", b"\x07" + gz_raw(text, 9, zlib.Z_FIXED))
    out("inflate", "dynamic", b"\x01" + gz_raw(text, 9))
    out("inflate", "empty", b"\x00" + gzip.compress(b"", mtime=0))
    out("inflate", "far-back", b"\x00" + gz_raw(bytes(40000) + b"x" * 300 + bytes(40000), 9))
    # Every optional header field: FTEXT|FHCRC|FEXTRA|FNAME|FCOMMENT.
    body = zlib.compressobj(9, zlib.DEFLATED, -15)
    deflated = body.compress(text) + body.flush()
    hdr = b"\x1f\x8b\x08\x1f" + b"\0\0\0\0\0\xff"
    hdr += struct.pack("<H", 4) + b"AB\0\0" + b"name.img\0" + b"comment\0"
    hdr += struct.pack("<H", zlib.crc32(hdr) & 0xFFFF)
    trailer = struct.pack("<II", zlib.crc32(text), len(text))
    out("inflate", "all-header-fields", b"\x03" + hdr + deflated + trailer)


# ── gpt: [sector byte][disk image] ────────────────────────────────
def guid(n):
    return bytes((n * 37 + i * 11) & 0xFF for i in range(16))


ESP = bytes.fromhex("28732ac11ff8d211ba4b00a0c93ec93b")
MSDATA = bytes.fromhex("a2a0d0ebe5b9334487c068b6b72699c7")


def gpt_disk(ss, sectors, n_ent, parts, corrupt_primary=False):
    disk = bytearray(ss * sectors)
    arr_bytes = n_ent * 128
    arr_secs = (arr_bytes + ss - 1) // ss
    first_usable = 2 + arr_secs
    last_usable = sectors - 2 - arr_secs
    arr = bytearray(arr_bytes)
    for i, (t, a, b, name) in enumerate(parts):
        e = t + guid(i + 1) + struct.pack("<QQQ", a, b, 0) + name.encode("utf-16-le").ljust(72, b"\0")
        arr[i * 128:(i + 1) * 128] = e
    acrc = zlib.crc32(arr)

    def header(my, alt, entry_lba):
        h = bytearray(b"EFI PART" + struct.pack("<IIII", 0x10000, 92, 0, 0))
        h += struct.pack("<QQQQ", my, alt, first_usable, last_usable)
        h += guid(99) + struct.pack("<QIII", entry_lba, n_ent, 128, acrc)
        struct.pack_into("<I", h, 16, zlib.crc32(h))
        return bytes(h)

    alt = sectors - 1
    disk[ss:ss + 92] = header(1, alt, 2)
    disk[2 * ss:2 * ss + arr_bytes] = arr
    barr = alt - arr_secs
    disk[barr * ss:barr * ss + arr_bytes] = arr
    disk[alt * ss:alt * ss + 92] = header(alt, 1, barr)
    if corrupt_primary:
        disk[ss + 20] ^= 0xFF
    return bytes(disk)


def gpt_seeds():
    parts = [(ESP, 6, 9, "EFI system partition"), (MSDATA, 10, 40, "Basic data partition")]
    out("gpt", "512-four-entries", b"\x00" + gpt_disk(512, 64, 4, parts))
    out("gpt", "512-backup-only", b"\x00" + gpt_disk(512, 64, 4, parts, corrupt_primary=True))
    out("gpt", "4096-four-entries", b"\x01" + gpt_disk(4096, 16, 4, [(ESP, 3, 5, "EFI"), (MSDATA, 6, 12, "Windows")]))
    # 128 entries fill LBA 2-33, so the partitions start after them.
    out("gpt", "512-128-entries", b"\x00" + gpt_disk(512, 100, 128,
        [(ESP, 34, 40, "EFI system partition"), (MSDATA, 41, 60, "Basic data partition")]))


# ── journal: what AurBridge writes before the restart ─────────────
DISK_HASH = "8f434346648f6b96df89dda901c5176b10a6d83961dd3c1ac88b59b2dc327aa4"


def journal_seeds():
    base = {
        "disk_serial": "S4EW NX0N-123456", "disk_model": "Samsung SSD 860",
        "disk_bytes": 256060514304, "logical_sector": 512,
        "win_part": "3", "win_start_lba": 1288192, "win_sectors": 497000000,
        "win_ntfs_serial": 1234567890123, "gpt_sha256": DISK_HASH,
        "stage": "armed", "boot_from": "esp", "profile": "desktop",
        "image_on": "stick", "run_id": 42, "written_unix": 1761200000,
    }
    out("journal", "stick.json", json.dumps(base).encode())
    gap = dict(base, image_on="gap", win_orig_sectors=600000000,
               gap_root_first=498300000, gap_root_last=560000000,
               gap_rsc_first=560000001, gap_rsc_last=560100000,
               gap_rec_first=560100001, gap_rec_last=560200000,
               root_len=8000000000, root_sha256="ab" * 32,
               boot_len=200000000, boot_sha256="CD" * 32,
               rsc_bytes=50000000, min_root_bytes=7000000000,
               bitlocker=1, wifi=1)
    out("journal", "gap.json", json.dumps(gap, indent=1).encode())
    esc = dict(base, disk_serial="0025_3886_81B7_8F17.", win_part='C:\\Windows\t\n/"')
    out("journal", "escapes.json", json.dumps(esc).encode())


# ── aurwl: the request encoding fuzz_aurwl.c reads ────────────────
# Each step: one byte (low 3 bits 7 = a shell step, else top bit = which
# program), then for a request: object index, request index, arguments.
def num(v):
    if -8 <= v <= 55:
        return bytes([v + 8])
    return b"\x80" + struct.pack("<i" if v < 0 else "<I", v)


SERIAL = bytes([0x40 | 18])          # "the serial the compositor just sent"


def s(text):
    b = text.encode()
    assert len(b) <= 16          # longer strings are synthesised after 16 input bytes
    return bytes([len(b)]) + b


def aurwl_window(prog=0x00):
    p = bytes([prog])
    q = b""
    q += p + b"\x00\x01"                           # display.get_registry -> [1]
    q += p + b"\x01\x00" + b"\x00\x04"             # bind compositor v5 -> [2]
    q += p + b"\x01\x00" + b"\x09\x00"             # bind shm v1 -> [3]
    q += p + b"\x01\x00" + b"\x04\x04"             # bind xdg_wm_base v5 -> [4]
    q += p + b"\x01\x00" + b"\x02\x06"             # bind seat v7 -> [5]
    q += p + b"\x02\x00"                           # create_surface -> [6]
    q += p + b"\x03\x00" + b"\x01" + bytes([0x40 | 10])   # create_pool(fd 16 KiB, 16384) -> [7]
    q += p + b"\x07\x00" + num(0) + num(32) + num(32) + num(128) + num(0)  # create_buffer -> [8]
    q += p + b"\x04\x02" + b"\x00"                 # get_xdg_surface(surface) -> [9]
    q += p + b"\x09\x01"                           # get_toplevel -> [10]
    q += p + b"\x0a\x02" + s("Letters")            # set_title
    q += p + b"\x0a\x03" + s("org.example.App")  # set_app_id
    q += p + b"\x06\x06"                           # commit (asks for a configure)
    q += p + b"\x09\x04" + SERIAL                  # ack_configure
    q += p + b"\x06\x01" + b"\x00" + num(0) + num(0)      # attach(buffer)
    q += p + b"\x06\x09" + num(0) + num(0) + num(32) + num(32)  # damage_buffer
    q += p + b"\x06\x06"                           # commit: mapped
    q += p + b"\x05\x00"                           # seat.get_pointer -> [11]
    q += p + b"\x05\x01"                           # seat.get_keyboard -> [12]
    return q


def shell(case, *args):
    return b"\x07" + b"\x00" + b"\0\0\0\0" + bytes([case]) + b"".join(args)


def aurwl_seeds():
    win = aurwl_window()
    paint = (shell(0, bytes([0x40 | 8, 0x40 | 7, 1, 0, 0])) + shell(1) +
             shell(2, num(5), num(5)) + shell(3, b"\x00\x01") + shell(5, b"\x1e\x01") +
             shell(7) + shell(6))
    out("aurwl", "window", win + paint)
    # Program 1 puts something on the clipboard while program 0 has focus.
    clip = (b"\x80\x00\x01" + b"\x80\x01\x00\x02\x06" + b"\x80\x01\x00\x05\x02" +
            b"\x80\x03\x00" +                      # ddm.create_data_source -> [4]
            b"\x80\x04\x00" + s("text/plain") +    # source.offer
            b"\x80\x03\x01\x00" +                  # ddm.get_data_device(seat) -> [5]
            b"\x80\x05\x01\x00" + SERIAL)          # device.set_selection(source, serial)
    out("aurwl", "clipboard", win + paint + clip + shell(1) + shell(8) + shell(9, b"\x07"))
    popup = (b"\x00\x04\x01" +                     # wm.create_positioner -> [13]
             b"\x00\x0d\x01" + num(10) + num(10) +  # set_size
             b"\x00\x0d\x02" + num(0) + num(0) + num(5) + num(5) +  # set_anchor_rect
             b"\x00\x02\x00" +                     # create_surface -> [14]
             b"\x00\x04\x02\x01" +                 # get_xdg_surface(surface 2) -> [15]
             b"\x00\x0f\x02\x00\x00" +             # get_popup(parent xdg_surface 0, positioner)
             b"\x00\x0e\x06")                      # commit
    out("aurwl", "popup", win + popup + shell(7) + shell(10, b"\x05\x05"))

    # Subsurfaces (stream WL). After aurwl_window(): [13] wl_subcompositor,
    # [14] wp_viewporter, [15] a second surface, [16] its wl_subsurface (child
    # of [6]), [17] its wp_viewport. wl_subsurface: destroy 0, set_position 1,
    # place_above 2, place_below 3, set_sync 4, set_desync 5. An object
    # argument is "the Nth object of that interface": 0 is the window's own
    # surface, 1 the second.
    sub = (b"\x00\x01\x00\x01\x00" +             # bind wl_subcompositor -> [13]
           b"\x00\x01\x00\x07\x00" +             # bind wp_viewporter -> [14]
           b"\x00\x02\x00" +                       # create_surface -> [15]
           b"\x00\x0d\x01\x01\x00")              # get_subsurface(surface 1, parent 0) -> [16]
    child_pos = b"\x00\x10\x01" + num(5) + num(5)          # set_position(5, 5)
    child_paint = (b"\x00\x0f\x01\x00" + num(0) + num(0) +  # attach the buffer
                   b"\x00\x0f\x02" + num(0) + num(0) + num(32) + num(32) +  # damage
                   b"\x00\x0f\x06")                          # commit the child
    parent_commit = b"\x00\x06\x06"
    pointer = shell(2, num(10), num(10)) + shell(3, b"\x00\x01") + shell(2, num(200), num(60))
    out("aurwl", "subsurface", win + sub + child_pos + child_paint + parent_commit + shell(7) + pointer + shell(3, b"\x00\x00"))
    # synchronized: two commits before the parent's, a restack, desync, destroy.
    out("aurwl", "subsurface-sync", win + sub + child_paint + child_paint + parent_commit + shell(7) +
        b"\x00\x10\x03\x00" + parent_commit +            # place_below(parent)
        b"\x00\x10\x05" + b"\x00\x0f\x06" + shell(7) +    # set_desync, commit
        b"\x00\x10\x00" + parent_commit + shell(7))       # destroy
    # a viewport on the child, a source rectangle, an empty input region
    vp = (b"\x00\x0e\x01\x01" +                              # get_viewport(surface 1) -> [17]
          b"\x00\x11\x02" + num(40) + num(30) +              # set_destination
          b"\x00\x11\x01" + num(0) + num(0) + num(16) + num(16) +   # set_source
          b"\x00\x02\x01" +                                  # create_region -> [18]
          b"\x00\x0f\x05\x00")                              # set_input_region(region)
    out("aurwl", "subsurface-viewport", win + sub + vp + child_pos + child_paint + parent_commit + shell(7) + pointer)


# ── fuzz_textinput: fuzz_aurwl's encoding with the text-input manager (global
# 10, after wl_shm) in the registry. Objects after aurwl_window(): [13] the
# manager, [14] the text input. Request numbers: manager.get_text_input = 1;
# text_input: destroy 0, enable 1, disable 2, set_surrounding_text 3,
# set_text_change_cause 4, set_content_type 5, set_cursor_rectangle 6, commit 7.
def textinput_seeds():
    p = b"\x00"
    base = aurwl_window()
    base += p + b"\x01\x00" + b"\x0a\x00"            # bind text-input manager v1 -> [13]
    base += p + b"\x0d\x01" + b"\x05"                # manager.get_text_input(seat) -> [14]
    base += shell(1)                                   # the window gets the keyboard (and `enter`)
    def typing():
        # p o t a t o <space>, as evdev codes into aurwl_key
        return b"".join(shell(5, bytes([c, 1])) + shell(5, bytes([c, 0])) for c in (25, 24, 20, 30, 20, 24, 57))
    secret = (p + b"\x0e\x01" + p + b"\x0e\x05" + num(0x40 | 0x80) + num(8) + p + b"\x0e\x07")
    plain = (p + b"\x0e\x01" + p + b"\x0e\x05" + num(0) + num(0) + p + b"\x0e\x07")
    out("textinput", "password", base + secret + typing())
    out("textinput", "plain", base + plain + typing())
    # a field turns secret half-way through, the window goes, a second input enables
    out("textinput", "switch", base + plain + typing() + secret + typing() + shell(8) + shell(7) +
        p + b"\x0d\x01\x05" + p + b"\x0f\x01" + p + b"\x0f\x07" + plain + typing())
    out("textinput", "hostile", base + p + b"\x0e\x07" + p + b"\x0e\x03" + s("x" * 16) + num(100) + num(-5) +
        p + b"\x0e\x05" + num(-1) + num(99) + p + b"\x0e\x01" + p + b"\x0e\x01" + p + b"\x0e\x00" +
        p + b"\x0e\x07" + shell(1) + shell(9, b"\x07"))


# ── hive: [mode] KEY \n [NAME \n] regf, built by Ferry's own test generator
def hive_seeds():
    import subprocess, tempfile
    gen = os.path.join(HERE, "..", "..", "src", "ferry", "tests", "mkhive.py")
    spec = json.dumps({
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders": {
            "Personal": ["exp", "%OneDrive%\\Documents"],
            "My Video": ["exp", "D:\\Media\\Videos"],
            "\u00e9t\u00e9": ["sz", "Unicode name"]},
        "Control Panel\\Desktop": {"LogPixels": ["dword", 120], "TileWallpaper": ["sz", "0"]},
        "Keyboard Layout\\Preload": {"1": ["sz", "00000809"], "2": ["multi", ["a", "b"]]},
        "Policy\\Secrets": {"@": ["binary", "not a real secret"]},
    })
    with tempfile.TemporaryDirectory() as d:
        f = os.path.join(d, "NTUSER.DAT")
        subprocess.run([sys.executable, gen, f, spec], check=True)
        hive = open(f, "rb").read()
    folders = b"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders\n"
    out("hive", "values", b"\x00" + folders + hive)
    out("hive", "subkeys", b"\x01" + b"Software\\Microsoft\n" + hive)
    out("hive", "typed", b"\x02" + b"Control Panel\\Desktop\nLogPixels\n" + hive)
    out("hive", "default", b"\x03" + b"Policy\\Secrets\n@\n" + hive)


# ── wish: [door byte] then that door's input (see fuzz_wish.c) ────
def wish_seeds():
    potato = ('{"v":1,"when":{"type":"typed_word","word":"potato"},'
              '"do":[{"type":"fill_screen","emoji":"\U0001F954","seconds":3}]}')
    compass = ('{"v":1,"when":{"type":"sensor","sensor":"compass","heading":"north",'
               '"tolerance_deg":20},"do":[{"type":"brightness","set_percent":100}]}')
    remind = ('{"v":1,"when":{"type":"time","at":"09:00"},'
              '"do":[{"type":"remind","text":"Call mum"},{"type":"play_sound","sound":"chime"}]}')
    app = ('{"v":1,"when":{"type":"app_opened","app_name":"Firefox"},'
           '"do":[{"type":"volume","set_percent":30},{"type":"open_app","app_name":"VLC media player"}]}')
    out("wish", "json", b"\x00" + potato.encode())
    out("wish", "potato", b"\x01potato\n" + potato.encode())
    out("wish", "compass", b"\x01north\n" + compass.encode())
    out("wish", "remind", b"\x01remind me to call mum at nine\n" + remind.encode())
    out("wish", "app", b"\x01when I open firefox turn it down and open vlc\n" + app.encode())
    store = ('{"v":1,"next":5,"consent":1,"wishes":['
             '{"id":"w1","status":"on","said":"potato","wish":' + potato + '},'
             '{"id":"w2","status":"on","said":"x","wish":{"v":2,"when":{"type":"teleport"}}},'
             '{"id":"w3","status":"off","said":"remind me to call mum at nine","wish":' + remind + '},'
             '{"id":"w1","status":"on","said":"x","wish":{"v":1,"when":{"type":"now"},"do":[{"type":"speak_time"}]}}'
             ']}')
    out("wish", "store", b"\x02" + store.encode())
    lines = ['{"id":1,"op":"hello"}', '{"id":2,"op":"consent","version":1}',
             '{"id":3,"op":"list"}', '{"op":"typed","word":"potato"}', '{"id":4,"op":"today"}',
             '{"op":"app","app":"firefox"}', '{"op":"hold","reasons":["password","call"]}',
             '{"id":5,"op":"all_off"}', '{"id":6,"op":"undo"}', '{"id":7,"op":"wish","said":"x"}']
    out("wish", "requests", b"\x03" + "\n".join(lines).encode())


if __name__ == "__main__":
    inflate_seeds()
    gpt_seeds()
    journal_seeds()
    aurwl_seeds()
    textinput_seeds()
    hive_seeds()
    wish_seeds()
