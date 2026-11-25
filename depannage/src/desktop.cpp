#include "desktop.hpp"

#include "session.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QTextStream>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace desktop {

namespace {
/// Variables reprises de l'environnement du bureau
const QStringList KEEP{
    u"DISPLAY"_s, u"XAUTHORITY"_s, u"WAYLAND_DISPLAY"_s, u"PACKARCH_PID"_s,
    u"DBUS_SESSION_BUS_ADDRESS"_s, u"XDG_RUNTIME_DIR"_s, u"XDG_CURRENT_DESKTOP"_s,
    u"XDG_SESSION_DESKTOP"_s, u"XDG_SESSION_TYPE"_s, u"XDG_CONFIG_HOME"_s, u"XDG_DATA_DIRS"_s, u"DESKTOP_SESSION"_s,
    u"HOME"_s, u"USER"_s, u"LOGNAME"_s, u"SHELL"_s, u"LANG"_s, u"LC_ALL"_s, u"PATH"_s,
};

/// Processus « racines » d'un bureau, préférés pour lire l'environnement.
/// Packarch : waybar ou mako, lancés par le compositeur (le processus packarch
/// n'a pas WAYLAND_DISPLAY ni PACKARCH_PID, qu'il donne seulement à ses enfants)
const QList<QByteArray> SESSION_PROCS{"xfce4-session", "openbox", "xfwm4", "polybar", "xfce4-panel", "waybar", "mako"};

std::optional<QString> userName(uint uid) {
    QFile f(u"/etc/passwd"_s);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    for (const QByteArray& line : f.readAll().split('\n')) {
        const QList<QByteArray> fields = line.split(':');
        bool ok = false;
        if (fields.size() > 2 && fields[2].toUInt(&ok) == uid && ok)
            return QString::fromUtf8(fields[0]);
    }
    return std::nullopt;
}

/// Packarch sans waybar ni mako (processus retenu sans WAYLAND_DISPLAY) : le
/// socket Wayland, retrouvé dans XDG_RUNTIME_DIR
void waylandFallback(Desktop& d) {
    if (!d.get(u"XDG_CURRENT_DESKTOP"_s).toLower().split(u':').contains(u"packarch"_s))
        return;
    if (!d.get(u"WAYLAND_DISPLAY"_s).isEmpty())
        return;
    QString run = d.get(u"XDG_RUNTIME_DIR"_s);
    if (run.isEmpty())
        run = u"/run/user/%1"_s.arg(d.uid);
    QStringList wayland;
    for (const QString& name : QDir(run).entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot)) {
        const QString number = name.mid(8);
        if (name.startsWith("wayland-"_L1) && !number.isEmpty()
            && std::ranges::all_of(number, [](QChar c) { return c >= u'0' && c <= u'9'; }))
            wayland << name;
    }
    wayland.sort();
    if (!wayland.isEmpty())
        d.env.append({u"WAYLAND_DISPLAY"_s, wayland.first()});
}

QString output(const QString& cmd, const QStringList& args) {
    QProcess process;
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(cmd, args);
    if (!process.waitForFinished(15000))
        return {};
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QString osName() {
    QFile f(u"/etc/os-release"_s);
    if (f.open(QIODevice::ReadOnly)) {
        for (const QByteArray& line : f.readAll().split('\n')) {
            if (line.startsWith("PRETTY_NAME=")) {
                QString name = QString::fromUtf8(line.mid(12));
                while (name.startsWith(u'"'))
                    name.removeFirst();
                while (name.endsWith(u'"'))
                    name.chop(1);
                return name;
            }
        }
    }
    return u"?"_s;
}
}  // namespace

QString Desktop::get(const QString& key) const {
    for (const auto& [k, v] : env) {
        if (k == key)
            return v;
    }
    return {};
}

std::optional<Desktop> find() {
    struct Candidate {
        bool preferred = false;
        uint uid = 0;
        QHash<QString, QString> env;
    };
    std::optional<Candidate> best;
    for (const QString& pid : QDir(u"/proc"_s).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool number = false;
        pid.toLongLong(&number);
        if (!number)
            continue;
        const QString dir = u"/proc/%1"_s.arg(pid);
        const uint uid = QFileInfo(dir).ownerId();
        // Ni compte système, ni « nobody », ni propriétaire illisible
        if (uid < 1000 || uid == 65534 || uid == static_cast<uint>(-2))
            continue;
        QFile environ(dir + u"/environ"_s);
        if (!environ.open(QIODevice::ReadOnly))
            continue;
        QHash<QString, QString> env;
        for (const QByteArray& kv : environ.readAll().split('\0')) {
            const qsizetype eq = kv.indexOf('=');
            if (eq > 0)
                env.insert(QString::fromUtf8(kv.left(eq)), QString::fromUtf8(kv.mid(eq + 1)));
        }
        if (!env.contains(u"DISPLAY"_s) && !env.contains(u"WAYLAND_DISPLAY"_s))
            continue;
        QFile comm(dir + u"/comm"_s);
        const bool preferred = comm.open(QIODevice::ReadOnly) && SESSION_PROCS.contains(comm.readAll().trimmed());
        if (!best || (preferred && !best->preferred)) {
            best = Candidate{preferred, uid, env};
            if (preferred)
                break;
        }
    }
    if (!best)
        return std::nullopt;
    Desktop d;
    d.uid = best->uid;
    if (best->env.contains(u"USER"_s)) {
        d.user = best->env.value(u"USER"_s);
    } else if (const auto name = userName(best->uid)) {
        d.user = *name;
    } else {
        return std::nullopt;
    }
    for (const QString& key : KEEP) {
        if (best->env.contains(key))
            d.env.append({key, best->env.value(key)});
    }
    if (d.get(u"PATH"_s).isEmpty() && !best->env.contains(u"PATH"_s))
        d.env.append({u"PATH"_s, u"/usr/local/bin:/usr/bin"_s});
    waylandFallback(d);
    return d;
}

int sessionRun(const QStringList& command, QString* error) {
    const auto fail = [error](const QString& text) {
        if (error)
            *error = text;
        return 1;
    };
    if (command.isEmpty())
        return fail(u"usage : packarch-depannage session-run COMMANDE…"_s);
    const auto d = find();
    if (!d)
        return fail(u"aucune session graphique trouvée (personne n'est connecté ?)"_s);
    QStringList args{u"-u"_s, d->user, u"--"_s, u"env"_s, u"-i"_s};
    for (const auto& [k, v] : d->env)
        args << u"%1=%2"_s.arg(k, v);
    // Dans un scope du systemd de l'utilisateur, pas dans la session SSH de
    // root : ce que la commande démarre (polybar relancé par packarch-skin…)
    // garderait sinon cette session distante, et ce qu'il lance à son tour
    // n'aurait plus les droits du bureau (Éteindre du menu de polybar refusé
    // sans rien dire par polkit). Le scope survit aussi à la connexion SSH.
    if (QFileInfo::exists(u"/run/user/%1/bus"_s.arg(d->uid)))
        args << u"systemd-run"_s << u"--user"_s << u"--scope"_s << u"--quiet"_s << u"--collect"_s << u"--"_s;
    args << u"sh"_s << u"-c"_s << command.join(u' ');
    QProcess process;
    // L'entrée et les sorties de la commande sont celles de ce programme
    process.setInputChannelMode(QProcess::ForwardedInputChannel);
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(u"runuser"_s, args);
    if (!process.waitForStarted(-1))
        return fail(u"runuser : %1"_s.arg(process.errorString()));
    process.waitForFinished(-1);
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 1;
}

void info() {
    QTextStream out(stdout);
    const session::Status st = session::status();
    out << u"Machine        : %1\n"_s.arg(st.dnsName.value_or(st.hostname));
    out << u"Système        : %1\n"_s.arg(osName());
    out << u"Noyau          : %1\n"_s.arg(output(u"uname"_s, {u"-r"_s}));
    if (const auto d = find()) {
        const QString home = d->get(u"HOME"_s);
        const QString current = d->get(u"XDG_CURRENT_DESKTOP"_s);
        out << u"Utilisateur    : %1 (uid %2, %3)\n"_s.arg(d->user).arg(d->uid).arg(home);
        out << u"Bureau         : %1\n"_s.arg(current);
        const QString w = d->get(u"WAYLAND_DISPLAY"_s);
        const QString x = d->get(u"DISPLAY"_s);
        if (w.isEmpty())
            out << u"Écran          : %1\n"_s.arg(x);
        else if (x.isEmpty())
            out << u"Écran          : %1 (Wayland)\n"_s.arg(w);
        else
            out << u"Écran          : %1 (Wayland, Xwayland %2)\n"_s.arg(w, x);
        const QString theme = output(u"runuser"_s, {u"-u"_s, d->user, u"--"_s, u"env"_s, u"HOME="_s + home,
                                                    u"XDG_CURRENT_DESKTOP="_s + current, u"packarch-skin"_s, u"current"_s});
        if (!theme.isEmpty())
            out << u"Thème          : %1 (packarch-skin)\n"_s.arg(theme);
    } else {
        out << u"Utilisateur    : aucune session graphique\n"_s;
    }
    if (st.end)
        out << u"Fin de session : dans %1 min\n"_s.arg(qMax<qint64>(*st.end - session::now(), 0) / 60);
    out << u"\nCommandes dans le bureau de l'utilisateur :\n  packarch-depannage session-run 'COMMANDE'\n"_s;
}

}  // namespace desktop
