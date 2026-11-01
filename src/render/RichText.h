// Rich text for plot labels: a small LaTeX-like math subset rendered with
// QTextDocument (vector output in SVG/PDF exports).
//
//   $...$        math: ^{sup}, _{sub}, \alpha..\Omega, \pm \times \cdot \to \infty
//                \approx \leq \geq \neq \sim \circ \degree \partial \nabla \hbar \ell
//                \AA \sqrt{x} \frac{a}{b} \mathrm{..} \text{..} \mathbf{..} \bar{x}
//                \, \; \quad spaces; Latin letters are set in italic (TeX style)
//   **bold**  *italic*   outside math;  "\n" = line break;  \$ = literal dollar
//
// Example: "m$_{\gamma\gamma}$ (GeV)", "$\delta$($^{1}$H) / ppm", "**CMS** *Preliminary*"
#pragma once

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <memory>

class QPainter;
class QTextDocument;

namespace gp {

// Converts the markup to Qt rich-text HTML. With enabled = false the text is
// only HTML-escaped (line breaks kept).
QString richTextToHtml(const QString& text, bool enabled = true);

// A laid-out piece of rich text.
class RichTextBlock {
public:
    RichTextBlock();
    ~RichTextBlock();
    RichTextBlock(const RichTextBlock&) = delete;
    RichTextBlock& operator=(const RichTextBlock&) = delete;

    void set(const QString& text, const QFont& font, bool math = true,
             Qt::Alignment align = Qt::AlignLeft);
    // Sets ready-made Qt rich-text HTML (e.g. coloured value readouts).
    void setHtml(const QString& html, const QFont& font);
    bool isEmpty() const { return m_text.isEmpty(); }
    QSizeF size() const;
    // Draws with the top-left corner at `topLeft`.
    void draw(QPainter* p, const QPointF& topLeft, const QColor& color) const;
    // Draws rotated by -90° (reading bottom-to-top) centred at `centre`.
    void drawVertical(QPainter* p, const QPointF& centre, const QColor& color) const;

private:
    std::unique_ptr<QTextDocument> m_doc;
    QString m_text;
};

}  // namespace gp
