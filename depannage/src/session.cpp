#include "session.hpp"

#include "chatfile.hpp"
#include "tailscale.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QThread>

#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace session {

namespace {
/// État de la session en cours (gardé après un redémarrage pour le nettoyage)
const QString STATE = u"/var/lib/packarch-depannage/session.json"_s;
/// Unités systemd transitoires de la session
const QString UP_UNIT = u"packarch-depannage-up"_s;
/// Instance de tailscaled de la session
const QString TSD_UNIT = u"packarch-depannage-tailscaled"_s;
const QString TAILSCALED = u"/usr/sbin/tailscaled"_s;
const QString STOP_UNIT = u"packarch-depannage-stop"_s;
const QString BIN = u"/usr/bin/packarch-depannage"_s;
/// Clé de connexion déposée par la personne qui dépanne (root seulement). Propre
/// à cet ordinateur, limitée au tag du dépannage, elle expire toute seule ;
/// sans elle, ou si elle ne fonctionne plus : le lien de connexion.
const QString KEY = u"/var/lib/packarch-depannage/authkey"_s;
/// Attente de la connexion avec la clé, avant de passer au lien
const QString KEY_TIMEOUT = u"30s"_s;

struct Saved {
    qint64 end = 0;
    QString hostname;
};

void say(const QString& text) {
    QTextStream(stdout) << text << Qt::endl;
}

std::optional<Saved> load() {
    QFile f(STATE);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject o = doc.object();
    if (!o.value("end"_L1).isDouble() || !o.value("hostname"_L1).isString())
        return std::nullopt;
    return Saved{o.value("end"_L1).toInteger(), o.value("hostname"_L1).toString()};
}

Error save(const Saved& s) {
    const QString dir = QFileInfo(STATE).path();
    if (!QDir().mkpath(dir))
        return u"%1 : création impossible"_s.arg(dir);
    QFile f(STATE);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return u"%1 : %2"_s.arg(STATE, f.errorString());
    QJsonObject o;
    o.insert("end"_L1, s.end);
    o.insert("hostname"_L1, s.hostname);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return {};
}

/// Lance CMD ARGS sans rien en afficher ; rend sa dernière ligne d'erreur en
/// cas d'échec (ERR), vrai s'il a réussi
bool run(const QString& cmd, const QStringList& args, QString* err = nullptr) {
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.start(cmd, args);
    if (!process.waitForFinished(-1)) {
        if (err)
            *err = u"%1 : %2"_s.arg(cmd, process.errorString());
        return false;
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0)
        return true;
    if (err) {
        *err = u"échec"_s;
        const QStringList lines = QString::fromUtf8(process.readAllStandardError()).split(u'\n');
        for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
            if (!it->trimmed().isEmpty()) {
                *err = it->trimmed();
                break;
            }
        }
    }
    return false;
}

/// Service tailscaled du système (Tailscale utilisé pour autre chose)
bool systemTailscaledActive() {
    return run(u"systemctl"_s, {u"is-active"_s, u"--quiet"_s, u"tailscaled"_s});
}

/// Instance de tailscaled de la session
bool sessionTailscaledActive() {
    return run(u"systemctl"_s, {u"is-active"_s, u"--quiet"_s, TSD_UNIT + u".service"_s});
}

/// Lance l'instance de tailscaled de la session : état en mémoire (machine
/// éphémère), mêmes réglages que le service du système (socket, nettoyage du
/// réseau à l'arrêt)
bool startSessionTailscaled(QString* err) {
    return run(u"systemd-run"_s,
               {u"--unit="_s + TSD_UNIT, u"--collect"_s,
                u"--description=Tailscale du dépannage à distance (Packarch)"_s,
                u"-p"_s, u"Type=notify"_s,
                u"-p"_s, u"RuntimeDirectory=tailscale packarch-depannage"_s,
                u"-p"_s, u"RuntimeDirectoryMode=0755"_s,
                u"-p"_s, u"ExecStopPost=%1 --cleanup"_s.arg(TAILSCALED),
                TAILSCALED, u"--state=mem:"_s, u"--statedir=/run/packarch-depannage/tailscale"_s,
                u"--socket=/run/tailscale/tailscaled.sock"_s, u"--port=0"_s},
               err);
}

/// Programme l'arrêt de la session dans SECS secondes (remplace l'ancien)
Error scheduleStop(qint64 secs) {
    run(u"systemctl"_s, {u"stop"_s, STOP_UNIT + u".timer"_s});
    const QString delay = u"--on-active=%1s"_s.arg(qMax<qint64>(secs, 1));
    if (!run(u"systemd-run"_s, {u"--unit="_s + STOP_UNIT, u"--collect"_s, delay, BIN, u"stop"_s}))
        return u"impossible de programmer l'arrêt automatique (systemd-run)"_s;
    return {};
}

/// Processus des connexions Tailscale SSH (« tailscaled be-child ssh »)
int sshSessions() {
    int count = 0;
    const QStringList pids = QDir(u"/proc"_s).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& pid : pids) {
        bool number = false;
        pid.toLongLong(&number);
        if (!number)
            continue;
        QFile f(u"/proc/%1/cmdline"_s.arg(pid));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QList<QByteArray> args = f.readAll().split('\0');
        for (qsizetype i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == "be-child" && args[i + 1] == "ssh") {
                ++count;
                break;
            }
        }
    }
    return count;
}

std::optional<QString> text(const QJsonValue& v) {
    const QString s = v.toString();
    return s.isEmpty() ? std::nullopt : std::optional(s);
}
}  // namespace

bool Status::helping() const {
    return sshSessions > 0 || (lastActivity && now() - *lastActivity < ACTIVITY_SECS);
}

QJsonObject Status::toJson() const {
    const auto str = [](const std::optional<QString>& v) { return v ? QJsonValue(*v) : QJsonValue(); };
    const auto num = [](const std::optional<qint64>& v) { return v ? QJsonValue(*v) : QJsonValue(); };
    QJsonObject o;
    o.insert("active"_L1, active);
    o.insert("end"_L1, num(end));
    o.insert("hostname"_L1, hostname);
    o.insert("backend"_L1, backend);
    o.insert("auth_url"_L1, str(authUrl));
    o.insert("dns_name"_L1, str(dnsName));
    o.insert("ssh_sessions"_L1, sshSessions);
    o.insert("helper"_L1, str(helper));
    o.insert("last_activity"_L1, num(lastActivity));
    o.insert("helper_name"_L1, str(helperName));
    o.insert("helper_login"_L1, str(helperLogin));
    return o;
}

Status Status::fromJson(const QJsonObject& o) {
    const auto str = [&o](QLatin1StringView key) {
        const QJsonValue v = o.value(key);
        return v.isString() ? std::optional(v.toString()) : std::nullopt;
    };
    const auto num = [&o](QLatin1StringView key) {
        const QJsonValue v = o.value(key);
        return v.isDouble() ? std::optional(v.toInteger()) : std::nullopt;
    };
    Status st;
    st.active = o.value("active"_L1).toBool();
    st.end = num("end"_L1);
    st.hostname = o.value("hostname"_L1).toString();
    st.backend = o.value("backend"_L1).toString();
    st.authUrl = str("auth_url"_L1);
    st.dnsName = str("dns_name"_L1);
    st.sshSessions = o.value("ssh_sessions"_L1).toInt();
    st.helper = str("helper"_L1);
    st.lastActivity = num("last_activity"_L1);
    st.helperName = str("helper_name"_L1);
    st.helperLogin = str("helper_login"_L1);
    return st;
}

qint64 now() {
    return QDateTime::currentSecsSinceEpoch();
}

std::optional<qint64> unixTime(const QString& date) {
    // Tailscale donne des nanosecondes : seules les secondes comptent
    static const QRegularExpression fraction(u"\\.\\d+"_s);
    QString plain = date;
    plain.remove(fraction);
    const QDateTime t = QDateTime::fromString(plain, Qt::ISODate);
    if (!t.isValid() || t.toSecsSinceEpoch() <= 0)
        return std::nullopt;
    return t.toSecsSinceEpoch();
}

std::optional<Peer> lastPeer(const QJsonObject& j) {
    std::optional<Peer> best;
    const QJsonObject peers = j.value("Peer"_L1).toObject();
    const QJsonObject users = j.value("User"_L1).toObject();
    for (const QJsonValue& value : peers) {
        const QJsonObject p = value.toObject();
        if (p.value("RxBytes"_L1).toInteger() <= 0)
            continue;
        const auto time = unixTime(p.value("LastWrite"_L1).toString());
        if (!time || (best && *time < best->time))
            continue;
        const QJsonObject user = users.value(QString::number(p.value("UserID"_L1).toInteger())).toObject();
        const QString host = p.value("HostName"_L1).toString();
        best = Peer{host.isEmpty() ? u"?"_s : host, *time, text(user.value("DisplayName"_L1)),
                    text(user.value("LoginName"_L1))};
    }
    return best;
}

QString machineName(const QString& raw) {
    QString name;
    for (QChar c : raw.trimmed().toLower()) {
        const char16_t u = c.unicode();
        name += ((u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9')) ? c : QChar(u'-');
    }
    while (name.startsWith(u'-'))
        name.removeFirst();
    while (name.endsWith(u'-'))
        name.chop(1);
    if (name.isEmpty())
        return u"packarch-ordinateur"_s;
    if (name.startsWith("packarch"_L1))
        return name;
    return u"packarch-"_s + name;
}

QString hostname() {
    QFile f(u"/etc/hostname"_s);
    return machineName(f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString());
}

bool active() {
    return load().has_value();
}

Error requireRoot() {
    if (getuid() != 0)
        return u"à lancer en root (pkexec packarch-depannage …)"_s;
    return {};
}

Error start(qint64 minutes) {
    if (const Error e = requireRoot(); !e.isEmpty())
        return e;
    if (const auto s = load()) {
        if (s->end > now()) {
            say(u"Session déjà en cours (%1)."_s.arg(s->hostname));
            return {};
        }
    } else if (systemTailscaledActive()) {
        // Tailscale utilisé pour autre chose sur cet ordinateur : on n'y touche pas
        return u"Tailscale est déjà utilisé sur cet ordinateur, en dehors du dépannage"_s;
    }
    const QString host = hostname();
    const qint64 end = now() + minutes * 60;
    if (const Error e = save({end, host}); !e.isEmpty())
        return e;
    if (!sessionTailscaledActive()) {
        QString err;
        if (!startSessionTailscaled(&err)) {
            QFile::remove(STATE);
            return u"impossible de démarrer tailscaled (%1)"_s.arg(err);
        }
    }
    // tailscaled prêt à répondre
    for (int i = 0; i < 50 && !tailscale::status(); ++i)
        QThread::msleep(200);
    // Le dossier de la session existe (RuntimeDirectory de tailscaled)
    if (const Error e = chatfile::ensure(); !e.isEmpty())
        QTextStream(stderr) << u"tchat indisponible : %1"_s.arg(e) << Qt::endl;
    if (const Error e = scheduleStop(minutes * 60); !e.isEmpty())
        return e;
    // La connexion attend que le lien soit validé (sans clé de connexion) : elle
    // tourne dans sa propre unité, la fenêtre lit le lien dans « tailscale status »
    run(u"systemctl"_s, {u"stop"_s, UP_UNIT + u".service"_s});
    QString err;
    if (!run(u"systemd-run"_s, {u"--unit="_s + UP_UNIT, u"--collect"_s, BIN, u"connect"_s}, &err)) {
        stop();
        return u"impossible de lancer la connexion à Tailscale (%1)"_s.arg(err);
    }
    say(u"Session démarrée : %1, fin dans %2 min."_s.arg(host).arg(minutes));
    return {};
}

Error extend(qint64 minutes) {
    if (const Error e = requireRoot(); !e.isEmpty())
        return e;
    auto s = load();
    if (!s)
        return u"aucune session en cours"_s;
    s->end = qMax(s->end, now()) + minutes * 60;
    if (const Error e = save(*s); !e.isEmpty())
        return e;
    if (const Error e = scheduleStop(s->end - now()); !e.isEmpty())
        return e;
    say(u"Session prolongée de %1 min."_s.arg(minutes));
    return {};
}

Error stop(bool ifActive) {
    if (const Error e = requireRoot(); !e.isEmpty())
        return e;
    if (ifActive && !load())
        return {};
    run(u"systemctl"_s, {u"stop"_s, STOP_UNIT + u".timer"_s});
    run(u"systemctl"_s, {u"stop"_s, UP_UNIT + u".service"_s});
    if (sessionTailscaledActive()) {
        chatfile::announceStop();
        // Retire la machine éphémère du réseau tout de suite (sans cela,
        // Tailscale la supprime peu après sa déconnexion)
        run(u"tailscale"_s, {u"logout"_s});
        run(u"systemctl"_s, {u"stop"_s, TSD_UNIT + u".service"_s});
    }
    QFile::remove(STATE);
    say(u"Session de dépannage terminée."_s);
    return {};
}

bool validKey(const QString& key) {
    static const QRegularExpression form(u"^tskey-auth-[A-Za-z0-9]+-[A-Za-z0-9]+$"_s);
    return key.size() <= 200 && form.match(key).hasMatch();
}

Error setKey() {
    if (const Error e = requireRoot(); !e.isEmpty())
        return e;
    QFile in;
    if (!in.open(stdin, QIODevice::ReadOnly))
        return u"entrée standard illisible"_s;
    const QString key = QString::fromUtf8(in.read(1000)).trimmed();
    if (!validKey(key))
        return u"clé de connexion inattendue"_s;
    const QString dir = QFileInfo(KEY).path();
    if (!QDir().mkpath(dir))
        return u"%1 : création impossible"_s.arg(dir);
    // Créé pour root seul, puis mis à sa place d'un coup
    const mode_t before = ::umask(0077);
    QSaveFile f(KEY);
    const bool written = f.open(QIODevice::WriteOnly) && f.write(key.toUtf8() + '\n') > 0 && f.commit();
    ::umask(before);
    if (!written)
        return u"%1 : %2"_s.arg(KEY, f.errorString());
    say(u"Clé de connexion enregistrée."_s);
    return {};
}

Error connectRun() {
    if (const Error e = requireRoot(); !e.isEmpty())
        return e;
    const auto saved = load();
    if (!saved)
        return u"aucune session en cours"_s;
    const QStringList up{u"up"_s, u"--ssh"_s, u"--reset"_s, u"--hostname="_s + saved->hostname,
                         u"--advertise-tags=%1"_s.arg(QLatin1StringView(tailscale::TAG))};
    if (QFile::exists(KEY)) {
        // La clé est lue par tailscale dans le fichier : elle n'apparaît dans aucune commande
        QString err;
        if (run(u"tailscale"_s, up + QStringList{u"--auth-key=file:"_s + KEY, u"--timeout="_s + KEY_TIMEOUT}, &err)) {
            say(u"Connecté avec la clé de connexion."_s);
            return {};
        }
        QTextStream(stderr) << u"clé de connexion refusée (%1), passage au lien"_s.arg(err) << Qt::endl;
        // Expirée ou révoquée : elle ne servira plus. Autre échec (réseau…) : elle est gardée.
        static const QRegularExpression dead(u"invalid|expired|not valid|revoked|not found"_s,
                                             QRegularExpression::CaseInsensitiveOption);
        if (dead.match(err).hasMatch())
            QFile::remove(KEY);
    }
    QString err;
    if (!run(u"tailscale"_s, up + QStringList{u"--timeout=0"_s}, &err))
        return u"tailscale up : %1"_s.arg(err);
    return {};
}

Status status() {
    const auto saved = load();
    Status st;
    if (!saved) {
        st.hostname = hostname();
        return st;
    }
    st.active = true;
    st.end = saved->end;
    st.hostname = saved->hostname;
    if (sessionTailscaledActive()) {
        if (const auto j = tailscale::status()) {
            st.backend = j->value("BackendState"_L1).toString();
            st.authUrl = text(j->value("AuthURL"_L1));
            if (const auto p = lastPeer(*j)) {
                st.helper = p->host;
                st.lastActivity = p->time;
                st.helperName = p->name;
                st.helperLogin = p->login;
            }
            QString dns = j->value("Self"_L1).toObject().value("DNSName"_L1).toString();
            while (dns.endsWith(u'.'))
                dns.chop(1);
            if (!dns.isEmpty() && st.backend == "Running"_L1)
                st.dnsName = dns;
        }
    }
    st.sshSessions = sshSessions();
    return st;
}

}  // namespace session
