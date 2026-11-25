#include "emoji.hpp"

#include <QGridLayout>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QWidgetAction>

using namespace Qt::StringLiterals;

namespace emoji {

QToolButton* button(QLineEdit* target) {
    static const QStringList choices{
        u"🙂"_s, u"😀"_s, u"😂"_s, u"😉"_s, u"😊"_s, u"😍"_s, u"😘"_s, u"😎"_s,
        u"🤔"_s, u"😅"_s, u"😬"_s, u"😕"_s, u"😢"_s, u"😭"_s, u"😡"_s, u"😱"_s,
        u"👍"_s, u"👎"_s, u"👌"_s, u"🙏"_s, u"👏"_s, u"💪"_s, u"👋"_s, u"🤞"_s,
        u"❤️"_s, u"🎉"_s, u"✅"_s, u"❌"_s, u"⚠️"_s, u"❓"_s, u"⏳"_s, u"☕"_s,
    };
    auto* open = new QToolButton;
    open->setText(u"🙂"_s);
    open->setToolTip(u"Émoticônes"_s);
    open->setPopupMode(QToolButton::InstantPopup);
    open->setFocusPolicy(Qt::NoFocus);
    auto* menu = new QMenu(open);
    auto* grid = new QWidget;
    auto* layout = new QGridLayout(grid);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    QFont big = grid->font();
    big.setPointSizeF(big.pointSizeF() * 1.5);
    for (qsizetype i = 0; i < choices.size(); ++i) {
        auto* b = new QToolButton;
        b->setText(choices[i]);
        b->setFont(big);
        b->setAutoRaise(true);
        b->setFixedSize(40, 40);
        layout->addWidget(b, static_cast<int>(i / 8), static_cast<int>(i % 8));
        QObject::connect(b, &QToolButton::clicked, target, [target, menu, choice = choices[i]] {
            target->insert(choice);
            menu->close();
            target->setFocus();
        });
    }
    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(grid);
    menu->addAction(action);
    open->setMenu(menu);
    return open;
}

}  // namespace emoji
