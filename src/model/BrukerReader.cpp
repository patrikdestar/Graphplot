#include "BrukerReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QtEndian>
#include <cmath>
#include <cstring>

namespace gp {

QMap<QString, QString> readBrukerParameters(const QString& fileName, QString* error)
{
    QMap<QString, QString> params;
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString("Cannot open '%1'").arg(fileName);
        return params;
    }
    static const QRegularExpression re(R"(^##\$?([^=]+)=\s*(.*)$)");
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const auto m = re.match(line);
        if (!m.hasMatch()) continue;
        QString value = m.captured(2).trimmed();
        if (value.startsWith('<') && value.endsWith('>')) value = value.mid(1, value.size() - 2);
        params.insert(m.captured(1).trimmed(), value);
    }
    return params;
}

namespace {

double num(const QMap<QString, QString>& p, const char* key, bool* ok)
{
    bool good = false;
    const double v = p.value(key).toDouble(&good);
    if (!good && ok) *ok = false;
    return v;
}

QString resolvePdataDir(const QString& path)
{
    QFileInfo fi(path);
    if (fi.isFile()) return fi.absolutePath();
    QDir d(path);
    if (QFileInfo::exists(d.filePath("2rr"))) return d.absolutePath();
    if (QFileInfo::exists(d.filePath("pdata/1/2rr"))) return QDir(d.filePath("pdata/1")).absolutePath();
    return d.absolutePath();
}

}  // namespace

BrukerSpectrum readBruker2D(const QString& path)
{
    BrukerSpectrum out;
    const QString dir = resolvePdataDir(path);
    QDir d(dir);
    QString err;
    const auto p2 = readBrukerParameters(d.filePath("procs"), &err);   // F2
    if (p2.isEmpty()) {
        out.error = err.isEmpty() ? "procs file is empty" : err;
        return out;
    }
    const auto p1 = readBrukerParameters(d.filePath("proc2s"), &err);  // F1
    if (p1.isEmpty()) {
        out.error = "proc2s not found - is this a 2D processed data set? (" + err + ")";
        return out;
    }
    bool ok = true;
    // Sizes are bounded before converting to int (a corrupt SI must not overflow).
    const double si2d = num(p2, "SI", &ok), si1d = num(p1, "SI", &ok);
    if (!(si2d >= 2 && si2d <= 1 << 20 && si1d >= 2 && si1d <= 1 << 20)) {
        out.error = "SI out of range in procs / proc2s";
        return out;
    }
    const int si2 = int(si2d), si1 = int(si1d);
    int xd2 = int(num(p2, "XDIM", nullptr)), xd1 = int(num(p1, "XDIM", nullptr));
    const double sw2 = num(p2, "SW_p", &ok), sw1 = num(p1, "SW_p", &ok);
    const double sf2 = num(p2, "SF", &ok), sf1 = num(p1, "SF", &ok);
    const double off2 = num(p2, "OFFSET", &ok), off1 = num(p1, "OFFSET", &ok);
    if (!ok || si1 < 2 || si2 < 2 || sf1 <= 0 || sf2 <= 0) {
        out.error = "Missing or invalid SI / SW_p / SF / OFFSET in procs or proc2s";
        return out;
    }
    if (xd2 <= 0 || si2 % xd2 != 0) xd2 = si2;  // no tiling
    if (xd1 <= 0 || si1 % xd1 != 0) xd1 = si1;
    const bool big = p2.value("BYTORDP").trimmed() == "1";
    const bool isDouble = p2.value("DTYPP").trimmed() == "2";
    const double scale = std::pow(2.0, p2.value("NC_proc").toDouble());

    QFile f(d.filePath("2rr"));
    if (!f.open(QIODevice::ReadOnly)) {
        out.error = QString("Cannot open '%1'").arg(f.fileName());
        return out;
    }
    const qsizetype n = qsizetype(si1) * si2;
    const qsizetype bytes = n * (isDouble ? 8 : 4);
    const QByteArray raw = f.readAll();
    if (raw.size() < bytes) {
        out.error = QString("2rr has %1 bytes, expected %2 (SI %3 x %4)")
                        .arg(raw.size()).arg(bytes).arg(si1).arg(si2);
        return out;
    }

    Matrix& m = out.matrix;
    m.nx = si2;
    m.ny = si1;
    m.z.resize(n);
    const char* src = raw.constData();
    const int tilesX = si2 / xd2;
    qsizetype k = 0;  // running index in file order
    const int tilesY = si1 / xd1;
    for (int ty = 0; ty < tilesY; ++ty)
        for (int tx = 0; tx < tilesX; ++tx)
            for (int r = 0; r < xd1; ++r)
                for (int c = 0; c < xd2; ++c, ++k) {
                    double v;
                    if (isDouble) {
                        quint64 u;
                        std::memcpy(&u, src + k * 8, 8);
                        u = big ? qFromBigEndian(u) : qFromLittleEndian(u);
                        std::memcpy(&v, &u, 8);
                    } else {
                        qint32 i;
                        std::memcpy(&i, src + k * 4, 4);
                        v = double(big ? qFromBigEndian(i) : qFromLittleEndian(i));
                    }
                    const int row = ty * xd1 + r, col = tx * xd2 + c;
                    m.z[qsizetype(row) * si2 + col] = v * scale;
                }

    const double step2 = sw2 / (sf2 * si2), step1 = sw1 / (sf1 * si1);
    m.x0 = off2;
    m.x1 = off2 - (si2 - 1) * step2;
    m.y0 = off1;
    m.y1 = off1 - (si1 - 1) * step1;

    out.nucleusF2 = p2.value("AXNUC");
    out.nucleusF1 = p1.value("AXNUC");
    QFile t(d.filePath("title"));
    if (t.open(QIODevice::ReadOnly | QIODevice::Text))
        out.title = QString::fromUtf8(t.readLine()).trimmed();
    // Name: <experiment folder>/<expno> if available, else the title.
    const QDir expDir(QFileInfo(dir + "/../..").absoluteFilePath());
    m.name = out.title.isEmpty() ? expDir.dirName() + " (" + QFileInfo(dir).fileName() + ")"
                                 : out.title;
    return out;
}

bool writeBruker2D(const QString& pdataDir, const Matrix& m, const QString& nucF2,
                   const QString& nucF1, double sfF2, double sfF1, int xdimF2, int xdimF1,
                   QString* error)
{
    // Validate first: tiles must divide the grid exactly, axes need >= 2 points.
    if (m.nx < 2 || m.ny < 2 || m.z.size() != qsizetype(m.nx) * m.ny || xdimF2 <= 0 || xdimF1 <= 0 ||
        m.nx % xdimF2 != 0 || m.ny % xdimF1 != 0 || !(sfF2 > 0) || !(sfF1 > 0)) {
        if (error) *error = "invalid grid, submatrix size or spectrometer frequency";
        return false;
    }
    QDir().mkpath(pdataDir);
    QDir d(pdataDir);
    // Store as int32 with a scale such that max |z| ~ 2^28.
    double maxAbs = 0;
    for (double v : m.z)
        if (std::isfinite(v)) maxAbs = std::max(maxAbs, std::abs(v));
    const int nc = maxAbs > 0 ? int(std::floor(std::log2(maxAbs))) - 28 : 0;
    auto writeProcs = [&](const QString& name, int si, int xdim, double sf, double first,
                          double last, const QString& nuc) {
        QFile f(d.filePath(name));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        const double step = (first - last) / (si - 1);
        QTextStream s(&f);
        s << "##TITLE= Parameter file, Graphplot\n##JCAMPDX= 5.0\n"
          << "##$AXNUC= <" << nuc << ">\n"
          << "##$BYTORDP= 0\n##$DTYPP= 0\n"
          << "##$NC_proc= " << nc << "\n"
          << "##$OFFSET= " << QString::number(first, 'g', 17) << "\n"
          << "##$SF= " << QString::number(sf, 'g', 17) << "\n"
          << "##$SI= " << si << "\n"
          << "##$SW_p= " << QString::number(step * sf * si, 'g', 17) << "\n"
          << "##$XDIM= " << xdim << "\n##END=\n";
        return true;
    };
    if (!writeProcs("procs", m.nx, xdimF2, sfF2, m.x0, m.x1, nucF2) ||
        !writeProcs("proc2s", m.ny, xdimF1, sfF1, m.y0, m.y1, nucF1)) {
        if (error) *error = "cannot write parameter files";
        return false;
    }
    QFile f(d.filePath("2rr"));
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = "cannot write 2rr";
        return false;
    }
    const double scale = std::pow(2.0, -nc);
    QByteArray raw;
    raw.resize(qsizetype(m.nx) * m.ny * 4);
    qsizetype k = 0;
    for (int ty = 0; ty < m.ny / xdimF1; ++ty)
        for (int tx = 0; tx < m.nx / xdimF2; ++tx)
            for (int r = 0; r < xdimF1; ++r)
                for (int c = 0; c < xdimF2; ++c, ++k) {
                    double v = m.z[qsizetype(ty * xdimF1 + r) * m.nx + tx * xdimF2 + c];
                    if (!std::isfinite(v)) v = 0;  // int32 has no NaN
                    const qint32 i = qToLittleEndian(qint32(std::lround(v * scale)));
                    std::memcpy(raw.data() + k * 4, &i, 4);
                }
    if (f.write(raw) != raw.size()) {
        if (error) *error = "cannot write 2rr (disk full?)";
        return false;
    }
    return true;
}

}  // namespace gp
