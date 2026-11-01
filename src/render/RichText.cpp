#include "RichText.h"

#include <QAbstractTextDocumentLayout>
#include <QHash>
#include <QPainter>
#include <QTextDocument>
#include <QTextOption>

namespace gp {

namespace {

const QHash<QString, QString>& symbols()
{
    static const QHash<QString, QString> s = {
        // lower-case Greek
        {"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"}, {"epsilon", "ϵ"},
        {"varepsilon", "ε"}, {"zeta", "ζ"}, {"eta", "η"}, {"theta", "θ"}, {"vartheta", "ϑ"},
        {"iota", "ι"}, {"kappa", "κ"}, {"lambda", "λ"}, {"mu", "μ"}, {"nu", "ν"}, {"xi", "ξ"},
        {"pi", "π"}, {"rho", "ρ"}, {"sigma", "σ"}, {"tau", "τ"}, {"upsilon", "υ"}, {"phi", "ϕ"},
        {"varphi", "φ"}, {"chi", "χ"}, {"psi", "ψ"}, {"omega", "ω"},
        // upper-case Greek
        {"Gamma", "Γ"}, {"Delta", "Δ"}, {"Theta", "Θ"}, {"Lambda", "Λ"}, {"Xi", "Ξ"},
        {"Pi", "Π"}, {"Sigma", "Σ"}, {"Upsilon", "Υ"}, {"Phi", "Φ"}, {"Psi", "Ψ"}, {"Omega", "Ω"},
        // operators / symbols
        {"pm", "±"}, {"mp", "∓"}, {"times", "×"}, {"cdot", "·"}, {"div", "÷"}, {"to", "→"},
        {"rightarrow", "→"}, {"leftarrow", "←"}, {"leftrightarrow", "↔"}, {"Rightarrow", "⇒"},
        {"infty", "∞"}, {"approx", "≈"}, {"simeq", "≃"}, {"sim", "∼"}, {"propto", "∝"},
        {"leq", "≤"}, {"le", "≤"}, {"geq", "≥"}, {"ge", "≥"}, {"neq", "≠"}, {"ne", "≠"},
        {"ll", "≪"}, {"gg", "≫"}, {"equiv", "≡"}, {"circ", "∘"}, {"degree", "°"},
        {"partial", "∂"}, {"nabla", "∇"}, {"hbar", "ħ"}, {"ell", "ℓ"}, {"AA", "Å"},
        {"sum", "∑"}, {"prod", "∏"}, {"int", "∫"}, {"langle", "⟨"}, {"rangle", "⟩"},
        {"perp", "⊥"}, {"parallel", "∥"}, {"in", "∈"}, {"cdots", "⋯"}, {"ldots", "…"},
        {"prime", "′"}, {"star", "⋆"}, {"dagger", "†"},
        // spacing
        {",", " "}, {";", " "}, {":", " "}, {"quad", " "}, {" ", " "},
        {"qquad", "  "}, {"!", ""},
        // escaped characters
        {"%", "%"}, {"$", "$"}, {"{", "{"}, {"}", "}"}, {"_", "_"}, {"&", "&amp;"}, {"#", "#"},
    };
    return s;
}

QString esc(const QString& s)
{
    QString o = s.toHtmlEscaped();
    return o;
}

// Recursive-descent converter for the math subset.
class MathParser {
public:
    explicit MathParser(const QString& s) : m_s(s) {}
    QString parse(bool italic = true) { return parseUntil(QChar(), italic); }

private:
    QString parseUntil(QChar end, bool italic)
    {
        QString out;
        while (m_i < m_s.size()) {
            const QChar c = m_s[m_i];
            if (!end.isNull() && c == end) {
                ++m_i;
                return out;
            }
            out += parseAtom(italic, true);
        }
        return out;
    }

    // One atom: a group, a command, ^/_ scripts, a letter run or a char.
    QString parseAtom(bool italic, bool allowScripts)
    {
        const QChar c = m_s[m_i];
        if (allowScripts && (c == '^' || c == '_')) {
            ++m_i;
            if (m_i >= m_s.size()) return {};
            const QString arg = parseArg(italic);
            return c == '^' ? "<sup>" + arg + "</sup>" : "<sub>" + arg + "</sub>";
        }
        if (c == '{') {
            ++m_i;
            return parseUntil('}', italic);
        }
        if (c == '\\') return parseCommand(italic);
        if (c.isLetter() && c.unicode() < 128) {
            QString run;
            while (m_i < m_s.size() && m_s[m_i].isLetter() && m_s[m_i].unicode() < 128) run += m_s[m_i++];
            return italic ? "<i>" + esc(run) + "</i>" : esc(run);
        }
        if (c == '-') {  // real minus sign in math
            ++m_i;
            return "−";
        }
        if (c == '=' || c == '<' || c == '>') {  // relations: thin spaces around
            ++m_i;
            return QString(QChar(0x2009)) + esc(QString(c)) + QChar(0x2009);
        }
        ++m_i;
        return esc(QString(c));
    }

    // Script/command argument: a {group} or a single atom (no nested scripts).
    QString parseArg(bool italic)
    {
        if (m_i >= m_s.size()) return {};
        if (m_s[m_i] == '{') {
            ++m_i;
            return parseUntil('}', italic);
        }
        if (m_s[m_i] == '\\') return parseCommand(italic);
        const QChar c = m_s[m_i++];
        if (c == '-') return "−";
        if (c.isLetter() && c.unicode() < 128) return italic ? "<i>" + esc(QString(c)) + "</i>" : esc(QString(c));
        return esc(QString(c));
    }

    QString parseCommand(bool italic)
    {
        ++m_i;  // backslash
        if (m_i >= m_s.size()) return "\\";
        QString name;
        if (m_s[m_i].isLetter()) {
            while (m_i < m_s.size() && m_s[m_i].isLetter()) name += m_s[m_i++];
        } else {
            name = m_s[m_i++];
        }
        if (name == "mathrm" || name == "text" || name == "rm" || name == "operatorname")
            return parseArg(false);
        if (name == "mathit") return "<i>" + parseArg(false) + "</i>";
        if (name == "mathbf" || name == "textbf" || name == "bf") return "<b>" + parseArg(italic) + "</b>";
        if (name == "frac") {
            const QString a = parseArg(italic), b = parseArg(italic);
            return a + "/" + b;
        }
        if (name == "sqrt") return "√<span style=\"text-decoration: overline\">" + parseArg(italic) + "</span>";
        if (name == "bar" || name == "overline")
            return "<span style=\"text-decoration: overline\">" + parseArg(italic) + "</span>";
        if (name == "hat") return parseArg(italic) + "̂";
        if (name == "tilde") return parseArg(italic) + "̃";
        if (name == "vec") return parseArg(italic) + "⃗";
        if (name == "left" || name == "right" || name == "big" || name == "Big") return {};
        const auto& sym = symbols();
        if (auto it = sym.find(name); it != sym.end()) {
            // Relations and arrows get thin spaces on both sides, as in TeX.
            static const QStringList rel = {"to", "rightarrow", "leftarrow", "leftrightarrow",
                                            "Rightarrow", "approx", "simeq", "sim", "propto",
                                            "leq", "le", "geq", "ge", "neq", "ne", "ll", "gg",
                                            "equiv", "in", "pm", "mp", "times", "cdot"};
            return rel.contains(name) ? "\u2009" + *it + "\u2009" : *it;
        }
        return esc(name);  // unknown command: show its name
    }

    const QString& m_s;
    qsizetype m_i = 0;
};

}  // namespace

QString richTextToHtml(const QString& text, bool enabled)
{
    if (!enabled) return esc(text).replace('\n', "<br>");
    QString out;
    QString plain;
    bool bold = false, italic = false;
    auto flushPlain = [&] {
        out += esc(plain);
        plain.clear();
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == '\\' && i + 1 < text.size() && (text[i + 1] == '$' || text[i + 1] == '*')) {
            plain += text[++i];
            continue;
        }
        if (c == '$') {
            const qsizetype end = text.indexOf('$', i + 1);
            if (end < 0) {
                plain += c;
                continue;
            }
            flushPlain();
            out += MathParser(text.mid(i + 1, end - i - 1)).parse();
            i = end;
            continue;
        }
        if (c == '*') {
            const bool dbl = i + 1 < text.size() && text[i + 1] == '*';
            const QString marker = dbl ? "**" : "*";
            bool& state = dbl ? bold : italic;
            // Open only if a matching closing marker exists later.
            if (state || text.indexOf(marker, i + marker.size()) > 0) {
                flushPlain();
                out += state ? (dbl ? "</b>" : "</i>") : (dbl ? "<b>" : "<i>");
                state = !state;
                i += marker.size() - 1;
                continue;
            }
        }
        if (c == '\n') {
            flushPlain();
            out += "<br>";
            continue;
        }
        plain += c;
    }
    flushPlain();
    if (italic) out += "</i>";
    if (bold) out += "</b>";
    return out;
}

RichTextBlock::RichTextBlock() : m_doc(std::make_unique<QTextDocument>()) {}
RichTextBlock::~RichTextBlock() = default;

void RichTextBlock::set(const QString& text, const QFont& font, bool math, Qt::Alignment align)
{
    m_text = text;
    m_doc->setDocumentMargin(0);
    m_doc->setDefaultFont(font);
    QTextOption opt = m_doc->defaultTextOption();
    opt.setWrapMode(QTextOption::NoWrap);
    opt.setAlignment(align);
    m_doc->setDefaultTextOption(opt);
    m_doc->setHtml(richTextToHtml(text, math));
    m_doc->setTextWidth(-1);
    if (align != Qt::AlignLeft) m_doc->setTextWidth(m_doc->idealWidth());
}

QSizeF RichTextBlock::size() const
{
    if (m_text.isEmpty()) return {};
    return {m_doc->idealWidth(), m_doc->size().height()};
}

void RichTextBlock::draw(QPainter* p, const QPointF& topLeft, const QColor& color) const
{
    if (m_text.isEmpty()) return;
    p->save();
    p->translate(topLeft);
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.palette.setColor(QPalette::Text, color);
    m_doc->documentLayout()->draw(p, ctx);
    p->restore();
}

void RichTextBlock::drawVertical(QPainter* p, const QPointF& centre, const QColor& color) const
{
    if (m_text.isEmpty()) return;
    const QSizeF s = size();
    p->save();
    p->translate(centre);
    p->rotate(-90);
    draw(p, QPointF(-s.width() / 2, -s.height() / 2), color);
    p->restore();
}

}  // namespace gp
