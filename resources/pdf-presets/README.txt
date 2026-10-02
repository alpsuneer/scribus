PDF export presets shipped with the package (read-only).

Each *.json file is one preset as saved by File > Export > Save as PDF.
office-default.txt, if present, holds the name of the preset that is the
Default on a PC whose user has not chosen a Default of their own.

Do not edit by hand: tools/update-office-pdf-presets.py (run by
tools/release.sh) regenerates this folder from the release laptop's presets
marked "Office preset". Passwords are never copied here.

A user's own presets live in ~/.config/scribus/pdf-presets/ and are not
touched by an update. A user preset with the same name hides the office one.
