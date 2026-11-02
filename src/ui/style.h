#pragma once

#include <QLabel>
#include <QPalette>
#include <QVBoxLayout>

namespace fanctl {
namespace style {

// The window shares one vertical rhythm. Hard-coding it here keeps sections at
// predictable distances instead of leaving it to whichever Qt style is active.
inline constexpr int kPageMargin = 16;
inline constexpr int kGap = 10;
inline constexpr int kSectionGap = 20;
inline constexpr int kControlGap = 6;

inline void applyPage(QVBoxLayout *layout)
{
    layout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    layout->setSpacing(kGap);
}

// A layout row already contributes kGap, so only the remainder is inserted.
inline void addSectionGap(QVBoxLayout *layout)
{
    layout->addSpacing(kSectionGap - layout->spacing());
}

// For the inside of a QGroupBox: the frame already reserves the title band, so
// only the gutters away from the border are ours to set.
inline void applySection(QVBoxLayout *layout)
{
    layout->setContentsMargins(kGap, 0, kGap, kGap);
    layout->setSpacing(kGap);
}

// Secondary text: explanations that must not compete with the values above them.
inline void applyHint(QLabel *label)
{
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    label->setPalette(pal);
}

inline QLabel *hint(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    applyHint(label);
    return label;
}

}  // namespace style
}  // namespace fanctl
