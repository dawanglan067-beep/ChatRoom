#include "emojipicker.h"

#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace
{
struct EmojiCategory
{
    const char *label;
    const char *emojis[30];
};

const EmojiCategory categories[] = {
    {"表情", {"😀","😂","🤣","😊","😍","🥰","😘","😜","🤔","😤","😭","😱","😴","🙄","😇","🤗","🤩","😏","😶","🫠","😬","🥺","😈","💀","👻","🤡","💩","👽","🤖", nullptr}},
    {"手势", {"👍","👎","👏","🙌","🤝","✌️","🤞","🫶","💪","👋","🙏","👀","🫰","🤘","🖐️","✋","🤌","👌","🫵","🤙","👈","👉","👆","👇","☝️","✊","👊","🤛","🤜", nullptr}},
    {"爱心", {"❤️","🧡","💛","💚","💙","💜","🖤","🤍","🤎","💔","❣️","💕","💞","💓","💗","💖","💘","💝","💟","♥️","🫀","❤️‍🔥","❤️‍🩹","💋","👄","🫦","🥰","😍","😘", nullptr}},
    {"动物", {"🐶","🐱","🐭","🐹","🐰","🦊","🐻","🐼","🐨","🐯","🦁","🐮","🐷","🐸","🐵","🐔","🐧","🐦","🦆","🦅","🦉","🦇","🐺","🐗","🐴","🦄","🐝","🐛","🦋", nullptr}},
    {"食物", {"🍎","🍐","🍊","🍋","🍌","🍉","🍇","🍓","🫐","🍒","🍑","🥭","🍍","🥝","🍅","🥑","🍕","🍔","🍟","🌭","🍿","🧁","🍰","🍩","🍪","🍫","🍬","☕","🍵", nullptr}},
    {"活动", {"⚽","🏀","🏈","⚾","🎾","🏐","🏉","🎱","🏓","🏸","🥅","⛳","🎯","🏆","🥇","🥈","🥉","🎮","🎲","🎭","🎨","🎬","🎤","🎧","🎼","🎹","🥁","🎷","🎸", nullptr}},
    {"旅行", {"🚗","🚕","🚌","🚎","🏎️","🚑","🚒","✈️","🚀","🛸","🏠","🏢","🏥","🏫","⛪","🕌","🕍","⛩️","🏰","🗽","🗼","🌉","🌋","🗻","🏕️","🏖️","🌅","🌄","🌠", nullptr}},
    {"符号", {"✅","❌","⭕","❓","❗","💯","🔥","⭐","🌟","💫","✨","⚡","💥","🎉","🎊","🏆","📌","📎","🔗","💰","💎","🔔","🔒","🔓","🔑","⏰","⏳","💡","📱", nullptr}},
};
}

EmojiPicker::EmojiPicker(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setFixedSize(360, 320);
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(2);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_contentWidget = new QWidget();
    m_mainLayout = new QGridLayout(m_contentWidget);
    m_mainLayout->setContentsMargins(4, 4, 4, 4);
    m_mainLayout->setSpacing(2);

    int row = 0;
    for (const auto &cat : categories) {
        auto *header = new QLabel(QString::fromUtf8(cat.label), m_contentWidget);
        header->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 600; color: #6b7280; padding: 4px 0;"));
        m_mainLayout->addWidget(header, row, 0, 1, 10);
        row++;

        int col = 0;
        for (int i = 0; cat.emojis[i] != nullptr; ++i) {
            auto *btn = new QPushButton(QString::fromUtf8(cat.emojis[i]), m_contentWidget);
            btn->setFixedSize(32, 32);
            btn->setStyleSheet(
                QStringLiteral("QPushButton { border: none; font-size: 18px; background: transparent; border-radius: 6px; }"
                                "QPushButton:hover { background: #e5e7eb; }"));
            btn->setCursor(Qt::PointingHandCursor);
            connect(btn, &QPushButton::clicked, this, [this, btn]() {
                emit emojiSelected(btn->text());
            });
            m_mainLayout->addWidget(btn, row, col);
            col++;
            if (col >= 10) {
                col = 0;
                row++;
            }
        }
        if (col > 0) {
            row++;
        }
    }

    m_scrollArea->setWidget(m_contentWidget);
    mainLayout->addWidget(m_scrollArea);
}

void EmojiPicker::focusOutEvent(QFocusEvent *event)
{
    Q_UNUSED(event);
    hide();
}

void EmojiPicker::hideEvent(QHideEvent *event)
{
    Q_UNUSED(event);
}
