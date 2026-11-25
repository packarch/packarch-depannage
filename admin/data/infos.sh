# Infos utiles d'un ordinateur dépanné, pour Packarch Admin : envoyé à « sh -s »
# en root par Tailscale SSH. Ne fait que lire ; chaque commande absente est
# sautée. Une ligne « Libellé<tabulation>valeur » par information, sous des
# lignes de section « == Titre == » : Packarch Admin en fait un tableau.
export LC_ALL=C
have() { command -v "$1" >/dev/null 2>&1; }
row() {
    v="$(printf '%s' "$2" | tr '\t' ' ' | sed 's/^ *//; s/ *$//')"
    [ -n "$v" ] && printf '%s\t%s\n' "$1" "$v"
    return 0
}
first() { head -n 1; }
section() { printf '\n== %s ==\n' "$1"; }
file() { [ -r "$1" ] && tr -d '\0' < "$1" | first; }

section "Système"
row "Système" "$(. /etc/os-release 2>/dev/null; echo "$PRETTY_NAME")"
row "Noyau" "$(uname -r)"
row "Allumé depuis" "$(uptime -p 2>/dev/null | sed 's/^up //; s/ weeks\?/ sem./; s/ days\?/ j/; s/ hours\?/ h/; s/ minutes\?/ min/')"
row "Dernière mise à jour" "$(grep -a 'starting full system upgrade' /var/log/pacman.log 2>/dev/null | tail -n 1 | cut -c2-17 | tr T ' ')"
have pacman && row "Mises à jour connues" "$(pacman -Qu 2>/dev/null | wc -l) (d'après la dernière synchronisation)"
row "Services en échec" "$(systemctl --failed --no-legend --plain 2>/dev/null | awk '{print $1}' | tr '\n' ' ')"
for p in packarch-depannage packarch-desktop packarch-desktop-settings packarch-openbox-settings packarch-xfce-settings packarch-skin; do
    row "$p" "$(pacman -Q "$p" 2>/dev/null | awk '{print $2}')"
done

section "Session"
if have packarch-depannage; then
    packarch-depannage info 2>/dev/null | grep -E '^(Utilisateur|Bureau|Écran|Thème|Fin de session) ' | sed 's/^\([^:]*[^ :]\) *: /\1: /' |
        while IFS= read -r l; do row "${l%%: *}" "${l#*: }"; done
    # Écrans et définition, dans le bureau de l'utilisateur (Wayland puis X11)
    out="$(packarch-depannage session-run 'if [ -n "$WAYLAND_DISPLAY" ] && command -v wlr-randr >/dev/null; then wlr-randr | awk "/^[^ ]/{n=\$1} /current/{printf \"%s %s %.0f Hz\\n\", n, \$1, \$3}"; elif command -v xrandr >/dev/null; then xrandr --current | awk "/ connected/{n=\$1} /\\*/{print n\" \"\$1\" \"\$2}"; fi' 2>/dev/null | tr '\n' ';' | sed 's/;$//; s/;/ ; /g')"
    row "Écrans" "$out"
fi
row "Gestionnaire de connexion" "$(systemctl list-units --type=service --state=running --no-legend --plain 2>/dev/null | awk '{print $1}' | grep -E '^(sddm|lightdm|gdm|ly|greetd)' | sed 's/\.service$//' | tr '\n' ' ')"

section "Matériel"
row "Ordinateur" "$(file /sys/class/dmi/id/sys_vendor) $(file /sys/class/dmi/id/product_name)"
row "Carte mère" "$(file /sys/class/dmi/id/board_vendor) $(file /sys/class/dmi/id/board_name)"
row "BIOS" "$(file /sys/class/dmi/id/bios_version) ($(file /sys/class/dmi/id/bios_date))"
[ -d /sys/firmware/efi ] && row "Démarrage" "UEFI" || row "Démarrage" "BIOS (hérité)"
row "Processeur" "$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//') ($(nproc) fils)"
row "Mémoire" "$(free -h 2>/dev/null | awk '/^Mem:/{print $2" au total, "$7" disponibles"}')"
row "Échange (swap)" "$(free -h 2>/dev/null | awk '/^Swap:/{print $2" au total, "$3" utilisés"}')"
if have lspci; then
    lspci -k 2>/dev/null | awk '
        /VGA compatible controller|3D controller|Display controller/ { sub(/^[^ ]+ [^:]+: /, ""); gpu=$0; next }
        gpu != "" && /Kernel driver in use/ { sub(/.*: /, ""); printf "Carte graphique\t%s (pilote %s)\n", gpu, $0; gpu=""; next }
        gpu != "" && /^[0-9a-f]/ { printf "Carte graphique\t%s (aucun pilote chargé)\n", gpu; gpu="" }
        END { if (gpu != "") printf "Carte graphique\t%s (aucun pilote chargé)\n", gpu }'
fi
row "Pilotes graphiques" "$(pacman -Qq 2>/dev/null | grep -E '^(mesa|nvidia[a-z0-9-]*|vulkan-(intel|radeon|nouveau)|xf86-video-[a-z]+|intel-media-driver|libva-mesa-driver)$' | tr '\n' ' ')"
for b in /sys/class/power_supply/BAT*; do
    [ -r "$b/capacity" ] && row "Batterie" "$(cat "$b/capacity") % ($(file "$b/status"))"
done

section "Disques"
have lsblk && lsblk -d -n -o NAME,SIZE,ROTA,MODEL 2>/dev/null | grep -v -E '^(loop|zram|sr)' |
    awk '{ kind = ($3 == "1") ? "disque dur" : "SSD"; name=$1; size=$2; $1=$2=$3=""; sub(/^ +/, ""); printf "%s\t%s, %s%s\n", name, size, kind, ($0 != "" ? ", " $0 : "") }'
df -h -x tmpfs -x devtmpfs -x efivarfs -x overlay 2>/dev/null | awk 'NR>1 && !seen[$1]++ { printf "%s\t%s libres sur %s (%s utilisés, %s)\n", $6, $4, $2, $5, $1 }' | head -n 8
row "Système de fichiers" "$(findmnt -n -o FSTYPE / 2>/dev/null)"

section "Réseau"
if have ip; then
    row "Sortie Internet" "$(ip route show default 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i=="dev") print $(i+1)}' | sort -u | tr '\n' ' ')"
    ip -brief -4 addr show scope global 2>/dev/null | awk '{ printf "%s\t%s\n", $1, $3 }'
fi
have nmcli && row "Wi-Fi" "$(nmcli -t -f active,ssid,signal dev wifi 2>/dev/null | awk -F: '$1=="yes"{print $2" (signal "$3" %)"}' | first)"
row "Internet" "$(ping -c 1 -W 3 archlinux.org >/dev/null 2>&1 && echo 'joignable' || echo 'injoignable (ou ping bloqué)')"
exit 0
