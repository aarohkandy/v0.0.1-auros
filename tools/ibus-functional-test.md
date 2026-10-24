# Multilingual IBus functional check

This check is evidence for a built and booted `multilingual` image. Passing
`tools/ibustest.sh` alone does not prove that input works.

1. Boot `out/auros-multilingual.img` into its real AurOS session. From a
   technician connection running as the desktop user, record:

       systemctl --user is-active auros-ibus.service
       systemctl --user show-environment | grep -E '^(GTK_IM_MODULE|QT_IM_MODULE|XMODIFIERS|WAYLAND_DISPLAY|DISPLAY)='
       ibus list-engine --name-only | grep -Fx pinyin

   The service must be `active`, all three input-method variables must have
   the values from `/etc/auros/input-method.env`, and a Pinyin engine must be
   listed. A missing value or engine is a failure.

2. Run `ibus engine pinyin`. Open Mousepad from AurOS, focus a new document,
   type `nihao`, choose the `你好` candidate, save the document, and verify its
   UTF-8 contents from the technician connection with:

       python3 -c 'from pathlib import Path; assert Path("/path/from-mousepad").read_text() == "你好"'

   Record the saved file and a screenshot showing the candidate selection.
   This is the GTK proof.

3. From the technician connection, create and serve this local form:

       mkdir -p /tmp/ibus-form
       printf '%s\n' '<form method="get"><input name="text" autofocus><button>Submit</button></form>' > /tmp/ibus-form/index.html
       python3 -m http.server 8765 --directory /tmp/ibus-form 2>&1 | tee /tmp/ibus-firefox-http.log

   Open `http://127.0.0.1:8765/` in the image's Firefox, enter `nihao`, choose
   `你好`, and submit. The server log must contain exactly this query value:

       /?text=%E4%BD%A0%E5%A5%BD

   Also record `/proc/<firefox-pid>/environ` with secrets removed, showing
   `GTK_IM_MODULE=ibus`. This is the Firefox proof and avoids treating a
   screenshot alone as text evidence.

4. Restart the image and repeat steps 2 and 3 without manually starting
   `ibus-daemon`. Repeat once with the ordinary `desktop` image and verify that
   `auros-ibus.service` is not enabled and the three IBus variables are absent.

Until both application checks pass after a restart, report the edition as
“IBus wiring present; functional input unproven,” not as working Pinyin.

The boot harness automates this exact check when explicitly requested:

    sudo env PROFILETEST_IBUS_FUNCTIONAL=1 \
      sh tools/profiletest.sh boot multilingual out/profiletest-multilingual

The ordinary profile boot remains a single, cheaper boot. The functional mode
uses QMP's USB keyboard for all typed text, keeps screenshots of each candidate
selection, validates Mousepad's exact UTF-8 file and Firefox's exact local form
request, then reboots the same scratch disk and repeats both checks.
