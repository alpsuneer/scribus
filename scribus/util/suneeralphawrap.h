#ifndef SUNEERALPHAWRAP_H
#define SUNEERALPHAWRAP_H

#include <QImage>
#include <QPolygonF>
#include "fpointarray.h"

class SuneerAlphaWrap
{
public:
    // Generate contour from PNG alpha channel using OpenCV
    static FPointArray createContour(
        const QString& imagePath,
        double itemWidth,
        double itemHeight,
        double imageXScale,
        double imageYScale,
        double imageXOffset,
        double imageYOffset,
        double epsilon = 0.006
    );
};

#endif
