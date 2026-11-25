#include "terminal.hpp"

#include <QDir>
#include <QFontDatabase>
#include <QShortcut>
#include <QStandardPaths>
#include <qtermwidget.h>

using namespace Qt::StringLiterals;

namespace terminal {

QString findProgram(const QString& name) {
    const QString found = QStandardPaths::findExecutable(name);
    if (!found.isEmpty())
        return found;
    return QStandardPaths::findExecutable(name, {QDir::homePath() + u"/.local/bin"_s});
}

QWidget* create(const QString& program, const QStringList& args, QWidget* parent) {
    // 0 : le programme est lancé plus bas, une fois réglé
    auto* term = new QTermWidget(0, parent);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    term->setTerminalFont(font);
    term->setColorScheme(u"Linux"_s);
    term->setScrollBarPosition(QTermWidget::ScrollBarRight);
    term->setTerminalSizeHint(false);
    term->setWorkingDirectory(QDir::homePath());
    term->setShellProgram(program);
    term->setArgs(args);
    // Copier et coller comme dans les autres terminaux (Ctrl+C reste l'interruption)
    auto* copy = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), term);
    copy->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(copy, &QShortcut::activated, term, &QTermWidget::copyClipboard);
    auto* paste = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), term);
    paste->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(paste, &QShortcut::activated, term, &QTermWidget::pasteClipboard);
    term->startShellProgram();
    return term;
}

}  // namespace terminal
