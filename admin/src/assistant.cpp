#include "assistant.hpp"

#include "chat.hpp"
#include "tailnet.hpp"
#include "tailscale.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTextStream>
#include <QTimer>

#include <unistd.h>

using namespace Qt::StringLiterals;

namespace assistant {

namespace {
const QString FOLLOW = u"tchat-suivre"_s;
const QString SAY = u"tchat-dire"_s;
/// Nom sous lequel l'assistant écrit
const QString NAME = u"Claude"_s;
constexpr int POLL_MS = 1000;
constexpr int RETRY_MS = 5000;

void say(const QString& line) {
    QTextStream(stdout) << line << Qt::endl;
}

bool allowed(const QString& host) {
    const QString flag = chat::assistantFlag(host);
    return !flag.isEmpty() && QFile::exists(flag);
}

/// Adresse Tailscale de HOST, s'il est en ligne et porte le tag du dépannage
QString address(const QString& host) {
    const auto status = tailscale::status();
    if (!status)
        return {};
    const auto machines = tailnet::machinesIn(QJsonDocument(*status).toJson());
    if (!machines)
        return {};
    for (const Machine& m : *machines) {
        if (m.host == host && tailnet::validIp(m.ip))
            return m.ip;
    }
    return {};
}

QStringList sshTo(const QString& ip, const QString& command) {
    QStringList args = chat::sshOptions(true);
    args << u"root@%1"_s.arg(ip) << command;
    return args;
}

/// Suit le tchat de HOST tant que la case est cochée
class Follower : public QObject {
public:
    explicit Follower(QString host) : host_(std::move(host)) {
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &Follower::check);
        timer->start(POLL_MS);
        if (!allowed(host_))
            say(u"[tchat interdit : la case « Claude peut lire le tchat et y écrire » n'est pas cochée ; en attente]"_s);
        check();
    }

private:
    void check() {
        const bool now = allowed(host_);
        if (now == allowed_)
            return;
        allowed_ = now;
        if (allowed_) {
            say(u"[tchat autorisé par l'utilisateur : les messages de la personne suivent ; lui répondre avec « packarch-admin tchat-dire %1 »]"_s.arg(host_));
            start();
        } else {
            stop();
            say(u"[tchat interdit : l'utilisateur a décoché la case ; ne plus lire ni écrire]"_s);
        }
    }

    void start() {
        if (ssh_ || !allowed_)
            return;
        const QString ip = address(host_);
        if (ip.isEmpty()) {
            say(u"[%1 n'est plus dans le réseau : la session de dépannage est terminée]"_s.arg(host_));
            QCoreApplication::exit(0);
            return;
        }
        buffer_.clear();
        since_ = -1;
        ssh_ = new QProcess(this);
        ssh_->setStandardInputFile(QProcess::nullDevice());
        ssh_->setStandardErrorFile(QProcess::nullDevice());
        connect(ssh_, &QProcess::readyReadStandardOutput, this, &Follower::read);
        connect(ssh_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            ssh_->deleteLater();
            ssh_ = nullptr;
            if (!allowed_)
                return;
            if (status == QProcess::NormalExit && code == 0) {
                say(u"[la session de dépannage est terminée]"_s);
                QCoreApplication::exit(0);
            } else {
                QTimer::singleShot(RETRY_MS, this, &Follower::start);
            }
        });
        // L'heure de l'ordinateur d'abord : ce qui a été écrit avant l'autorisation n'est pas lu
        ssh_->start(u"ssh"_s, sshTo(ip, u"date +%s && exec packarch-depannage chat-follow"_s));
    }

    void stop() {
        if (!ssh_)
            return;
        ssh_->disconnect(this);
        ssh_->kill();
        ssh_->waitForFinished(1000);
        ssh_->deleteLater();
        ssh_ = nullptr;
    }

    void read() {
        buffer_ += ssh_->readAllStandardOutput();
        qsizetype end;
        while ((end = buffer_.indexOf('\n')) >= 0) {
            const QByteArray line = buffer_.left(end);
            buffer_.remove(0, end + 1);
            if (since_ < 0) {
                bool ok = false;
                since_ = line.trimmed().toLongLong(&ok);
                if (!ok)
                    since_ = 0;
                continue;
            }
            const auto m = chat::parseLine(line);
            // Seulement ce que la personne écrit, depuis l'autorisation
            if (!m || !m->fromUser || m->time < since_)
                continue;
            QString text = m->text;
            text.replace(u'\n', u' ');
            say(u"[message de la personne] %1"_s.arg(chat::shown(m->name.isEmpty() ? tailnet::shownHost(host_) : m->name, text)));
        }
    }

    QString host_;
    bool allowed_ = false;
    QProcess* ssh_ = nullptr;
    QByteArray buffer_;
    /// Heure de l'ordinateur dépanné au début du suivi (-1 : pas encore reçue)
    qint64 since_ = -1;
};

int sayTo(const QString& host) {
    QTextStream err(stderr);
    if (!allowed(host)) {
        err << u"packarch-admin : tchat interdit (la case « Claude peut lire le tchat et y écrire » n'est pas cochée) : rien n'est envoyé\n"_s;
        return 3;
    }
    QFile in;
    const QString text = chat::withEmoji(chat::clean(in.open(stdin, QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()) : QString()));
    if (text.isEmpty()) {
        err << u"packarch-admin : message vide\n"_s;
        return 2;
    }
    const QString ip = address(host);
    if (ip.isEmpty()) {
        err << u"packarch-admin : %1 n'est pas dans le réseau\n"_s.arg(host);
        return 1;
    }
    QProcess ssh;
    ssh.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    ssh.start(u"ssh"_s, sshTo(ip, u"packarch-depannage chat-say %1"_s.arg(NAME)));
    if (!ssh.waitForStarted(10000))
        return 1;
    // Le texte passe par l'entrée standard, pas par le shell distant
    ssh.write(text.toUtf8());
    ssh.closeWriteChannel();
    if (!ssh.waitForFinished(30000) || ssh.exitStatus() != QProcess::NormalExit)
        return 1;
    return ssh.exitCode();
}
}  // namespace

bool isCommand(const QStringList& args) {
    return !args.isEmpty() && (args.first() == FOLLOW || args.first() == SAY);
}

int run(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments().mid(1);
    const QString host = args.value(1);
    // Le nom va dans un chemin de fichier : seulement la forme attendue
    if (!tailnet::validHost(host)) {
        QTextStream(stderr) << u"Usage : packarch-admin %1 packarch-NOM\n"_s.arg(args.first());
        return 2;
    }
    if (args.first() == SAY)
        return sayTo(host);
    Follower follower(host);
    return QCoreApplication::exec();
}

}  // namespace assistant
