#include "emojipicker.h"

#include <QAction>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QWidgetAction>

namespace
{
struct EmojiGroup
{
    const char *title;
    const char *emojis[20];
};

const EmojiGroup groups[] = {
    {"表情", {"😀","😂","🤣","😊","😍","🥰","😘","😜","🤔","😤","😭","😱","😴","🙄","😇","🤗","🤩","😏","😶","🫠"}},
    {"手势", {"👍","👎","👏","🙌","🤝","✌️","🤞","💪","👋","🙏","👀","🤙","👈","👉","👆","👇","✊","👊","🤛","🤜"}},
    {"爱心", {"❤️","🧡","💛","💚","💙","💜","🖤","🤍","🤎","💔","❣️","💕","💞","💓","💗","💖","💘","💝","💟","💋"}},
    {"动物", {"🐶","🐱","🐭","🐹","🐰","🦊","🐻","🐼","🐨","🐯","🦁","🐮","🐷","🐸","🐵","🐔","🐧","🐦","🦆","🦅"}},
    {"食物", {"🍎","🍊","🍋","🍌","🍉","🍇","🍓","🍒","🍑","🥭","🍕","🍔","🍟","🌭","🍿","🍰","🍩","🍪","🍫","☕"}},
    {"符号", {"✅","❌","⭕","❓","❗","💯","🔥","⭐","🌟","💫","✨","⚡","💥","🎉","🎊","🏆","📌","💎","🔔","💡"}},
};
}

EmojiPicker::EmojiPicker(QWidget *parent)
    : QObject(parent)
{
    m_menu = new QMenu(parent);
    m_menu->setStyleSheet(
        QStringLiteral("QMenu { background: white; border: 1px solid #e5e7eb; border-radius: 8px; padding: 4px; }"
                        "QMenu::item { padding: 4px 8px; border-radius: 4px; }"
                        "QMenu::item:selected { background: #f3f4f6; }"));

    for (const auto &group : groups) {
        auto *header = new QLabel(QString::fromUtf8(group.title), parent);
        header->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 600; color: #9ca3af; padding: 4px 8px;"));
        auto *headerAction = new QWidgetAction(m_menu);
        headerAction->setDefaultWidget(header);
        headerAction->setEnabled(false);
        m_menu->addAction(headerAction);

        for (int i = 0; group.emojis[i] != nullptr; ++i) {
            const QString emoji = QString::fromUtf8(group.emojis[i]);
            auto *action = m_menu->addAction(emoji);
            action->setIconText(emoji);
            connect(action, &QAction::triggered, this, [this, emoji]() {
                emit emojiSelected(emoji);
            });
        }

        m_menu->addSeparator();
    }
}

void EmojiPicker::showAt(const QPoint &globalPos)
{
    if (m_menu) {
        m_menu->popup(globalPos);
    }
}
