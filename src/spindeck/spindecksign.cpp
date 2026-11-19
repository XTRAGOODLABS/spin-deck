#include "spindeck/spindecksign.h"

#include <QApplication>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <algorithm>

namespace spindeck {

namespace {

QPointer<QLabel> s_pSign;
QPointer<QTimer> s_pTimer;
QPointer<QWidget> s_pLastWindow;

QWidget* findWindow(QWidget* pWindow) {
    if (pWindow) {
        return pWindow->window();
    }
    if (QWidget* pActive = QApplication::activeWindow()) {
        return pActive->window();
    }
    if (s_pLastWindow) {
        return s_pLastWindow;
    }
    // The biggest visible top-level window is the main window.
    QWidget* pBest = nullptr;
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget* pWidget : widgets) {
        if (pWidget->isVisible() &&
                (!pBest || pWidget->width() * pWidget->height() > pBest->width() * pBest->height())) {
            pBest = pWidget;
        }
    }
    return pBest;
}

} // namespace

void showSign(const QString& headline, const QString& detail, SignStyle style, QWidget* pWindow) {
    QWidget* pParent = findWindow(pWindow);
    if (!pParent) {
        return;
    }
    s_pLastWindow = pParent;
    if (!s_pSign || s_pSign->parentWidget() != pParent) {
        delete s_pSign;
        s_pSign = new QLabel(pParent);
        s_pSign->setAlignment(Qt::AlignCenter);
        s_pSign->setAttribute(Qt::WA_TransparentForMouseEvents);
        s_pSign->setTextFormat(Qt::RichText);
    }
    if (!s_pTimer) {
        s_pTimer = new QTimer(qApp);
        s_pTimer->setSingleShot(true);
        QObject::connect(s_pTimer, &QTimer::timeout, qApp, []() {
            if (s_pSign) {
                s_pSign->hide();
            }
        });
    }
    QString text = QStringLiteral(
            "<div style='font-family: Impact, \"Anton\", sans-serif; font-size: 44px; "
            "color: #ffffff;'>%1</div>")
                           .arg(headline.toHtmlEscaped());
    if (!detail.isEmpty()) {
        text += QStringLiteral(
                "<div style='font-size: 17px; color: #ffffff; margin-top: 6px;'>%1</div>")
                        .arg(detail.toHtmlEscaped().replace(
                                QStringLiteral("\n"), QStringLiteral("<br>")));
    }
    s_pSign->setText(text);
    const QString border = style == SignStyle::Good ? QStringLiteral("#3ddc84")
                                                    : QStringLiteral("#ffffff");
    s_pSign->setStyleSheet(QStringLiteral(
            "QLabel { background-color: rgba(0, 0, 0, 235); border: 4px solid %1; "
            "border-radius: 18px; padding: 18px 34px; }")
                                   .arg(border));
    s_pSign->adjustSize();
    const int x = (pParent->width() - s_pSign->width()) / 2;
    const int y = std::max(40, pParent->height() / 5);
    s_pSign->move(std::max(0, x), y);
    s_pSign->raise();
    s_pSign->show();
    if (style == SignStyle::Working) {
        s_pTimer->stop();
    } else {
        s_pTimer->start(2500);
    }
}

void hideSign() {
    if (s_pTimer) {
        s_pTimer->stop();
    }
    if (s_pSign) {
        s_pSign->hide();
    }
}

} // namespace spindeck
