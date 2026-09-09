#ifndef UPDATEBANNERWIDGET_H
#define UPDATEBANNERWIDGET_H

#include <QWidget>

class QLabel;
class ClickableLabel;

class UpdateBannerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit UpdateBannerWidget(QWidget *parent = nullptr);

    void setMessage(const QString &text);

public slots:
    void setActionState(const QString &text, bool enabled);

signals:
    void actionClicked();

private:
    QString actionMarkup(const QString &text, bool enabled) const;

    QLabel *m_messageLabel;
    ClickableLabel *m_actionLabel;
    bool m_actionEnabled = true;
};

#endif // UPDATEBANNERWIDGET_H