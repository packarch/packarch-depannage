// Tchat entre la personne dépannée et la personne qui dépanne, côté ordinateur
// dépanné : un fichier dans le dossier de la session (en mémoire, effacé à
// l'arrêt de la session, rien sur le disque), un message JSON par ligne.
//
// L'ordinateur dépanné ne peut joindre personne (règles du réseau Tailscale) :
// c'est la personne qui dépanne qui vient lire et écrire ce fichier par
// Tailscale SSH (`chat-follow`, `chat-say`), depuis packarch-admin. Le fichier
// appartient à l'utilisateur, pour que sa fenêtre y écrive sans mot de passe ;
// le dossier appartient à root, il ne peut pas le remplacer par un lien.
#pragma once

#include "chatmsg.hpp"

#include <QList>
#include <QString>

namespace chatfile {

inline const QString CHAT = QStringLiteral("/run/packarch-depannage/chat.jsonl");

/// Lignes complètes écrites depuis la position POS, qui avance
QList<ChatMessage> readNew(const QString& path, qint64& pos);
/// Ajoute un message (une seule écriture, en fin de fichier) ; rend une erreur, vide si tout va bien
QString appendTo(const QString& path, bool fromUser, const QString& text);
/// Crée le fichier du tchat et le donne à l'utilisateur (root) : celui qui a
/// autorisé le dépannage (pkexec), sinon celui du bureau
QString ensure();
/// Annonce la fin de la session à packarch-admin, tant que la connexion existe :
/// sans cela, il ne verrait qu'une connexion perdue
void announceStop();
/// packarch-depannage chat-say (root) : envoie le texte lu sur l'entrée standard
/// (pas d'argument : le texte ne passe pas par le shell distant) et rouvre la
/// fenêtre de l'utilisateur si elle est fermée
QString say();
/// packarch-depannage chat-follow (root) : écrit les messages, depuis le début,
/// au fil de l'eau, jusqu'à la fin de la session ou de la connexion. Une ligne
/// vide de temps en temps (la première : connexion établie).
QString follow();

}  // namespace chatfile
