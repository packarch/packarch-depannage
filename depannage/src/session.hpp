// Session de dépannage, côté système (root, lancé par pkexec ou systemd) :
// démarrage de Tailscale avec Tailscale SSH, arrêt programmé, prolongation,
// nettoyage au démarrage de l'ordinateur. Et l'état de la session, lisible par
// tous (fenêtre).
//
// Rien ne tourne en dehors d'une session : le service tailscaled du système
// n'est pas utilisé. La session lance sa propre instance de tailscaled, avec
// l'état en mémoire (`--state=mem:`) : la machine est **éphémère**, Tailscale
// la supprime du réseau dès qu'elle se déconnecte (`tailscale logout` à
// l'arrêt), ou peu après si l'ordinateur s'éteint. Rien n'est écrit sur le
// disque (dossiers dans /run, effacés à l'arrêt de l'instance).
#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>

namespace session {

/// État affiché par la fenêtre (packarch-depannage status)
struct Status {
    bool active = false;
    /// Fin de la session (secondes depuis 1970)
    std::optional<qint64> end;
    /// Nom de la machine dans le réseau Tailscale
    QString hostname;
    /// État de Tailscale : NeedsLogin (en attente du lien), Running (prêt),
    /// Starting, Stopped… (vide si tailscaled ne tourne pas)
    QString backend;
    /// Lien de connexion à transmettre
    std::optional<QString> authUrl;
    /// Nom complet de la machine dans le réseau (une fois connectée)
    std::optional<QString> dnsName;
    /// Connexions SSH en cours (dépannage en cours)
    int sshSessions = 0;
    /// Appareil qui a échangé avec l'ordinateur le plus récemment (la personne
    /// qui dépanne), et l'heure de ce dernier échange
    std::optional<QString> helper;
    std::optional<qint64> lastActivity;
    /// Compte Tailscale de cet appareil : nom affiché et identifiant de
    /// connexion (adresse e-mail…)
    std::optional<QString> helperName;
    std::optional<QString> helperLogin;

    bool operator==(const Status&) const = default;
    /// Dépannage en cours : connexion ouverte, ou activité récente
    bool helping() const;
    QJsonObject toJson() const;
    static Status fromJson(const QJsonObject& json);
};

/// Délai pendant lequel une activité récente compte comme un dépannage en
/// cours : les commandes passent par des connexions SSH séparées et très
/// courtes, la fenêtre ne les verrait presque jamais ouvertes
inline constexpr qint64 ACTIVITY_SECS = 300;

/// Où en est la session, pour la personne dépannée
enum class Phase { Idle, Preparing, Link, Ready, Helping };
struct Description {
    Phase phase;
    QString title;
    QString text;
};
/// Titre et texte de l'état de la session (fenêtre et mode texte)
Description describe(const Status& status);

/// Chaque fonction rend une erreur à afficher, vide si tout va bien
using Error = QString;

qint64 now();
/// Nom de la machine dans le réseau : packarch-<nom d'hôte>
QString hostname();
/// packarch-<RAW> en minuscules, lettres, chiffres et tirets
QString machineName(const QString& raw);
/// Session en cours ?
bool active();
Error requireRoot();

/// Démarre une session de MINUTES minutes
Error start(qint64 minutes);
/// Prolonge la session de MINUTES minutes
Error extend(qint64 minutes);
/// Arrête la session : la machine quitte le réseau Tailscale, tailscaled est
/// arrêté. IF_ACTIVE : ne fait rien sans session (nettoyage au démarrage).
Error stop(bool ifActive = false);
/// État de la session (lisible sans être root)
Status status();

/// packarch-depannage key-set (root, par Tailscale SSH) : enregistre la clé de
/// connexion lue sur l'entrée standard. Avec elle, les sessions suivantes
/// rejoignent le réseau de la personne qui dépanne sans lien à lui envoyer.
Error setKey();
/// packarch-depannage connect (root, lancé par start dans sa propre unité) :
/// rejoint le réseau avec la clé de connexion si elle existe et fonctionne,
/// sinon par le lien de connexion (attend qu'il soit validé)
Error connectRun();
/// packarch-depannage helper-set (root, par Tailscale SSH) : le nom sous lequel
/// la personne qui dépanne se présente (lu sur l'entrée standard), affiché à la
/// place de celui de son compte Tailscale, jusqu'à la fin de la session
Error setHelper();
/// Nom présentable : court, des lettres, des chiffres, espaces et « -_. »
bool validHelperName(const QString& name);
/// Nom donné par la personne qui dépanne pour cette session (vide s'il n'y en a pas)
QString helperName();
/// Nom de session de l'utilisateur de ce programme : celui de ses messages
QString userName();
/// KEY a la forme d'une clé d'autorisation Tailscale (elle est passée à tailscale)
bool validKey(const QString& key);

/// Appareil qui a échangé le plus récemment avec l'ordinateur
struct Peer {
    QString host;
    qint64 time = 0;
    std::optional<QString> name;
    std::optional<QString> login;
    bool operator==(const Peer&) const = default;
};
/// Pair le plus récemment actif (dernier paquet échangé), avec son compte
/// (table « User » de tailscale status, par UserID)
std::optional<Peer> lastPeer(const QJsonObject& tailscaleStatus);
/// Date RFC 3339 de Tailscale (« 2026-11-01T12:06:30.469+01:00 ») en secondes
/// depuis 1970 ; rien pour la date nulle (an 1)
std::optional<qint64> unixTime(const QString& date);

}  // namespace session
