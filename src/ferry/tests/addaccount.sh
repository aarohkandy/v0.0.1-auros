#!/bin/sh
# Give a fake Windows volume (tests/mkfakewin.sh) a second real account,
# the way a grandson's administrator account or a husband's appears on a
# real PC: a ProfileList entry and a profile folder, with or without
# files of its own.
#
#   tests/addaccount.sh ROOT NAME [files]
set -eu
ROOT=${1:?usage: addaccount.sh ROOT NAME [files]}
NAME=${2:?}
HERE=$(cd "$(dirname "$0")" && pwd)
SOFT="$ROOT/Windows/System32/config/SOFTWARE"
python3 "$HERE/mkhive.py" "$SOFT" '{
  "Microsoft\\Windows NT\\CurrentVersion": {
    "ProductName": ["sz", "Windows 10 Pro"], "DisplayVersion": ["sz", "22H2"],
    "CurrentBuild": ["sz", "19045"], "EditionID": ["sz", "Professional"]
  },
  "Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\S-1-5-21-1111111111-2222222222-3333333333-1001": {
    "ProfileImagePath": ["exp", "C:\\Users\\Bob"]
  },
  "Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\S-1-5-21-1111111111-2222222222-3333333333-1002": {
    "ProfileImagePath": ["exp", "C:\\Users\\'"$NAME"'"]
  },
  "Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\S-1-5-18": {
    "ProfileImagePath": ["exp", "C:\\Windows\\system32\\config\\systemprofile"]
  }
}'
mkdir -p "$ROOT/users/$NAME/Desktop" "$ROOT/users/$NAME/Documents" "$ROOT/users/$NAME/Pictures"
if [ "${3:-}" = files ]; then
    echo "$NAME's private letter"  > "$ROOT/users/$NAME/Documents/private-letter.txt"
    echo "$NAME's photo"           > "$ROOT/users/$NAME/Pictures/his-photo.jpg"
else
    : > "$ROOT/users/$NAME/Desktop/desktop.ini"
fi
