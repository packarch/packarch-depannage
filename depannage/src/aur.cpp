#include "aur.hpp"

#include "desktop.hpp"

#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <csignal>
#include <pwd.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace aur {

namespace {
/// Compte ordinaire de l'ordinateur : celui du bureau ouvert, sinon le premier
/// compte (uid 1000 et suivants) qui a un dossier personnel
struct Account {
    QString user;
    QString home;
};

std::optional<Account> account() {
    if (const auto d = desktop::find()) {
        if (const passwd* pw = ::getpwuid(d->uid))
            return Account{QString::fromUtf8(pw->pw_name), QString::fromUtf8(pw->pw_dir)};
    }
    std::optional<Account> first;
    uint best = 0;
    ::setpwent();
    while (const passwd* pw = ::getpwent()) {
        if (pw->pw_uid < 1000 || pw->pw_uid >= 60000 || !QFile::exists(QString::fromUtf8(pw->pw_dir)))
            continue;
        if (!first || pw->pw_uid < best) {
            first = Account{QString::fromUtf8(pw->pw_name), QString::fromUtf8(pw->pw_dir)};
            best = pw->pw_uid;
        }
    }
    ::endpwent();
    return first;
}

/// Écrit la règle (vérifiée par visudo avant d'être mise en place)
QString install(const QString& rule) {
    const QString tmp = sudoersPath() + u"~"_s;  // « ~ » : fichier ignoré par sudo
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return u"%1 : %2"_s.arg(tmp, f.errorString());
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadGroup);
    f.write(rule.toUtf8());
    f.close();
    QProcess visudo;
    visudo.setProcessChannelMode(QProcess::MergedChannels);
    visudo.start(u"visudo"_s, {u"-c"_s, u"-q"_s, u"-f"_s, tmp});
    if (!visudo.waitForFinished(15000) || visudo.exitStatus() != QProcess::NormalExit || visudo.exitCode() != 0) {
        QFile::remove(tmp);
        return u"règle sudo refusée par visudo"_s;
    }
    QFile::remove(sudoersPath());
    if (!QFile::rename(tmp, sudoersPath())) {
        QFile::remove(tmp);
        return u"impossible d'écrire %1"_s.arg(sudoersPath());
    }
    return {};
}
}  // namespace

QString sudoersPath() {
    return u"/etc/sudoers.d/zz-packarch-depannage-aur"_s;
}

QString sudoersRule(const QString& user) {
    static const QRegularExpression form(u"^[a-z_][a-z0-9_-]{0,31}$"_s);
    if (user == "root"_L1 || !form.match(user).hasMatch())
        return {};
    return u"# Temporaire : packarch-depannage aur (retirée à la fin de la commande)\n"
           "%1 ALL=(root) NOPASSWD: /usr/bin/pacman\n"_s.arg(user);
}

void cleanup() {
    QFile::remove(sudoersPath());
    QFile::remove(sudoersPath() + u"~"_s);
}

int run(const QStringList& args, QString* error) {
    const auto fail = [error](const QString& text) {
        if (error)
            *error = text;
        return 1;
    };
    if (args.isEmpty())
        return fail(u"usage : packarch-depannage aur [OPTIONS DE YAY] PAQUET…"_s);
    if (::geteuid() != 0)
        return fail(u"cette commande doit être lancée en root"_s);
    if (QStandardPaths::findExecutable(u"yay"_s).isEmpty())
        return fail(u"yay n'est pas installé (pacman -S yay)"_s);
    const auto who = account();
    if (!who)
        return fail(u"aucun compte d'utilisateur trouvé sur cet ordinateur"_s);
    const QString rule = sudoersRule(who->user);
    if (rule.isEmpty())
        return fail(u"nom de compte inattendu : %1"_s.arg(who->user));

    // La règle est retirée quoi qu'il arrive à yay : ce programme ne meurt pas
    // d'un Ctrl+C ni de la fin de la connexion SSH, que yay reçoit de son côté
    for (const int sig : {SIGINT, SIGTERM, SIGHUP, SIGQUIT})
        std::signal(sig, SIG_IGN);
    if (const QString e = install(rule); !e.isEmpty())
        return fail(e);

    QStringList command{u"-u"_s, who->user, u"--"_s, u"env"_s, u"-i"_s,
                        u"HOME="_s + who->home, u"USER="_s + who->user, u"LOGNAME="_s + who->user,
                        u"PATH=/usr/local/bin:/usr/bin"_s};
    for (const char* key : {"LANG", "LC_ALL", "TERM", "COLORTERM"}) {
        if (qEnvironmentVariableIsSet(key))
            command << u"%1=%2"_s.arg(QString::fromLatin1(key), qEnvironmentVariable(key));
    }
    command << u"yay"_s << u"-S"_s << args;
    QProcess process;
    process.setInputChannelMode(QProcess::ForwardedInputChannel);
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    // Le dossier courant de root (/root) est illisible pour l'utilisateur
    process.setWorkingDirectory(who->home);
    process.setChildProcessModifier([] {
        for (const int sig : {SIGINT, SIGTERM, SIGHUP, SIGQUIT})
            ::signal(sig, SIG_DFL);
    });
    process.start(u"runuser"_s, command);
    const bool started = process.waitForStarted(-1);
    if (started)
        process.waitForFinished(-1);
    cleanup();
    if (!started)
        return fail(u"runuser : %1"_s.arg(process.errorString()));
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 1;
}

}  // namespace aur
