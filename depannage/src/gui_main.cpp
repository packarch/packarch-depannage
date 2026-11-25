// Fenêtre de packarch-depannage : un programme à part, lancé par
// packarch-depannage (sans commande). Les commandes et le mode texte n'ont
// ainsi pas besoin de la partie graphique de Qt.

#include "singleinstance.hpp"
#include "theme.hpp"
#include "window.hpp"

#include <QApplication>

using namespace Qt::StringLiterals;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"packarch-depannage"_s);
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setDesktopFileName(u"org.packarch.Depannage"_s);
    theme::apply();
    // Déjà ouverte (depuis le menu, le plus souvent) : elle est ramenée devant
    SingleInstance instance(u"packarch-depannage"_s);
    if (instance.alreadyRunning(true))
        return 0;
    Window w;
    QObject::connect(&instance, &SingleInstance::showRequested, &w, &Window::present);
    w.show();
    return QApplication::exec();
}
