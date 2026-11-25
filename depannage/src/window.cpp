#include "window.hpp"

#include "chatfile.hpp"

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
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {
const QString MINUTES = u"60"_s;
const QString TITLE = u"Packarch Dépannage"_s;
const QString BIN = u"/usr/bin/packarch-depannage"_s;
constexpr int TOAST_MS = 10000;

QString localTime(qint64 secs) {
    return QDateTime::fromSecsSinceEpoch(secs).toString(u"HH 'h' mm"_s);
}

/// Icônes dessinées, en lignes dans un carré de côté 1 : la fenêtre ne dépend
/// pas du thème d'icônes
using Icon = QList<QList<QPointF>>;
const Icon MONITOR{{{0.0, 0.1}, {1.0, 0.1}, {1.0, 0.7}, {0.0, 0.7}, {0.0, 0.1}}, {{0.5, 0.7}, {0.5, 0.9}}, {{0.25, 0.9}, {0.75, 0.9}}};
const Icon CHECK{{{0.1, 0.55}, {0.4, 0.85}, {0.9, 0.2}}};
const Icon ARROWS{{{0.1, 0.35}, {0.9, 0.35}, {0.7, 0.15}}, {{0.9, 0.65}, {0.1, 0.65}, {0.3, 0.85}}};
const Icon ENVELOPE{{{0.0, 0.2}, {1.0, 0.2}, {1.0, 0.8}, {0.0, 0.8}, {0.0, 0.2}}, {{0.0, 0.2}, {0.5, 0.55}, {1.0, 0.2}}};
const Icon HOURGLASS{{{0.2, 0.05}, {0.8, 0.05}, {0.2, 0.95}, {0.8, 0.95}, {0.2, 0.05}}};

struct Description {
    const Icon* icon;
    QString title;
    QString text;
};

/// Icône, titre et texte de l'état de la session
Description describe(const session::Status& st) {
    if (!st.active) {
        return {&MONITOR, TITLE,
                u"Autorise une personne de confiance à se connecter à cet ordinateur pour le réparer. Elle aura un "
                "accès complet, pendant une heure seulement : l'accès se coupe tout seul à la fin, ou dès que vous "
                "cliquez sur « Arrêter »."_s};
    }
    if (st.helping()) {
        QString account;
        if (st.helperName && st.helperLogin && *st.helperName != *st.helperLogin)
            account = u"%1 (%2)"_s.arg(*st.helperName, *st.helperLogin);
        else if (st.helperName)
            account = *st.helperName;
        else if (st.helperLogin)
            account = *st.helperLogin;
        const QString host = st.helper ? u"depuis « %1 »"_s.arg(*st.helper) : QString();
        QString who;
        if (!account.isEmpty() && !host.isEmpty())
            who = u" : %1, %2"_s.arg(account, host);
        else if (!account.isEmpty())
            who = u" : %1"_s.arg(account);
        else if (!host.isEmpty())
            who = u" (%1)"_s.arg(host);
        QString when = u"en ce moment"_s;
        if (st.lastActivity) {
            const qint64 minutes = qMax<qint64>(session::now() - *st.lastActivity, 0) / 60;
            if (minutes == 1)
                when = u"il y a 1 min"_s;
            else if (minutes > 1)
                when = u"il y a %1 min"_s.arg(minutes);
        }
        return {&CHECK, u"Dépannage en cours"_s,
                u"La personne qui vous dépanne travaille sur cet ordinateur%1. Dernière activité : %2."_s.arg(who, when)};
    }
    if (st.backend == "Running"_L1)
        return {&ARROWS, u"Prêt pour le dépannage"_s,
                u"L'ordinateur est joignable. La personne qui vous dépanne peut se connecter."_s};
    if (st.authUrl)
        return {&ENVELOPE, u"Envoyez ce lien"_s,
                u"Envoyez ce lien à la personne qui vous dépanne (SMS, e-mail…). Elle doit l'ouvrir pour se connecter."_s};
    return {&HOURGLASS, u"Préparation…"_s, u"Démarrage de la connexion sécurisée."_s};
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

class StatusIcon : public QWidget {
public:
    using QWidget::QWidget;
    void set(const Icon* icon, int size) {
        icon_ = icon;
        setFixedSize(size, size);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (!icon_)
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal margin = 3;
        const qreal side = width() - 2 * margin;
        QColor color = palette().color(QPalette::WindowText);
        color.setAlphaF(0.75F);
        painter.setPen(QPen(color, qMax(side / 14.0, 2.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        for (const QList<QPointF>& line : *icon_) {
            QPolygonF polygon;
            for (const QPointF& p : line)
                polygon << QPointF(margin + p.x() * side, margin + p.y() * side);
            painter.drawPolyline(polygon);
        }
    }

private:
    const Icon* icon_ = nullptr;
};

Window::Window(QWidget* parent) : QWidget(parent) {
    setWindowTitle(u"%1 %2"_s.arg(TITLE, QStringLiteral(APP_VERSION)));
    resize(560, 760);
    setMinimumSize(440, 520);

    icon_ = new StatusIcon(this);
    title_ = new QLabel;
    title_->setAlignment(Qt::AlignCenter);
    text_ = new QLabel;
    text_->setWordWrap(true);
    text_->setAlignment(Qt::AlignCenter);
    link_ = new QLabel;
    link_->setWordWrap(true);
    link_->setAlignment(Qt::AlignCenter);
    link_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    link_->setTextFormat(Qt::PlainText);
    QFont mono = link_->font();
    mono.setFamily(u"monospace"_s);
    mono.setPointSizeF(mono.pointSizeF() * 1.15);
    link_->setFont(mono);
    copy_ = new QPushButton(u"Copier le lien"_s);
    details_ = new QLabel;
    details_->setAlignment(Qt::AlignCenter);
    details_->setEnabled(false);
    allow_ = new QPushButton(u"Autoriser le dépannage (1 h)"_s);
    allow_->setDefault(true);
    extend_ = new QPushButton(u"+1 h"_s);
    stop_ = new QPushButton(u"Arrêter maintenant"_s);
    stop_->setStyleSheet(u"QPushButton { background: #c01c28; color: white; padding: 8px 16px; border-radius: 6px; }"
                         "QPushButton:disabled { background: #6b3a3e; }"_s);
    for (QPushButton* b : {copy_, allow_, extend_, stop_})
        b->setMinimumHeight(38);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(extend_);
    buttons->addWidget(stop_);
    buttons->addStretch();

    // Tchat, une fois l'ordinateur connecté
    chat_ = new QWidget;
    auto* chatLayout = new QVBoxLayout(chat_);
    chatLayout->setContentsMargins(0, 8, 0, 0);
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    input_ = new QLineEdit;
    input_->setPlaceholderText(u"Écrire à la personne qui vous dépanne…"_s);
    input_->setMaxLength(chat::MAX_CHARS);
    send_ = new QPushButton(u"Envoyer"_s);
    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(input_);
    inputRow->addWidget(send_);
    chatLayout->addWidget(log_, 1);
    chatLayout->addLayout(inputRow);

    toast_ = new QLabel;
    toast_->setWordWrap(true);
    toast_->setAlignment(Qt::AlignCenter);
    toast_->setStyleSheet(u"background: #464646; color: white; padding: 6px; border-radius: 4px;"_s);
    toast_->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(8);
    layout->addStretch(1);
    layout->addWidget(icon_, 0, Qt::AlignHCenter);
    layout->addWidget(title_);
    layout->addWidget(text_);
    layout->addWidget(link_);
    layout->addWidget(copy_, 0, Qt::AlignHCenter);
    layout->addWidget(details_);
    layout->addWidget(allow_, 0, Qt::AlignHCenter);
    layout->addLayout(buttons);
    layout->addWidget(chat_, 8);
    layout->addStretch(1);
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
    // Ce programme lui-même : le même, en cours de mise au point comme installé
    statusProcess_->start(QCoreApplication::applicationFilePath(), {u"status"_s});
}

void Window::apply(const session::Status& st) {
    status_ = st;
    const Description d = describe(st);
    icon_->set(d.icon, st.active ? 44 : 76);
    title_->setText(u"<span style='font-size:%1pt; font-weight:600'>%2</span>"_s.arg(st.active ? 15 : 19).arg(d.title.toHtmlEscaped()));
    text_->setTextFormat(Qt::PlainText);
    text_->setText(d.text);

    const bool waitingLink = st.active && st.authUrl && st.backend != "Running"_L1;
    link_->setVisible(waitingLink);
    copy_->setVisible(waitingLink);
    if (waitingLink)
        link_->setText(*st.authUrl);

    details_->setVisible(st.active);
    if (st.active) {
        QString details = u"Nom de l'ordinateur : %1"_s.arg(st.dnsName.value_or(st.hostname));
        if (st.end) {
            const qint64 left = (qMax<qint64>(*st.end - session::now(), 0) + 59) / 60;
            details += u"\nFin automatique à %1 (dans %2 min)."_s.arg(localTime(*st.end)).arg(left);
        }
        details_->setText(details);
    }
    allow_->setVisible(!st.active);
    allow_->setEnabled(!busy_);
    extend_->setVisible(st.active);
    extend_->setEnabled(!busy_);
    stop_->setVisible(st.active);
    stop_->setEnabled(!busy_);
    chat_->setVisible(st.active && st.backend == "Running"_L1);
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
    const QString text = chat::clean(input_->text());
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
