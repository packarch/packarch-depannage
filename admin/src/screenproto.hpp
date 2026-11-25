// Ce que Packarch Admin échange avec les scripts de l'écran (data/capture.sh,
// data/saisie.py) : sans fenêtre, pour les tests.
#pragma once

#include <QByteArray>

namespace screen {

/// Coordonnée du pointeur pour data/saisie.py (0 à 32767) : position POS dans
/// une longueur LENGTH de l'image affichée
int pointerAxis(double pos, double length);

/// Code noyau (evdev) d'une touche, d'après le code natif donné par Qt (X11 et
/// Wayland : decalé de 8) ; 0 si la touche ne se relaie pas
int evdevKey(quint32 nativeScanCode);

/// En-tête « F OCTETS » d'une capture : sa taille, -1 si ce n'en est pas un ou
/// si elle est démesurée
qint64 frameSize(const QByteArray& header);

}  // namespace screen
