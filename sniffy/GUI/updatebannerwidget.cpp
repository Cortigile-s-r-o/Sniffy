#include "updatebannerwidget.h"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>

#include "clickablelabel.h"
#include "../graphics/graphics.h"

UpdateBannerWidget::UpdateBannerWidget(QWidget *parent)
    : QWidget(parent),
      m_messageLabel(new QLabel(this)),
      m_actionLabel(new ClickableLabel(this))
{
    setObjectName(QStringLiteral("updateBannerWidget"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(30);
    setMaximumHeight(30);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(12);
    layout->addWidget(m_messageLabel, 0, Qt::AlignVCenter | Qt::AlignLeft);
    layout->addStretch(1);
    layout->addWidget(m_actionLabel, 0, Qt::AlignVCenter | Qt::AlignRight);

    const QColor warningColor(Graphics::palette().warning);
    QColor backgroundColor = warningColor;
    backgroundColor.setAlpha(55);
    const QColor borderColor = warningColor.darker(115);

    setStyleSheet(QString(
        "QWidget#updateBannerWidget {"
        "background-color: %1;"
        "border: 1px solid %2;"
        "border-left: none;"
        "border-right: none;"
        "}"
        "QLabel { color: %3; font-weight: 600; }"
    ).arg(
        backgroundColor.name(QColor::HexArgb),
        borderColor.name(),
        warningColor.darker(170).name()
    ));

    m_messageLabel->setText(QStringLiteral("New update available"));
    m_actionLabel->setTextFormat(Qt::RichText);
    m_actionLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_actionLabel->setCursor(Qt::PointingHandCursor);
    setActionState(QStringLiteral("Install and relaunch"), true);

    connect(m_actionLabel, &ClickableLabel::clicked, this, [this]() {
        if (m_actionEnabled) {
            emit actionClicked();
        }
    });
}

void UpdateBannerWidget::setMessage(const QString &text)
{
    m_messageLabel->setText(text);
}

void UpdateBannerWidget::setActionState(const QString &text, bool enabled)
{
    m_actionEnabled = enabled;
    m_actionLabel->setText(actionMarkup(text, enabled));
    m_actionLabel->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

QString UpdateBannerWidget::actionMarkup(const QString &text, bool enabled) const
{
    const QColor warningColor(Graphics::palette().warning);
    const QColor actionColor = enabled ? warningColor.darker(200) : QColor(Graphics::palette().textLabel);
    return QStringLiteral("<span style=\"text-decoration: underline; color: %1;\">%2</span>")
        .arg(actionColor.name(), text.toHtmlEscaped());
}