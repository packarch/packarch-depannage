#include "chatfile.hpp"

#include "desktop.hpp"
#include "session.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace chatfile {

namespace {
/// Présent quand la session s'arrête : chat-follow se termine normalement, avant
/// que la connexion ne soit coupée (effacé avec le dossier de la session)
const QString STOPPING = u"/run/packarch-depannage/stopping"_s;
/// Attente de la fin de chat-follow à l'arrêt de la session : relectures, délai
/// laissé à son code de sortie pour parvenir à packarch-admin
constexpr int STOP_WAIT_MS = 100;
constexpr int STOP_WAIT_MAX = 20;
constexpr int STOP_GRACE_MS = 400;
/// Délai entre deux relectures, et entre deux lignes vides de chat-follow : une
/// écriture qui échoue révèle une connexion perdue
constexpr int POLL_MS = 1000;
constexpr int HEARTBEAT_POLLS = 15;
/// Nom du processus de la fenêtre (/proc/PID/comm, tronqué à 15 caractères)
const QByteArray COMM = "packarch-depann";

QStringList processes() {
    QStringList pids;
    for (const QString& name : QDir(u"/proc"_s).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool number = false;
        name.toLongLong(&number);
        if (number)
            pids << name;
    }
    return pids;
}

/// Processus chat-follow en cours
int followers() {
    int count = 0;
    for (const QString& pid : processes()) {
        QFile f(u"/proc/%1/cmdline"_s.arg(pid));
        if (f.open(QIODevice::ReadOnly) && f.readAll().split('\0').contains("chat-follow"))
            ++count;
    }
    return count;
}

/// La fenêtre de l'utilisateur est-elle ouverte ?
bool windowOpen() {
    for (const QString& pid : processes()) {
        const QString dir = u"/proc/%1"_s.arg(pid);
        QFile f(dir + u"/comm"_s);
        if (QFileInfo(dir).ownerId() >= 1000 && f.open(QIODevice::ReadOnly) && f.readAll().trimmed() == COMM)
            return true;
    }
    return false;
}

/// Écrit tout DATA sur la sortie standard ; faux si la connexion est perdue
bool writeOut(const QByteArray& data) {
    qsizetype done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(STDOUT_FILENO, data.constData() + done, static_cast<size_t>(data.size() - done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        done += n;
    }
    return true;
}
}  // namespace

QList<ChatMessage> readNew(const QString& path, qint64& pos) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    // Fichier recréé (nouvelle session) : relu depuis le début
    if (f.size() < pos)
        pos = 0;
    if (!f.seek(pos))
        return {};
    const QByteArray buf = f.readAll();
    const qsizetype end = buf.lastIndexOf('\n');
    if (end < 0)
        return {};
    pos += end + 1;
    QList<ChatMessage> messages;
    for (const QByteArray& line : buf.left(end).split('\n')) {
        if (const auto m = chat::parseLine(line))
            messages.append(*m);
    }
    return messages;
}

QString appendTo(const QString& path, bool fromUser, const QString& text) {
    const QString cleaned = chat::clean(text);
    if (cleaned.isEmpty())
        return {};
    const QByteArray line = chat::toLine({fromUser, session::now(), cleaned});
    // Le fichier doit exister : il est créé par root (ensure)
    const int fd = ::open(QFile::encodeName(path).constData(), O_WRONLY | O_APPEND | O_CLOEXEC);
    if (fd < 0)
        return QString::fromLocal8Bit(std::strerror(errno));
    const ssize_t written = ::write(fd, line.constData(), static_cast<size_t>(line.size()));
    const int error = errno;
    ::close(fd);
    if (written != line.size())
        return written < 0 ? QString::fromLocal8Bit(std::strerror(error)) : u"écriture incomplète"_s;
    return {};
}

QString ensure() {
    const QByteArray path = QFile::encodeName(CHAT);
    const int fd = ::open(path.constData(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return u"%1 : %2"_s.arg(CHAT, QString::fromLocal8Bit(std::strerror(errno)));
    struct stat st {};
    QString error;
    if (::fstat(fd, &st) != 0) {
        error = u"%1 : %2"_s.arg(CHAT, QString::fromLocal8Bit(std::strerror(errno)));
    } else if (st.st_uid == 0) {
        bool ok = false;
        uint uid = qEnvironmentVariable("PKEXEC_UID").toUInt(&ok);
        if (!ok || uid == 0) {
            const auto d = desktop::find();
            ok = d.has_value();
            uid = ok ? d->uid : 0;
        }
        if (ok && ::fchown(fd, uid, static_cast<gid_t>(-1)) != 0)
            error = u"%1 : %2"_s.arg(CHAT, QString::fromLocal8Bit(std::strerror(errno)));
    }
    ::close(fd);
    return error;
}

void announceStop() {
    if (followers() == 0)
        return;
    QFile stopping(STOPPING);
    if (!stopping.open(QIODevice::WriteOnly))
        return;
    stopping.close();
    for (int i = 0; i < STOP_WAIT_MAX; ++i) {
        if (followers() == 0) {
            QThread::msleep(STOP_GRACE_MS);
            return;
        }
        QThread::msleep(STOP_WAIT_MS);
    }
}

QString say() {
    if (const QString e = session::requireRoot(); !e.isEmpty())
        return e;
    if (!session::active())
        return u"aucune session en cours"_s;
    QFile in;
    if (!in.open(stdin, QIODevice::ReadOnly))
        return u"entrée standard illisible"_s;
    const QString text = QString::fromUtf8(in.readAll());
    if (const QString e = ensure(); !e.isEmpty())
        return e;
    if (const QString e = appendTo(CHAT, false, chat::withEmoji(text)); !e.isEmpty())
        return e;
    if (!windowOpen())
        desktop::sessionRun({u"cd ~ && setsid -f packarch-depannage >/dev/null 2>&1"_s});
    return {};
}

QString follow() {
    if (const QString e = session::requireRoot(); !e.isEmpty())
        return e;
    if (const QString e = ensure(); !e.isEmpty())
        return e;
    // Connexion perdue : l'écriture échoue, le programme n'est pas tué
    std::signal(SIGPIPE, SIG_IGN);
    qint64 pos = 0;
    uint polls = 0;
    while (session::active() && !QFile::exists(STOPPING)) {
        QByteArray text;
        for (const ChatMessage& m : readNew(CHAT, pos))
            text += chat::toLine(m);
        if (polls % HEARTBEAT_POLLS == 0)
            text += '\n';
        if (!text.isEmpty() && !writeOut(text))
            return {};
        ++polls;
        QThread::msleep(POLL_MS);
    }
    return {};
}

}  // namespace chatfile
