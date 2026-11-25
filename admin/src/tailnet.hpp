// Réseau Tailscale de la personne qui dépanne : ordinateurs à dépanner (tag
// tag:depannage), relus régulièrement, et leurs arrivées.
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <optional>

class QProcess;

struct Machine {
    QString host;
    QString ip;
    bool operator==(const Machine&) const = default;
};

namespace tailnet {

/// Nom tel que packarch-depannage le donne à un ordinateur dépanné (minuscules,
/// chiffres, tirets). Le nom vient de l'autre ordinateur : un nom différent
/// n'est jamais repris dans une commande ni dans une demande à Claude.
bool validHost(const QString& host);

/// Nom affichable : sans caractère de contrôle, borné
QString shownHost(const QString& host);

/// IP est une adresse (elle va dans la commande ssh)
bool validIp(const QString& ip);

/// Machines en ligne qui portent le tag du dépannage, triées, dans la sortie
/// JSON de « tailscale status ». Rien si Tailscale n'est pas connecté.
std::optional<QList<Machine>> machinesIn(const QByteArray& statusJson);

}  // namespace tailnet

/// Arrivées et départs, d'une relecture à l'autre
class Arrivals {
public:
    /// Absence au bout de laquelle un ordinateur est oublié : il sera annoncé de
    /// nouveau à son retour (une coupure plus courte n'est pas une nouvelle session).
    /// Revenu plus tôt avec une autre adresse, c'est une nouvelle session (machine
    /// éphémère : Tailscale lui donne une adresse neuve à chaque session) : il
    /// est annoncé aussi.
    static constexpr qint64 FORGET_SECS = 120;

    struct Change {
        QList<Machine> arrived;
        QStringList gone;
    };
    Change update(const QList<Machine>& machines, qint64 now);

private:
    struct Seen {
        qint64 time = 0;
        QString ip;
    };
    QHash<QString, Seen> seen_;
};

/// Relit l'état de Tailscale toutes les trois secondes
class Tailnet : public QObject {
    Q_OBJECT
public:
    explicit Tailnet(QObject* parent = nullptr);
    void start();
    bool connected() const { return connected_; }
    const QList<Machine>& machines() const { return machines_; }

signals:
    /// Après chaque relecture
    void polled();
    void arrived(const Machine& machine);
    /// Oublié : parti depuis plus de FORGET_SECS
    void gone(const QString& host);

private:
    void poll();
    QProcess* process_ = nullptr;
    Arrivals arrivals_;
    QList<Machine> machines_;
    bool connected_ = false;
};
