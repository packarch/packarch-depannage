#include "chatview.hpp"

#include <QDateTime>
#include <QScrollBar>

using namespace Qt::StringLiterals;

namespace {
const QString ASSISTANT = u"Claude"_s;

QString color(ChatView::Role role) {
    switch (role) {
    case ChatView::Role::Person: return u"#5fd38d"_s;     // vert : la personne dépannée
    case ChatView::Role::Helper: return u"#62a8ff"_s;     // bleu : la personne qui dépanne
    case ChatView::Role::Assistant: return u"#f0a45d"_s;  // orange : l'assistant
    }
    return u"#cccccc"_s;
}
}  // namespace

ChatView::ChatView(QWidget* parent) : QTextBrowser(parent) {
    setReadOnly(true);
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setLineWrapMode(QTextEdit::WidgetWidth);
    document()->setDocumentMargin(6);
}

ChatView::Role ChatView::roleOf(const ChatMessage& message) {
    if (message.fromUser)
        return Role::Person;
    return message.name == ASSISTANT ? Role::Assistant : Role::Helper;
}

QString ChatView::html(const QString& name, const QString& text, const QString& time, Role role) {
    QString body = text.toHtmlEscaped();
    body.replace(u'\n', u"<br>"_s);
    return u"<span style='color:%1; font-weight:600'>%2:</span> %3"
           "&nbsp;&nbsp;<span style='color:#7d7d7d; font-size:small'>%4</span>"_s
        .arg(color(role), name.toHtmlEscaped(), body, time.toHtmlEscaped());
}

void ChatView::addMessage(const ChatMessage& message, const QString& name) {
    const QString time = QDateTime::fromSecsSinceEpoch(message.time).toString(u"HH:mm"_s);
    // En bas si on y était : un message reçu ne ramène pas en bas celui qui relit plus haut
    QScrollBar* bar = verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;
    append(html(name, message.text, time, roleOf(message)));
    if (atBottom)
        bar->setValue(bar->maximum());
}
