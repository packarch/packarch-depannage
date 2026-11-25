// Émoticônes du tchat : un bouton qui ouvre une petite grille, et les
// émoticônes tapées au clavier (« :) ») remplacées à l'envoi.
#pragma once

#include <QString>

class QLineEdit;
class QToolButton;

namespace emoji {

/// Bouton qui insère l'émoticône choisie dans TARGET, à l'endroit du curseur
QToolButton* button(QLineEdit* target);

}  // namespace emoji
