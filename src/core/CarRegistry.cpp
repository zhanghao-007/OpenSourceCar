#include "CarRegistry.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

CarRegistry::CarRegistry(QObject *parent)
    : QObject(parent)
{
}

void CarRegistry::setHistoryCapacity(int n)
{
    m_historyCap = qMax(10, n);
    QHashIterator<QString, CarState> it(m_cars);
    while (it.hasNext()) {
        it.next();
        // 历史数据在 QHash 里是值拷贝，这里借助 carId 重新取引用修整
        CarState &st = m_cars[it.key()];
        ensureCapacity(st);
    }
}

QStringList CarRegistry::carIds() const
{
    QStringList ids = m_cars.keys();
    ids.sort();
    return ids;
}

CarState CarRegistry::car(const QString &carId) const
{
    return m_cars.value(carId);
}

QList<CarState> CarRegistry::allCars() const
{
    QList<CarState> list = m_cars.values();
    return list;
}

void CarRegistry::ensureCapacity(CarState &st)
{
    while (st.speedHistory.size() > m_historyCap) {
        st.speedHistory.remove(0);
        st.batteryHistory.remove(0);
        st.timeHistory.remove(0);
    }
}

bool CarRegistry::parseCarTopic(const QString &topic, QString &carId, QString &kind) const
{
    // 期望：<prefix>/<carId>/<kind>
    const QStringList parts = topic.split(QLatin1Char('/'));
    if (parts.size() < 3)
        return false;
    if (parts.at(0) != m_prefix)
        return false;
    carId = parts.at(1);
    kind  = parts.at(2);
    return !carId.isEmpty() && !kind.isEmpty();
}

void CarRegistry::onMessage(const Mqtt::MessageRecord &rec)
{
    QString carId, kind;
    if (!parseCarTopic(rec.topic, carId, kind))
        return;

    const bool known = m_cars.contains(carId);
    CarState &st = m_cars[carId];
    st.carId = carId;
    if (!known)
        st.status = QStringLiteral("UNKNOWN");

    const QJsonDocument doc = QJsonDocument::fromJson(rec.payload);
    const QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();

    if (kind == QLatin1String("telemetry")) {
        // 上报遥测即视为在线
        st.online = true;
        if (obj.contains(QStringLiteral("speed")))
            st.speed = obj.value(QStringLiteral("speed")).toDouble();
        if (obj.contains(QStringLiteral("battery")))
            st.battery = obj.value(QStringLiteral("battery")).toDouble();
        if (obj.contains(QStringLiteral("heading")))
            st.heading = obj.value(QStringLiteral("heading")).toDouble();
        if (obj.contains(QStringLiteral("x")))
            st.x = obj.value(QStringLiteral("x")).toDouble();
        if (obj.contains(QStringLiteral("y")))
            st.y = obj.value(QStringLiteral("y")).toDouble();

        // ---- 经纬度：支持 lat/lon、latitude/longitude、lat/lng 与嵌套 gps 对象 ----
        const QJsonObject gps = obj.value(QStringLiteral("gps")).toObject();
        bool hasLat = false, hasLon = false;
        double lat = 0.0, lon = 0.0;

        if (gps.contains(QStringLiteral("lat"))) {
            lat = gps.value(QStringLiteral("lat")).toDouble();    hasLat = true;
        } else if (obj.contains(QStringLiteral("lat"))) {
            lat = obj.value(QStringLiteral("lat")).toDouble();    hasLat = true;
        } else if (obj.contains(QStringLiteral("latitude"))) {
            lat = obj.value(QStringLiteral("latitude")).toDouble(); hasLat = true;
        }

        if (gps.contains(QStringLiteral("lon"))) {
            lon = gps.value(QStringLiteral("lon")).toDouble();    hasLon = true;
        } else if (obj.contains(QStringLiteral("lon"))) {
            lon = obj.value(QStringLiteral("lon")).toDouble();    hasLon = true;
        } else if (obj.contains(QStringLiteral("lng"))) {
            lon = obj.value(QStringLiteral("lng")).toDouble();    hasLon = true;
        } else if (obj.contains(QStringLiteral("longitude"))) {
            lon = obj.value(QStringLiteral("longitude")).toDouble(); hasLon = true;
        }

        if (hasLat && hasLon) {
            st.lat      = lat;
            st.lon      = lon;
            st.gpsValid = Geo::LatLon(lat, lon).isValid();
        }

        // ---- 坐标系：coord / coordSys / coordSystem，缺省沿用上一次的值 ----
        QString csStr;
        if (obj.contains(QStringLiteral("coord")))
            csStr = obj.value(QStringLiteral("coord")).toString();
        else if (obj.contains(QStringLiteral("coordSys")))
            csStr = obj.value(QStringLiteral("coordSys")).toString();
        else if (obj.contains(QStringLiteral("coordSystem")))
            csStr = obj.value(QStringLiteral("coordSystem")).toString();
        else if (gps.contains(QStringLiteral("coord")))
            csStr = gps.value(QStringLiteral("coord")).toString();
        if (!csStr.isEmpty())
            st.coordSys = Geo::systemFromString(csStr, st.coordSys);
        if (obj.contains(QStringLiteral("temp")))
            st.temperature = obj.value(QStringLiteral("temp")).toDouble();
        if (obj.contains(QStringLiteral("error")))
            st.errorCode = obj.value(QStringLiteral("error")).toInt();
        if (obj.contains(QStringLiteral("status")))
            st.status = obj.value(QStringLiteral("status")).toString();
        if (obj.contains(QStringLiteral("carId"))) {
            const QString id = obj.value(QStringLiteral("carId")).toString();
            if (!id.isEmpty())
                st.carId = id;
        }
        st.lastUpdate = rec.timestamp;
        appendHistory(st, rec.timestamp);
    } else if (kind == QLatin1String("status")) {
        if (obj.contains(QStringLiteral("online")))
            st.online = obj.value(QStringLiteral("online")).toBool(true);
        else
            st.online = true;
        if (obj.contains(QStringLiteral("status")))
            st.status = obj.value(QStringLiteral("status")).toString();
        st.lastUpdate = rec.timestamp;
    } else {
        // 其它 kind（cmd/ack/cfg 等）只更新活跃时间
        st.lastUpdate = rec.timestamp;
    }

    if (known)
        emit carUpdated(carId);
    else
        emit carAdded(carId);
}

void CarRegistry::appendHistory(CarState &st, const QDateTime &ts)
{
    st.speedHistory.append(st.speed);
    st.batteryHistory.append(st.battery);
    st.timeHistory.append(ts.isValid() ? ts.toMSecsSinceEpoch()
                                       : QDateTime::currentMSecsSinceEpoch());
    ensureCapacity(st);
}

Geo::LatLon CarRegistry::displayLatLon(const QString &carId) const
{
    const CarState st = m_cars.value(carId);
    const Geo::LatLon raw(st.lat, st.lon);
    if (!st.gpsValid)
        return raw;
    return Geo::convert(raw, st.coordSys, m_displaySys);
}

void CarRegistry::markOffline(const QString &carId)
{
    if (!m_cars.contains(carId))
        return;
    m_cars[carId].online = false;
    m_cars[carId].status = QStringLiteral("OFFLINE");
    emit carUpdated(carId);
}

void CarRegistry::removeCar(const QString &carId)
{
    if (m_cars.remove(carId) > 0)
        emit carRemoved(carId);
}

void CarRegistry::clearHistory()
{
    QMutableHashIterator<QString, CarState> it(m_cars);
    while (it.hasNext()) {
        it.next();
        it.value().speedHistory.clear();
        it.value().batteryHistory.clear();
        it.value().timeHistory.clear();
    }
}
