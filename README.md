# packarch-depannage

Petit logiciel en Rust ([egui](https://github.com/emilk/egui)) pour le **dépannage à distance** d'un ordinateur
[Packarch](https://github.com/packarch/CachyOS-Packarch) par une personne de confiance, en
ligne de commande, à travers [Tailscale](https://tailscale.com) et Tailscale SSH.

## Fonctionnement

1. La personne à dépanner ouvre **Dépannage à distance** (menu Système) et clique sur
   « Autoriser le dépannage (1 h) ». Son mot de passe est demandé (pkexec) : rien ne
   s'ouvre sans son accord.
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

## Tchat

Dès que l'ordinateur est connecté, sa fenêtre affiche un tchat. La personne qui dépanne ouvre
elle aussi **Dépannage à distance**, clique sur « Dépanner quelqu'un » (il lui faut Tailscale
connecté à son réseau et `packarch-depannage` à jour) et choisit l'ordinateur dans la liste :
aucune commande à taper. Une notification signale un message si la fenêtre n'est pas au premier
plan ; si la personne dépannée a fermé la sienne, un message reçu la rouvre.

L'ordinateur dépanné ne pouvant joindre personne, c'est la fenêtre de la personne qui dépanne
qui vient lire et écrire les messages par Tailscale SSH. Ils sont dans
`/run/packarch-depannage/chat.jsonl` : en mémoire, effacés à la fin de la session. Tant que le
tchat est ouvert de son côté, l'ordinateur dépanné affiche « Dépannage en cours ».

## Fin de session et traces

En dehors d'une session, rien ne tourne : le service `tailscaled` du système n'est pas utilisé.
La session lance sa propre instance de `tailscaled` avec l'état **en mémoire**
(`--state=mem:`) : l'ordinateur est une machine **éphémère**, que Tailscale supprime du réseau
dès qu'elle se déconnecte (ou peu après si l'ordinateur s'éteint). Aucune entrée morte ne
s'accumule dans la console, le nom reste le même d'une session à l'autre, et rien n'est écrit
sur le disque.
L'arrêt est programmé par une minuterie systemd : il a lieu même si la fenêtre est fermée.
Si l'ordinateur est éteint pendant une session, `packarch-depannage-cleanup.service` la
termine au démarrage suivant. Aucun secret n'est stocké sur l'ordinateur : c'est la personne
qui dépanne qui valide chaque connexion en ouvrant le lien.

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
packarch-depannage                       fenêtre
packarch-depannage status                état de la session (JSON)
packarch-depannage info                  description de l'ordinateur (utilisateur, bureau, thème…)
packarch-depannage session-run 'CMD'     CMD dans le bureau de l'utilisateur connecté (root)

pkexec packarch-depannage start [MIN]    démarre une session (60 min par défaut)
pkexec packarch-depannage extend [MIN]   prolonge la session
pkexec packarch-depannage stop           arrête la session (sans mot de passe)

packarch-depannage chat-say              envoie le message lu sur l'entrée standard (root)
packarch-depannage chat-follow           messages du tchat au fil de l'eau, en JSON (root)
```

`session-run` lance la commande en tant que l'utilisateur connecté, avec l'environnement de
son bureau (`DISPLAY`, `XAUTHORITY`, bus D-Bus, `XDG_*`, repris d'un de ses processus
graphiques) : relancer la barre (`packarch-skin bar`), appliquer un thème, notifier…

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

Installation, sur l'ordinateur de la personne qui dépanne (avec Tailscale connecté à son
réseau) : un lien vers le dossier du dépôt, pour que les mises à jour du dépôt s'appliquent.

```sh
mkdir -p ~/.claude/skills
ln -s "$PWD/claude/depannage" ~/.claude/skills/depannage
```

## Construction

```sh
cargo build --release
```

Fichiers à installer (paquet `packarch-depannage` de
[myrepo-pkgs](https://github.com/packarch/myrepo-pkgs)) : le binaire dans `/usr/bin`,
`data/org.packarch.Depannage.desktop`, `data/org.packarch.depannage.policy` (polkit) et
`data/packarch-depannage-cleanup.service` (activé). Dépendances : `tailscale`, `polkit`,
`openssh` (tchat de la personne qui dépanne), `libnotify` et `wmctrl` (fenêtre), et ce
qu'egui charge au démarrage (`libglvnd`, `libxkbcommon`, `libx11`, `libxcursor`, `libxi`,
`libxrandr`). Jusqu'à la version 0.2.2 (tag `v0.2.2`), la fenêtre était en GTK4/libadwaita.

## Licence

GPL-3.0-or-later.
