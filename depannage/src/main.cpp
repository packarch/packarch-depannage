// packarch-depannage : dépannage à distance temporaire de Packarch.
//
// La fenêtre (sans argument) démarre, sur demande de l'utilisateur et avec son
// mot de passe (pkexec), une session Tailscale avec Tailscale SSH : elle
// affiche le lien de connexion à transmettre à la personne qui dépanne, qui
// l'ouvre avec son compte Tailscale ; l'ordinateur rejoint alors son réseau et
// elle peut s'y connecter en SSH (root). La session s'arrête toute seule
// (minuterie systemd), ou avec le bouton « Arrêter maintenant ».
//
// Le programme de la personne qui dépanne est packarch-admin (dossier admin).

#include "chatfile.hpp"
#include "desktop.hpp"
#include "session.hpp"
#include "singleinstance.hpp"
#include "theme.hpp"
#include "window.hpp"

#include <QApplication>
#include <QJsonDocument>
#include <QTextStream>

using namespace Qt::StringLiterals;

namespace {
const char USAGE[] = R"(Usage : packarch-depannage [COMMANDE]

  (sans commande)         fenêtre du dépannage à distance
  status                  état de la session (JSON)
  info                    description de l'ordinateur (pour la personne qui dépanne)
  session-run COMMANDE…   lance COMMANDE dans le bureau de l'utilisateur connecté (root)

En root (pkexec, ou par la fenêtre) :
  start [MINUTES]         démarre une session (60 min par défaut)
  extend [MINUTES]        prolonge la session (60 min par défaut)
  stop                    arrête la session
  cleanup                 arrête une session restée ouverte (démarrage de l'ordinateur)
  chat-say                envoie à l'utilisateur le message lu sur l'entrée standard
  chat-follow             affiche les messages du tchat au fil de l'eau (JSON)
  key-set                 enregistre la clé de connexion lue sur l'entrée standard : les
                          sessions suivantes se connectent sans lien à envoyer
)";

/// Durée en minutes (60 par défaut) ; ERROR si elle est invalide
qint64 minutes(const QStringList& args, QString& error) {
    if (args.size() < 2)
        return 60;
    bool ok = false;
    const qint64 m = args[1].toLongLong(&ok);
    if (!ok || m < 1 || m > 24 * 60)
        error = u"durée invalide : %1"_s.arg(args[1]);
    return m;
}

int window(int argc, char** argv) {
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
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2)
        return window(argc, argv);

    // Commandes : sans fenêtre ni écran (root, connexion SSH, systemd)
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments().mid(1);
    const QString command = args.first();
    QTextStream out(stdout);
    QTextStream err(stderr);
    QString error;
    if (command == "status"_L1) {
        out << QJsonDocument(session::status().toJson()).toJson(QJsonDocument::Indented);
    } else if (command == "info"_L1) {
        desktop::info();
    } else if (command == "session-run"_L1) {
        const int code = desktop::sessionRun(args.mid(1), &error);
        if (error.isEmpty())
            return qBound(0, code, 255);
    } else if (command == "start"_L1) {
        const qint64 m = minutes(args, error);
        if (error.isEmpty())
            error = session::start(m);
    } else if (command == "extend"_L1) {
        const qint64 m = minutes(args, error);
        if (error.isEmpty())
            error = session::extend(m);
    } else if (command == "stop"_L1) {
        error = session::stop();
    } else if (command == "cleanup"_L1) {
        error = session::stop(true);
    } else if (command == "key-set"_L1) {
        error = session::setKey();
    } else if (command == "connect"_L1) {
        error = session::connectRun();
    } else if (command == "chat-say"_L1) {
        error = chatfile::say();
    } else if (command == "chat-follow"_L1) {
        error = chatfile::follow();
    } else if (command == "-h"_L1 || command == "--help"_L1 || command == "help"_L1) {
        out << QString::fromUtf8(USAGE);
    } else {
        err << QString::fromUtf8(USAGE);
        return 2;
    }
    if (!error.isEmpty()) {
        err << u"packarch-depannage : %1"_s.arg(error) << Qt::endl;
        return 1;
    }
    return 0;
}
