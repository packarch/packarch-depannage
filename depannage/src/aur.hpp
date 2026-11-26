// Paquets de l'AUR, pour la personne qui dépanne (root, par Tailscale SSH).
//
// yay lancé en root compile dans un bac à sable de systemd (DynamicUser), dont le
// dossier est monté sans droit d'exécution (systemd 262) : toute compilation qui
// lance un programme qu'elle vient de produire échoue (« Permission non
// accordée » : banc d'essai de Stockfish, scripts de compilation de Cargo…).
// `aur` lance donc yay en tant que l'utilisateur de l'ordinateur, à qui une règle
// sudo temporaire donne pacman sans mot de passe, le temps de la commande.
#pragma once

#include <QString>
#include <QStringList>

namespace aur {

/// Fichier de la règle sudo temporaire
QString sudoersPath();

/// Contenu de la règle pour USER ; vide si le nom n'est pas celui d'un compte ordinaire
QString sudoersRule(const QString& user);

/// Retire la règle sudo si elle est restée (commande tuée) : arrêt de la session
void cleanup();

/// packarch-depannage aur ARGUMENTS… : `yay -S ARGUMENTS…` en tant que
/// l'utilisateur. Rend le code de sortie de yay ; ERROR s'il n'a pas pu être lancé.
int run(const QStringList& args, QString* error = nullptr);

}  // namespace aur
