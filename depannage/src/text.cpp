#include "text.hpp"

#include "chatfile.hpp"
#include "session.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QProcess>
#include <QTextStream>

#include <poll.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace text {

namespace {
const QString BIN = u"/usr/bin/packarch-depannage"_s;
const QString MINUTES = u"60"_s;
/// Délai entre deux relectures de l'état, en attendant une ligne au clavier
constexpr int POLL_MS = 2000;

QTextStream& out() {
    static QTextStream stream(stdout);
    return stream;
}

void say(const QString& line = {}) {
    out() << line << Qt::endl;
}

QString localTime(qint64 secs) {
    return QDateTime::fromSecsSinceEpoch(secs).toString(u"HH 'h' mm"_s);
}

/// Ligne tapée au clavier, attendue au plus POLL_MS ; rien s'il n'y en a pas.
/// CLOSED : l'entrée est fermée (Ctrl+D).
std::optional<QString> readLine(bool& closed) {
    pollfd fd{STDIN_FILENO, POLLIN, 0};
    if (::poll(&fd, 1, POLL_MS) <= 0)
        return std::nullopt;
    QByteArray line;
    char c = 0;
    while (true) {
        const ssize_t n = ::read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            closed = true;
            break;
        }
        if (c == '\n')
            break;
        line += c;
    }
    return QString::fromUtf8(line).trimmed();
}

/// Lance une commande de session en root : directement si ce programme l'est
/// (console de root, sudo), sinon par pkexec, qui demande le mot de passe dans
/// le terminal
bool privileged(const QStringList& args) {
    if (session::requireRoot().isEmpty()) {
        QString error;
        if (args.first() == "start"_L1)
            error = session::start(MINUTES.toLongLong());
        else if (args.first() == "extend"_L1)
            error = session::extend(MINUTES.toLongLong());
        else
            error = session::stop();
        if (!error.isEmpty())
            say(u"Échec : %1"_s.arg(error));
        return error.isEmpty();
    }
    QProcess process;
    process.setInputChannelMode(QProcess::ForwardedInputChannel);
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(u"pkexec"_s, QStringList{BIN} + args);
    const bool ok = process.waitForStarted(-1) && process.waitForFinished(-1)
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!ok)
        say(u"Échec (mot de passe refusé ?). Autre moyen : sudo packarch-depannage %1"_s.arg(args.join(u' ')));
    return ok;
}

void help() {
    say(u"Écrivez un message puis Entrée pour l'envoyer à la personne qui vous dépanne."_s);
    say(u"  /plus      prolonger d'une heure"_s);
    say(u"  /arreter   arrêter le dépannage maintenant"_s);
    say(u"  /quitter   quitter ce programme (le dépannage continue jusqu'à sa fin)"_s);
    say(u"  Entrée     réafficher l'état"_s);
}

void show(const session::Status& st) {
    const session::Description d = session::describe(st);
    say();
    say(u"== %1 =="_s.arg(d.title));
    say(d.text);
    if (d.phase == session::Phase::Link && st.authUrl) {
        say();
        say(u"    %1"_s.arg(*st.authUrl));
        say();
    }
    if (st.active) {
        say(u"Nom de l'ordinateur : %1"_s.arg(st.dnsName.value_or(st.hostname)));
        if (st.end) {
            const qint64 left = (qMax<qint64>(*st.end - session::now(), 0) + 59) / 60;
            say(u"Fin automatique à %1 (dans %2 min)."_s.arg(localTime(*st.end)).arg(left));
        }
    }
}
}  // namespace

int run() {
    session::Status st = session::status();
    say(u"Packarch Dépannage %1 — mode texte"_s.arg(QCoreApplication::applicationVersion()));
    if (!st.active) {
        show(st);
        say();
        out() << u"Autoriser le dépannage pendant 1 heure ? [o/N] "_s << Qt::flush;
        bool closed = false;
        std::optional<QString> answer;
        while (!answer && !closed)
            answer = readLine(closed);
        if (!answer || !(answer->startsWith(u'o', Qt::CaseInsensitive) || answer->startsWith(u'y', Qt::CaseInsensitive))) {
            say(u"Dépannage non autorisé."_s);
            return 0;
        }
        if (!privileged({u"start"_s, MINUTES}))
            return 1;
        st = session::status();
        if (!st.active) {
            say(u"La session n'a pas démarré."_s);
            return 1;
        }
    }
    show(st);
    say();
    help();

    session::Phase phase = session::describe(st).phase;
    std::optional<QString> link = st.authUrl;
    qint64 chatPos = 0;
    // L'historique d'une session déjà commencée est affiché une fois
    while (true) {
        for (const ChatMessage& m : chatfile::readNew(chatfile::CHAT, chatPos)) {
            const QString who = m.fromUser ? u"Vous"_s : st.helperName.value_or(u"La personne qui vous dépanne"_s);
            say(u"[%1] %2 : %3"_s.arg(localTime(m.time), who, m.text));
        }
        bool closed = false;
        const std::optional<QString> line = readLine(closed);
        if (closed) {
            say(u"\nLe dépannage continue jusqu'à sa fin. Pour l'arrêter : packarch-depannage texte, puis /arreter"_s);
            return 0;
        }
        if (line) {
            if (line->isEmpty()) {
                show(session::status());
            } else if (*line == "/quitter"_L1) {
                say(u"Le dépannage continue jusqu'à sa fin. Pour l'arrêter : packarch-depannage texte, puis /arreter"_s);
                return 0;
            } else if (*line == "/arreter"_L1 || *line == u"/arrêter"_s) {
                privileged({u"stop"_s});
            } else if (*line == "/plus"_L1) {
                if (privileged({u"extend"_s, MINUTES}))
                    show(session::status());
            } else if (line->startsWith(u'/')) {
                help();
            } else if (const QString error = chatfile::appendTo(chatfile::CHAT, true, *line); !error.isEmpty()) {
                say(u"Message non envoyé (%1) : le tchat fonctionne une fois l'ordinateur connecté."_s.arg(error));
            }
        }
        st = session::status();
        if (!st.active) {
            say();
            say(u"Session de dépannage terminée."_s);
            return 0;
        }
        // L'état a changé (lien à transmettre, connecté, dépannage en cours) : il est réaffiché
        const session::Phase now = session::describe(st).phase;
        if (now != phase || st.authUrl != link) {
            phase = now;
            link = st.authUrl;
            show(st);
        }
    }
}

}  // namespace text
