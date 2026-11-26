// Fenêtre : la liste des ordinateurs à dépanner et la page de celui qui est
// choisi. Elle se réduit dans la zone de notification : l'icône reste, prévient
// quand un ordinateur arrive, et rouvre la fenêtre.
#pragma once

#include "tailnet.hpp"

#include <QHash>
#include <QMainWindow>

class KeyProvisioner;
class MachinePage;
class QDBusServiceWatcher;
class QLabel;
class QListWidget;
class QMenu;
class QStackedWidget;
class QSystemTrayIcon;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    /// Ramène la fenêtre devant, sur HOST s'il est donné et présent
    void showMachine(const QString& host = {});
    /// Essai : la notification d'arrivée, comme si MACHINE arrivait
    void announce(const Machine& machine);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void refresh();
    /// Crée l'icône de la zone de notification, dès qu'il y a une zone
    void setupTray();
    /// Les pages sont rangées par session (nom et adresse de l'ordinateur)
    static QString sessionKey(const Machine& machine);
    void select(const QString& key);
    /// Ferme une page (terminaux et tchat compris)
    void closePage(const QString& key);
    void notifyMessage(const QString& host, const QString& text);
    QString summary() const;

    Tailnet* tailnet_;
    KeyProvisioner* keys_;
    /// Où en est la clé de connexion de chaque ordinateur
    QHash<QString, QString> keyStatus_;
    QListWidget* list_;
    QStackedWidget* pages_;
    QLabel* empty_;
    QHash<QString, MachinePage*> open_;
    QSystemTrayIcon* tray_ = nullptr;
    QMenu* trayMenu_ = nullptr;
    /// Attend la zone de notification d'une barre lancée après le programme
    QDBusServiceWatcher* trayWatcher_ = nullptr;
    int trayTries_ = 0;
    /// La fermeture quitte vraiment (menu « Quitter »)
    bool quitting_ = false;
};
