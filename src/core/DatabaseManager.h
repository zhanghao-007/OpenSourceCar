#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include "../mqtt/MqttTypes.h"
#include "CarRegistry.h"
#include "DeviceProtocol.h"

#include <QObject>
#include <QSqlDatabase>
#include <QDateTime>
#include <QVector>
#include <QString>
#include <QTimer>

// ---------------------------------------------------------------------------
// 单条遥测落库记录（仅用于查询返回，不进协议层）
// ---------------------------------------------------------------------------
struct TelemetryRow {
    qint64  id          = 0;
    QString carId;
    qint64  tsMs        = 0;     // 毫秒时间戳
    double  speed       = 0.0;
    double  battery     = 0.0;
    double  heading     = 0.0;
    double  x           = 0.0;
    double  y           = 0.0;
    double  lat         = 0.0;
    double  lon         = 0.0;
    QString coordSys;            // WGS84 / GCJ02 / BD09
    double  temperature = 0.0;
    int     errorCode   = 0;
    QString status;
    QByteArray payload;          // 原始 JSON 载荷
};

// ---------------------------------------------------------------------------
// 单条消息落库记录
// ---------------------------------------------------------------------------
struct MessageRow {
    qint64  id          = 0;
    qint64  tsMs        = 0;
    QString direction;           // IN（上行）/ OUT（下行）
    QString topic;
    int     qos         = 0;
    bool    retain      = false;
    int     size        = 0;
    QByteArray payload;
};

// ---------------------------------------------------------------------------
// 设备数据行（device_data，来自 device/link/req 的 REP_DATA/SETTLEMENT）
// ---------------------------------------------------------------------------
struct DeviceDataRow {
    qint64   id         = 0;
    qint64   tsMs       = 0;     // 服务端收到时刻
    QString  devSn;
    qint64   mid        = 0;
    QString  method;
    QString  devTs;              // 设备自报时间（原样）
    qint64   devTsMs    = 0;     // 设备时间可解析且在合理区间时才有值
    int      rssi       = 0;
    double   settlement = 0.0;
    double   angX       = 0.0;
    double   angY       = 0.0;
    double   batVol     = 0.0;
    QByteArray payload;
};

// ---------------------------------------------------------------------------
// 设备事件行（device_event，LINKUP / DORMANCY / 未登记类型）
// ---------------------------------------------------------------------------
struct DeviceEventRow {
    qint64   id     = 0;
    qint64   tsMs   = 0;
    QString  devSn;
    qint64   mid    = 0;
    QString  type;
    QString  method;
    QString  devTs;
    qint64   devTsMs = 0;
    QString  mac;
    QString  ipv4;
    QString  reason;
    QByteArray payload;
};

// ---------------------------------------------------------------------------
// DatabaseManager —— 基于 Qt SQL（QSQLITE 驱动）的本地持久化
//
//  * 所有数据库访问都在创建它的线程里进行（UI 线程）。
//  * 写入采用"先入缓冲、定时器批量事务落盘"策略，
//    遥测/消息高频上报时不会每条都锁库，最大化吞吐。
//  * 打开失败（路径不可写、驱动缺失等）时优雅降级：
//    程序照常运行，只是数据不落盘，并通过 errorOccurred 上报。
// ---------------------------------------------------------------------------
class DatabaseManager : public QObject
{
    Q_OBJECT
public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager() override;

    // 打开（或创建）SQLite 数据库文件；返回是否成功
    bool open(const QString &path);
    void close();
    bool isOpen() const { return m_open; }
    QString lastError() const { return m_lastError; }
    QString filePath() const { return m_path; }

    // ---- 写入（入缓冲，由定时器统一落盘）----
    void enqueueTelemetry(const CarState &st, const QByteArray &rawPayload);
    void enqueueMessage(const Mqtt::MessageRecord &rec);
    // 设备链路报文（device/link/req）：数据帧进 device_data，事件帧进 device_event，
    // 并同步刷新 devices 最新状态快照
    void enqueueDevice(const Dev::Frame &f);
    // 小车上/下线状态变化频率低，立即落盘
    void upsertCar(const QString &carId, bool online, const QString &status, qint64 tsMs);

    // 立即把缓冲落盘（程序退出前务必调用）
    void flush();

    // ---- 查询 ----
    QList<TelemetryRow> queryTelemetry(const QString &carId, qint64 sinceMs, int limit);
    QList<MessageRow>   queryMessages(const QString &direction,
                                      const QString &topicContains,
                                      qint64 sinceMs, int limit);
    qint64 telemetryCount() const;
    qint64 messageCount()  const;
    qint64 carCount()      const;

    // ---- 设备数据查询 ----
    QList<DeviceDataRow>  queryDeviceData(const QString &devSn, qint64 sinceMs, int limit);
    QList<DeviceEventRow> queryDeviceEvents(const QString &devSn, qint64 sinceMs, int limit);
    qint64 deviceDataCount()  const;
    qint64 deviceEventCount() const;

    // ---- 维护 ----
    void trimTelemetry(qint64 keepSinceMs);   // 删除早于 keepSinceMs 的记录
    void clearTelemetry();
    void clearMessages();
    void clearDeviceData();
    void clearDeviceEvents();

    // 坐标系枚举 -> 存储字符串
    static QString coordSysName(Geo::CoordSystem cs);

signals:
    void errorOccurred(const QString &text);
    void rowsWritten(qint64 telemetry, qint64 messages);

private slots:
    void onFlushTimer();

private:
    bool    ensureSchema();
    bool    openConnection();
    void    setError(const QString &text);
    void    upsertDeviceLatest(const Dev::Frame &f);

    QSqlDatabase m_db;
    QString      m_path;
    QString      m_lastError;
    bool         m_open = false;

    QTimer  *m_flushTimer = nullptr;

    QVector<TelemetryRow>   m_teleBuf;
    QVector<MessageRow>     m_msgBuf;
    QVector<DeviceDataRow>  m_devDataBuf;
    QVector<DeviceEventRow> m_devEventBuf;
};

#endif // DATABASEMANAGER_H
