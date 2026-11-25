#include "machinepage.hpp"

#include "terminal.hpp"

#include "emoji.hpp"
#include "info.hpp"
#include "screenview.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <qtermwidget.h>

using namespace Qt::StringLiterals;

MachinePage::MachinePage(Machine machine, QWidget* parent)
    : QWidget(parent), machine_(std::move(machine)), chat_(new ChatSession(machine_, this)) {
    const QString name = tailnet::shownHost(machine_.host);

    // Terminaux : un premier onglet d'accueil, qui ne se ferme pas
    tabs_ = new QTabWidget;
    tabs_->setTabsClosable(true);
    tabs_->setDocumentMode(true);
    auto* home = new QWidget;
    auto* homeLayout = new QVBoxLayout(home);
    homeLayout->setContentsMargins(16, 12, 16, 12);
    homeLayout->setSpacing(8);
    auto* title = new QLabel(u"<h2>%1</h2>"_s.arg(name.toHtmlEscaped()));
    title->setAlignment(Qt::AlignCenter);
    auto* address = new QLabel(machine_.ip.toHtmlEscaped());
    address->setAlignment(Qt::AlignCenter);
    address->setEnabled(false);
    auto* root = new QPushButton(u"Terminal (root)"_s);
    auto* claude = new QPushButton(u"Dépanner avec Claude Code"_s);
    auto* screen = new QPushButton(u"Écran"_s);
    for (QPushButton* b : {root, claude, screen})
        b->setMinimumSize(190, 38);
    screen->setEnabled(tailnet::validIp(machine_.ip));
    // Jamais avec un nom inattendu : il irait dans la demande à Claude
    const bool claudeReady = tailnet::validHost(machine_.host) && !terminal::findProgram(u"claude"_s).isEmpty();
    claude->setEnabled(claudeReady);
    if (!claudeReady)
        claude->setToolTip(u"Claude Code n'est pas installé, ou le nom de cet ordinateur est inattendu."_s);
    root->setEnabled(tailnet::validIp(machine_.ip));
    auto* actions = new QHBoxLayout;
    actions->addStretch();
    actions->addWidget(root);
    actions->addWidget(claude);
    actions->addWidget(screen);
    actions->addStretch();
    keyStatus_ = new QLabel;
    keyStatus_->setAlignment(Qt::AlignCenter);
    keyStatus_->setWordWrap(true);
    keyStatus_->setTextFormat(Qt::PlainText);
    keyStatus_->setEnabled(false);

    // Infos de l'ordinateur : lues à l'ouverture de la page
    info_ = new QTreeWidget;
    info_->setColumnCount(2);
    info_->setHeaderHidden(true);
    info_->setRootIsDecorated(false);
    info_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    info_->setAlternatingRowColors(true);
    info_->setTextElideMode(Qt::ElideRight);
    info_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    info_->header()->setStretchLastSection(true);
    refreshInfo_ = new QPushButton(u"Actualiser"_s);
    copyInfo_ = new QPushButton(u"Copier"_s);
    auto* infoBar = new QHBoxLayout;
    infoBar->addWidget(new QLabel(u"<b>Infos de l'ordinateur</b>"_s));
    infoBar->addStretch();
    infoBar->addWidget(copyInfo_);
    infoBar->addWidget(refreshInfo_);
    connect(refreshInfo_, &QPushButton::clicked, this, &MachinePage::fetchInfo);
    connect(copyInfo_, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(infoText_); });

    homeLayout->addWidget(title);
    homeLayout->addWidget(address);
    homeLayout->addLayout(actions);
    homeLayout->addWidget(keyStatus_);
    homeLayout->addSpacing(6);
    homeLayout->addLayout(infoBar);
    homeLayout->addWidget(info_, 1);
    tabs_->addTab(home, u"Ordinateur"_s);
    tabs_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    tabs_->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    connect(root, &QPushButton::clicked, this, &MachinePage::openRootTerminal);
    connect(claude, &QPushButton::clicked, this, &MachinePage::openClaude);
    connect(screen, &QPushButton::clicked, this, &MachinePage::openScreen);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (index > 0)
            tabs_->widget(index)->deleteLater();
    });

    // Tchat
    auto* chatPanel = new QWidget;
    auto* chatLayout = new QVBoxLayout(chatPanel);
    chatLayout->setContentsMargins(6, 0, 0, 0);
    auto* chatTitle = new QLabel(u"<b>Tchat avec la personne</b>"_s);
    banner_ = new QLabel;
    banner_->setWordWrap(true);
    banner_->setAlignment(Qt::AlignCenter);
    banner_->setStyleSheet(u"background: #464646; color: white; padding: 6px; border-radius: 4px;"_s);
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    input_ = new QLineEdit;
    input_->setPlaceholderText(u"Écrire à la personne dépannée…"_s);
    input_->setMaxLength(chat::MAX_CHARS);
    send_ = new QPushButton(u"Envoyer"_s);
    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(input_);
    inputRow->addWidget(emoji::button(input_));
    inputRow->addWidget(send_);
    chatLayout->addWidget(chatTitle);
    // Sans cette case, l'assistant ne lit pas le tchat et n'y écrit pas
    auto* assistant = new QCheckBox(u"Claude peut lire le tchat et y écrire"_s);
    assistant->setToolTip(u"Décochée : Claude Code ne lit pas le tchat et n'écrit pas à la personne.\n"
                          "Cochée : il suit ses messages et lui répond, sous le nom « Claude »."_s);
    assistant->setEnabled(!chat::assistantFlag(machine_.host).isEmpty());
    connect(assistant, &QCheckBox::toggled, this, &MachinePage::allowAssistant);
    chatLayout->addWidget(banner_);
    chatLayout->addWidget(assistant);
    chatLayout->addWidget(log_, 1);
    chatLayout->addLayout(inputRow);
    connect(send_, &QPushButton::clicked, this, &MachinePage::sendInput);
    connect(input_, &QLineEdit::returnPressed, this, &MachinePage::sendInput);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(tabs_);
    splitter->addWidget(chatPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({760, 340});
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    setBanner(u"Connexion à l'ordinateur…"_s);
    setSendable(false);
    fetchInfo();
    connect(chat_, &ChatSession::connected, this, [this] {
        setBanner({});
        setSendable(true);
        // L'historique arrive d'un coup à la connexion : ce qui suit est nouveau
        if (!live_)
            QTimer::singleShot(1500, this, [this] { live_ = true; });
    });
    connect(chat_, &ChatSession::message, this, [this](const ChatMessage& m) {
        append(m);
        if (live_ && m.fromUser)
            emit userMessage(machine_.host, m.text);
    });
    connect(chat_, &ChatSession::lost, this, [this](const QString& error) {
        setBanner(u"Connexion perdue, nouvel essai… %1"_s.arg(error));
        setSendable(false);
    });
    connect(chat_, &ChatSession::finished, this, [this](const QString& text) {
        setBanner(text);
        setSendable(false);
    });
    connect(chat_, &ChatSession::notSent, this, [this](const QString& text, const QString& reason) {
        if (input_->text().isEmpty())
            input_->setText(text);
        setBanner(u"Message non envoyé : %1"_s.arg(reason));
    });
}

void MachinePage::setGone() {
    gone_ = true;
    chat_->setGone();
}

void MachinePage::showInfoMessage(const QString& text) {
    info_->clear();
    auto* item = new QTreeWidgetItem(info_, {text});
    item->setFirstColumnSpanned(true);
    item->setDisabled(true);
}

void MachinePage::fetchInfo() {
    if (infoProcess_ || !tailnet::validIp(machine_.ip))
        return;
    QFile script(u":/infos.sh"_s);
    if (!script.open(QIODevice::ReadOnly))
        return;
    const QByteArray commands = script.readAll();
    refreshInfo_->setEnabled(false);
    copyInfo_->setEnabled(false);
    showInfoMessage(u"Lecture des infos de l'ordinateur…"_s);
    // Le script passe par l'entrée standard de ssh : rien n'est installé ni écrit là-bas
    infoProcess_ = new QProcess(this);
    QStringList args = chat::sshOptions(true);
    args << u"root@%1"_s.arg(machine_.ip) << u"sh -s"_s;
    connect(infoProcess_, &QProcess::started, infoProcess_, [this, commands] {
        infoProcess_->write(commands);
        infoProcess_->closeWriteChannel();
    });
    const auto done = [this](bool started) {
        const auto sections = info::parse(infoProcess_->readAllStandardOutput());
        infoProcess_->deleteLater();
        infoProcess_ = nullptr;
        refreshInfo_->setEnabled(true);
        if (sections.isEmpty()) {
            showInfoMessage(started ? u"Infos illisibles (connexion à l'ordinateur perdue ?)."_s
                                    : u"ssh n'a pas pu être lancé."_s);
            return;
        }
        infoText_ = info::toText(sections);
        copyInfo_->setEnabled(true);
        info_->clear();
        QFont bold = info_->font();
        bold.setBold(true);
        for (const info::Section& section : sections) {
            auto* head = new QTreeWidgetItem(info_, {section.title});
            head->setFirstColumnSpanned(true);
            head->setFont(0, bold);
            head->setFlags(Qt::ItemIsEnabled);
            for (const info::Row& row : section.rows) {
                // Du texte, jamais de balisage ; la valeur entière en infobulle si elle est coupée
                auto* item = new QTreeWidgetItem(info_, {u"  "_s + row.label, row.value});
                item->setToolTip(1, row.value.toHtmlEscaped());
            }
        }
    };
    connect(infoProcess_, &QProcess::finished, this, [done] { done(true); });
    connect(infoProcess_, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(false);
    });
    infoProcess_->setStandardErrorFile(QProcess::nullDevice());
    infoProcess_->start(u"ssh"_s, args);
}

void MachinePage::setKeyStatus(const QString& text) {
    keyStatus_->setText(text);
}

void MachinePage::addTerminal(QWidget* term, const QString& title) {
    const int index = tabs_->addTab(term, title);
    tabs_->setCurrentIndex(index);
    term->setFocus();
    // Programme terminé (exit, connexion coupée) : l'onglet le dit et garde ce qui est affiché
    connect(qobject_cast<QTermWidget*>(term), &QTermWidget::finished, this, [this, term, title] {
        const int at = tabs_->indexOf(term);
        if (at >= 0)
            tabs_->setTabText(at, u"%1 (terminé)"_s.arg(title));
    });
}

void MachinePage::openRootTerminal() {
    QStringList args = chat::sshOptions(false);
    args << u"root@%1"_s.arg(machine_.ip);
    addTerminal(terminal::create(u"ssh"_s, args, tabs_), u"root %1"_s.arg(++terminals_));
}

void MachinePage::openScreen() {
    if (!screen_) {
        screen_ = new ScreenView(machine_, tabs_);
        tabs_->addTab(screen_, u"Écran"_s);
        connect(screen_, &QObject::destroyed, this, [this] { screen_ = nullptr; });
    }
    tabs_->setCurrentWidget(screen_);
}

void MachinePage::openClaude() {
    const QString claude = terminal::findProgram(u"claude"_s);
    if (claude.isEmpty() || !tailnet::validHost(machine_.host))
        return;
    addTerminal(terminal::create(claude, {u"Ouvre un dépannage sur %1"_s.arg(machine_.host)}, tabs_), u"Claude Code"_s);
}

void MachinePage::append(const ChatMessage& m) {
    // Chaque message commence par le nom de celui qui l'écrit : celui qu'il porte
    // (« Claude », le nom de session de la personne), sinon le nôtre ou celui de l'ordinateur
    const QString name = !m.name.isEmpty() ? m.name
                       : m.fromUser ? tailnet::shownHost(machine_.host)
                       : chat::helperName();
    if (const QString time = QDateTime::fromSecsSinceEpoch(m.time).toString(u"HH 'h' mm"_s); time != chatTime_) {
        chatTime_ = time;
        log_->appendPlainText(u"— %1 —"_s.arg(time));
    }
    // Du texte, jamais de balisage
    log_->appendPlainText(chat::shown(name, m.text));
}

void MachinePage::allowAssistant(bool allowed) {
    const QString flag = chat::assistantFlag(machine_.host);
    if (flag.isEmpty())
        return;
    if (allowed) {
        QDir().mkpath(QFileInfo(flag).path());
        QFile f(flag);
        if (f.open(QIODevice::WriteOnly))
            f.write(machine_.ip.toUtf8() + '\n');
    } else {
        QFile::remove(flag);
    }
}

MachinePage::~MachinePage() {
    // La page fermée, l'autorisation ne vaut plus
    allowAssistant(false);
}

void MachinePage::sendInput() {
    const QString text = chat::withEmoji(chat::clean(input_->text()));
    if (text.isEmpty() || !send_->isEnabled())
        return;
    input_->clear();
    setBanner({});
    chat_->send(text);
}

void MachinePage::setBanner(const QString& text) {
    banner_->setText(text);
    banner_->setVisible(!text.isEmpty());
}

void MachinePage::setSendable(bool sendable) {
    input_->setEnabled(sendable);
    send_->setEnabled(sendable);
}
