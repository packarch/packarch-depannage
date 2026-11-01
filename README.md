# packarch-depannage

Petit logiciel GTK4/libadwaita en Rust pour le **dépannage à distance** d'un ordinateur
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

En dehors d'une session, rien ne tourne : `tailscaled` n'est jamais activé au démarrage.
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

## Construction

```sh
cargo build --release
```

Fichiers à installer (paquet `packarch-depannage` de
[myrepo-pkgs](https://github.com/packarch/myrepo-pkgs)) : le binaire dans `/usr/bin`,
`data/org.packarch.Depannage.desktop`, `data/org.packarch.depannage.policy` (polkit) et
`data/packarch-depannage-cleanup.service` (activé). Dépendances : `gtk4`, `libadwaita`,
`tailscale`, `polkit`.

## Licence

GPL-3.0-or-later.
