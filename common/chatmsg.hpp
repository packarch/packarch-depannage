// Messages du tchat entre la personne dépannée et la personne qui dépanne : un
// message JSON par ligne, dans le fichier de la session de l'ordinateur dépanné
// comme dans la sortie de `packarch-depannage chat-follow`.
#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

struct ChatMessage {
    bool fromUser = false;  ///< de la personne dépannée (sinon : de la personne qui dépanne)
    qint64 time = 0;
    QString text;
    bool operator==(const ChatMessage&) const = default;
};

namespace chat {

/// Longueur maximale d'un message (caractères)
inline constexpr int MAX_CHARS = 2000;

/// Texte d'un message : sans caractères de contrôle, sans blancs autour, borné
QString clean(const QString& text);

/// Message d'une ligne ; rien si elle n'en contient pas
std::optional<ChatMessage> parseLine(const QByteArray& line);

/// TEXT avec les émoticônes tapées au clavier (« :) », « ;-) », « <3 »…) en vraies
/// émoticônes ; seulement entre des blancs, pour ne pas toucher aux adresses et au code
QString withEmoji(const QString& text);

/// Ligne JSON de MESSAGE, saut de ligne compris
QByteArray toLine(const ChatMessage& message);

/// Début d'un message pour une notification : sur une ligne, en texte échappé
/// (le texte des notifications est interprété comme du balisage). Celles de
/// Packarch ont deux lignes d'environ 38 caractères, le reste est coupé.
QString notificationBody(const QString& text);

}  // namespace chat
