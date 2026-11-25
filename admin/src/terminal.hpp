// Terminal intégré à la fenêtre (QTermWidget, celui de QTerminal).
#pragma once

#include <QString>
#include <QStringList>

class QWidget;

namespace terminal {

/// PROGRAMME dans le PATH, ou dans ~/.local/bin (Claude Code s'y installe, et ce
/// dossier manque au PATH d'un programme lancé à l'ouverture du bureau)
QString findProgram(const QString& name);

/// Terminal qui lance PROGRAM ARGS dans le dossier personnel. FINISHED est appelé
/// quand le programme se termine.
QWidget* create(const QString& program, const QStringList& args, QWidget* parent);

}  // namespace terminal
