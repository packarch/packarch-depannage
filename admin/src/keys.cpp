#include "keys.hpp"

#include "chat.hpp"
#include "tailscale.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace {
const QString API = u"https://api.tailscale.com/api/v2"_s;
constexpr int TIMEOUT_MS = 20000;
/// Code de sortie d'un packarch-depannage sans clé de connexion (commande inconnue)
constexpr int EXIT_USAGE = 2;

QString secretPath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + u"/packarch-admin/tailscale-oauth"_s;
}

QString readSecret() {
    QFile f(secretPath());
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

/// Identifiants des clés en cours, par ordinateur (pour révoquer la précédente)
QString registryPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + u"/cles.json"_s;
}

QJsonObject readRegistry() {
    QFile f(registryPath());
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

void writeRegistry(const QJsonObject& registry) {
    QDir().mkpath(QFileInfo(registryPath()).path());
    QSaveFile f(registryPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(registry).toJson());
        f.commit();
    }
}

QNetworkRequest request(const QString& path, const QByteArray& token = {}) {
    QNetworkRequest r{QUrl(API + path)};
    r.setTransferTimeout(TIMEOUT_MS);
    if (!token.isEmpty())
        r.setRawHeader("Authorization", "Bearer " + token);
    return r;
}
}  // namespace

namespace keys {

QString clientId(const QString& secret) {
    static const QRegularExpression form(u"^tskey-client-([A-Za-z0-9]+)-[A-Za-z0-9]+$"_s);
    return form.match(secret).captured(1);
}

QByteArray createBody(const QString& host) {
    QJsonObject create;
    create.insert("reusable"_L1, true);
    // Comme une session ouverte par le lien : machine éphémère, déjà autorisée, avec le tag du dépannage
    create.insert("ephemeral"_L1, true);
    create.insert("preauthorized"_L1, true);
    create.insert("tags"_L1, QJsonArray{QString::fromLatin1(tailscale::TAG)});
    QJsonObject body;
    body.insert("capabilities"_L1, QJsonObject{{u"devices"_s, QJsonObject{{u"create"_s, create}}}});
    body.insert("expirySeconds"_L1, EXPIRY_SECS);
    body.insert("description"_L1, u"packarch-depannage %1"_s.arg(host).left(50));
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

}  // namespace keys

KeyProvisioner::KeyProvisioner(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {}

bool KeyProvisioner::configured() {
    return QFile::exists(secretPath());
}

void KeyProvisioner::fail(const Machine& machine, const QString& what, QNetworkReply* reply) {
    QString detail;
    if (reply) {
        const QString message = QJsonDocument::fromJson(reply->readAll()).object().value("message"_L1).toString();
        detail = message.isEmpty() ? reply->errorString() : message;
    }
    emit status(machine.host, detail.isEmpty() ? u"Clé de connexion : %1."_s.arg(what)
                                               : u"Clé de connexion : %1 (%2)."_s.arg(what, detail));
}

void KeyProvisioner::provision(const Machine& machine) {
    // Le nom et l'adresse viennent de l'autre ordinateur : rien avec des valeurs inattendues
    if (!tailnet::validHost(machine.host) || !tailnet::validIp(machine.ip))
        return;
    const QString secret = readSecret();
    if (secret.isEmpty())
        return;
    const QString id = keys::clientId(secret);
    if (id.isEmpty()) {
        fail(machine, u"le secret de %1 n'a pas la forme tskey-client-…"_s.arg(secretPath()));
        return;
    }
    emit status(machine.host, u"Clé de connexion : création…"_s);
    QUrlQuery form;
    form.addQueryItem(u"client_id"_s, id);
    form.addQueryItem(u"client_secret"_s, secret);
    form.addQueryItem(u"grant_type"_s, u"client_credentials"_s);
    QNetworkRequest r = request(u"/oauth/token"_s);
    r.setHeader(QNetworkRequest::ContentTypeHeader, u"application/x-www-form-urlencoded"_s);
    QNetworkReply* reply = network_->post(r, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, machine] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(machine, u"Tailscale a refusé le client OAuth"_s, reply);
            return;
        }
        const QByteArray token = QJsonDocument::fromJson(reply->readAll()).object().value("access_token"_L1).toString().toUtf8();
        if (token.isEmpty())
            fail(machine, u"réponse inattendue de Tailscale"_s);
        else
            createKey(machine, token);
    });
}

void KeyProvisioner::createKey(const Machine& machine, const QByteArray& token) {
    QNetworkRequest r = request(u"/tailnet/-/keys"_s, token);
    r.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    QNetworkReply* reply = network_->post(r, keys::createBody(machine.host));
    connect(reply, &QNetworkReply::finished, this, [this, reply, machine, token] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(machine, u"création refusée par Tailscale"_s, reply);
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QString id = o.value("id"_L1).toString();
        const QString key = o.value("key"_L1).toString();
        if (id.isEmpty() || key.isEmpty())
            fail(machine, u"réponse inattendue de Tailscale"_s);
        else
            deposit(machine, token, id, key, o.value("expires"_L1).toString());
    });
}

void KeyProvisioner::deposit(const Machine& machine, const QByteArray& token, const QString& id, const QString& key,
                             const QString& expires) {
    // La clé passe par l'entrée standard de ssh : elle n'apparaît dans aucune commande
    auto* ssh = new QProcess(this);
    QStringList args = chat::sshOptions(true);
    args << u"root@%1"_s.arg(machine.ip) << u"packarch-depannage key-set"_s;
    connect(ssh, &QProcess::started, ssh, [ssh, key] {
        ssh->write(key.toUtf8());
        ssh->closeWriteChannel();
    });
    const auto done = [this, ssh, machine, token, id, expires](bool ok, int code) {
        ssh->deleteLater();
        if (!ok) {
            // Pas déposée : elle ne doit pas rester valable
            revoke(token, id);
            if (code == EXIT_USAGE)
                fail(machine, u"cet ordinateur doit d'abord être mis à jour, le lien restera demandé"_s);
            else
                fail(machine, u"dépôt impossible sur l'ordinateur"_s);
            return;
        }
        QJsonObject registry = readRegistry();
        const QString previous = registry.value(machine.host).toString();
        registry.insert(machine.host, id);
        writeRegistry(registry);
        if (!previous.isEmpty() && previous != id)
            revoke(token, previous);
        const QDateTime until = QDateTime::fromString(expires, Qt::ISODate).toLocalTime();
        emit status(machine.host,
                    until.isValid() ? u"Clé de connexion déposée : plus de lien à envoyer jusqu'au %1."_s.arg(
                                          QLocale().toString(until.date(), QLocale::LongFormat))
                                    : u"Clé de connexion déposée : plus de lien à envoyer."_s);
    };
    connect(ssh, &QProcess::finished, this, [done](int code, QProcess::ExitStatus st) {
        done(st == QProcess::NormalExit && code == 0, st == QProcess::NormalExit ? code : -1);
    });
    connect(ssh, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(false, -1);
    });
    ssh->setStandardOutputFile(QProcess::nullDevice());
    ssh->start(u"ssh"_s, args);
}

void KeyProvisioner::revoke(const QByteArray& token, const QString& id) {
    static const QRegularExpression form(u"^[A-Za-z0-9]+$"_s);
    if (!form.match(id).hasMatch())
        return;
    QNetworkReply* reply = network_->deleteResource(request(u"/tailnet/-/keys/%1"_s.arg(id), token));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}
