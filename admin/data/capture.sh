# Captures de l'écran d'un ordinateur dépanné, pour Packarch Admin : lancé dans
# le bureau de l'utilisateur (packarch-depannage session-run, par Tailscale
# SSH). Une capture par ligne « QUALITÉ ÉCHELLE » lue sur l'entrée standard
# (qualité JPEG 1 à 100 ; échelle 1 ou 0.5, bureaux Wayland seulement). Réponse :
# « F OCTETS », puis l'image JPEG ; ou « E raison ». Tous les écrans, pointeur
# compris. L'image n'existe que le temps de l'envoi, en mémoire.
f="${XDG_RUNTIME_DIR:-/tmp}/packarch-admin-ecran.$$.jpg"
trap 'rm -f "$f"' EXIT
while read -r q s; do
    case "$q" in '' | *[!0-9]*) continue ;; esac
    [ "$q" -ge 1 ] && [ "$q" -le 100 ] || continue
    [ "$s" = 0.5 ] || s=1
    rm -f "$f"
    if [ -n "$WAYLAND_DISPLAY" ] && command -v grim >/dev/null 2>&1; then
        grim -c -t jpeg -q "$q" -s "$s" "$f" 2>/dev/null
    elif [ -n "$DISPLAY" ] && command -v maim >/dev/null 2>&1; then
        maim -f jpg -m "$(((q + 9) / 10))" "$f" 2>/dev/null
    else
        printf 'E aucun outil de capture (grim ou maim)\n'
        continue
    fi
    if [ -s "$f" ]; then
        printf 'F %s\n' "$(wc -c <"$f")"
        cat "$f"
    else
        printf 'E capture impossible\n'
    fi
done
