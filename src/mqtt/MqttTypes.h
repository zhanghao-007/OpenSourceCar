#ifndef MQTTPROTOCOL_H
#define MQTTPROTOCOL_H

#include <QString>
#include <QByteArray>
#include <QList>
#include <QDateTime>
#include <QHash>
#include <QMetaType>

namespace Mqtt {

// ---------------------------------------------------------------------------
// MQTT 3.1.1 协议常量
// ---------------------------------------------------------------------------
static const char     PROTOCOL_NAME[] = "MQTT";
static const quint8   PROTOCOL_LEVEL  = 4;          // MQTT 3.1.1
static const int      MAX_PACKET_SIZE = 268435455;  // 256MB，剩余长度上限
static const int      MAX_CLIENT_ID_LEN = 65535;

// ---------------------------------------------------------------------------
// 控制报文类型 (MQTT-3.1.1 规范 2.2)
// ---------------------------------------------------------------------------
enum PacketType {
    PACKET_CONNECT     = 1,
    PACKET_CONNACK     = 2,
    PACKET_PUBLISH     = 3,
    PACKET_PUBACK      = 4,
    PACKET_PUBREC      = 5,
    PACKET_PUBREL      = 6,
    PACKET_PUBCOMP     = 7,
    PACKET_SUBSCRIBE   = 8,
    PACKET_SUBACK      = 9,
    PACKET_UNSUBSCRIBE = 10,
    PACKET_UNSUBACK    = 11,
    PACKET_PINGREQ     = 12,
    PACKET_PINGRESP    = 13,
    PACKET_DISCONNECT  = 14
};

// ---------------------------------------------------------------------------
// 服务质量等级
// ---------------------------------------------------------------------------
enum QoS {
    QoS0 = 0,   // 至多一次
    QoS1 = 1,   // 至少一次
    QoS2 = 2,   // 只有一次
    QoS_INVALID = 0x80  // SUBACK 中表示订阅失败
};

inline QString qosToString(QoS q)
{
    switch (q) {
    case QoS0: return QStringLiteral("0");
    case QoS1: return QStringLiteral("1");
    case QoS2: return QStringLiteral("2");
    default:   return QStringLiteral("FAIL");
    }
}

// ---------------------------------------------------------------------------
// CONNACK 返回码
// ---------------------------------------------------------------------------
enum ConnectReturnCode {
    CONN_ACCEPTED                    = 0,
    CONN_REFUSED_PROTOCOL_VERSION   = 1,
    CONN_REFUSED_IDENTIFIER_REJECT  = 2,
    CONN_REFUSED_SERVER_UNAVAILABLE = 3,
    CONN_REFUSED_BAD_USERNAME_PASS   = 4,
    CONN_REFUSED_NOT_AUTHORIZED      = 5
};

inline QString connAckText(ConnectReturnCode c)
{
    switch (c) {
    case CONN_ACCEPTED:                    return QStringLiteral("连接已接受");
    case CONN_REFUSED_PROTOCOL_VERSION:   return QStringLiteral("拒绝：不支持的协议版本");
    case CONN_REFUSED_IDENTIFIER_REJECT:  return QStringLiteral("拒绝：ClientID 不合法");
    case CONN_REFUSED_SERVER_UNAVAILABLE:  return QStringLiteral("拒绝：服务端不可用");
    case CONN_REFUSED_BAD_USERNAME_PASS:   return QStringLiteral("拒绝：用户名或密码错误");
    case CONN_REFUSED_NOT_AUTHORIZED:      return QStringLiteral("拒绝：未授权");
    }
    return QStringLiteral("未知返回码");
}

// ---------------------------------------------------------------------------
// 主题合法性校验：不允许通配符，不允许空主题 (MQTT-4.7)
// ---------------------------------------------------------------------------
bool isValidTopicName(const QString &topic, bool allowWildcard = false);

// ---------------------------------------------------------------------------
// 主题过滤器匹配：支持 '+' 单层、'#' 多层通配符
//   filter="car/+/telemetry"  => 匹配 "car/001/telemetry"
//   filter="car/#"            => 匹配 "car/001/telemetry"
//   以 '#' 或 '+' 开头的共享订阅形如 "$share/group/car/#" 需剥离前缀
// ---------------------------------------------------------------------------
bool topicFilterMatch(const QString &filter, const QString &topic);

// 把 "$share/<group>/<rest>" 还原成真正的过滤器 "<rest>"
QString stripSharePrefix(const QString &filter);

// ---------------------------------------------------------------------------
// 遗嘱消息
// ---------------------------------------------------------------------------
struct WillMessage {
    bool      valid   = false;
    QString   topic;
    QByteArray payload;
    QoS       qos     = QoS0;
    bool      retain  = false;

    WillMessage() = default;
};

// ---------------------------------------------------------------------------
// CONNECT 报文解析结果
// ---------------------------------------------------------------------------
struct ConnectInfo {
    QString     clientId;
    QString     username;
    QByteArray  password;
    quint16     keepAlive = 60;
    bool        cleanSession = true;
    WillMessage will;
    quint8      protocolLevel = 0;
    QString     protocolName;

    // CONNECT 标志位中声明"后续存在该字段"的开关（与字段是否为空是两回事）
    bool        usernameFlag = false;
    bool        passwordFlag = false;

    bool hasUsername() const { return !username.isEmpty(); }
    bool hasPassword() const { return !password.isEmpty(); }
};

// ---------------------------------------------------------------------------
// PUBLISH 消息（内部流转用，已解码）
// ---------------------------------------------------------------------------
struct PublishMessage {
    QString    topic;
    QByteArray payload;
    QoS        qos      = QoS0;
    bool       retain   = false;
    bool       dup      = false;
    quint16    packetId = 0;
    QString    sourceClientId;    // 空表示来自服务端自身（后台下发指令）
    QDateTime  timestamp;

    PublishMessage() : timestamp(QDateTime::currentDateTime()) {}
};

// ---------------------------------------------------------------------------
// 订阅项
// ---------------------------------------------------------------------------
struct Subscription {
    QString filter;
    QoS     qos = QoS0;

    Subscription() = default;
    Subscription(const QString &f, QoS q) : filter(f), qos(q) {}
};

// ---------------------------------------------------------------------------
// 单条连接的运行统计
// ---------------------------------------------------------------------------
struct ConnectionStats {
    qint64  bytesReceived = 0;
    qint64  bytesSent     = 0;
    qint64  msgReceived   = 0;
    qint64  msgSent       = 0;
};

// ---------------------------------------------------------------------------
// 服务端对外广播的客户端视图（UI 直接消费）
// ---------------------------------------------------------------------------
struct ClientViewInfo {
    QString   clientId;
    QString   username;
    QString   peerAddress;      // "192.168.1.20:54321"
    quint16   keepAlive   = 0;
    bool      cleanSession = true;
    bool      connected   = true;
    QDateTime connectedAt;
    QDateTime lastSeen;
    QList<Subscription> subscriptions;
    ConnectionStats stats;
};

// ---------------------------------------------------------------------------
// 消息流水（UI 消息监控页消费）
// ---------------------------------------------------------------------------
struct MessageRecord {
    qint64     seq       = 0;
    QDateTime  timestamp;
    QString    topic;
    QByteArray payload;
    QoS        qos       = QoS0;
    bool       retain    = false;
    QString    direction;    // "IN"（上行）/"OUT"（下行）
    QString    clientId;
};

} // namespace Mqtt

Q_DECLARE_METATYPE(Mqtt::MessageRecord)
Q_DECLARE_METATYPE(Mqtt::ClientViewInfo)

#endif // MQTTPROTOCOL_H
