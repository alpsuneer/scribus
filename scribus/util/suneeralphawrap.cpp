#include "suneeralphawrap.h"
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>

FPointArray SuneerAlphaWrap::createContour(
    const QString& imagePath,
    double itemWidth,
    double itemHeight,
    double imageXScale,
    double imageYScale,
    double imageXOffset,
    double imageYOffset,
    double epsilon)
{
    FPointArray result;
    result.resize(0);

    QString ptFile = QDir::tempPath() + "/scribus_alphawrap.txt";

    QString script = QString(
        "import cv2, numpy as np, sys\n"
        "img = cv2.imread(r'%1', cv2.IMREAD_UNCHANGED)\n"
        "if img is None: sys.exit(1)\n"
        "h0, w0 = img.shape[:2]\n"
        "if img.shape[2] >= 4:\n"
        "    alpha = img[:,:,3]\n"
        "else:\n"
        "    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)\n"
        "    _, alpha = cv2.threshold(gray, 240, 255, cv2.THRESH_BINARY_INV)\n"
        "maxS = 600\n"
        "sc = min(1.0, maxS / max(h0, w0))\n"
        "sw, sh = int(w0*sc), int(h0*sc)\n"
        "small = cv2.resize(alpha, (sw, sh), interpolation=cv2.INTER_AREA)\n"
        "_, mask = cv2.threshold(small, 10, 255, cv2.THRESH_BINARY)\n"
        "k5 = np.ones((5,5), np.uint8)\n"
        "k3 = np.ones((3,3), np.uint8)\n"
        "mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, k5)\n"
        "mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, k3)\n"
        "cs, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)\n"
        "if not cs: sys.exit(1)\n"
        "cnt = max(cs, key=cv2.contourArea)\n"
        "eps = %2 * cv2.arcLength(cnt, True)\n"
        "approx = cv2.approxPolyDP(cnt, eps, True)\n"
        "with open(r'%3','w') as f:\n"
        "    f.write(f'{w0} {h0}\\n')\n"
        "    for p in approx:\n"
        "        x = float(p[0][0]) / sc\n"
        "        y = float(p[0][1]) / sc\n"
        "        f.write(f'{x:.2f} {y:.2f}\\n')\n"
    ).arg(imagePath).arg(epsilon).arg(ptFile);

    QProcess proc;
    proc.start("python3", QStringList() << "-c" << script);
    proc.waitForFinished(30000);
    if (proc.exitCode() != 0) {
        qDebug() << "AlphaWrap error:" << proc.readAllStandardError();
        return result;
    }

    QFile f(ptFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    QTextStream in(&f);
    int imgW = 1, imgH = 1;
    in >> imgW >> imgH;

    QVector<FPoint> pts;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        QStringList parts = line.split(' ');
        if (parts.size() < 2) continue;
        double x = parts[0].toDouble() * imageXScale + imageXOffset;
        double y = parts[1].toDouble() * imageYScale + imageYOffset;
        pts.append(FPoint(x, y));
    }
    f.close();

    if (pts.size() < 3) return result;

    // Build FPointArray with sharp corners
    for (const FPoint& p : pts)
        result.addQuadPoint(p.x(), p.y(), p.x(), p.y(), p.x(), p.y(), p.x(), p.y());
    result.addQuadPoint(pts[0].x(), pts[0].y(), pts[0].x(), pts[0].y(),
                        pts[0].x(), pts[0].y(), pts[0].x(), pts[0].y());

    return result;
}
