# packarch-depannage

Petit logiciel en C++ ([Qt 6](https://www.qt.io)) pour le **dépannage à distance** d'un ordinateur
[Packarch](https://github.com/packarch/Packarch) par une personne de confiance, en
ligne de commande, à travers [Tailscale](https://tailscale.com) et Tailscale SSH.

Deux programmes, dans ce dépôt :

- **packarch-depannage** (Packarch Dépannage), installé avec Packarch sur l'ordinateur à
  dépanner : il ne contient que ce qu'il faut pour être dépanné ;
- **packarch-admin** (Packarch Admin, dossier `admin`), pour la personne qui dépanne, sur son
  ordinateur seulement.

Ils ne se parlent que par des commandes lancées en Tailscale SSH sur l'ordinateur dépanné
(`packarch-depannage info`, `session-run`, `chat-follow`, `chat-say`) : ils peuvent être de
versions différentes (`common` : ce qu'ils partagent, le format du tchat surtout).
Jusqu'à la version 0.3.7 (tag `v0.3.7`), le projet était en Rust (egui) et tenait en un seul
programme.

## Fonctionnement

1. La personne à dépanner ouvre **Packarch Dépannage** (menu Système) et clique sur
   « Autoriser le dépannage (1 h) » : rien ne s'ouvre sans ce clic. Aucun mot de passe n'est
   demandé à un administrateur de l'ordinateur (groupe `wheel`, qui a déjà `sudo` sans mot de
   passe sur Packarch) ; un autre compte doit donner celui d'un administrateur.
2. La fenêtre affiche un **lien de connexion Tailscale** (`https://login.tailscale.com/a/…`)
   et le nom de l'ordinateur (`packarch-<nom d'hôte>`). Elle l'envoie à la personne qui
   dépanne (SMS, e-mail…).
3. La personne qui dépanne ouvre le lien, connectée à **son** compte Tailscale, et valide :
   l'ordinateur rejoint son réseau Tailscale avec le tag `tag:depannage`.
4. Elle s'y connecte en root : `ssh root@packarch-<nom>` (Tailscale SSH : ni mot de passe,
   ni clé, c'est son identité Tailscale qui l'autorise).
   Pendant qu'elle travaille, la fenêtre affiche « Dépannage en cours » avec son compte
   Tailscale (nom et adresse) et l'appareil d'où elle travaille, tant qu'il y a eu de
   l'activité dans les 5 dernières minutes (les commandes passent souvent par des connexions
   SSH très courtes : l'activité réseau Tailscale est suivie, pas seulement les connexions
   ouvertes).
5. Au bout d'une heure (« +1 h » pour prolonger), ou avec « Arrêter maintenant », la session
   s'arrête : `tailscale logout` (l'ordinateur quitte le réseau), puis arrêt de `tailscaled`.

## Sans bureau : mode texte

Si le bureau de la personne ne démarre pas, ou si la fenêtre ne s'ouvre pas, le dépannage se
lance dans une console (Ctrl+Alt+F3, puis son nom et son mot de passe) :

```
packarch-depannage
```

Sans bureau, la commande passe toute seule en mode texte (`packarch-depannage texte` le
force) : « Autoriser le dépannage pendant 1 heure ? », puis le lien à
transmettre s'il en faut un, l'état, et le tchat (une ligne tapée est un message). `/plus`
prolonge, `/arreter` arrête, `/quitter` quitte en laissant la session ouverte.

Les commandes et le mode texte n'utilisent que Qt Core : la fenêtre est un programme à part
(`/usr/lib/packarch-depannage/packarch-depannage-gui`, lancé par `packarch-depannage`). Un Qt
graphique cassé, un pilote ou un compositeur en panne ne les empêchent pas de fonctionner.

## Tchat

Dès que l'ordinateur est connecté, sa fenêtre affiche un tchat. La personne qui dépanne ouvre
**Packarch Admin** (il lui faut Tailscale connecté à son réseau) et choisit l'ordinateur dans
la liste : le tchat est à droite de sa page, aucune commande à taper. Une notification signale un message si la fenêtre n'est pas au premier
plan ; si la personne dépannée a fermé la sienne, un message reçu la rouvre.

Chaque message commence par le nom de celui qui l'écrit : le nom de session de la personne
dépannée, celui de la personne qui dépanne, ou « Claude » pour l'assistant. Claude Code ne
lit pas le tchat et n'y écrit pas, sauf si la personne qui dépanne coche « Claude peut lire le
tchat et y écrire » dans la page de l'ordinateur, ou le lui demande expressément.

Les deux côtés ont un bouton d'émoticônes, et celles qui sont tapées au clavier (`:)`, `;)`,
`<3`…) sont remplacées à l'envoi. La personne dépannée voit le nom sous lequel la personne
qui dépanne se présente : son nom de session, donné par Packarch Admin à chaque dépannage
(`packarch-depannage helper-set`), plutôt que celui de son compte Tailscale.

L'ordinateur dépanné ne pouvant joindre personne, c'est Packarch Admin, chez la personne qui dépanne,
qui vient lire et écrire les messages par Tailscale SSH. Ils sont dans
`/run/packarch-depannage/chat.jsonl` : en mémoire, effacés à la fin de la session. Tant que le
tchat est ouvert de son côté, l'ordinateur dépanné affiche « Dépannage en cours ».

## Packarch Admin (personne qui dépanne)

La fenêtre liste les ordinateurs à dépanner du réseau Tailscale. La page d'un ordinateur a,
à gauche, des **terminaux intégrés** en onglets et, à droite, le tchat avec la personne :

- « Terminal (root) » : une session root sur l'ordinateur (Tailscale SSH), pour intervenir
  soi-même ; autant d'onglets que voulu, Ctrl+Maj+C et Ctrl+Maj+V pour copier et coller ;
- « Dépanner avec Claude Code » : [Claude Code](https://claude.com/claude-code), s'il est
  installé, avec la demande « Ouvre un dépannage sur packarch-<nom> ».

- « Écran » : l'**écran de la personne** (tous ses écrans, pointeur compris), en captures
  continues dont le rythme suit le débit de sa connexion (« Légère » : demi-taille, pour une
  connexion lente ; « Pause » ; « Enregistrer l'image »). Avec « Contrôler (souris et
  clavier) », les gestes faits sur l'image sont rejoués chez elle : pointeur, clics, molette,
  touches (celles du clavier, dans la disposition de son ordinateur). Rien n'est capturé quand
  l'onglet n'est pas affiché.

L'écran ne demande rien de plus sur l'ordinateur dépanné, quelle que soit sa version de
packarch-depannage : les captures sont faites par `grim` (bureau Packarch, Wayland) ou `maim`
(Openbox, XFCE), déjà installés, dans le bureau de l'utilisateur (`session-run`) ; la saisie
par deux périphériques d'entrée virtuels du noyau (`/dev/uinput`), créés par un script Python
de la bibliothèque standard et retirés à la fin. Les scripts sont dans `admin/data`.

Le premier onglet, « Ordinateur », affiche les **infos de l'ordinateur**, lues à l'ouverture
de la page : système (noyau, dernière mise à jour, services en échec, versions des paquets
Packarch), session (utilisateur, bureau, écrans, thème), matériel (modèle, processeur, mémoire,
carte graphique et son pilote, batterie), disques (place libre) et réseau. « Actualiser » les
relit, « Copier » les met dans le presse-papiers. Elles sont lues par un script de Packarch
Admin envoyé par Tailscale SSH (`admin/data/infos.sh`, qui ne fait que lire) : rien n'est
installé sur l'ordinateur, et toutes les versions de packarch-depannage conviennent.

Une page vaut pour une session : quand la personne rouvre un dépannage, son ordinateur
revient dans la liste et l'ancienne page reste, marquée « (terminée) », avec ses onglets (un
Claude Code qui doit encore refaire ici ce qu'il a fait là-bas, par exemple). Aucune page ne
se ferme toute seule : « Fermer la page », après confirmation, arrête ses terminaux.

Fermer la fenêtre la **réduit dans la zone de notification** : les terminaux et le tchat
continuent, un clic sur l'icône la rouvre telle quelle (« Quitter » est dans le menu de
l'icône, qui liste aussi les ordinateurs). L'icône est lancée à l'ouverture du bureau et
change quand un ordinateur attend. Dès qu'un ordinateur apparaît dans le réseau (le lien vient
d'être validé), une notification « Dépannage : packarch-<nom> » s'affiche et reste à l'écran ;
un clic dessus ouvre la fenêtre sur cet ordinateur. Plusieurs ordinateurs peuvent être
dépannés en même temps, chacun a sa page et sa notification.

Rien n'est envoyé nulle part : Packarch Admin relit l'état du Tailscale de l'ordinateur. Le
nom d'un ordinateur vient de cet ordinateur : s'il n'a pas la forme que lui donne
packarch-depannage (minuscules, chiffres, tirets), il n'est jamais repris dans la demande à
Claude Code.

```
packarch-admin                 fenêtre (une seule instance : la rouvre si elle est réduite)
packarch-admin --tray          démarre réduit dans la zone de notification
packarch-admin --essai NOM     essai de la notification, comme si NOM arrivait
packarch-admin tchat-suivre NOM   pour l'assistant : messages de la personne, case cochée seulement
packarch-admin tchat-dire NOM     pour l'assistant : envoie l'entrée standard, signée Claude, case cochée seulement
```

## Clé de connexion : le lien, une seule fois

Sans réglage, la personne dépannée envoie le lien à chaque session. Pour qu'elle n'ait à le
faire que la première fois, la personne qui dépanne crée un client OAuth dans la console
Tailscale (**Settings → Trust credentials → Credential**, type OAuth, droit **Auth Keys** en
écriture, tag `tag:depannage`) et enregistre son secret, sur son ordinateur seulement :

```sh
install -Dm600 /dev/null ~/.config/packarch-admin/tailscale-oauth
$EDITOR ~/.config/packarch-admin/tailscale-oauth      # le secret tskey-client-…, sur une ligne
```

À chaque dépannage, Packarch Admin crée alors une clé d'autorisation Tailscale propre à
l'ordinateur (90 jours, le maximum de Tailscale ; machine éphémère, tag `tag:depannage`
seulement) et la dépose chez lui par Tailscale SSH (`packarch-depannage key-set`, dans
`/var/lib/packarch-depannage/authkey`, lisible par root seul). La clé précédente de
l'ordinateur est révoquée. Aux sessions suivantes, « Autoriser le dépannage » connecte l'ordinateur tout seul, et la personne qui dépanne est
prévenue par l'icône.

Si la clé a expiré (plus de 90 jours sans dépannage), a été révoquée, ou si l'ordinateur n'en
a jamais reçu, la session retombe sur le lien : rien n'est bloqué. Une clé ne donne à
l'ordinateur que ce que lui donnait le lien validé : entrer dans le réseau comme machine
`tag:depannage`, qui n'y atteint rien. Elle se révoque dans la console Tailscale
(**Settings → Keys**).

## Fin de session et traces

En dehors d'une session, rien ne tourne : le service `tailscaled` du système n'est pas utilisé.
La session lance sa propre instance de `tailscaled` avec l'état **en mémoire**
(`--state=mem:`) : l'ordinateur est une machine **éphémère**, que Tailscale supprime du réseau
dès qu'elle se déconnecte (ou peu après si l'ordinateur s'éteint). Aucune entrée morte ne
s'accumule dans la console, le nom reste le même d'une session à l'autre, et rien n'est écrit
sur le disque.
L'arrêt est programmé par une minuterie systemd : il a lieu même si la fenêtre est fermée. Fermer
la fenêtre pendant une session demande une confirmation, puisque le dépannage continue sans elle.
Si l'ordinateur est éteint pendant une session, `packarch-depannage-cleanup.service` la
termine au démarrage suivant. Sans clé de connexion (voir plus haut), aucun secret n'est stocké sur
l'ordinateur : c'est la personne qui dépanne qui valide chaque connexion en ouvrant le lien.

Si Tailscale est déjà utilisé sur l'ordinateur pour autre chose, le dépannage refuse de
démarrer (il n'y touche pas).

## Réglage du réseau Tailscale (une fois, par la personne qui dépanne)

Dans la console d'administration Tailscale, **Access controls**, la politique doit :

- empêcher les ordinateurs dépannés d'atteindre quoi que ce soit dans le réseau (la
  politique par défaut, `"src": ["*"]`, leur donnerait accès à vos autres appareils) ;
- autoriser Tailscale SSH de vos appareils vers eux, en root.

```json
{
  "tagOwners": {
    "tag:depannage": ["autogroup:admin"]
  },
  "acls": [
    // Vos appareils : accès à tout, comme avant. Aucune règle n'a tag:depannage
    // pour source : les ordinateurs dépannés n'atteignent rien.
    { "action": "accept", "src": ["autogroup:member"], "dst": ["*:*"] }
  ],
  "ssh": [
    { "action": "accept", "src": ["autogroup:admin"], "dst": ["tag:depannage"],
      "users": ["root", "autogroup:nonroot"] }
  ]
}
```

À adapter si votre politique contient déjà d'autres règles (les garder, en vérifiant
qu'aucune n'a `*` ou `tag:depannage` pour source).

## Ligne de commande

```
packarch-depannage                       fenêtre (mode texte dans une console)
packarch-depannage texte                 mode texte
packarch-depannage status                état de la session (JSON)
packarch-depannage info                  description de l'ordinateur (utilisateur, bureau, thème…)
packarch-depannage session-run 'CMD'     CMD dans le bureau de l'utilisateur connecté (root)

pkexec packarch-depannage start [MIN]    démarre une session (60 min par défaut)
pkexec packarch-depannage extend [MIN]   prolonge la session
pkexec packarch-depannage stop           arrête la session (sans mot de passe)

packarch-depannage aur [OPTIONS] PAQUET  installe des paquets de l'AUR : yay -S en tant que l'utilisateur (root)
packarch-depannage chat-say              envoie le message lu sur l'entrée standard (root)
packarch-depannage chat-follow           messages du tchat au fil de l'eau, en JSON (root)
```

`session-run` lance la commande en tant que l'utilisateur connecté, avec l'environnement de
son bureau (`DISPLAY`, `XAUTHORITY`, bus D-Bus, `XDG_*`, repris d'un de ses processus
graphiques) : relancer la barre (`packarch-skin bar`), appliquer un thème, notifier…

`aur` installe des paquets de l'AUR. `yay` lancé en root compile dans un bac à sable de
systemd dont le dossier est monté sans droit d'exécution (systemd 262) : toute compilation qui
lance un programme qu'elle vient de produire y échoue (« Permission non accordée » : banc
d'essai de Stockfish, scripts de compilation de Cargo…). `aur` lance donc `yay -S` en tant que
l'utilisateur de l'ordinateur, avec les options et les paquets donnés ; une règle sudo
temporaire (`/etc/sudoers.d/zz-packarch-depannage-aur` : `pacman` sans mot de passe, pour ce
compte) est écrite le temps de la commande, puis retirée (au plus tard à la fin de la session).

Exemple, depuis l'ordinateur de la personne qui dépanne :

```sh
ssh root@packarch-maman packarch-depannage info
ssh root@packarch-maman 'pacman -Syu --noconfirm'
ssh root@packarch-maman "packarch-depannage session-run 'packarch-skin apply red-satanic'"
```

## Dépanner avec Claude Code

`claude/depannage/SKILL.md` est une compétence (skill) pour
[Claude Code](https://claude.com/claude-code) : elle lui explique comment trouver
l'ordinateur dans le réseau Tailscale, s'y connecter, agir dans le bureau de l'utilisateur
(`session-run`), les repères de Packarch (packarch-skin, polybar, XFCE, mises à jour) et les
règles de prudence (accord avant toute action risquée, pas de redémarrage, ne pas toucher à
Tailscale sur l'ordinateur dépanné). Il suffit alors de demander, par exemple : « connecte-toi
à packarch-maman et mets à jour le système ».

Le paquet `packarch-admin` l'installe : au lancement, Packarch Admin la lie dans
`~/.claude/skills/depannage` si Claude Code est utilisé sur l'ordinateur et qu'aucune
compétence de ce nom n'y est déjà. Depuis les sources :

```sh
mkdir -p ~/.claude/skills
ln -s "$PWD/claude/depannage" ~/.claude/skills/depannage
```

## Construction

Qt 6 (Widgets, Network), CMake, et [QTermWidget](https://github.com/lxqt/qtermwidget) pour
packarch-admin.

```sh
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build
```

`-DBUILD_ADMIN=OFF` ne construit que packarch-depannage (sans QTermWidget),
`-DBUILD_DEPANNAGE=OFF` que packarch-admin.

Sur une distribution optimisée pour un processeur récent (CachyOS v3, v4, znver4…), le binaire
hérite d'un marqueur de la glibc et refuse de démarrer sur un processeur plus ancien (« CPU
ISA level is lower than required »). Pour le distribuer, lier avec lld
(`-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld`) ; `readelf -n build/packarch-depannage | grep ISA`
ne doit rien afficher.

Deux paquets, dans [myrepo-pkgs](https://github.com/packarch/myrepo-pkgs).

**packarch-depannage** : le binaire dans `/usr/bin`, la fenêtre dans
`/usr/lib/packarch-depannage`, `org.packarch.Depannage.desktop`,
`org.packarch.depannage.policy` et `50-org.packarch.depannage.rules` (polkit : pas de mot de
passe pour un administrateur) et `packarch-depannage-cleanup.service` (à activer).
Dépendances : `qt6-base`, `tailscale`, `polkit`, `libnotify`, `util-linux` (runuser).

**packarch-admin** : le binaire dans `/usr/bin`, `org.packarch.Admin.desktop`,
`packarch-admin-tray.desktop` dans `/etc/xdg/autostart` (l'icône à l'ouverture du bureau) et
la compétence `claude/depannage` (dans `/usr/share/packarch-admin/claude`, liée au lancement
dans `~/.claude/skills` si Claude Code est utilisé et qu'elle n'y est pas). Dépendances :
`qt6-base`, `qtermwidget`, `tailscale`, `openssh`, `libnotify` ; en option Claude Code.

Les deux programmes ont la même version, celle du tag du dépôt ; chaque paquet prend son
composant d'installation (`cmake --install build --component depannage` ou `admin`).

## Licence

GPL-3.0-or-later.
