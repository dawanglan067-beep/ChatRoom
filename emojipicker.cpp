#include "emojipicker.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
struct EmojiEntry
{
    const char *emoji;
    const char *name;
};

struct EmojiCategory
{
    const char *title;
    const char *icon;
    EmojiEntry emojis[24];
};

const EmojiCategory categories[] = {
    {"表情", "😀", {
        {"😀", "微笑"}, {"😂", "笑哭"}, {"🤣", "打滚"}, {"😊", "开心"},
        {"😍", "花痴"}, {"🥰", "可爱"}, {"😘", "亲亲"}, {"😜", "调皮"},
        {"🤔", "思考"}, {"😤", "生气"}, {"😭", "大哭"}, {"😱", "惊恐"},
        {"😴", "睡觉"}, {"🙄", "白眼"}, {"😇", "天使"}, {"🤗", "拥抱"},
        {"🤩", "崇拜"}, {"😏", "得意"}, {"😶", "沉默"}, {"🫠", "融化"},
        {"😎", "酷"}, {"🥳", "庆祝"}, {"😮", "惊讶"}, {"😢", "难过"}
    }},
    {"手势", "👍", {
        {"👍", "点赞"}, {"👎", "踩"}, {"👏", "鼓掌"}, {"🙌", "举手"},
        {"🤝", "握手"}, {"✌️", "胜利"}, {"🤞", "交叉手指"}, {"💪", "强壮"},
        {"👋", "挥手"}, {"🙏", "祈祷"}, {"👀", "看"}, {"🤙", "打电话"},
        {"👈", "左指"}, {"👉", "右指"}, {"👆", "上指"}, {"👇", "下指"},
        {"✊", "拳头"}, {"👊", "出拳"}, {"🤛", "左拳"}, {"🤜", "右拳"},
        {"✋", "停"}, {"🖐️", "手掌"}, {"👋", "再见"}, {"🤌", "捏"}
    }},
    {"爱心", "❤️", {
        {"❤️", "红心"}, {"🧡", "橙心"}, {"💛", "黄心"}, {"💚", "绿心"},
        {"💙", "蓝心"}, {"💜", "紫心"}, {"🖤", "黑心"}, {"🤍", "白心"},
        {"🤎", "棕心"}, {"💔", "碎心"}, {"❣️", "感叹心"}, {"💕", "两心"},
        {"💞", "旋转心"}, {"💓", "心跳"}, {"💗", "增长心"}, {"💖", "闪心"},
        {"💘", "箭心"}, {"💝", "礼盒心"}, {"💟", "装饰心"}, {"💋", "嘴唇"},
        {"🫶", "双手心"}, {"🥰", "爱慕"}, {"😘", "飞吻"}, {"💑", "情侣"}
    }},
    {"动物", "🐶", {
        {"🐶", "狗"}, {"🐱", "猫"}, {"🐭", "鼠"}, {"🐹", "仓鼠"},
        {"🐰", "兔"}, {"🦊", "狐"}, {"🐻", "熊"}, {"🐼", "熊猫"},
        {"🐨", "考拉"}, {"🐯", "虎"}, {"🦁", "狮"}, {"🐮", "牛"},
        {"🐷", "猪"}, {"🐸", "蛙"}, {"🐵", "猴"}, {"🐔", "鸡"},
        {"🐧", "企鹅"}, {"🐦", "鸟"}, {"🦆", "鸭"}, {"🦅", "鹰"},
        {"🦋", "蝴蝶"}, {"🐛", "虫"}, {"🐝", "蜜蜂"}, {"🐞", "瓢虫"}
    }},
    {"食物", "🍎", {
        {"🍎", "苹果"}, {"橙子", "橙子"}, {"🍋", "柠檬"}, {"🍌", "香蕉"},
        {"西瓜", "西瓜"}, {"🍇", "葡萄"}, {"草莓", "草莓"}, {"🍒", "樱桃"},
        {"🍑", "桃子"}, {"🥭", "芒果"}, {"披萨", "披萨"}, {"🍔", "汉堡"},
        {"薯条", "薯条"}, {"🌭", "热狗"}, {"🍿", "爆米花"}, {"🍰", "蛋糕"},
        {"🍩", "甜甜圈"}, {"🍪", "饼干"}, {"🍫", "巧克力"}, {"☕", "咖啡"},
        {"🧋", "奶茶"}, {"🍺", "啤酒"}, {"🥤", "饮料"}, {"🍜", "面条"}
    }},
    {"符号", "✅", {
        {"✅", "对勾"}, {"❌", "叉"}, {"⭕", "圆圈"}, {"❓", "问号"},
        {"❗", "感叹号"}, {"💯", "满分"}, {"🔥", "火"}, {"⭐", "星星"},
        {"🌟", "闪星"}, {"💫", "晕星"}, {"✨", "闪光"}, {"⚡", "闪电"},
        {"💥", "爆炸"}, {"🎉", "派对"}, {"🎊", "彩球"}, {"🏆", "奖杯"},
        {"📌", "图钉"}, {"💎", "钻石"}, {"🔔", "铃铛"}, {"💡", "灯泡"},
        {"🎯", "靶心"}, {"🚀", "火箭"}, {"💪", "加油"}, {"👍", "棒"}
    }},
};

constexpr int kGridColumns = 8;
constexpr int kButtonSize = 36;
}

EmojiPicker::EmojiPicker(QWidget *parent)
    : QObject(parent)
{
    setupUi();
}

void EmojiPicker::setupUi()
{
    m_widget = new QWidget(parentWidget(), Qt::Popup | Qt::FramelessWindowHint);
    m_widget->setFixedSize(340, 380);
    m_widget->setStyleSheet(
        QStringLiteral("QWidget { background: white; border: 1px solid #e5e7eb; border-radius: 12px; }"));

    auto *mainLayout = new QVBoxLayout(m_widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // Search input
    m_searchInput = new QLineEdit(m_widget);
    m_searchInput->setPlaceholderText(QStringLiteral("搜索表情..."));
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->setStyleSheet(
        QStringLiteral("QLineEdit { border: 1px solid #e5e7eb; border-radius: 8px; padding: 6px 10px; "
                        "font-size: 13px; background: #f9fafb; }"
                        "QLineEdit:focus { border-color: #60a5fa; background: white; }"));
    mainLayout->addWidget(m_searchInput);

    // Category tabs
    auto *tabLayout = new QHBoxLayout();
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(4);

    for (int i = 0; i < static_cast<int>(sizeof(categories) / sizeof(categories[0])); ++i) {
        auto *tabButton = new QPushButton(QString::fromUtf8(categories[i].icon), m_widget);
        tabButton->setFixedSize(36, 36);
        tabButton->setToolTip(QString::fromUtf8(categories[i].title));
        tabButton->setCursor(Qt::PointingHandCursor);
        tabButton->setStyleSheet(
            QStringLiteral("QPushButton { border: none; border-radius: 8px; font-size: 18px; background: transparent; }"
                            "QPushButton:hover { background: #f3f4f6; }"
                            "QPushButton:checked { background: #dbeafe; }"));
        tabButton->setCheckable(true);
        connect(tabButton, &QPushButton::clicked, this, [this, i]() { showCategory(i); });
        tabLayout->addWidget(tabButton);
    }
    tabLayout->addStretch();
    mainLayout->addLayout(tabLayout);

    // Category label
    m_categoryLabel = new QLabel(m_widget);
    m_categoryLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 600; color: #6b7280; padding: 2px 4px;"));
    mainLayout->addWidget(m_categoryLabel);

    // Emoji grid in scroll area
    auto *scrollArea = new QScrollArea(m_widget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_gridContainer = new QWidget();
    m_gridLayout = new QGridLayout(m_gridContainer);
    m_gridLayout->setContentsMargins(0, 0, 0, 0);
    m_gridLayout->setSpacing(2);
    scrollArea->setWidget(m_gridContainer);
    mainLayout->addWidget(scrollArea, 1);

    // Connect search
    connect(m_searchInput, &QLineEdit::textChanged, this, &EmojiPicker::filterEmojis);

    // Show first category
    showCategory(0);
}

void EmojiPicker::showCategory(int index)
{
    m_currentCategory = index;
    m_searchInput->clear();

    // Update tab buttons
    auto *tabLayout = m_widget->findChild<QHBoxLayout *>();
    if (tabLayout) {
        for (int i = 0; i < tabLayout->count(); ++i) {
            auto *widget = tabLayout->itemAt(i)->widget();
            if (auto *btn = qobject_cast<QPushButton *>(widget)) {
                btn->setChecked(i == index);
            }
        }
    }

    m_categoryLabel->setText(QString::fromUtf8(categories[index].title));

    // Clear grid
    while (m_gridLayout->count() > 0) {
        auto *item = m_gridLayout->takeAt(0);
        delete item->widget();
        delete item;
    }

    // Populate grid
    const auto &cat = categories[index];
    int row = 0, col = 0;
    for (int i = 0; i < 24 && cat.emojis[i].emoji != nullptr; ++i) {
        const QString emoji = QString::fromUtf8(cat.emojis[i].emoji);
        auto *btn = new QPushButton(emoji, m_gridContainer);
        btn->setFixedSize(kButtonSize, kButtonSize);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(QString::fromUtf8(cat.emojis[i].name));
        btn->setStyleSheet(
            QStringLiteral("QPushButton { border: none; border-radius: 6px; font-size: 20px; background: transparent; }"
                            "QPushButton:hover { background: #f3f4f6; }"));
        connect(btn, &QPushButton::clicked, this, [this, emoji]() {
            emit emojiSelected(emoji);
        });
        m_gridLayout->addWidget(btn, row, col);

        col++;
        if (col >= kGridColumns) {
            col = 0;
            row++;
        }
    }
}

void EmojiPicker::filterEmojis(const QString &keyword)
{
    if (keyword.trimmed().isEmpty()) {
        showCategory(m_currentCategory);
        return;
    }

    // Clear grid
    while (m_gridLayout->count() > 0) {
        auto *item = m_gridLayout->takeAt(0);
        delete item->widget();
        delete item;
    }

    m_categoryLabel->setText(QStringLiteral("搜索结果"));

    int row = 0, col = 0, count = 0;
    const QString lowerKeyword = keyword.toLower();

    for (const auto &cat : categories) {
        for (int i = 0; i < 24 && cat.emojis[i].emoji != nullptr; ++i) {
            const QString name = QString::fromUtf8(cat.emojis[i].name);
            if (name.contains(lowerKeyword)) {
                const QString emoji = QString::fromUtf8(cat.emojis[i].emoji);
                auto *btn = new QPushButton(emoji, m_gridContainer);
                btn->setFixedSize(kButtonSize, kButtonSize);
                btn->setCursor(Qt::PointingHandCursor);
                btn->setToolTip(name);
                btn->setStyleSheet(
                    QStringLiteral("QPushButton { border: none; border-radius: 6px; font-size: 20px; background: transparent; }"
                                    "QPushButton:hover { background: #f3f4f6; }"));
                connect(btn, &QPushButton::clicked, this, [this, emoji]() {
                    emit emojiSelected(emoji);
                });
                m_gridLayout->addWidget(btn, row, col);

                col++;
                if (col >= kGridColumns) {
                    col = 0;
                    row++;
                }
                count++;
            }
        }
    }

    if (count == 0) {
        auto *noResult = new QLabel(QStringLiteral("没有找到匹配的表情"), m_gridContainer);
        noResult->setStyleSheet(QStringLiteral("color: #9ca3af; font-size: 13px; padding: 20px;"));
        noResult->setAlignment(Qt::AlignCenter);
        m_gridLayout->addWidget(noResult, 0, 0, 1, kGridColumns);
    }
}

void EmojiPicker::showAt(const QPoint &globalPos)
{
    if (m_widget) {
        m_widget->move(globalPos);
        m_widget->show();
        m_searchInput->setFocus();
    }
}

QWidget *EmojiPicker::parentWidget() const
{
    return qobject_cast<QWidget *>(parent());
}
