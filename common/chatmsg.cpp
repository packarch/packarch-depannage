#include "chatmsg.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace chat {

QString clean(const QString& text) {
    QString out;
    int count = 0;
    for (const char32_t c : text.toUcs4()) {
        if (count == MAX_CHARS)
            break;
        const char32_t kept = c == U'\t' ? U' ' : c;
        if (QChar::category(kept) == QChar::Other_Control && kept != U'\n')
            continue;
        out += QString::fromUcs4(&kept, 1);
        ++count;
    }
    return out.trimmed();
}

std::optional<ChatMessage> parseLine(const QByteArray& line) {
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject())
        return std::nullopt;
    const QJsonObject o = doc.object();
    const QString from = o.value("from"_L1).toString();
    if (from != "user"_L1 && from != "helper"_L1)
        return std::nullopt;
    if (!o.value("time"_L1).isDouble() || !o.value("text"_L1).isString())
        return std::nullopt;
    ChatMessage m{from == "user"_L1, o.value("time"_L1).toInteger(), clean(o.value("text"_L1).toString())};
    if (m.text.isEmpty())
        return std::nullopt;
    return m;
}

QString withEmoji(const QString& text) {
    static const QList<QPair<QString, QString>> table{
        {u":-)"_s, u"🙂"_s}, {u":)"_s, u"🙂"_s},  {u":-D"_s, u"😀"_s}, {u":D"_s, u"😀"_s},
        {u";-)"_s, u"😉"_s}, {u";)"_s, u"😉"_s},  {u":-("_s, u"😕"_s}, {u":("_s, u"😕"_s},
        {u":'("_s, u"😢"_s}, {u":-P"_s, u"😛"_s}, {u":P"_s, u"😛"_s},  {u":-O"_s, u"😮"_s},
        {u":O"_s, u"😮"_s},  {u"<3"_s, u"❤️"_s},  {u"xD"_s, u"😂"_s},  {u"XD"_s, u"😂"_s},
    };
    QStringList lines = text.split(u'\n');
    for (QString& line : lines) {
        QStringList words = line.split(u' ');
        for (QString& word : words) {
            for (const auto& [typed, shown] : table) {
                if (word == typed) {
                    word = shown;
                    break;
                }
            }
        }
        line = words.join(u' ');
    }
    return lines.join(u'\n');
}

QByteArray toLine(const ChatMessage& message) {
    QJsonObject o;
    o.insert("from"_L1, message.fromUser ? u"user"_s : u"helper"_s);
    o.insert("time"_L1, message.time);
    o.insert("text"_L1, message.text);
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

QString notificationBody(const QString& text) {
    constexpr qsizetype BODY_CHARS = 36;
    QString line = text.simplified();
    if (line.size() > BODY_CHARS)
        line = line.left(BODY_CHARS) + u'…';
    return line.toHtmlEscaped();
}

}  // namespace chat
