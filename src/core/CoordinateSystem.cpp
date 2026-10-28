#include "CoordinateSystem.h"

#include <cmath>
#include <QLatin1String>

namespace Geo {

// ---- 常量 ----
static const double PI        = 3.14159265358979323846;
static const double AXIS_A    = 6378245.0;                  // 克拉索夫斯基椭球长半轴
static const double AXIS_EE   = 0.00669342162296594323;     // 偏心率平方
static const double X_PI      = PI * 3000.0 / 180.0;

bool LatLon::isValid() const
{
    return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0
           && !(lat == 0.0 && lon == 0.0);   // (0,0) 视为无效定位
}

QString systemName(CoordSystem cs)
{
    switch (cs) {
    case CS_WGS84: return QStringLiteral("WGS-84");
    case CS_GCJ02: return QStringLiteral("GCJ-02");
    case CS_BD09:  return QStringLiteral("BD-09");
    }
    return QStringLiteral("未知");
}

QString systemNote(CoordSystem cs)
{
    switch (cs) {
    case CS_WGS84: return QStringLiteral("GPS 原始坐标 / 国际标准 / Google Earth");
    case CS_GCJ02: return QStringLiteral("火星坐标 / 高德、腾讯地图");
    case CS_BD09:  return QStringLiteral("百度坐标 / 百度地图");
    }
    return QString();
}

CoordSystem systemFromString(const QString &s, CoordSystem def)
{
    const QString v = s.trimmed().toUpper();
    if (v == QStringLiteral("WGS84") || v == QStringLiteral("WGS-84")
            || v == QStringLiteral("WGS_84") || v == QStringLiteral("GPS"))
        return CS_WGS84;
    if (v == QStringLiteral("GCJ02") || v == QStringLiteral("GCJ-02")
            || v == QStringLiteral("GCJ_02") || v == QStringLiteral("AMAP")
            || v == QStringLiteral("MARS") || v == QStringLiteral("火星"))
        return CS_GCJ02;
    if (v == QStringLiteral("BD09") || v == QStringLiteral("BD-09")
            || v == QStringLiteral("BD_09") || v == QStringLiteral("BAIDU")
            || v == QStringLiteral("百度"))
        return CS_BD09;
    return def;
}

bool outOfChina(double lat, double lon)
{
    // 境外不做 GCJ-02 偏移
    if (lon < 72.004 || lon > 137.8347)
        return true;
    if (lat < 0.8293 || lat > 55.8271)
        return true;
    return false;
}

static double transformLat(double x, double y)
{
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y
                 + 0.1 * x * y + 0.2 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * PI) + 20.0 * std::sin(2.0 * x * PI)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(y * PI) + 40.0 * std::sin(y / 3.0 * PI)) * 2.0 / 3.0;
    ret += (160.0 * std::sin(y / 12.0 * PI) + 320.0 * std::sin(y * PI / 30.0)) * 2.0 / 3.0;
    return ret;
}

static double transformLon(double x, double y)
{
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x
                 + 0.1 * x * y + 0.1 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * PI) + 20.0 * std::sin(2.0 * x * PI)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(x * PI) + 40.0 * std::sin(x / 3.0 * PI)) * 2.0 / 3.0;
    ret += (150.0 * std::sin(x / 12.0 * PI) + 300.0 * std::sin(x / 30.0 * PI)) * 2.0 / 3.0;
    return ret;
}

LatLon wgs84ToGcj02(const LatLon &p)
{
    if (outOfChina(p.lat, p.lon))
        return p;

    double dLat = transformLat(p.lon - 105.0, p.lat - 35.0);
    double dLon = transformLon(p.lon - 105.0, p.lat - 35.0);

    const double radLat = p.lat / 180.0 * PI;
    double magic = std::sin(radLat);
    magic = 1 - AXIS_EE * magic * magic;
    const double sqrtMagic = std::sqrt(magic);

    dLat = (dLat * 180.0) / ((AXIS_A * (1 - AXIS_EE)) / (magic * sqrtMagic) * PI);
    dLon = (dLon * 180.0) / (AXIS_A / sqrtMagic * std::cos(radLat) * PI);

    return LatLon(p.lat + dLat, p.lon + dLon);
}

LatLon gcj02ToWgs84(const LatLon &p)
{
    // 官方偏移算法不可逆，用不动点迭代反解：
    // wgs <- wgs - (wgs84ToGcj02(wgs) - gcj)，收敛极快
    if (outOfChina(p.lat, p.lon))
        return p;

    LatLon wgs = p;
    for (int i = 0; i < 12; ++i) {
        const LatLon tmp = wgs84ToGcj02(wgs);
        const double dLat = tmp.lat - p.lat;
        const double dLon = tmp.lon - p.lon;
        if (std::fabs(dLat) < 1e-10 && std::fabs(dLon) < 1e-10)
            break;
        wgs.lat -= dLat;
        wgs.lon -= dLon;
    }
    return wgs;
}

LatLon gcj02ToBd09(const LatLon &p)
{
    const double z = std::sqrt(p.lon * p.lon + p.lat * p.lat)
                     + 0.00002 * std::sin(p.lat * X_PI);
    const double theta = std::atan2(p.lat, p.lon) + 0.000003 * std::cos(p.lon * X_PI);
    return LatLon(z * std::sin(theta) + 0.006,    // 注意：纬度用 sin
                  z * std::cos(theta) + 0.0065);
}

LatLon bd09ToGcj02(const LatLon &p)
{
    const double x = p.lon - 0.0065;
    const double y = p.lat - 0.006;
    const double z = std::sqrt(x * x + y * y) - 0.00002 * std::sin(y * X_PI);
    const double theta = std::atan2(y, x) - 0.000003 * std::cos(x * X_PI);
    return LatLon(z * std::sin(theta),    // 纬度
                  z * std::cos(theta));   // 经度
}

LatLon convert(const LatLon &p, CoordSystem from, CoordSystem to)
{
    if (from == to)
        return p;

    // 统一先转到 WGS-84，再从 WGS-84 转到目标系
    LatLon wgs = p;
    if (from == CS_GCJ02)
        wgs = gcj02ToWgs84(p);
    else if (from == CS_BD09)
        wgs = gcj02ToWgs84(bd09ToGcj02(p));

    if (to == CS_WGS84)
        return wgs;

    const LatLon gcj = wgs84ToGcj02(wgs);
    if (to == CS_GCJ02)
        return gcj;
    return gcj02ToBd09(gcj);   // CS_BD09
}

QString format(const LatLon &p, int decimals)
{
    if (!p.isValid())
        return QStringLiteral("无定位");
    const QString ns = p.lat >= 0 ? QStringLiteral("N") : QStringLiteral("S");
    const QString ew = p.lon >= 0 ? QStringLiteral("E") : QStringLiteral("W");
    return QStringLiteral("%1°%2, %3°%4")
        .arg(std::fabs(p.lat), 0, 'f', decimals)
        .arg(ns)
        .arg(std::fabs(p.lon), 0, 'f', decimals)
        .arg(ew);
}

} // namespace Geo
