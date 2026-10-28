#include "DatabaseManager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>
#include <QDir>

// 固定的连接名，全局唯一即可
static const char *kConnName = "carbroker";

DatabaseManager::DatabaseManager(QObject *parent)
    : QObject(parent)
{
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(500);   // 每 500ms 批量落盘一次
    connect(m_flushTimer, &QTimer::timeout, this, &DatabaseManager::onFlushTimer);
}

DatabaseManager::~DatabaseManager()
{
    flush();      // 退出前尽量把缓冲写进去
    close();
}

void DatabaseManager::setError(const QString &text)
{
    m_lastError = text;
    if (!text.isEmpty())
        emit errorOccurred(text);
}

bool DatabaseManager::openConnection()
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        setError(QStringLiteral("系统缺少 QSQLITE 驱动，无法启用数据库"));
        return false;
    }
    // 同名连接已存在则复用，避免重复 addDatabase 告警
    if (QSqlDatabase::contains(QString::fromLatin1(kConnName)))
        m_db = QSqlDatabase::database(QString::fromLatin1(kConnName));
    else
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                        QString::fromLatin1(kConnName));

    m_db.setDatabaseName(m_path);
    if (!m_db.open()) {
        setError(QStringLiteral("打开数据库失败：%1").arg(m_db.lastError().text()));
        m_open = false;
        return false;
    }
    m_open = true;
    return ensureSchema();
}

bool DatabaseManager::open(const QString &path)
{
    m_path = path;
    m_lastError.clear();

    // 确保父目录存在
    const QFileInfo fi(path);
    if (!fi.dir().exists()) {
        if (!QDir().mkpath(fi.dir().absolutePath())) {
            setError(QStringLiteral("无法创建数据库目录：%1").arg(fi.dir().absolutePath()));
            return false;
        }
    }
    const bool ok = openConnection();
    if (ok)
        m_flushTimer->start();
    return ok;
}

void DatabaseManager::close()
{
    if (m_flushTimer && m_flushTimer->isActive())
        m_flushTimer->stop();
    if (m_db.isOpen())
        m_db.close();
    m_open = false;
    // 先把成员里的 QSqlDatabase 句柄释放掉，否则 removeDatabase 会报警
    // "connection ... is still in use"
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(QString::fromLatin1(kConnName)))
        QSqlDatabase::removeDatabase(QString::fromLatin1(kConnName));
}

bool DatabaseManager::ensureSchema()
{
    if (!m_db.isOpen())
        return false;

    const QStringList statements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS telemetry ("
            "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  car_id      TEXT    NOT NULL,"
            "  ts_ms       INTEGER NOT NULL,"
            "  speed       REAL,"
            "  battery     REAL,"
            "  heading     REAL,"
            "  x           REAL,"
            "  y           REAL,"
            "  lat         REAL,"
            "  lon         REAL,"
            "  coord_sys   TEXT,"
            "  temperature REAL,"
            "  error_code  INTEGER,"
            "  status      TEXT,"
            "  payload     TEXT"
            ")"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_telemetry_car ON telemetry(car_id, ts_ms)"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS messages ("
            "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  ts_ms      INTEGER NOT NULL,"
            "  direction  TEXT,"
            "  topic      TEXT,"
            "  qos        INTEGER,"
            "  retain     INTEGER,"
            "  size       INTEGER,"
            "  payload    TEXT"
            ")"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_messages_ts ON messages(ts_ms)"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS cars ("
            "  car_id       TEXT PRIMARY KEY,"
            "  online       INTEGER,"
            "  last_seen_ms INTEGER,"
            "  last_status  TEXT"
            ")"),

        // ---- 设备链路数据（device/link/req · REP_DATA/SETTLEMENT）----
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS device_data ("
            "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  ts_ms       INTEGER NOT NULL,"
            "  dev_sn      TEXT    NOT NULL,"
            "  mid         INTEGER,"
            "  method      TEXT,"
            "  dev_ts      TEXT,"
            "  dev_ts_ms   INTEGER,"
            "  rssi        INTEGER,"
            "  settlement  REAL,"
            "  ang_x       REAL,"
            "  ang_y       REAL,"
            "  bat_vol     REAL,"
            "  payload     TEXT"
            ")"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_device_data_sn "
                       "ON device_data(dev_sn, ts_ms)"),

        // ---- 设备链路事件（LINKUP / DORMANCY / 未登记类型）----
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS device_event ("
            "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  ts_ms      INTEGER NOT NULL,"
            "  dev_sn     TEXT    NOT NULL,"
            "  mid        INTEGER,"
            "  type       TEXT,"
            "  method     TEXT,"
            "  dev_ts     TEXT,"
            "  dev_ts_ms  INTEGER,"
            "  mac        TEXT,"
            "  ipv4       TEXT,"
            "  reason     TEXT,"
            "  payload    TEXT"
            ")"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_device_event_sn "
                       "ON device_event(dev_sn, ts_ms)"),

        // ---- 设备最新状态快照（每台设备一行，便于界面秒开）----
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS devices ("
            "  dev_sn       TEXT PRIMARY KEY,"
            "  online       INTEGER,"
            "  last_seen_ms INTEGER,"
            "  last_type    TEXT,"
            "  mac          TEXT,"
            "  ipv4         TEXT,"
            "  rssi         INTEGER,"
            "  bat_vol      REAL,"
            "  settlement   REAL,"
            "  ang_x        REAL,"
            "  ang_y        REAL,"
            "  msg_count    INTEGER DEFAULT 0"
            ")")
    };

    for (const QString &sql : statements) {
        QSqlQuery q(m_db);
        if (!q.exec(sql)) {
            setError(QStringLiteral("建表失败：%1").arg(q.lastError().text()));
            return false;
        }
    }
    return true;
}

QString DatabaseManager::coordSysName(Geo::CoordSystem cs)
{
    switch (cs) {
    case Geo::CS_GCJ02: return QStringLiteral("GCJ02");
    case Geo::CS_BD09:  return QStringLiteral("BD09");
    default:            return QStringLiteral("WGS84");
    }
}

void DatabaseManager::enqueueTelemetry(const CarState &st, const QByteArray &rawPayload)
{
    if (!m_open)
        return;
    TelemetryRow r;
    r.carId       = st.carId;
    r.tsMs        = st.lastUpdate.isValid() ? st.lastUpdate.toMSecsSinceEpoch()
                                            : QDateTime::currentMSecsSinceEpoch();
    r.speed       = st.speed;
    r.battery     = st.battery;
    r.heading     = st.heading;
    r.x           = st.x;
    r.y           = st.y;
    r.lat         = st.lat;
    r.lon         = st.lon;
    r.coordSys    = coordSysName(st.coordSys);
    r.temperature = st.temperature;
    r.errorCode   = st.errorCode;
    r.status      = st.status;
    r.payload     = rawPayload;
    m_teleBuf.append(r);
}

void DatabaseManager::enqueueMessage(const Mqtt::MessageRecord &rec)
{
    if (!m_open)
        return;
    MessageRow r;
    r.tsMs       = rec.timestamp.isValid() ? rec.timestamp.toMSecsSinceEpoch()
                                           : QDateTime::currentMSecsSinceEpoch();
    r.direction  = rec.direction;
    r.topic      = rec.topic;
    r.qos        = static_cast<int>(rec.qos);
    r.retain     = rec.retain;
    r.size       = rec.payload.size();
    r.payload    = rec.payload;
    m_msgBuf.append(r);
}

// ---------------------------------------------------------------------------
// 设备链路报文落库
//
//   * 数据帧（REP_DATA/SETTLEMENT）-> device_data 缓冲，批量落盘
//   * 事件帧（LINKUP / DORMANCY / 其它）-> device_event 缓冲，批量落盘
//   * 无论哪类，都立刻刷新 devices 最新状态快照（每帧一次单行 upsert，
//     频率远低于数据帧本身，不会成为瓶颈）
// ---------------------------------------------------------------------------
void DatabaseManager::enqueueDevice(const Dev::Frame &f)
{
    if (!m_open || !f.isValid())
        return;

    const qint64 ts = f.recvAt.isValid() ? f.recvAt.toMSecsSinceEpoch()
                                         : QDateTime::currentMSecsSinceEpoch();

    if (f.kind == Dev::KindData) {
        DeviceDataRow r;
        r.tsMs       = ts;
        r.devSn      = f.devSn;
        r.mid        = f.mid;
        r.method     = f.method;
        r.devTs      = f.devTs;
        r.devTsMs    = f.devTsMs;
        r.rssi       = f.rssi;
        r.settlement = f.settlement;
        r.angX       = f.angX;
        r.angY       = f.angY;
        r.batVol     = f.batVol;
        r.payload    = f.payload;
        m_devDataBuf.append(r);
    } else {
        DeviceEventRow r;
        r.tsMs    = ts;
        r.devSn   = f.devSn;
        r.mid     = f.mid;
        r.type    = f.type;
        r.method  = f.method;
        r.devTs   = f.devTs;
        r.devTsMs = f.devTsMs;
        r.mac     = f.mac;
        r.ipv4    = f.ipv4;
        r.reason  = f.reason;
        r.payload = f.payload;
        m_devEventBuf.append(r);
    }

    upsertDeviceLatest(f);
}

void DatabaseManager::upsertDeviceLatest(const Dev::Frame &f)
{
    if (!m_open)
        return;

    const qint64 ts = f.recvAt.isValid() ? f.recvAt.toMSecsSinceEpoch()
                                         : QDateTime::currentMSecsSinceEpoch();

    // 只有"带该字段"的帧才覆盖旧值，其余传 NULL 由 COALESCE 保留原值
    QVariant online;
    if (f.kind == Dev::KindLinkUp || f.kind == Dev::KindData)
        online = 1;
    else if (f.kind == Dev::KindDormancy)
        online = 0;

    QVariant rssi, bat, sett, ax, ay;
    if (f.kind == Dev::KindData) {
        rssi = f.rssi;
        bat  = f.batVol;
        sett = f.settlement;
        ax   = f.angX;
        ay   = f.angY;
    }
    QVariant mac  = f.mac.isEmpty()  ? QVariant() : QVariant(f.mac);
    QVariant ip   = f.ipv4.isEmpty() ? QVariant() : QVariant(f.ipv4);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO devices "
        "(dev_sn, online, last_seen_ms, last_type, mac, ipv4, rssi, bat_vol, "
        " settlement, ang_x, ang_y, msg_count) "
        "VALUES (:sn,:on,:ts,:ty,:mac,:ip,:rssi,:bat,:sett,:ax,:ay,1) "
        "ON CONFLICT(dev_sn) DO UPDATE SET "
        "  online       = COALESCE(:on, online),"
        "  last_seen_ms = :ts,"
        "  last_type    = :ty,"
        "  mac          = COALESCE(:mac, mac),"
        "  ipv4         = COALESCE(:ip, ipv4),"
        "  rssi         = COALESCE(:rssi, rssi),"
        "  bat_vol      = COALESCE(:bat, bat_vol),"
        "  settlement   = COALESCE(:sett, settlement),"
        "  ang_x        = COALESCE(:ax, ang_x),"
        "  ang_y        = COALESCE(:ay, ang_y),"
        "  msg_count    = msg_count + 1"));
    q.bindValue(QStringLiteral(":sn"),   f.devSn);
    q.bindValue(QStringLiteral(":on"),   online);
    q.bindValue(QStringLiteral(":ts"),   ts);
    q.bindValue(QStringLiteral(":ty"),   f.type);
    q.bindValue(QStringLiteral(":mac"),  mac);
    q.bindValue(QStringLiteral(":ip"),   ip);
    q.bindValue(QStringLiteral(":rssi"), rssi);
    q.bindValue(QStringLiteral(":bat"),  bat);
    q.bindValue(QStringLiteral(":sett"), sett);
    q.bindValue(QStringLiteral(":ax"),   ax);
    q.bindValue(QStringLiteral(":ay"),   ay);
    if (!q.exec())
        setError(QStringLiteral("更新设备状态失败：%1").arg(q.lastError().text()));
}

void DatabaseManager::upsertCar(const QString &carId, bool online,
                                const QString &status, qint64 tsMs)
{
    if (!m_open)
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO cars (car_id, online, last_seen_ms, last_status) "
        "VALUES (:id, :on, :ts, :st) "
        "ON CONFLICT(car_id) DO UPDATE SET "
        "online=:on, last_seen_ms=:ts, last_status=:st"));
    q.bindValue(QStringLiteral(":id"), carId);
    q.bindValue(QStringLiteral(":on"), online ? 1 : 0);
    q.bindValue(QStringLiteral(":ts"), tsMs);
    q.bindValue(QStringLiteral(":st"), status);
    if (!q.exec())
        setError(QStringLiteral("更新小车状态失败：%1").arg(q.lastError().text()));
}

void DatabaseManager::flush()
{
    if (!m_open)
        return;
    const int nTele = m_teleBuf.size();
    const int nMsg  = m_msgBuf.size();
    const int nDevD = m_devDataBuf.size();
    const int nDevE = m_devEventBuf.size();
    if (nTele == 0 && nMsg == 0 && nDevD == 0 && nDevE == 0)
        return;

    if (!m_db.transaction()) {
        setError(QStringLiteral("开启事务失败：%1").arg(m_db.lastError().text()));
        return;
    }

    bool ok = true;
    if (nTele > 0) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "INSERT INTO telemetry "
            "(car_id, ts_ms, speed, battery, heading, x, y, lat, lon, "
            " coord_sys, temperature, error_code, status, payload) "
            "VALUES "
            "(:id,:ts,:sp,:ba,:he,:x,:y,:la,:lo,:cs,:te,:ec,:st,:pl)"));
        for (const TelemetryRow &r : qAsConst(m_teleBuf)) {
            q.bindValue(QStringLiteral(":id"), r.carId);
            q.bindValue(QStringLiteral(":ts"), r.tsMs);
            q.bindValue(QStringLiteral(":sp"), r.speed);
            q.bindValue(QStringLiteral(":ba"), r.battery);
            q.bindValue(QStringLiteral(":he"), r.heading);
            q.bindValue(QStringLiteral(":x"),  r.x);
            q.bindValue(QStringLiteral(":y"),  r.y);
            q.bindValue(QStringLiteral(":la"), r.lat);
            q.bindValue(QStringLiteral(":lo"), r.lon);
            q.bindValue(QStringLiteral(":cs"), r.coordSys);
            q.bindValue(QStringLiteral(":te"), r.temperature);
            q.bindValue(QStringLiteral(":ec"), r.errorCode);
            q.bindValue(QStringLiteral(":st"), r.status);
            q.bindValue(QStringLiteral(":pl"), QString::fromUtf8(r.payload));
            if (!q.exec()) {
                setError(QStringLiteral("写入遥测失败：%1").arg(q.lastError().text()));
                ok = false;
                break;
            }
        }
    }

    if (ok && nMsg > 0) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "INSERT INTO messages "
            "(ts_ms, direction, topic, qos, retain, size, payload) "
            "VALUES (:ts,:dir,:tp,:qos,:re,:sz,:pl)"));
        for (const MessageRow &r : qAsConst(m_msgBuf)) {
            q.bindValue(QStringLiteral(":ts"),  r.tsMs);
            q.bindValue(QStringLiteral(":dir"), r.direction);
            q.bindValue(QStringLiteral(":tp"),  r.topic);
            q.bindValue(QStringLiteral(":qos"), r.qos);
            q.bindValue(QStringLiteral(":re"),  r.retain ? 1 : 0);
            q.bindValue(QStringLiteral(":sz"),  r.size);
            q.bindValue(QStringLiteral(":pl"),  QString::fromUtf8(r.payload));
            if (!q.exec()) {
                setError(QStringLiteral("写入消息失败：%1").arg(q.lastError().text()));
                ok = false;
                break;
            }
        }
    }

    if (ok && nDevD > 0) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "INSERT INTO device_data "
            "(ts_ms, dev_sn, mid, method, dev_ts, dev_ts_ms, rssi, settlement, "
            " ang_x, ang_y, bat_vol, payload) "
            "VALUES (:ts,:sn,:mid,:me,:dt,:dtm,:rs,:se,:ax,:ay,:bv,:pl)"));
        for (const DeviceDataRow &r : qAsConst(m_devDataBuf)) {
            q.bindValue(QStringLiteral(":ts"),  r.tsMs);
            q.bindValue(QStringLiteral(":sn"),  r.devSn);
            q.bindValue(QStringLiteral(":mid"), r.mid);
            q.bindValue(QStringLiteral(":me"),  r.method);
            q.bindValue(QStringLiteral(":dt"),  r.devTs);
            q.bindValue(QStringLiteral(":dtm"), r.devTsMs);
            q.bindValue(QStringLiteral(":rs"),  r.rssi);
            q.bindValue(QStringLiteral(":se"),  r.settlement);
            q.bindValue(QStringLiteral(":ax"),  r.angX);
            q.bindValue(QStringLiteral(":ay"),  r.angY);
            q.bindValue(QStringLiteral(":bv"),  r.batVol);
            q.bindValue(QStringLiteral(":pl"),  QString::fromUtf8(r.payload));
            if (!q.exec()) {
                setError(QStringLiteral("写入设备数据失败：%1").arg(q.lastError().text()));
                ok = false;
                break;
            }
        }
    }

    if (ok && nDevE > 0) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "INSERT INTO device_event "
            "(ts_ms, dev_sn, mid, type, method, dev_ts, dev_ts_ms, mac, ipv4, "
            " reason, payload) "
            "VALUES (:ts,:sn,:mid,:ty,:me,:dt,:dtm,:mac,:ip,:re,:pl)"));
        for (const DeviceEventRow &r : qAsConst(m_devEventBuf)) {
            q.bindValue(QStringLiteral(":ts"),  r.tsMs);
            q.bindValue(QStringLiteral(":sn"),  r.devSn);
            q.bindValue(QStringLiteral(":mid"), r.mid);
            q.bindValue(QStringLiteral(":ty"),  r.type);
            q.bindValue(QStringLiteral(":me"),  r.method);
            q.bindValue(QStringLiteral(":dt"),  r.devTs);
            q.bindValue(QStringLiteral(":dtm"), r.devTsMs);
            q.bindValue(QStringLiteral(":mac"), r.mac);
            q.bindValue(QStringLiteral(":ip"),  r.ipv4);
            q.bindValue(QStringLiteral(":re"),  r.reason);
            q.bindValue(QStringLiteral(":pl"),  QString::fromUtf8(r.payload));
            if (!q.exec()) {
                setError(QStringLiteral("写入设备事件失败：%1").arg(q.lastError().text()));
                ok = false;
                break;
            }
        }
    }

    if (ok) {
        if (!m_db.commit()) {
            setError(QStringLiteral("提交事务失败：%1").arg(m_db.lastError().text()));
            m_db.rollback();
            return;
        }
        m_teleBuf.clear();
        m_msgBuf.clear();
        m_devDataBuf.clear();
        m_devEventBuf.clear();
        emit rowsWritten(nTele, nMsg);
    } else {
        m_db.rollback();
    }
}

void DatabaseManager::onFlushTimer()
{
    flush();
}

QList<TelemetryRow> DatabaseManager::queryTelemetry(const QString &carId,
                                                    qint64 sinceMs, int limit)
{
    QList<TelemetryRow> rows;
    if (!m_open)
        return rows;
    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT id, car_id, ts_ms, speed, battery, heading, x, y, lat, lon, "
        "coord_sys, temperature, error_code, status, payload FROM telemetry WHERE 1");
    if (!carId.isEmpty())
        sql += QStringLiteral(" AND car_id = :id");
    if (sinceMs > 0)
        sql += QStringLiteral(" AND ts_ms >= :since");
    sql += QStringLiteral(" ORDER BY ts_ms DESC LIMIT :lim");
    q.prepare(sql);
    if (!carId.isEmpty())
        q.bindValue(QStringLiteral(":id"), carId);
    if (sinceMs > 0)
        q.bindValue(QStringLiteral(":since"), sinceMs);
    q.bindValue(QStringLiteral(":lim"), qMin(limit, 10000));

    if (!q.exec()) {
        setError(QStringLiteral("查询遥测失败：%1").arg(q.lastError().text()));
        return rows;
    }
    while (q.next()) {
        TelemetryRow r;
        r.id          = q.value(0).toLongLong();
        r.carId       = q.value(1).toString();
        r.tsMs        = q.value(2).toLongLong();
        r.speed       = q.value(3).toDouble();
        r.battery     = q.value(4).toDouble();
        r.heading     = q.value(5).toDouble();
        r.x           = q.value(6).toDouble();
        r.y           = q.value(7).toDouble();
        r.lat         = q.value(8).toDouble();
        r.lon         = q.value(9).toDouble();
        r.coordSys    = q.value(10).toString();
        r.temperature = q.value(11).toDouble();
        r.errorCode   = q.value(12).toInt();
        r.status      = q.value(13).toString();
        r.payload     = q.value(14).toByteArray();
        rows.append(r);
    }
    return rows;
}

QList<MessageRow> DatabaseManager::queryMessages(const QString &direction,
                                                 const QString &topicContains,
                                                 qint64 sinceMs, int limit)
{
    QList<MessageRow> rows;
    if (!m_open)
        return rows;
    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT id, ts_ms, direction, topic, qos, retain, size, payload "
        "FROM messages WHERE 1");
    if (!direction.isEmpty())
        sql += QStringLiteral(" AND direction = :dir");
    if (!topicContains.isEmpty())
        sql += QStringLiteral(" AND topic LIKE :tp");
    if (sinceMs > 0)
        sql += QStringLiteral(" AND ts_ms >= :since");
    sql += QStringLiteral(" ORDER BY ts_ms DESC LIMIT :lim");
    q.prepare(sql);
    if (!direction.isEmpty())
        q.bindValue(QStringLiteral(":dir"), direction);
    if (!topicContains.isEmpty())
        q.bindValue(QStringLiteral(":tp"),
                    QStringLiteral("%%%1%%").arg(topicContains));
    if (sinceMs > 0)
        q.bindValue(QStringLiteral(":since"), sinceMs);
    q.bindValue(QStringLiteral(":lim"), qMin(limit, 10000));

    if (!q.exec()) {
        setError(QStringLiteral("查询消息失败：%1").arg(q.lastError().text()));
        return rows;
    }
    while (q.next()) {
        MessageRow r;
        r.id         = q.value(0).toLongLong();
        r.tsMs       = q.value(1).toLongLong();
        r.direction  = q.value(2).toString();
        r.topic      = q.value(3).toString();
        r.qos        = q.value(4).toInt();
        r.retain     = q.value(5).toInt() != 0;
        r.size       = q.value(6).toInt();
        r.payload    = q.value(7).toByteArray();
        rows.append(r);
    }
    return rows;
}

qint64 DatabaseManager::telemetryCount() const
{
    if (!m_open)
        return 0;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM telemetry")))
        return 0;
    return q.next() ? q.value(0).toLongLong() : 0;
}

qint64 DatabaseManager::messageCount() const
{
    if (!m_open)
        return 0;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM messages")))
        return 0;
    return q.next() ? q.value(0).toLongLong() : 0;
}

qint64 DatabaseManager::carCount() const
{
    if (!m_open)
        return 0;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM cars")))
        return 0;
    return q.next() ? q.value(0).toLongLong() : 0;
}

void DatabaseManager::trimTelemetry(qint64 keepSinceMs)
{
    if (!m_open || keepSinceMs <= 0)
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM telemetry WHERE ts_ms < :since"));
    q.bindValue(QStringLiteral(":since"), keepSinceMs);
    if (!q.exec())
        setError(QStringLiteral("裁剪遥测失败：%1").arg(q.lastError().text()));
}

void DatabaseManager::clearTelemetry()
{
    if (!m_open)
        return;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("DELETE FROM telemetry")))
        setError(QStringLiteral("清空遥测失败：%1").arg(q.lastError().text()));
}

void DatabaseManager::clearMessages()
{
    if (!m_open)
        return;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("DELETE FROM messages")))
        setError(QStringLiteral("清空消息失败：%1").arg(q.lastError().text()));
}

// ---------------------------------------------------------------------------
// 设备数据 / 事件查询
// ---------------------------------------------------------------------------
QList<DeviceDataRow> DatabaseManager::queryDeviceData(const QString &devSn,
                                                      qint64 sinceMs, int limit)
{
    QList<DeviceDataRow> rows;
    if (!m_open)
        return rows;

    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT id, ts_ms, dev_sn, mid, method, dev_ts, dev_ts_ms, rssi, "
        "       settlement, ang_x, ang_y, bat_vol, payload "
        "FROM device_data WHERE 1");
    if (!devSn.isEmpty())
        sql += QStringLiteral(" AND dev_sn = :sn");
    if (sinceMs > 0)
        sql += QStringLiteral(" AND ts_ms >= :since");
    sql += QStringLiteral(" ORDER BY ts_ms DESC LIMIT :lim");
    q.prepare(sql);
    if (!devSn.isEmpty())
        q.bindValue(QStringLiteral(":sn"), devSn);
    if (sinceMs > 0)
        q.bindValue(QStringLiteral(":since"), sinceMs);
    q.bindValue(QStringLiteral(":lim"), qMin(limit, 10000));

    if (!q.exec()) {
        setError(QStringLiteral("查询设备数据失败：%1").arg(q.lastError().text()));
        return rows;
    }
    while (q.next()) {
        DeviceDataRow r;
        r.id         = q.value(0).toLongLong();
        r.tsMs       = q.value(1).toLongLong();
        r.devSn      = q.value(2).toString();
        r.mid        = q.value(3).toLongLong();
        r.method     = q.value(4).toString();
        r.devTs      = q.value(5).toString();
        r.devTsMs    = q.value(6).toLongLong();
        r.rssi       = q.value(7).toInt();
        r.settlement = q.value(8).toDouble();
        r.angX       = q.value(9).toDouble();
        r.angY       = q.value(10).toDouble();
        r.batVol     = q.value(11).toDouble();
        r.payload    = q.value(12).toByteArray();
        rows.append(r);
    }
    return rows;
}

QList<DeviceEventRow> DatabaseManager::queryDeviceEvents(const QString &devSn,
                                                         qint64 sinceMs, int limit)
{
    QList<DeviceEventRow> rows;
    if (!m_open)
        return rows;

    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT id, ts_ms, dev_sn, mid, type, method, dev_ts, dev_ts_ms, "
        "       mac, ipv4, reason, payload "
        "FROM device_event WHERE 1");
    if (!devSn.isEmpty())
        sql += QStringLiteral(" AND dev_sn = :sn");
    if (sinceMs > 0)
        sql += QStringLiteral(" AND ts_ms >= :since");
    sql += QStringLiteral(" ORDER BY ts_ms DESC LIMIT :lim");
    q.prepare(sql);
    if (!devSn.isEmpty())
        q.bindValue(QStringLiteral(":sn"), devSn);
    if (sinceMs > 0)
        q.bindValue(QStringLiteral(":since"), sinceMs);
    q.bindValue(QStringLiteral(":lim"), qMin(limit, 10000));

    if (!q.exec()) {
        setError(QStringLiteral("查询设备事件失败：%1").arg(q.lastError().text()));
        return rows;
    }
    while (q.next()) {
        DeviceEventRow r;
        r.id      = q.value(0).toLongLong();
        r.tsMs    = q.value(1).toLongLong();
        r.devSn   = q.value(2).toString();
        r.mid     = q.value(3).toLongLong();
        r.type    = q.value(4).toString();
        r.method  = q.value(5).toString();
        r.devTs   = q.value(6).toString();
        r.devTsMs = q.value(7).toLongLong();
        r.mac     = q.value(8).toString();
        r.ipv4    = q.value(9).toString();
        r.reason  = q.value(10).toString();
        r.payload = q.value(11).toByteArray();
        rows.append(r);
    }
    return rows;
}

qint64 DatabaseManager::deviceDataCount() const
{
    if (!m_open)
        return 0;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM device_data")))
        return 0;
    return q.next() ? q.value(0).toLongLong() : 0;
}

qint64 DatabaseManager::deviceEventCount() const
{
    if (!m_open)
        return 0;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM device_event")))
        return 0;
    return q.next() ? q.value(0).toLongLong() : 0;
}

void DatabaseManager::clearDeviceData()
{
    if (!m_open)
        return;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("DELETE FROM device_data")))
        setError(QStringLiteral("清空设备数据失败：%1").arg(q.lastError().text()));
}

void DatabaseManager::clearDeviceEvents()
{
    if (!m_open)
        return;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("DELETE FROM device_event")))
        setError(QStringLiteral("清空设备事件失败：%1").arg(q.lastError().text()));
}
