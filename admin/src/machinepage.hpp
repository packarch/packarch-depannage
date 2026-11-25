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
class QTreeWidget;
class QProcess;

class MachinePage : public QWidget {
    Q_OBJECT
public:
    explicit MachinePage(Machine machine, QWidget* parent = nullptr);
    ~MachinePage() override;
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
    void openScreen();
    void addTerminal(QWidget* term, const QString& title);
    void append(const ChatMessage& message);
    void sendInput();
    void setBanner(const QString& text);
    void setSendable(bool sendable);
    /// Autorise ou non l'assistant à lire le tchat et à y écrire (fichier témoin)
    void allowAssistant(bool allowed);
    void fetchInfo();
    void showInfoMessage(const QString& text);

    Machine machine_;
    ChatSession* chat_;
    QTabWidget* tabs_;
    QLabel* banner_;
    QLabel* keyStatus_;
    /// Infos de l'ordinateur (système, matériel…)
    QTreeWidget* info_;
    QPushButton* refreshInfo_;
    QPushButton* copyInfo_;
    QProcess* infoProcess_ = nullptr;
    QString infoText_;
    QPlainTextEdit* log_;
    QLineEdit* input_;
    QPushButton* send_;
    int terminals_ = 0;
    /// Heure du dernier message affiché (rappelée quand elle change)
    QString chatTime_;
    /// Onglet de l'écran (un seul)
    QWidget* screen_ = nullptr;
    bool gone_ = false;
    /// L'historique est arrivé : les messages suivants sont nouveaux
    bool live_ = false;
};
