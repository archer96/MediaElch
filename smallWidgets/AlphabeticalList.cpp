#include "AlphabeticalList.h"

#include <QDebug>
#include <QPainter>
#include <QStyleOption>
#include <QToolButton>

AlphabeticalList::AlphabeticalList(QWidget *parent, MyTableView *parentTableView) :
    QWidget(parent),
    m_layout{new QVBoxLayout(this)},
    m_bottomSpace{10},
    m_topSpace{10},
    m_rightSpace{10},
    m_leftSpace{10},
    m_animDuration{100},
    m_tableView{parentTableView}
{
    m_layout->setMargin(0);
    setContentsMargins(0, 0, 0, 0);
    QString style =
        "QWidget { background-color: rgba(0, 0, 0, 80); border: 1px solid rgba(0, 0, 0, 20); border-radius: 7px; }";
    style.append(
        "QToolButton { background-color: transparent; border: none; color: rgb(255, 255, 255); font-weight: bold; } ");
    setStyleSheet(style);
    setFixedWidth(20);
}

void AlphabeticalList::adjustSize()
{
    int parentHeight = static_cast<QWidget *>(parent())->size().height();
    move(-width(), m_topSpace);
    setFixedHeight(parentHeight - m_topSpace - m_bottomSpace);
}

void AlphabeticalList::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void AlphabeticalList::show()
{
    if (m_outAnim) {
        m_outAnim->stop();
    }

    if (pos().x() == m_leftSpace) {
        return;
    }

    int duration = m_animDuration;
    if (width() + m_leftSpace != 0) {
        duration *= 1 - ((pos().x() + width()) / (width() + m_leftSpace));
    }

    m_inAnim = new QPropertyAnimation(this, "pos");
    m_inAnim->setDuration(duration);
    m_inAnim->setStartValue(QPoint(pos().x(), m_topSpace));
    m_inAnim->setEndValue(QPoint(m_leftSpace, m_topSpace));
    m_inAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void AlphabeticalList::hide()
{
    if (m_inAnim) {
        m_inAnim->stop();
    }

    if (pos().x() == -width()) {
        return;
    }

    int duration = m_animDuration;
    if (width() + m_leftSpace != 0) {
        duration *= (pos().x() + width()) / (width() + m_leftSpace);
    }

    m_outAnim = new QPropertyAnimation(this, "pos");
    m_outAnim->setDuration(duration);
    m_outAnim->setStartValue(QPoint(pos().x(), m_topSpace));
    m_outAnim->setEndValue(QPoint(-width(), m_topSpace));
    m_outAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void AlphabeticalList::setTopSpace(const int space)
{
    m_topSpace = space;
}

void AlphabeticalList::setBottomSpace(const int space)
{
    m_bottomSpace = space;
}

void AlphabeticalList::setRightSpace(const int space)
{
    m_rightSpace = space;
}

void AlphabeticalList::setAlphas(QStringList alphas)
{
    foreach (QToolButton *button, findChildren<QToolButton *>()) {
        m_layout->removeWidget(button);
        button->deleteLater();
    }

    foreach (const QString &alpha, alphas) {
        auto button = new QToolButton(this);
        button->setText(alpha);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        m_layout->addWidget(button);
        connect(button, &QAbstractButton::clicked, this, &AlphabeticalList::onAlphaClicked);
    }
}

void AlphabeticalList::onAlphaClicked()
{
    auto button = static_cast<QToolButton *>(sender());
    if (!button) {
        return;
    }
    emit sigAlphaClicked(button->text());
}

int AlphabeticalList::leftSpace() const
{
    return m_leftSpace;
}
