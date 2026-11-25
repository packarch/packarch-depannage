// Session graphique de l'utilisateur, pour la personne qui dépanne (root, par
// Tailscale SSH) : `session-run` lance une commande en tant que l'utilisateur
// connecté, avec l'environnement de son bureau (écran, bus D-Bus, XDG…) ;
// `info` décrit l'ordinateur.
#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <optional>

namespace desktop {

struct Desktop {
    QString user;
    uint uid = 0;
    QList<QPair<QString, QString>> env;
    QString get(const QString& key) const;
};

/// Bureau de l'utilisateur connecté : l'environnement d'un de ses processus
/// graphiques (le processus de session de préférence)
std::optional<Desktop> find();

/// packarch-depannage session-run COMMANDE… : la commande (passée à sh -c) en
/// tant que l'utilisateur, dans son bureau. Rend son code de sortie ; ERROR si
/// elle n'a pas pu être lancée.
int sessionRun(const QStringList& command, QString* error = nullptr);

/// packarch-depannage info : description de l'ordinateur pour la personne qui dépanne
void info();

}  // namespace desktop
