#include "tailscale.hpp"

#include <QJsonDocument>
#include <QProcess>

using namespace Qt::StringLiterals;

namespace tailscale {

std::optional<QJsonObject> status() {
    QProcess process;
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(u"tailscale"_s, {u"status"_s, u"--json"_s});
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        return std::nullopt;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput());
    if (!doc.isObject())
        return std::nullopt;
    return doc.object();
}

}  // namespace tailscale
