#include "DataLoader.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <limits>

namespace gp {

LoadResult loadColumnFile(const QString& fileName)
{
    LoadResult result;
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = QString("Cannot open '%1': %2").arg(fileName, f.errorString());
        return result;
    }
    static const QRegularExpression sep(R"([,;\t ]+)");
    QTextStream in(&f);
    QStringList header;
    std::vector<QVector<double>> columns;
    int lineNo = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        ++lineNo;
        if (line.isEmpty() || line.startsWith('#') || line.startsWith('%')) continue;
        const QStringList parts = line.split(sep, Qt::SkipEmptyParts);
        QVector<double> values;
        values.reserve(parts.size());
        bool numeric = true;
        for (const QString& p : parts) {
            bool ok = false;
            const double v = p.toDouble(&ok);
            if (!ok) {
                const QString l = p.toLower();
                if (l == "nan") values.push_back(std::numeric_limits<double>::quiet_NaN());
                else { numeric = false; break; }
            } else {
                values.push_back(v);
            }
        }
        if (!numeric) {
            if (columns.empty() && header.isEmpty()) {
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
        for (size_t c = 0; c < columns.size(); ++c) columns[c].push_back(values[c]);
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

}  // namespace gp
