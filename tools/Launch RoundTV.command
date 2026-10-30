#!/bin/bash
cd -- "$(dirname -- "$0")" || exit 1
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
for candidate in python3.13 python3.12 python3.14 python3; do
  if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c 'import tkinter; assert tkinter.TkVersion >= 8.6' >/dev/null 2>&1; then
    "$candidate" prepare_media.py
    result=$?
    if [ "$result" -ne 0 ]; then read -r -p 'Press Return to close.'; fi
    exit "$result"
  fi
done
printf '%s\n' 'Python with Tk is required. Install with Homebrew:' 'brew install python@3.13 python-tk@3.13 ffmpeg' '' 'Then double-click this launcher again.'
read -r -p 'Press Return to close.'
