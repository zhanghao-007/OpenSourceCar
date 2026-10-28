#ifndef CARREGISTRY_H
#define CARREGISTRY_H

#include "../mqtt/MqttTypes.h"
#include "CoordinateSystem.h"

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <QDateTime>

// ---------------------------------------------------------------------------
// 小车遥测/状态协议约定（后台与小车固件之间的约定，可在 UI 里改前缀）
//
//   上行遥测   car/<carId>/telemetry
//              {"carId":"CAR-001","ts":1730000000000,
//               "speed":1.20,"battery":87.5,"heading":90.0,
//               "lat":39.908692,"lon":116.397499,"coord":"WGS84",
//               "x":1.5,"y":2.3,"temp":42.1,"status":"RUNNING","error":0}
//
//   经纬度字段兼容三种写法（任选其一即可）：
//       "lat"/"lon"、"latitude"/"longitude"、"lat"/"lng"
//       或嵌套对象： "gps":{"lat":39.908692,"lon":116.397499}
//   坐标系字段（可选，缺省按 WGS-84 处理）：
//       "coord" / "coordSys" / "coordSystem"，取值 "WGS84" | "GCJ02" | "BD09"
//
//   上行状态   car/<carId>/status      {"online":true}
//   下行指令   car/<carId>/cmd         {"cmd":"MOVE","seq":1,"params":{"v":0.5,"w":0.1}}
//   下行配置   car/<carId>/cfg         {"maxSpeed":1.0}
//
// 字段全部可选，缺字段保持上一次的值。
// ---------------------------------------------------------------------------

struct CarState {
    QString   carId;
    bool      online      = false;
    double    speed       = 0.0;     // m/s
    double    battery     = 0.0;     // %
    double    heading     = 0.0;     // 度，0=正北
    double    x           = 0.0;     // m，车体坐标系下的本地坐标
    double    y           = 0.0;     // m
    double    lat         = 0.0;     // 纬度（上报时的原始坐标系）
    double    lon         = 0.0;     // 经度
    bool      gpsValid    = false;   // 是否收到过有效定位
    Geo::CoordSystem coordSys = Geo::CS_WGS84;  // 上报所用坐标系
    double    temperature = 0.0;     // ℃
    int       errorCode   = 0;
    QString   status      = QStringLiteral("UNKNOWN");
    QDateTime lastUpdate;

    // 曲线历史（固定长度环形缓冲，超出后丢弃最旧的点）
    QVector<double>   speedHistory;
    QVector<double>   batteryHistory;
    QVector<qint64>   timeHistory;

    CarState() = default;
};

// ---------------------------------------------------------------------------
// CarRegistry —— 从 MQTT 消息流中还原每台小车的最新状态
//
// 运行在 UI 线程：由 MainWindow 拿到 broker 的 messageRecord 信号后转发进来。
// ---------------------------------------------------------------------------
class CarRegistry : public QObject
{
    Q_OBJECT
public:
    explicit CarRegistry(QObject *parent = nullptr);

    void setTopicPrefix(const QString &prefix) { m_prefix = prefix; }
    QString topicPrefix() const { return m_prefix; }

    // 单个会话内保留的历史点数
    void setHistoryCapacity(int n);
    int  historyCapacity() const { return m_historyCap; }

    // 界面展示用的目标坐标系（不影响存储，存储始终是小车上报的原始坐标系）
    void setDisplaySystem(Geo::CoordSystem cs) { m_displaySys = cs; }
    Geo::CoordSystem displaySystem() const { return m_displaySys; }
    // 取某台小车转换到目标坐标系后的经纬度
    Geo::LatLon displayLatLon(const QString &carId) const;

    QStringList carIds() const;
    CarState    car(const QString &carId) const;
    QList<CarState> allCars() const;

    // 判断主题是否属于小车遥测，并取出 carId
    bool parseCarTopic(const QString &topic, QString &carId, QString &kind) const;

public slots:
    // 喂入一条 MQTT 消息（任意方向），内部自行判定是否为小车报文
    void onMessage(const Mqtt::MessageRecord &rec);
    void markOffline(const QString &carId);
    void removeCar(const QString &carId);
    void clearHistory();

signals:
    void carAdded(const QString &carId);
    void carUpdated(const QString &carId);
    void carRemoved(const QString &carId);

private:
    void appendHistory(CarState &st, const QDateTime &ts);
    void ensureCapacity(CarState &st);

    QHash<QString, CarState> m_cars;
    QString    m_prefix = QStringLiteral("car");
    int        m_historyCap = 300;
    Geo::CoordSystem m_displaySys = Geo::CS_WGS84;
};

#endif // CARREGISTRY_H
