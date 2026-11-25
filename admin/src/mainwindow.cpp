#include "mainwindow.hpp"

#include "chatmsg.hpp"
#include "keys.hpp"
#include "machinepage.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QProcess>
#include <QSplitter>
#include <QStackedWidget>
#include <QSystemTrayIcon>

using namespace Qt::StringLiterals;

namespace {
const QString TITLE = u"Packarch Admin"_s;
/// Icône au repos, et quand un ordinateur est à dépanner
const QString ICON_IDLE = u"preferences-desktop-remote-desktop-symbolic"_s;
const QString ICON_WAITING = u"preferences-desktop-remote-desktop"_s;
/// Action par défaut d'une notification : celle d'un clic sur la notification
const QString ACTION = u"default"_s;
QIcon icon(bool waiting) {
    const QIcon fallback = QIcon::fromTheme(ICON_WAITING, QIcon::fromTheme(u"computer"_s));
    return waiting ? fallback : QIcon::fromTheme(ICON_IDLE, fallback);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), tailnet_(new Tailnet(this)), keys_(new KeyProvisioner(this)) {
    setWindowTitle(u"%1 %2"_s.arg(TITLE, QStringLiteral(APP_VERSION)));
    setWindowIcon(icon(true));
    resize(1280, 760);

    list_ = new QListWidget;
    list_->setMinimumWidth(170);
    pages_ = new QStackedWidget;
    empty_ = new QLabel;
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    empty_->setMargin(40);
    pages_->addWidget(empty_);
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(list_);
    splitter->addWidget(pages_);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({190, 1090});
    setCentralWidget(splitter);
    connect(list_, &QListWidget::currentTextChanged, this, &MainWindow::select);

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_ = new QSystemTrayIcon(icon(false), this);
        trayMenu_ = new QMenu(this);
        tray_->setContextMenu(trayMenu_);
        connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason != QSystemTrayIcon::Trigger)
                return;
            if (isVisible() && isActiveWindow())
                hide();
            else
                showMachine();
        });
        tray_->show();
    }

    connect(tailnet_, &Tailnet::polled, this, &MainWindow::refresh);
    connect(tailnet_, &Tailnet::arrived, this, &MainWindow::announce);
    connect(tailnet_, &Tailnet::gone, this, &MainWindow::forget);
    // À chaque dépannage, une clé de connexion neuve pour l'ordinateur
    connect(tailnet_, &Tailnet::arrived, keys_, &KeyProvisioner::provision);
    connect(keys_, &KeyProvisioner::status, this, [this](const QString& host, const QString& text) {
        keyStatus_.insert(host, text);
        // Aussi dans le journal : la fenêtre peut être réduite
        qInfo().noquote() << u"%1 — %2"_s.arg(host, text);
        if (MachinePage* page = open_.value(host))
            page->setKeyStatus(text);
    });
    refresh();
    tailnet_->start();
}

QString MainWindow::summary() const {
    if (!tailnet_->connected())
        return u"Tailscale n'est pas connecté"_s;
    switch (const qsizetype n = tailnet_->machines().size()) {
    case 0: return u"Aucun ordinateur à dépanner"_s;
    case 1: return u"1 ordinateur à dépanner"_s;
    default: return u"%1 ordinateurs à dépanner"_s.arg(n);
    }
}

void MainWindow::refresh() {
    const QList<Machine>& machines = tailnet_->machines();

    // Liste : les ordinateurs présents, et ceux dont la page est encore ouverte
    QStringList hosts;
    for (const Machine& m : machines)
        hosts << m.host;
    for (auto it = open_.cbegin(); it != open_.cend(); ++it) {
        if (!hosts.contains(it.key()))
            hosts << it.key();
    }
    QStringList shown;
    for (int i = 0; i < list_->count(); ++i)
        shown << list_->item(i)->text();
    if (shown != hosts) {
        const QString current = list_->currentItem() ? list_->currentItem()->text() : QString();
        const QSignalBlocker blocker(list_);
        list_->clear();
        list_->addItems(hosts);
        for (int i = 0; i < list_->count(); ++i)
            list_->item(i)->setIcon(icon(true));
        const int row = hosts.indexOf(current);
        if (row >= 0)
            list_->setCurrentRow(row);
        else
            pages_->setCurrentWidget(empty_);
    }

    // Un seul ordinateur et rien de choisi : sa page, sans clic de plus (pas tant
    // que la fenêtre est réduite : la page ouvre le tchat chez la personne)
    if (isVisible() && !list_->currentItem() && machines.size() == 1 && list_->count() == 1)
        list_->setCurrentRow(0);

    if (!tailnet_->connected()) {
        empty_->setText(u"<h2>Tailscale n'est pas connecté</h2><p>Connectez cet ordinateur à votre réseau Tailscale, "
                        "dans un terminal :<br><code>sudo systemctl enable --now tailscaled &amp;&amp; sudo tailscale up</code></p>"_s);
    } else if (machines.isEmpty()) {
        empty_->setText(u"<h2>Aucun ordinateur à dépanner</h2><p>La personne doit ouvrir « Packarch Dépannage », cliquer "
                        "sur « Autoriser le dépannage » et vous envoyer le lien. Ouvrez ce lien avec votre compte "
                        "Tailscale : son ordinateur apparaîtra ici.</p>"_s);
    } else {
        empty_->setText(u"<h2>%1</h2><p>Choisissez un ordinateur dans la liste.</p>"_s.arg(summary()));
    }

    if (!tray_)
        return;
    tray_->setIcon(icon(!machines.isEmpty()));
    tray_->setToolTip(u"%1 — %2"_s.arg(TITLE, summary()));
    trayMenu_->clear();
    trayMenu_->addAction(summary())->setEnabled(false);
    for (const Machine& m : machines) {
        const QString host = m.host;
        // Le nom vient de l'autre ordinateur : « & » n'y est pas un raccourci
        QString label = tailnet::shownHost(host);
        label.replace(u'&', u"&&"_s);
        connect(trayMenu_->addAction(label), &QAction::triggered, this, [this, host] { showMachine(host); });
    }
    trayMenu_->addSeparator();
    connect(trayMenu_->addAction(u"Ouvrir la fenêtre"_s), &QAction::triggered, this, [this] { showMachine(); });
    connect(trayMenu_->addAction(u"Quitter"_s), &QAction::triggered, this, [this] {
        quitting_ = true;
        qApp->quit();
    });
}

void MainWindow::select(const QString& host) {
    if (host.isEmpty()) {
        pages_->setCurrentWidget(empty_);
        return;
    }
    MachinePage* page = open_.value(host);
    if (!page) {
        const QList<Machine>& machines = tailnet_->machines();
        const auto it = std::ranges::find(machines, host, &Machine::host);
        if (it == machines.cend())
            return;
        page = new MachinePage(*it);
        page->setKeyStatus(keyStatus_.value(
            host, KeyProvisioner::configured() ? QString() : u"Sans clé de connexion : le lien sera demandé à chaque session."_s));
        connect(page, &MachinePage::userMessage, this, &MainWindow::notifyMessage);
        pages_->addWidget(page);
        open_.insert(host, page);
    }
    pages_->setCurrentWidget(page);
}

void MainWindow::forget(const QString& host) {
    // La page reste (ce qui est affiché dans les terminaux, le tchat) ; elle sait
    // seulement que la session est terminée. Elle disparaît avec la fenêtre.
    if (MachinePage* page = open_.value(host))
        page->setGone();
}

void MainWindow::showMachine(const QString& host) {
    if (!host.isEmpty()) {
        const auto items = list_->findItems(host, Qt::MatchExactly);
        if (!items.isEmpty())
            list_->setCurrentItem(items.first());
    }
    show();
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
}

void MainWindow::announce(const Machine& machine) {
    refresh();
    // notify-send attend le clic (ou la fermeture) de la notification, qui reste affichée
    auto* notify = new QProcess(this);
    const QString host = machine.host;
    connect(notify, &QProcess::finished, this, [this, notify, host] {
        if (QString::fromUtf8(notify->readAllStandardOutput()).trimmed() == ACTION)
            showMachine(host);
        notify->deleteLater();
    });
    connect(notify, &QProcess::errorOccurred, notify, [notify](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            notify->deleteLater();
    });
    notify->start(u"notify-send"_s,
                  {u"--app-name=packarch-admin"_s, u"--urgency=critical"_s, u"--icon=%1"_s.arg(ICON_WAITING),
                   u"--action=%1=Dépanner"_s.arg(ACTION), u"--"_s,
                   u"Dépannage : %1"_s.arg(tailnet::shownHost(host)), u"Connecté. Cliquer pour dépanner."_s});
}

void MainWindow::notifyMessage(const QString& host, const QString& text) {
    if (isVisible() && isActiveWindow())
        return;
    QProcess::startDetached(u"notify-send"_s, {u"--app-name=packarch-admin"_s, u"--"_s,
                                               u"Message de %1"_s.arg(tailnet::shownHost(host)), chat::notificationBody(text)});
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Réduite dans la zone de notification : les terminaux et le tchat continuent
    if (tray_ && !quitting_) {
        hide();
        event->ignore();
        return;
    }
    event->accept();
}
