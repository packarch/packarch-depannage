#include "singleinstance.hpp"

#include <QLocalServer>
#include <QLocalSocket>

#include <unistd.h>

using namespace Qt::StringLiterals;

SingleInstance::SingleInstance(const QString& name, QObject* parent)
    : QObject(parent), name_(u"%1-%2"_s.arg(name).arg(getuid())) {}

bool SingleInstance::alreadyRunning(bool askShow) {
    QLocalSocket other;
    other.connectToServer(name_);
    if (other.waitForConnected(500)) {
        if (askShow) {
            other.write("show\n");
            other.waitForBytesWritten(500);
        }
        return true;
    }
    // Socket resté d'une instance arrêtée brutalement
    QLocalServer::removeServer(name_);
    server_ = new QLocalServer(this);
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    server_->listen(name_);
    connect(server_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* client = server_->nextPendingConnection()) {
            connect(client, &QLocalSocket::readyRead, this, &SingleInstance::showRequested);
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        }
    });
    return false;
}
