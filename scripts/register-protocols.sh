#!/bin/sh
set -eu
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 /absolute/path/BattleSpadesClient [--include-classic]" >&2
    exit 2
fi
client=$1
case "$client" in /*) ;; *) echo 'Use an absolute executable path.' >&2; exit 2;; esac
case "$client" in *'"'*|*'`'*|*'$'*|*'%'*|*'\'*|*'
'*) echo 'Unsupported character in executable path.' >&2; exit 2;; esac
[ -f "$client" ] && [ -x "$client" ] || { echo 'Client executable not found.' >&2; exit 2; }
classic=false
if [ "$#" -eq 2 ]; then
    [ "$2" = '--include-classic' ] || exit 2
    classic=true
fi
data_dir=${XDG_DATA_HOME:-"$HOME/.local/share"}
mkdir -p "$data_dir/applications"
desktop="$data_dir/applications/battlespades-join.desktop"
cat > "$desktop" <<EOF
[Desktop Entry]
Type=Application
Name=BattleSpades join link
Exec="$client" --join-url %u
NoDisplay=true
Terminal=false
MimeType=x-scheme-handler/aosbb;x-scheme-handler/aos;
EOF
xdg-mime default battlespades-join.desktop x-scheme-handler/aosbb
if "$classic"; then xdg-mime default battlespades-join.desktop x-scheme-handler/aos; fi
echo 'Registered BattleSpades join links for the current user.'
