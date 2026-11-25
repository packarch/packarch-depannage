#include "chat.hpp"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <pwd.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {
/// Délai avant de retenter une connexion perdue
constexpr int RETRY_MS = 5000;
/// Code de sortie d'une version sans tchat (commande inconnue)
constexpr int EXIT_USAGE = 2;

QString lastLine(const QByteArray& text) {
    const QStringList lines = QString::fromUtf8(text).split(u'\n', Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        if (!it->trimmed().isEmpty())
            return it->trimmed();
    }
    return {};
}

QProcess* ssh(const Machine& machine, const QString& command, QObject* parent) {
    auto* process = new QProcess(parent);
    process->setProgram(u"ssh"_s);
    QStringList args = chat::sshOptions(true);
    args << u"root@%1"_s.arg(machine.ip) << u"packarch-depannage %1"_s.arg(command);
    process->setArguments(args);
    return process;
}
}  // namespace

namespace chat {

QString helperName() {
    QString name = qEnvironmentVariable("USER");
    if (name.isEmpty()) {
        if (const passwd* user = ::getpwuid(::getuid()))
            name = QString::fromLocal8Bit(user->pw_name);
    }
    return name;
}

void announceHelper(const Machine& machine, QObject* parent) {
    const QString name = helperName();
    if (name.isEmpty() || !tailnet::validIp(machine.ip))
        return;
    // Le nom passe par l'entrée standard, pas par le shell distant
    QProcess* say = ssh(machine, u"helper-set"_s, parent);
    say->setStandardOutputFile(QProcess::nullDevice());
    say->setStandardErrorFile(QProcess::nullDevice());
    QObject::connect(say, &QProcess::started, say, [say, name] {
        say->write(name.toUtf8());
        say->closeWriteChannel();
    });
    QObject::connect(say, &QProcess::finished, say, &QObject::deleteLater);
    QObject::connect(say, &QProcess::errorOccurred, say, [say](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            say->deleteLater();
    });
    say->start();
}

QString assistantFlag(const QString& host) {
    if (!tailnet::validHost(host))
        return {};
    return u"%1/packarch-admin/tchat-claude/%2"_s.arg(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation), host);
}

void clearAssistantFlags() {
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + u"/packarch-admin/tchat-claude"_s);
    for (const QString& name : dir.entryList(QDir::Files))
        dir.remove(name);
}

QStringList sshOptions(bool batch) {
    QStringList options{u"StrictHostKeyChecking=no"_s, u"UserKnownHostsFile=/dev/null"_s, u"LogLevel=ERROR"_s,
                        u"ServerAliveInterval=15"_s, u"ServerAliveCountMax=3"_s};
    if (batch)
        options << u"BatchMode=yes"_s << u"ConnectTimeout=15"_s;
    QStringList args;
    for (const QString& o : options)
        args << u"-o"_s << o;
    return args;
}

}  // namespace chat

ChatSession::ChatSession(Machine machine, QObject* parent) : QObject(parent), machine_(std::move(machine)) {
    // Une adresse inattendue n'irait pas dans la commande ssh
    if (tailnet::validIp(machine_.ip))
        follow();
    else
        QTimer::singleShot(0, this, [this] { emit finished(u"Adresse de l'ordinateur inattendue."_s); });
}

ChatSession::~ChatSession() {
    over_ = true;
    if (follower_) {
        follower_->disconnect(this);
        follower_->kill();
        follower_->waitForFinished(1000);
    }
}

void ChatSession::follow() {
    seen_ = 0;
    buffer_.clear();
    follower_ = ssh(machine_, u"chat-follow"_s, this);
    follower_->setStandardInputFile(QProcess::nullDevice());
    connect(follower_, &QProcess::readyReadStandardOutput, this, [this] {
        buffer_ += follower_->readAllStandardOutput();
        qsizetype end;
        while ((end = buffer_.indexOf('\n')) >= 0) {
            const QByteArray line = buffer_.left(end);
            buffer_.remove(0, end + 1);
            // Une ligne, même vide : la connexion est établie
            emit connected();
            if (const auto m = chat::parseLine(line)) {
                if (++seen_ > shown_) {
                    shown_ = seen_;
                    emit message(*m);
                }
            }
        }
    });
    connect(follower_, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) { followEnded(code, status == QProcess::CrashExit); });
    connect(follower_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            over_ = true;
            emit finished(u"Connexion impossible : ssh n'a pas pu être lancé."_s);
        }
    });
    follower_->start();
}

void ChatSession::followEnded(int code, bool crashed) {
    const QString error = lastLine(follower_->readAllStandardError());
    follower_->deleteLater();
    follower_ = nullptr;
    if (over_)
        return;
    if (gone_ || (!crashed && code == 0)) {
        over_ = true;
        emit finished(u"La session de dépannage est terminée."_s);
    } else if (!crashed && code == EXIT_USAGE) {
        over_ = true;
        emit finished(u"Cet ordinateur doit d'abord être mis à jour pour avoir le tchat."_s);
    } else {
        emit lost(error);
        QTimer::singleShot(RETRY_MS, this, [this] {
            if (!over_ && !follower_)
                follow();
        });
    }
}

void ChatSession::send(const QString& text) {
    if (!tailnet::validIp(machine_.ip))
        return;
    // Le texte passe par l'entrée standard, pas par le shell distant
    QProcess* say = ssh(machine_, u"chat-say"_s, this);
    connect(say, &QProcess::started, say, [say, text] {
        say->write(text.toUtf8());
        say->closeWriteChannel();
    });
    connect(say, &QProcess::finished, this, [this, say, text](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0) {
            const QString reason = lastLine(say->readAllStandardError());
            emit notSent(text, reason.isEmpty() ? u"échec"_s : reason);
        }
        say->deleteLater();
    });
    connect(say, &QProcess::errorOccurred, this, [this, say, text](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit notSent(text, u"ssh n'a pas pu être lancé"_s);
            say->deleteLater();
        }
    });
    say->start();
}
