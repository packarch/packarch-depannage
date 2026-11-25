// Fenêtre : autoriser le dépannage, le lien à transmettre, l'état, le tchat.
// Rien n'attend ici : l'état est demandé à `packarch-depannage status` et les
// actions à pkexec, dans des processus séparés.
#pragma once

#include "session.hpp"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class StatusIcon;

class Window : public QWidget {
    Q_OBJECT
public:
    explicit Window(QWidget* parent = nullptr);
    /// Ramène la fenêtre devant
    void present();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void pollStatus();
    void pollChat();
    void apply(const session::Status& status);
    void privileged(const QStringList& args, const QString& failure);
    void sendInput();
    void toast(const QString& text);

    session::Status status_;
    QProcess* statusProcess_ = nullptr;
    bool busy_ = false;
    /// Position de lecture dans le fichier du tchat
    qint64 chatPos_ = 0;
    /// Les messages lus sont nouveaux (pas l'historique d'une fenêtre rouverte)
    bool chatLive_ = false;
    /// L'état de la session est connu (le nom de la personne qui dépanne)
    bool statusKnown_ = false;

    StatusIcon* icon_;
    QLabel* title_;
    QLabel* text_;
    QLabel* link_;
    QPushButton* copy_;
    QLabel* details_;
    QPushButton* allow_;
    QPushButton* extend_;
    QPushButton* stop_;
    QWidget* chat_;
    QPlainTextEdit* log_;
    QLineEdit* input_;
    QPushButton* send_;
    QLabel* toast_;
};
