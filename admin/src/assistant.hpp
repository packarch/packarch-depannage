// Le tchat pour l'assistant (Claude Code), en ligne de commande, sous le
// contrôle de la case « Claude peut lire le tchat et y écrire » de la page de
// l'ordinateur : rien n'est lu ni écrit tant qu'elle n'est pas cochée, et tout
// s'arrête dès qu'elle est décochée.
//
//   packarch-admin tchat-suivre HÔTE   les messages de la personne, au fil de
//                                      l'eau, pendant que la case est cochée
//   packarch-admin tchat-dire HÔTE     envoie le message lu sur l'entrée
//                                      standard, signé Claude, si elle l'est
#pragma once

#include <QStringList>

namespace assistant {

/// Vrai si ARGS (sans le nom du programme) est une de ces commandes
bool isCommand(const QStringList& args);
/// Exécute la commande ; rend le code de sortie du programme
int run(int argc, char** argv);

}  // namespace assistant
