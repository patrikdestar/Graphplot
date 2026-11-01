#include "DataLoader.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <limits>

namespace gp {

QChar detectSeparator(const QString& line)
{
    for (QChar c : {QChar('\t'), QChar(';'), QChar(',')})
        if (line.contains(c)) return c;
    return ' ';
}

QStringList splitFields(const QString& line, QChar sep)
{
    static const QRegularExpression whitespace(R"(\s+)");
    if (sep == ' ') return line.split(whitespace, Qt::SkipEmptyParts);
    QStringList parts = line.split(sep);
    for (QString& p : parts) {
        p = p.trimmed();
        if (p.size() >= 2 && p.startsWith('"') && p.endsWith('"')) p = p.mid(1, p.size() - 2);
    }
    return parts;
}

double parseCell(const QString& cell, bool* ok)
{
    *ok = true;
    if (cell.isEmpty() || cell.compare("nan", Qt::CaseInsensitive) == 0)
        return std::numeric_limits<double>::quiet_NaN();
    return cell.toDouble(ok);
}

LoadResult loadColumnFile(const QString& fileName)
{
    LoadResult result;
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = QString("Cannot open '%1': %2").arg(fileName, f.errorString());
        return result;
    }
    QTextStream in(&f);
    QChar sep;  // detected from the first non-comment line
    QStringList header;
    std::vector<QVector<double>> columns;
    int lineNo = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        ++lineNo;
        if (line.isEmpty() || line.startsWith('#') || line.startsWith('%')) continue;
        if (sep.isNull()) sep = detectSeparator(line);
        const QStringList parts = splitFields(line, sep);
        QVector<double> values;
        values.reserve(parts.size());
        bool numeric = true;
        for (const QString& p : parts) {
            bool ok = false;
            values.push_back(parseCell(p, &ok));
            if (!ok) {
                numeric = false;
                break;
            }
        }
        if (!numeric) {
            if (columns.empty() && header.isEmpty()) {  // the first text line is the header
                header = parts;
                continue;
            }
            result.error = QString("Line %1 is not numeric").arg(lineNo);
            return result;
        }
        if (columns.empty()) columns.resize(values.size());
        if (values.size() != qsizetype(columns.size())) {
            result.error = QString("Line %1 has %2 columns, expected %3")
                               .arg(lineNo)
                               .arg(values.size())
                               .arg(columns.size());
            return result;
        }
        for (size_t c = 0; c < columns.size(); ++c) columns[c].push_back(values[qsizetype(c)]);
    }
    if (columns.empty()) {
        result.error = "The file contains no numeric data";
        return result;
    }
    const QString base = QFileInfo(fileName).completeBaseName();
    if (columns.size() == 1) {  // single column: plot against index
        Dataset d;
        d.name = header.value(0, base);
        d.y = columns[0];
        d.x.resize(d.y.size());
        for (qsizetype i = 0; i < d.x.size(); ++i) d.x[i] = double(i);
        result.datasets.push_back(std::move(d));
        return result;
    }
    for (size_t c = 1; c < columns.size(); ++c) {
        Dataset d;
        d.name = header.value(int(c), columns.size() == 2 ? base : QString("%1 [%2]").arg(base).arg(c));
        d.x = columns[0];
        d.y = columns[c];
        result.datasets.push_back(std::move(d));
    }
    return result;
}

MatrixLoadResult loadMatrixFile(const QString& fileName)
{
    MatrixLoadResult result;
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = QString("Cannot open '%1': %2").arg(fileName, f.errorString());
        return result;
    }
    static const QRegularExpression sep(R"([,;\t ]+)");
    static const QRegularExpression extentRe(R"(^[#%]\s*extent\s*:?\s*(.*)$)",
                                             QRegularExpression::CaseInsensitiveOption);
    QTextStream in(&f);
    Matrix& m = result.matrix;
    bool haveExtent = false;
    int lineNo = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        ++lineNo;
        if (line.isEmpty()) continue;
        if (line.startsWith('#') || line.startsWith('%')) {
            const auto match = extentRe.match(line);
            if (match.hasMatch()) {
                const QStringList p = match.captured(1).split(sep, Qt::SkipEmptyParts);
                if (p.size() == 4) {
                    m.x0 = p[0].toDouble();
                    m.x1 = p[1].toDouble();
                    m.y0 = p[2].toDouble();
                    m.y1 = p[3].toDouble();
                    haveExtent = true;
                }
            }
            continue;
        }
        const QStringList parts = line.split(sep, Qt::SkipEmptyParts);
        if (m.nx == 0) m.nx = int(parts.size());
        if (parts.size() != m.nx) {
            result.error = QString("Line %1 has %2 values, expected %3").arg(lineNo).arg(parts.size()).arg(m.nx);
            return result;
        }
        for (const QString& p : parts) {
            bool ok = false;
            const double v = p.toDouble(&ok);
            if (!ok && p.toLower() != "nan") {
                result.error = QString("Line %1 is not numeric").arg(lineNo);
                return result;
            }
            m.z.push_back(ok ? v : std::numeric_limits<double>::quiet_NaN());
        }
        ++m.ny;
    }
    if (m.nx < 2 || m.ny < 2) {
        result.error = "A 2D grid needs at least 2 rows and 2 columns";
        return result;
    }
    if (!haveExtent) {
        m.x0 = 0;
        m.x1 = m.nx - 1;
        m.y0 = 0;
        m.y1 = m.ny - 1;
    }
    m.name = QFileInfo(fileName).completeBaseName();
    return result;
}

}  // namespace gp
