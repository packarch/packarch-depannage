// Tchat avec la personne dépannée. Son ordinateur ne peut joindre personne :
// les messages se lisent et s'écrivent chez lui, par Tailscale SSH
// (`packarch-depannage chat-follow`, `chat-say`), un message JSON par ligne.
#pragma once

#include "chatmsg.hpp"
#include "tailnet.hpp"

#include <QObject>
#include <QString>

class QProcess;

namespace chat {

/// Nom sous lequel la personne qui dépanne se présente : son nom de session sur
/// cet ordinateur (affiché chez la personne dépannée à la place du nom de son
/// compte Tailscale)
QString helperName();
/// Donne ce nom à MACHINE (packarch-depannage helper-set ; sans effet sur une
/// version qui ne connaît pas la commande)
void announceHelper(const Machine& machine, QObject* parent);

/// Options de ssh vers un ordinateur dépanné. Pas de vérification de la clé
/// d'hôte : elle change à chaque session (état en mémoire), et le réseau
/// Tailscale authentifie déjà la machine jointe par son adresse.
QStringList sshOptions(bool batch);

}  // namespace chat

/// Conversation avec un ordinateur : reçoit tant qu'elle existe, se reconnecte
/// si besoin
class ChatSession : public QObject {
    Q_OBJECT
public:
    ChatSession(Machine machine, QObject* parent = nullptr);
    ~ChatSession() override;
    void send(const QString& text);
    /// L'ordinateur a quitté le réseau : une connexion perdue est une fin de session
    void setGone() { gone_ = true; }

signals:
    void connected();
    void message(const ChatMessage& message);
    /// Connexion perdue, nouvel essai dans quelques secondes
    void lost(const QString& error);
    /// Plus de tchat (session terminée, ordinateur à mettre à jour)
    void finished(const QString& text);
    void notSent(const QString& text, const QString& reason);

private:
    void follow();
    void followEnded(int code, bool crashed);
    Machine machine_;
    QProcess* follower_ = nullptr;
    QByteArray buffer_;
    /// Chaque connexion redonne tout l'historique : déjà affiché, il est sauté
    int shown_ = 0;
    int seen_ = 0;
    bool gone_ = false;
    bool over_ = false;
};
