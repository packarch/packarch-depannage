#include "window.hpp"

#include "chatfile.hpp"
#include "emoji.hpp"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {
const QString MINUTES = u"60"_s;
const QString TITLE = u"Packarch Dépannage"_s;
const QString BIN = u"/usr/bin/packarch-depannage"_s;
constexpr int TOAST_MS = 10000;
/// Largeur de la fenêtre à l'ouverture
constexpr int WIDTH = 440;

/// Le programme en ligne de commande : à côté de celui-ci (mise au point), sinon celui du système
QString commandLine() {
    const QString beside = QCoreApplication::applicationDirPath() + u"/packarch-depannage"_s;
    return QFile::exists(beside) ? beside : BIN;
}

QString localTime(qint64 secs) {
    return QDateTime::fromSecsSinceEpoch(secs).toString(u"HH 'h' mm"_s);
}

/// Raison de l'échec : la dernière ligne d'erreur du programme, ou le code de
/// sortie de pkexec
QString failureReason(int code, const QByteArray& stderrText) {
    const QStringList lines = QString::fromUtf8(stderrText).split(u'\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        QString line = it->trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith("packarch-depannage : "_L1))
            line.remove(0, 21);
        return line;
    }
    switch (code) {
    case 126: return u"autorisation refusée ou annulée"_s;
    case 127: return u"authentification impossible (agent polkit absent ?)"_s;
    default: return u"code de sortie %1"_s.arg(code);
    }
}
}  // namespace

Window::Window(QWidget* parent) : QWidget(parent) {
    setWindowTitle(u"%1 %2"_s.arg(TITLE, QStringLiteral(APP_VERSION)));
    const QPixmap logo(u":/packarch-logo.png"_s);
    setWindowIcon(QIcon(logo));

    // En-tête : le logo de Packarch, l'état de la session à côté
    auto* logoLabel = new QLabel;
    logoLabel->setPixmap(logo.scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setFixedSize(56, 56);
    logoLabel->setAlignment(Qt::AlignCenter);
    title_ = new QLabel;
    title_->setTextFormat(Qt::RichText);
    text_ = new QLabel;
    text_->setWordWrap(true);
    text_->setTextFormat(Qt::PlainText);
    auto* headText = new QVBoxLayout;
    headText->setSpacing(2);
    headText->addWidget(title_);
    headText->addWidget(text_);
    auto* head = new QHBoxLayout;
    head->setSpacing(12);
    head->addWidget(logoLabel, 0, Qt::AlignTop);
    head->addLayout(headText, 1);

    link_ = new QLabel;
    link_->setWordWrap(true);
    link_->setAlignment(Qt::AlignCenter);
    link_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    link_->setTextFormat(Qt::PlainText);
    QFont mono = link_->font();
    mono.setFamily(u"monospace"_s);
    link_->setFont(mono);
    copy_ = new QPushButton(u"Copier le lien"_s);
    details_ = new QLabel;
    details_->setAlignment(Qt::AlignCenter);
    details_->setEnabled(false);
    QFont small = details_->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    details_->setFont(small);
    allow_ = new QPushButton(u"Autoriser le dépannage (1 h)"_s);
    allow_->setDefault(true);
    extend_ = new QPushButton(u"+1 h"_s);
    stop_ = new QPushButton(u"Arrêter maintenant"_s);
    stop_->setStyleSheet(u"QPushButton { background: #c01c28; color: white; padding: 6px 14px; border-radius: 6px; }"
                         "QPushButton:disabled { background: #6b3a3e; }"_s);
    for (QPushButton* b : {copy_, allow_, extend_, stop_})
        b->setMinimumHeight(34);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(copy_);
    buttons->addWidget(allow_);
    buttons->addWidget(extend_);
    buttons->addWidget(stop_);
    buttons->addStretch();

    // Tchat, une fois l'ordinateur connecté : la fenêtre s'agrandit alors
    chat_ = new QWidget;
    auto* chatLayout = new QVBoxLayout(chat_);
    chatLayout->setContentsMargins(0, 4, 0, 0);
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMinimumHeight(120);
    input_ = new QLineEdit;
    input_->setPlaceholderText(u"Écrire à la personne qui vous dépanne…"_s);
    input_->setMaxLength(chat::MAX_CHARS);
    send_ = new QPushButton(u"Envoyer"_s);
    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(input_);
    inputRow->addWidget(emoji::button(input_));
    inputRow->addWidget(send_);
    chatLayout->addWidget(log_, 1);
    chatLayout->addLayout(inputRow);

    toast_ = new QLabel;
    toast_->setWordWrap(true);
    toast_->setAlignment(Qt::AlignCenter);
    toast_->setStyleSheet(u"background: #464646; color: white; padding: 5px; border-radius: 4px;"_s);
    toast_->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    layout->addLayout(head);
    layout->addWidget(link_);
    layout->addWidget(details_);
    layout->addLayout(buttons);
    layout->addWidget(chat_, 1);
    layout->addWidget(toast_);

    connect(allow_, &QPushButton::clicked, this,
            [this] { privileged({u"start"_s, MINUTES}, u"Le dépannage n'a pas été autorisé"_s); });
    connect(extend_, &QPushButton::clicked, this,
            [this] { privileged({u"extend"_s, MINUTES}, u"La session n'a pas été prolongée"_s); });
    connect(stop_, &QPushButton::clicked, this,
            [this] { privileged({u"stop"_s}, u"La session n'a pas pu être arrêtée"_s); });
    connect(copy_, &QPushButton::clicked, this, [this] {
        if (status_.authUrl) {
            QApplication::clipboard()->setText(*status_.authUrl);
            toast(u"Lien copié"_s);
        }
    });
    connect(send_, &QPushButton::clicked, this, &Window::sendInput);
    connect(input_, &QLineEdit::returnPressed, this, &Window::sendInput);

    setMinimumWidth(380);
    status_.hostname = session::hostname();
    apply(status_);
    auto* statusTimer = new QTimer(this);
    connect(statusTimer, &QTimer::timeout, this, &Window::pollStatus);
    statusTimer->start(2000);
    pollStatus();
    auto* chatTimer = new QTimer(this);
    connect(chatTimer, &QTimer::timeout, this, &Window::pollChat);
    chatTimer->start(500);
}

void Window::present() {
    show();
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
}

void Window::pollStatus() {
    // La demande précédente n'a pas fini (tailscaled lent) : attendre
    if (statusProcess_)
        return;
    statusProcess_ = new QProcess(this);
    const auto done = [this] {
        const QJsonDocument doc = QJsonDocument::fromJson(statusProcess_->readAllStandardOutput());
        statusProcess_->deleteLater();
        statusProcess_ = nullptr;
        if (doc.isObject())
            apply(session::Status::fromJson(doc.object()));
        // Les premiers messages attendent l'état : ils portent le nom de la personne qui dépanne
        if (!statusKnown_) {
            statusKnown_ = true;
            pollChat();
        }
    };
    connect(statusProcess_, &QProcess::finished, this, done);
    connect(statusProcess_, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done();
    });
    statusProcess_->start(commandLine(), {u"status"_s});
}

void Window::apply(const session::Status& st) {
    status_ = st;
    const session::Description d = session::describe(st);
    title_->setText(u"<span style='font-size:13pt; font-weight:600'>%1</span>"_s.arg(d.title.toHtmlEscaped()));
    text_->setText(d.text);

    const bool waitingLink = st.active && st.authUrl && st.backend != "Running"_L1;
    link_->setVisible(waitingLink);
    copy_->setVisible(waitingLink);
    if (waitingLink)
        link_->setText(*st.authUrl);

    details_->setVisible(st.active);
    if (st.active) {
        QString details = st.dnsName.value_or(st.hostname);
        if (st.end) {
            const qint64 left = (qMax<qint64>(*st.end - session::now(), 0) + 59) / 60;
            details += u" · fin à %1 (dans %2 min)"_s.arg(localTime(*st.end)).arg(left);
        }
        details_->setText(details);
    }
    allow_->setVisible(!st.active);
    allow_->setEnabled(!busy_);
    extend_->setVisible(st.active);
    extend_->setEnabled(!busy_);
    stop_->setVisible(st.active);
    stop_->setEnabled(!busy_);
    const bool chat = st.active && st.backend == "Running"_L1;
    chat_->setVisible(chat);

    // La fenêtre prend la place de ce qu'elle montre : petite à l'ouverture, plus
    // haute avec le lien, puis avec le tchat. Seulement quand ce qu'elle montre
    // change : une taille choisie par l'utilisateur est gardée entre-temps.
    const int shape = (st.active ? 1 : 0) | (waitingLink ? 2 : 0) | (chat ? 4 : 0);
    if (shape != shape_) {
        shape_ = shape;
        QTimer::singleShot(0, this, [this] {
            // La largeur choisie par l'utilisateur est gardée, la hauteur suit le contenu
            const int w = shapeApplied_ ? width() : WIDTH;
            layout()->activate();
            const int h = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(w) : layout()->totalSizeHint().height();
            resize(w, h);
            shapeApplied_ = true;
        });
    }
}

void Window::pollChat() {
    if (!statusKnown_)
        return;
    if (!QFile::exists(chatfile::CHAT)) {
        if (chatPos_ > 0)
            log_->clear();
        chatPos_ = 0;
        // Fichier neuf : ses messages arriveront en direct
        chatLive_ = true;
    }
    const QString other = status_.helperName.value_or(u"La personne qui vous dépanne"_s);
    for (const ChatMessage& m : chatfile::readNew(chatfile::CHAT, chatPos_)) {
        // Du texte, jamais de balisage
        log_->appendPlainText(u"%1 · %2\n%3\n"_s.arg(m.fromUser ? u"Vous"_s : other, localTime(m.time), m.text));
        if (chatLive_ && !m.fromUser && !(isVisible() && isActiveWindow())) {
            QProcess::startDetached(u"notify-send"_s, {u"--app-name=packarch-depannage"_s, u"--"_s,
                                                       u"Dépannage : message reçu"_s, chat::notificationBody(m.text)});
        }
    }
    chatLive_ = true;
}

void Window::sendInput() {
    const QString text = chat::withEmoji(chat::clean(input_->text()));
    if (text.isEmpty())
        return;
    const QString error = chatfile::appendTo(chatfile::CHAT, true, text);
    if (!error.isEmpty()) {
        toast(u"Message non envoyé : %1"_s.arg(error));
        return;
    }
    input_->clear();
    pollChat();
}

void Window::privileged(const QStringList& args, const QString& failure) {
    busy_ = true;
    apply(status_);
    auto* process = new QProcess(this);
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(QProcess::nullDevice());
    const auto done = [this, process, failure](const QString& reason) {
        busy_ = false;
        if (!reason.isEmpty())
            toast(u"%1 : %2"_s.arg(failure, reason));
        process->deleteLater();
        apply(status_);
        pollStatus();
    };
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && code == 0)
            done({});
        else if (status != QProcess::NormalExit)
            done(u"arrêté par un signal"_s);
        else
            done(failureReason(code, process->readAllStandardError()));
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(process->errorString());
    });
    process->start(u"pkexec"_s, QStringList{BIN} + args);
}

void Window::toast(const QString& text) {
    toast_->setText(text);
    toast_->show();
    QTimer::singleShot(TOAST_MS, toast_, [this, text] {
        if (toast_->text() == text)
            toast_->hide();
    });
}

void Window::closeEvent(QCloseEvent* event) {
    // La session continue sans la fenêtre, qui porte le bouton pour l'arrêter :
    // une fermeture par mégarde ne doit pas la faire oublier
    if (!status_.active) {
        event->accept();
        return;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(TITLE);
    box.setText(u"Fermer cette fenêtre ?"_s);
    QString text = u"Le dépannage continuera : la personne qui vous dépanne gardera l'accès à cet ordinateur."_s;
    if (status_.end)
        text += u"\nIl s'arrêtera tout seul à %1."_s.arg(localTime(*status_.end));
    text += u"\nPour l'arrêter avant, rouvrez « Packarch Dépannage » depuis le menu."_s;
    box.setInformativeText(text);
    QPushButton* keep = box.addButton(u"Laisser la fenêtre ouverte"_s, QMessageBox::RejectRole);
    box.addButton(u"Fermer la fenêtre"_s, QMessageBox::AcceptRole);
    box.setDefaultButton(keep);
    box.exec();
    if (box.clickedButton() == keep)
        event->ignore();
    else
        event->accept();
}
