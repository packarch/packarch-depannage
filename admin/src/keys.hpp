// Clés de connexion des ordinateurs dépannés. À chaque dépannage, une clé
// d'autorisation Tailscale neuve (90 jours, limitée au tag du dépannage) est
// créée et déposée sur l'ordinateur : ses sessions suivantes rejoignent le
// réseau sans lien à envoyer. La clé précédente de l'ordinateur est révoquée.
//
// Il faut un client OAuth Tailscale (droit « Auth Keys », tag du dépannage),
// dont le secret est dans ~/.config/packarch-admin/tailscale-oauth : il ne
// quitte pas cet ordinateur. Sans ce fichier, rien n'est fait : le lien reste
// demandé à chaque session.
#pragma once

#include "tailnet.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace keys {

/// Durée de vie d'une clé : le maximum de Tailscale
inline constexpr int EXPIRY_SECS = 90 * 24 * 3600;

/// Identifiant du client dans son secret (tskey-client-IDENTIFIANT-SECRET) ; vide s'il n'a pas cette forme
QString clientId(const QString& secret);
/// Demande de création de la clé de HOST (API de Tailscale)
QByteArray createBody(const QString& host);

}  // namespace keys

class KeyProvisioner : public QObject {
    Q_OBJECT
public:
    explicit KeyProvisioner(QObject* parent = nullptr);
    /// Le secret du client OAuth est en place
    static bool configured();
    /// Crée une clé pour MACHINE et la dépose chez elle
    void provision(const Machine& machine);

signals:
    /// Où en est la clé de HOST, en une phrase
    void status(const QString& host, const QString& text);

private:
    void createKey(const Machine& machine, const QByteArray& token);
    void deposit(const Machine& machine, const QByteArray& token, const QString& id, const QString& key, const QString& expires);
    void revoke(const QByteArray& token, const QString& id);
    void fail(const Machine& machine, const QString& what, QNetworkReply* reply = nullptr);

    QNetworkAccessManager* network_;
};
