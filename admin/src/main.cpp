// packarch-admin : le programme de la personne qui dépanne des ordinateurs
// Packarch à distance (packarch-depannage, installé sur ces ordinateurs).
//
// Une seule instance : la fenêtre (ordinateurs à dépanner, terminaux, tchat) et
// son icône dans la zone de notification, qui reste quand la fenêtre est fermée
// et prévient quand un ordinateur arrive.

#include "chat.hpp"
#include "mainwindow.hpp"
#include "singleinstance.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace {
/// Compétence Claude Code du dépannage (« Dépanner avec Claude Code ») : liée
/// dans ~/.claude/skills si Claude Code est utilisé ici et qu'elle n'y est pas.
/// Une compétence déjà en place, quelle qu'elle soit, n'est pas touchée.
void linkSkill() {
    const QString installed = QStringLiteral(SKILL_DIR);
    const QDir claude(QDir::homePath() + u"/.claude"_s);
    if (!claude.exists() || !QFileInfo::exists(installed + u"/SKILL.md"_s))
        return;
    const QString link = claude.filePath(u"skills/depannage"_s);
    if (QFileInfo::exists(link) || QFileInfo(link).isSymLink())
        return;
    claude.mkpath(u"skills"_s);
    QFile::link(installed, link);
}
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"packarch-admin"_s);
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setDesktopFileName(u"org.packarch.Admin"_s);
    theme::apply();
    // La fenêtre fermée, l'icône reste
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(u"Dépannage à distance des ordinateurs Packarch (personne qui dépanne)"_s);
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption trayOption(u"tray"_s, u"Démarre réduit dans la zone de notification (ouverture du bureau)."_s);
    const QCommandLineOption testOption(u"essai"_s, u"Essai de la notification d'arrivée, comme si NOM arrivait."_s, u"NOM"_s);
    parser.addOptions({trayOption, testOption});
    parser.process(app);

    // Déjà lancé : lui demander de montrer sa fenêtre (sauf à l'ouverture du bureau)
    SingleInstance instance(u"packarch-admin"_s);
    if (instance.alreadyRunning(!parser.isSet(trayOption)))
        return 0;

    linkSkill();
    chat::clearAssistantFlags();
    MainWindow window;
    QObject::connect(&instance, &SingleInstance::showRequested, &window, [&window] { window.showMachine(); });
    if (!parser.isSet(trayOption))
        window.show();
    if (parser.isSet(testOption))
        window.announce({parser.value(testOption), u"100.64.0.1"_s});
    return QApplication::exec();
}
