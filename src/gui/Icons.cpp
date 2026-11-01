#include "Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace gp::icons {

namespace {

const QColor kInk(0x33, 0x33, 0x33);
const QColor kAccent(0x2a, 0x78, 0xd6);
const QColor kRed(0xe3, 0x49, 0x48);

void draw(QPainter& p, Kind kind)
{
    // All drawing happens in a 24x24 logical box.
    QPen ink(kInk, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(ink);
    p.setBrush(Qt::NoBrush);
    switch (kind) {
    case Kind::Zoom:
        p.drawEllipse(QPointF(10, 10), 6, 6);
        p.setPen(QPen(kInk, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(14.5, 14.5), QPointF(20, 20));
        p.setPen(QPen(kInk, 1.4));
        p.drawLine(QPointF(7, 10), QPointF(13, 10));
        p.drawLine(QPointF(10, 7), QPointF(10, 13));
        break;
    case Kind::Pan: {
        // Four-way arrow.
        p.drawLine(QPointF(12, 3), QPointF(12, 21));
        p.drawLine(QPointF(3, 12), QPointF(21, 12));
        auto head = [&](QPointF tip, QPointF a, QPointF b) {
            QPainterPath path(tip);
            path.lineTo(a);
            path.lineTo(b);
            path.closeSubpath();
            p.fillPath(path, kInk);
        };
        head({12, 2}, {9, 6}, {15, 6});
        head({12, 22}, {9, 18}, {15, 18});
        head({2, 12}, {6, 9}, {6, 15});
        head({22, 12}, {18, 9}, {18, 15});
        break;
    }
    case Kind::Region:
        p.fillRect(QRectF(8, 3, 8, 18), QColor(0x2a, 0x78, 0xd6, 70));
        p.setPen(QPen(kAccent, 1.4));
        p.drawLine(QPointF(8, 3), QPointF(8, 21));
        p.drawLine(QPointF(16, 3), QPointF(16, 21));
        p.setPen(QPen(kInk, 1.4));
        p.drawPolyline(QPolygonF({{2, 16}, {6, 14}, {10, 8}, {14, 12}, {18, 6}, {22, 9}}));
        break;
    case Kind::VMarker:
        p.setPen(QPen(kRed, 2, Qt::DashLine));
        p.drawLine(QPointF(12, 2), QPointF(12, 22));
        p.setPen(QPen(kInk, 1.4));
        p.drawLine(QPointF(17, 6), QPointF(21, 6));
        p.drawLine(QPointF(19, 4), QPointF(19, 8));
        break;
    case Kind::HMarker:
        p.setPen(QPen(kRed, 2, Qt::DashLine));
        p.drawLine(QPointF(2, 12), QPointF(22, 12));
        p.setPen(QPen(kInk, 1.4));
        p.drawLine(QPointF(17, 5), QPointF(21, 5));
        p.drawLine(QPointF(19, 3), QPointF(19, 7));
        break;
    case Kind::Annotation: {
        QFont f;
        f.setPixelSize(17);
        f.setBold(true);
        f.setFamily("serif");
        p.setFont(f);
        p.drawText(QRectF(2, 1, 18, 20), Qt::AlignCenter, "T");
        p.setPen(QPen(kAccent, 1.4));
        p.drawLine(QPointF(15, 20), QPointF(22, 20));
        break;
    }
    case Kind::Autoscale:
        p.drawRect(QRectF(5, 5, 14, 14));
        p.setPen(QPen(kAccent, 1.6, Qt::SolidLine, Qt::RoundCap));
        for (auto [a, b] : {std::pair{QPointF(2, 2), QPointF(7, 7)},
                            {QPointF(22, 2), QPointF(17, 7)},
                            {QPointF(2, 22), QPointF(7, 17)},
                            {QPointF(22, 22), QPointF(17, 17)}})
            p.drawLine(a, b);
        break;
    case Kind::ResetView: {
        QPainterPath house;
        house.moveTo(4, 12);
        house.lineTo(12, 4);
        house.lineTo(20, 12);
        p.drawPath(house);
        p.drawRect(QRectF(7, 12, 10, 8));
        break;
    }
    case Kind::Undo:
    case Kind::Redo: {
        if (kind == Kind::Redo) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        QPainterPath arc;
        arc.moveTo(7, 9);
        arc.cubicTo(QPointF(14, 4), QPointF(22, 8), QPointF(19, 17));
        p.drawPath(arc);
        QPainterPath head(QPointF(4, 9));
        head.lineTo(9, 5);
        head.lineTo(9, 13);
        head.closeSubpath();
        p.fillPath(head, kInk);
        break;
    }
    case Kind::Export:
        p.drawRect(QRectF(4, 10, 16, 10));
        p.drawLine(QPointF(12, 3), QPointF(12, 15));
        p.drawLine(QPointF(8, 7), QPointF(12, 3));
        p.drawLine(QPointF(16, 7), QPointF(12, 3));
        break;
    }
}

}  // namespace

QIcon make(Kind kind)
{
    QIcon icon;
    for (int scale : {1, 2}) {
        QPixmap pm(24 * scale, 24 * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        draw(p, kind);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

}  // namespace gp::icons
