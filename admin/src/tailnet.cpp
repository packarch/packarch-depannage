#include "tailnet.hpp"

#include "tailscale.hpp"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tailnet {

bool validHost(const QString& host) {
    if (host.isEmpty() || host.size() > 63)
        return false;
    return std::ranges::all_of(host, [](QChar c) {
        const char16_t u = c.unicode();
        return (u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9') || u == u'-';
    });
}

QString shownHost(const QString& host) {
    QString out;
    for (QChar c : host) {
        if (c.category() != QChar::Other_Control && out.size() < 40)
            out += c;
    }
    return out;
}

bool validIp(const QString& ip) {
    // QHostAddress accepte aussi des formes abrégées (« 1 ») : seulement les caractères d'une adresse
    static const QString allowed = u"0123456789abcdefABCDEF.:"_s;
    return !ip.isEmpty() && std::ranges::all_of(ip, [](QChar c) { return allowed.contains(c); })
        && !QHostAddress(ip).isNull();
}

std::optional<QList<Machine>> machinesIn(const QByteArray& statusJson) {
    const QJsonObject status = QJsonDocument::fromJson(statusJson).object();
    if (status.value("BackendState"_L1).toString() != "Running"_L1)
        return std::nullopt;
    QList<Machine> list;
    const QJsonObject peers = status.value("Peer"_L1).toObject();
    for (const QJsonValue& value : peers) {
        const QJsonObject peer = value.toObject();
        const QString host = peer.value("HostName"_L1).toString();
        const QString ip = peer.value("TailscaleIPs"_L1).toArray().first().toString();
        const bool tagged = peer.value("Tags"_L1).toArray().contains(QJsonValue(QLatin1StringView(tailscale::TAG)));
        if (peer.value("Online"_L1).toBool() && tagged && !host.isEmpty() && !ip.isEmpty())
            list.append({host, ip});
    }
    std::ranges::sort(list, {}, &Machine::host);
    return list;
}

}  // namespace tailnet

Arrivals::Change Arrivals::update(const QList<Machine>& machines, qint64 now) {
    Change change;
    for (const Machine& m : machines) {
        if (!seen_.contains(m.host))
            change.arrived.append(m);
        seen_.insert(m.host, now);
    }
    for (auto it = seen_.begin(); it != seen_.end();) {
        if (now - it.value() >= FORGET_SECS) {
            change.gone.append(it.key());
            it = seen_.erase(it);
        } else {
            ++it;
        }
    }
    return change;
}

Tailnet::Tailnet(QObject* parent) : QObject(parent) {}

void Tailnet::start() {
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Tailnet::poll);
    timer->start(3000);
    poll();
}

void Tailnet::poll() {
    // La relecture précédente n'a pas fini (tailscaled lent) : attendre
    if (process_)
        return;
    process_ = new QProcess(this);
    process_->setProgram(u"tailscale"_s);
    process_->setArguments({u"status"_s, u"--json"_s});
    process_->setStandardErrorFile(QProcess::nullDevice());
    auto done = [this] {
        const auto machines = tailnet::machinesIn(process_->readAllStandardOutput());
        process_->deleteLater();
        process_ = nullptr;
        connected_ = machines.has_value();
        machines_ = machines.value_or(QList<Machine>{});
        // Tailscale déconnecté : personne n'est oublié pour autant
        if (connected_) {
            const auto change = arrivals_.update(machines_, QDateTime::currentSecsSinceEpoch());
            for (const QString& host : change.gone)
                emit gone(host);
            for (const Machine& m : change.arrived)
                emit arrived(m);
        }
        emit polled();
    };
    connect(process_, &QProcess::finished, this, done);
    connect(process_, &QProcess::errorOccurred, this, [this, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done();
    });
    process_->start();
}
