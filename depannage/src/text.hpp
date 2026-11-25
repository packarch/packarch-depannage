// Mode texte de packarch-depannage, pour une console (tty) : quand le bureau de
// la personne ne démarre pas, ou que la fenêtre ne s'ouvre pas. Même parcours
// que la fenêtre : autoriser le dépannage (mot de passe), le lien à
// transmettre s'il en faut un, l'état, le tchat, prolonger, arrêter.
#pragma once

namespace text {

/// Dialogue dans le terminal, jusqu'à la fin de la session ou « /quitter ».
/// Rend le code de sortie du programme.
int run();

}  // namespace text
