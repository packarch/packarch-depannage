// Une seule fenêtre par programme et par utilisateur : la deuxième demande à
// la première de se montrer, puis se termine.
#pragma once

#include <QObject>

class QLocalServer;

class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(const QString& name, QObject* parent = nullptr);
    /// Vrai si le programme tourne déjà ; ASK_SHOW : lui demander de montrer sa fenêtre
    bool alreadyRunning(bool askShow);

signals:
    /// Une autre instance demande que la fenêtre soit montrée
    void showRequested();

private:
    QString name_;
    QLocalServer* server_ = nullptr;
};
