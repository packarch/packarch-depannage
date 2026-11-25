// Page d'un ordinateur à dépanner : des terminaux (root, Claude Code) à gauche,
// le tchat avec la personne à droite.
#pragma once

#include "chat.hpp"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;

class MachinePage : public QWidget {
    Q_OBJECT
public:
    explicit MachinePage(Machine machine, QWidget* parent = nullptr);
    const Machine& machine() const { return machine_; }
    /// L'ordinateur a quitté le réseau
    void setGone();
    bool gone() const { return gone_; }
    /// Où en est la clé de connexion de l'ordinateur (vide : rien à dire)
    void setKeyStatus(const QString& text);

signals:
    /// Message de la personne, reçu en direct (notification si la fenêtre est cachée)
    void userMessage(const QString& host, const QString& text);

private:
    void openRootTerminal();
    void openClaude();
    void addTerminal(QWidget* term, const QString& title);
    void append(const ChatMessage& message);
    void sendInput();
    void setBanner(const QString& text);
    void setSendable(bool sendable);

    Machine machine_;
    ChatSession* chat_;
    QTabWidget* tabs_;
    QLabel* banner_;
    QLabel* keyStatus_;
    QPlainTextEdit* log_;
    QLineEdit* input_;
    QPushButton* send_;
    int terminals_ = 0;
    bool gone_ = false;
    /// L'historique est arrivé : les messages suivants sont nouveaux
    bool live_ = false;
};
