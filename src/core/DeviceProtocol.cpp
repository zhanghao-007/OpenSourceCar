#include "DeviceProtocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QStringList>

namespace {

// 数值容错：double / "1.1" / true 都能取到值
bool asDouble(const QJsonValue &v, double &out)
{
    if (v.isDouble()) { out = v.toDouble(); return true; }
    if (v.isBool())   { out = v.toBool() ? 1.0 : 0.0; return true; }
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().trimmed().toDouble(&ok);
        if (ok) { out = d; return true; }
    }
    return false;
}

bool asInt(const QJsonValue &v, qint64 &out)
{
    double d = 0.0;
    if (!asDouble(v, d)) return false;
    out = static_cast<qint64>(d);
    return true;
}

QString asText(const QJsonValue &v)
{
    if (v.isString()) return v.toString().trimmed();
    if (v.isDouble()) {
        // MAC/IP 之类正常是字符串，个别固件会把纯数字 IPV4 当 number 发（几乎不可能，兜底而已）
        const double d = v.toDouble();
        if (d == static_cast<double>(static_cast<qint64>(d)))
            return QString::number(static_cast<qint64>(d));
        return QString::number(d);
    }
    return QString();
}

// 设备时间：格式 "yyyy-MM-dd HH:mm:ss"。设备未对时时是 2000-01-01 这类初值，
// 只作为自报时间留存，不作为检索时间；超出合理区间则置 0。
qint64 parseDeviceTime(const QString &s, const QDateTime &now)
{
    if (s.isEmpty()) return 0;
    const QStringList formats = {
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")
    };
    for (const QString &fmt : formats) {
        QDateTime dt = QDateTime::fromString(s, fmt);
        if (!dt.isValid()) continue;
        const qint64 ms = dt.toMSecsSinceEpoch();
        // 合理区间：2020-01-01 之后，且不晚于"现在 + 1 天"（容忍小幅时钟漂移）
        const qint64 lo = QDateTime(QDate(2020, 1, 1), QTime(0, 0)).toMSecsSinceEpoch();
        const qint64 hi = (now.isValid() ? now : QDateTime::currentDateTime())
                              .toMSecsSinceEpoch() + 86400000LL;
        return (ms >= lo && ms <= hi) ? ms : 0;
    }
    return 0;
}

void setErr(QString *err, const QString &text)
{
    if (err) *err = text;
}

} // namespace

namespace Dev {

FrameKind kindOf(const QString &type)
{
    if (type == QLatin1String("REP_DATA"))        return KindData;
    if (type == QLatin1String("EVENT_LINKUP"))    return KindLinkUp;
    if (type == QLatin1String("EVENT_DORMANCY"))  return KindDormancy;
    return KindOther;
}

QString kindName(FrameKind k)
{
    switch (k) {
    case KindData:     return QStringLiteral("数据");
    case KindLinkUp:   return QStringLiteral("上线");
    case KindDormancy: return QStringLiteral("休眠");
    case KindOther:    return QStringLiteral("其它");
    default:           return QStringLiteral("未知");
    }
}

bool isDeviceTopic(const QString &topic, const QString &prefix)
{
    if (topic.isEmpty()) return false;
    if (prefix.isEmpty()) return true;
    // 允许前缀精确匹配或作为层级前缀（device/link -> device/link/req）
    if (topic == prefix) return true;
    return topic.startsWith(prefix + QLatin1Char('/'));
}

bool parse(const Mqtt::MessageRecord &rec, Frame &out, QString *error)
{
    out = Frame();
    out.topic    = rec.topic;
    out.clientId = rec.clientId;
    out.recvAt   = rec.timestamp.isValid() ? rec.timestamp : QDateTime::currentDateTime();
    out.payload  = rec.payload;

    if (rec.payload.isEmpty()) {
        setErr(error, QStringLiteral("载荷为空"));
        return false;
    }

    QJsonParseError jerr;
    const QJsonDocument doc = QJsonDocument::fromJson(rec.payload, &jerr);
    if (jerr.error != QJsonParseError::NoError) {
        setErr(error, QStringLiteral("JSON 解析失败（偏移 %1）：%2")
                          .arg(jerr.offset).arg(jerr.errorString()));
        return false;
    }
    if (!doc.isObject()) {
        setErr(error, QStringLiteral("载荷不是 JSON 对象"));
        return false;
    }
    const QJsonObject root = doc.object();

    out.type   = asText(root.value(QStringLiteral("type")));
    out.method = asText(root.value(QStringLiteral("param")).toObject()
                            .value(QStringLiteral("method")));
    out.devSn  = asText(root.value(QStringLiteral("devSn")));
    if (out.devSn.isEmpty())
        out.devSn = asText(root.value(QStringLiteral("devId")));   // 兼容写法
    if (out.devSn.isEmpty())
        out.devSn = rec.clientId;                                  // 最后兜底：用 ClientID

    if (out.type.isEmpty()) {
        setErr(error, QStringLiteral("缺少 type 字段"));
        return false;
    }
    if (out.devSn.isEmpty()) {
        setErr(error, QStringLiteral("缺少 devSn 字段且发布方 ClientID 为空"));
        return false;
    }

    qint64 mid = 0;
    if (asInt(root.value(QStringLiteral("mid")), mid))
        out.mid = mid;

    out.devTs   = asText(root.value(QStringLiteral("timestamp")));
    out.devTsMs = parseDeviceTime(out.devTs, out.recvAt);

    const QJsonObject param = root.value(QStringLiteral("param")).toObject();
    out.kind = kindOf(out.type);

    switch (out.kind) {
    case KindData: {
        const QJsonObject data = param.value(QStringLiteral("data")).toObject();
        out.hasData = !data.isEmpty();
        double d = 0.0;

        if (asDouble(data.value(QStringLiteral("RSSI")), d))             out.rssi = static_cast<int>(d);
        // 主键名 settlement_sensor，兼容 settlement / settlementSensor
        if (asDouble(data.value(QStringLiteral("settlement_sensor")), d)) out.settlement = d;
        else if (asDouble(data.value(QStringLiteral("settlement")), d))   out.settlement = d;
        else if (asDouble(data.value(QStringLiteral("settlementSensor")), d)) out.settlement = d;
        if (asDouble(data.value(QStringLiteral("AngX")), d))              out.angX = d;
        if (asDouble(data.value(QStringLiteral("AngY")), d))              out.angY = d;
        if (asDouble(data.value(QStringLiteral("BatVol")), d))            out.batVol = d;
        break;
    }
    case KindLinkUp:
        out.mac  = asText(param.value(QStringLiteral("MAC")));
        out.ipv4 = asText(param.value(QStringLiteral("IPV4")));
        break;
    case KindDormancy:
        out.reason = asText(param.value(QStringLiteral("reason")));
        break;
    default:
        // 未登记类型：不猜字段，整条 param 随原始载荷归档
        break;
    }

    return true;
}

QString summary(const Frame &f)
{
    QString s = f.devSn + QLatin1Char(' ') + f.type;
    if (!f.method.isEmpty())
        s += QLatin1Char('/') + f.method;
    switch (f.kind) {
    case KindData:
        s += QStringLiteral("｜电压 %1V 沉降 %2 倾角X %3 倾角Y %4 RSSI %5")
                 .arg(f.batVol, 0, 'f', 2)
                 .arg(f.settlement, 0, 'f', 2)
                 .arg(f.angX, 0, 'f', 2)
                 .arg(f.angY, 0, 'f', 2)
                 .arg(f.rssi);
        break;
    case KindLinkUp:
        s += QStringLiteral("｜MAC %1 IP %2").arg(f.mac).arg(f.ipv4);
        break;
    case KindDormancy:
        s += QStringLiteral("｜原因 %1").arg(f.reason.isEmpty() ? QStringLiteral("-") : f.reason);
        break;
    default:
        break;
    }
    return s;
}

} // namespace Dev
