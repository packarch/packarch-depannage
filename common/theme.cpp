#include "theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

using namespace Qt::StringLiterals;

namespace theme {

void apply() {
    QApplication::setStyle(QStyleFactory::create(u"Fusion"_s));
    const QColor window(32, 32, 34);
    const QColor base(24, 24, 26);
    const QColor text(235, 235, 235);
    const QColor muted(130, 130, 130);
    const QColor accent(53, 132, 228);
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(50, 50, 53));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::ToolTipBase, QColor(50, 50, 53));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent);
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, muted);
    QApplication::setPalette(p);
}

}  // namespace theme
