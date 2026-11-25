#include "machinepage.hpp"

#include "terminal.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
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
    homeLayout->setAlignment(Qt::AlignCenter);
    homeLayout->setSpacing(12);
    auto* title = new QLabel(u"<h2>%1</h2>"_s.arg(name.toHtmlEscaped()));
    title->setAlignment(Qt::AlignCenter);
    auto* address = new QLabel(machine_.ip.toHtmlEscaped());
    address->setAlignment(Qt::AlignCenter);
    address->setEnabled(false);
    auto* root = new QPushButton(u"Terminal (root)"_s);
    auto* claude = new QPushButton(u"Dépanner avec Claude Code"_s);
    for (QPushButton* b : {root, claude}) {
        b->setMinimumSize(260, 40);
    }
    // Jamais avec un nom inattendu : il irait dans la demande à Claude
    const bool claudeReady = tailnet::validHost(machine_.host) && !terminal::findProgram(u"claude"_s).isEmpty();
    claude->setEnabled(claudeReady);
    if (!claudeReady)
        claude->setToolTip(u"Claude Code n'est pas installé, ou le nom de cet ordinateur est inattendu."_s);
    root->setEnabled(tailnet::validIp(machine_.ip));
    homeLayout->addWidget(title);
    homeLayout->addWidget(address);
    homeLayout->addSpacing(12);
    homeLayout->addWidget(root, 0, Qt::AlignCenter);
    homeLayout->addWidget(claude, 0, Qt::AlignCenter);
    keyStatus_ = new QLabel;
    keyStatus_->setAlignment(Qt::AlignCenter);
    keyStatus_->setWordWrap(true);
    keyStatus_->setTextFormat(Qt::PlainText);
    keyStatus_->setEnabled(false);
    homeLayout->addSpacing(12);
    homeLayout->addWidget(keyStatus_);
    tabs_->addTab(home, u"Ordinateur"_s);
    tabs_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    tabs_->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    connect(root, &QPushButton::clicked, this, &MachinePage::openRootTerminal);
    connect(claude, &QPushButton::clicked, this, &MachinePage::openClaude);
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
    inputRow->addWidget(send_);
    chatLayout->addWidget(chatTitle);
    chatLayout->addWidget(banner_);
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

void MachinePage::openClaude() {
    const QString claude = terminal::findProgram(u"claude"_s);
    if (claude.isEmpty() || !tailnet::validHost(machine_.host))
        return;
    addTerminal(terminal::create(claude, {u"Ouvre un dépannage sur %1"_s.arg(machine_.host)}, tabs_), u"Claude Code"_s);
}

void MachinePage::append(const ChatMessage& m) {
    const QString who = m.fromUser ? tailnet::shownHost(machine_.host) : u"Vous"_s;
    const QString when = QDateTime::fromSecsSinceEpoch(m.time).toString(u"HH 'h' mm"_s);
    // Du texte, jamais de balisage
    log_->appendPlainText(u"%1 · %2\n%3\n"_s.arg(who, when, m.text));
}

void MachinePage::sendInput() {
    const QString text = chat::clean(input_->text());
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
