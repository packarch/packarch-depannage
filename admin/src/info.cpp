#include "info.hpp"

using namespace Qt::StringLiterals;

namespace info {

namespace {
constexpr qsizetype MAX_BYTES = 64 * 1024;
constexpr qsizetype MAX_LABEL = 40;
constexpr qsizetype MAX_VALUE = 300;

QString printable(const QString& text, qsizetype max) {
    QString out;
    for (QChar c : text) {
        if (c.category() != QChar::Other_Control && out.size() < max)
            out += c;
    }
    return out.trimmed();
}
}  // namespace

QList<Section> parse(const QByteArray& output) {
    QList<Section> sections;
    const QStringList lines = QString::fromUtf8(output.left(MAX_BYTES)).split(u'\n');
    for (const QString& line : lines) {
        if (line.startsWith("== "_L1) && line.endsWith(" =="_L1)) {
            sections.append({printable(line.mid(3, line.size() - 6), MAX_LABEL), {}});
            continue;
        }
        const qsizetype tab = line.indexOf(u'\t');
        if (tab <= 0)
            continue;
        const Row row{printable(line.left(tab), MAX_LABEL), printable(line.mid(tab + 1), MAX_VALUE)};
        if (row.label.isEmpty() || row.value.isEmpty())
            continue;
        if (sections.isEmpty())
            sections.append({QString(), {}});
        sections.last().rows.append(row);
    }
    return sections;
}

QString toText(const QList<Section>& sections) {
    qsizetype width = 0;
    for (const Section& s : sections) {
        for (const Row& r : s.rows)
            width = qMax(width, r.label.size());
    }
    QString text;
    for (const Section& s : sections) {
        if (!text.isEmpty())
            text += u'\n';
        if (!s.title.isEmpty())
            text += u"== %1 ==\n"_s.arg(s.title);
        for (const Row& r : s.rows)
            text += u"%1 : %2\n"_s.arg(r.label.leftJustified(width), r.value);
    }
    return text;
}

}  // namespace info
