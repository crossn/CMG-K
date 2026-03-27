/*
 * Shared table styling helpers for Kaillera dialogs.
 * Suppresses native Windows accent rendering on selected/focused items.
 */
#pragma once

#include <QItemDelegate>
#include <QPainter>
#include <QProxyStyle>
#include <QStyledItemDelegate>
#include <QTableWidget>

class NoAccentTableStyle final : public QProxyStyle
{
public:
    NoAccentTableStyle(QStyle* style) : QProxyStyle(style) {}

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
        QPainter* painter, const QWidget* widget) const override
    {
        if (element == PE_PanelItemViewItem || element == PE_FrameFocusRect
            || element == PE_PanelItemViewRow)
        {
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void drawControl(ControlElement element, const QStyleOption* option,
        QPainter* painter, const QWidget* widget) const override
    {
        if (element == CE_ItemViewItem)
        {
            QStyleOptionViewItem opt(*qstyleoption_cast<const QStyleOptionViewItem*>(option));
            opt.state &= ~(State_HasFocus | State_Selected | State_MouseOver);
            QProxyStyle::drawControl(element, &opt, painter, widget);
            return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};

class NoAccentDelegate final : public QItemDelegate
{
public:
    using QItemDelegate::QItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        opt.state &= ~(QStyle::State_HasFocus | QStyle::State_Selected | QStyle::State_MouseOver);

        if (option.state & QStyle::State_Selected)
        {
            painter->fillRect(option.rect, QColor(100, 149, 237, 60));
        }
        else if (option.state & QStyle::State_MouseOver)
        {
            painter->fillRect(option.rect, QColor(100, 149, 237, 30));
        }

        QItemDelegate::paint(painter, opt, index);
    }
};

inline void applyNoAccentStyle(QTableWidget* table)
{
    table->setStyle(new NoAccentTableStyle(table->style()));
    table->viewport()->setStyle(table->style());
    table->setItemDelegate(new NoAccentDelegate(table));
    table->setFocusPolicy(Qt::NoFocus);
}
