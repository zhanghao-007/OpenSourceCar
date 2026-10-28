#ifndef COORDINATESYSTEM_H
#define COORDINATESYSTEM_H

#include <QString>

// ---------------------------------------------------------------------------
// 地理坐标系（国内小车 GPS 常见三种）
//
//   WGS-84  国际标准，GPS 模块原始输出，Google Earth / 境外地图使用
//   GCJ-02  中国国测局坐标（俗称"火星坐标"），高德 / 腾讯 / 微信地图使用，
//           是在 WGS-84 基础上加非线性偏移得到的
//   BD-09   百度坐标，在 GCJ-02 基础上再加一次偏移，百度地图使用
//
// 三者必须正确转换，直接混用会有 100~600 米的偏差。
// ---------------------------------------------------------------------------
namespace Geo {

enum CoordSystem {
    CS_WGS84 = 0,     // GPS 原始 / 国际通用
    CS_GCJ02 = 1,     // 火星坐标（高德、腾讯地图）
    CS_BD09  = 2      // 百度坐标
};

struct LatLon {
    double lat = 0.0;   // 纬度，北纬为正
    double lon = 0.0;   // 经度，东经为正

    LatLon() = default;
    LatLon(double la, double lo) : lat(la), lon(lo) {}
    bool isValid() const;
};

QString      systemName(CoordSystem cs);
QString      systemNote(CoordSystem cs);
CoordSystem  systemFromString(const QString &s, CoordSystem def = CS_WGS84);

// 坐标是否位于中国境内（境外不做 GCJ-02 偏移）
bool outOfChina(double lat, double lon);

// ---- 两两互转 ----
LatLon wgs84ToGcj02(const LatLon &p);
LatLon gcj02ToWgs84(const LatLon &p);   // 迭代反解，精度约 1e-9 度（<0.1 毫米）
LatLon gcj02ToBd09(const LatLon &p);
LatLon bd09ToGcj02(const LatLon &p);

// ---- 任意两种坐标系之间转换（内部自动经 WGS-84 / GCJ-02 中转）----
LatLon convert(const LatLon &p, CoordSystem from, CoordSystem to);

// 把经纬度格式化成便于阅读的字符串，如 "39.908692°N, 116.397499°E"
QString format(const LatLon &p, int decimals = 6);

} // namespace Geo

#endif // COORDINATESYSTEM_H
