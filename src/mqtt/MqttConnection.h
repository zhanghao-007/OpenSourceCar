#ifndef MQTTCONNECTION_H
#define MQTTCONNECTION_H

#include "MqttTypes.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QDateTime>
#include <QHash>
#include <QSet>

namespace Mqtt {
class MqttBroker;
}

QT_BEGIN_NAMESPACE
class QTcpSocket;
QT_END_NAMESPACE

// ---------------------------------------------------------------------------
// MqttConnection —— 服务端侧的一条 TCP 连接 + 会话状态机
//
// 生命周期：由 MqttBroker 在 newConnection 时创建，socket 的 parent 设为本对象。
// 连接断开后本对象通过 deleteLater 回收。
//
// 职责：
//   1. 从 TCP 字节流中做**报文分帧**（含跨 TCP 分片的半包/粘包处理）
//   2. 驱动 MQTT 状态机：CONNECT -> 订阅/发布 -> DISCONNECT
//   3. 维护本连接的 QoS1/QoS2 在途消息与重传计时
// ---------------------------------------------------------------------------
class MqttConnection : public QObject
{
    Q_OBJECT
public:
    MqttConnection(QTcpSocket *socket, Mqtt::MqttBroker *broker, QObject *parent = nullptr);
    ~MqttConnection() override;

    QString clientId() const { return m_clientId; }
    QString username() const { return m_username; }
    QString peerAddress() const;
    quint16 peerPort() const;
    bool    isAuthenticated() const { return m_authenticated; }
    QDateTime connectedAt() const { return m_connectedAt; }
    QDateTime lastSeen() const   { return m_lastSeen; }
    quint16  keepAlive() const  { return m_keepAlive; }
    bool     cleanSession() const { return m_cleanSession; }
    Mqtt::ConnectionStats stats() const { return m_stats; }

    // 客户端是否发送过 DISCONNECT（用于判断是否要触发遗嘱）
    bool     cleanDisconnect() const { return m_cleanDisconnect; }
    const Mqtt::WillMessage &will() const { return m_will; }
    // 触发遗嘱后由 Broker 调用，避免重复发布
    void     markWillConsumed() { m_will.valid = false; }

    // UI 展示用快照
    Mqtt::ClientViewInfo viewInfo() const;

    // 服务端向本连接投递一条消息。effectiveQos 是已经过"订阅 QoS 与发布 QoS 取小"后的值
    void deliverPublish(const Mqtt::PublishMessage &msg, Mqtt::QoS effectiveQos);

    // 服务端主动踢下线
    void kick(const QString &reason);

signals:
    // 以下信号由 MqttBroker 连接，用于驱动路由与 UI 广播
    void connectionReady(MqttConnection *conn);
    void connectionClosed(MqttConnection *conn);
    void packetReceived(MqttConnection *conn);          // 每收到一个完整报文触发一次（统计用）
    void subscribeRequested(MqttConnection *conn, quint16 packetId,
                            const QList<Mqtt::Subscription> &subs);
    void unsubscribeRequested(MqttConnection *conn, quint16 packetId,
                              const QStringList &filters);
    void publishReceived(MqttConnection *conn, const Mqtt::PublishMessage &msg);
    void pubAckReceived(MqttConnection *conn, quint16 packetId);
    void pubRecReceived(MqttConnection *conn, quint16 packetId);
    void pubCompReceived(MqttConnection *conn, quint16 packetId);
    void protocolError(MqttConnection *conn, const QString &reason);
    // 原始报文嗅探：direction 为 "IN"（收到）/"OUT"（发出），raw 是完整报文字节
    void packetSniffed(const QString &clientId, const QString &direction,
                       const QByteArray &raw);

public slots:
    // 由 Broker 在处理完 SUBSCRIBE 后回调，用于回复 SUBACK
    void sendSubAck(quint16 packetId, const QList<quint8> &returnCodes);
    void sendUnsubAck(quint16 packetId);
    void sendConnectAck(bool sessionPresent, Mqtt::ConnectReturnCode rc);

private slots:
    void onReadyRead();
    void onSocketDisconnected();
    void onSocketError();
    void onKeepAliveTimeout();
    void onRetransmitTimeout();

private:
    // ---- 报文分帧 ----
    // 返回 true 表示已从 m_rxBuffer 头部切出一个完整报文，body 为去掉固定头后的内容。
    // 若 raw 非空，同时输出**包含固定头在内**的完整原始字节，供日志转储使用
    bool tryTakePacket(quint8 &type, quint8 &flags, QByteArray &body,
                       QByteArray *raw = nullptr);

    // ---- 各报文处理 ----
    void handleConnect(quint8 flags, const QByteArray &body);
    void handlePublish(quint8 flags, const QByteArray &body);
    void handlePubAck(const QByteArray &body);
    void handlePubRec(const QByteArray &body);
    void handlePubRel(const QByteArray &body);
    void handlePubComp(const QByteArray &body);
    void handleSubscribe(quint8 flags, const QByteArray &body);
    void handleUnsubscribe(quint8 flags, const QByteArray &body);

    // ---- 工具 ----
    quint16 nextPacketId();
    void    writeRaw(const QByteArray &data);
    void    closeWithError(const QString &reason);

private:
    QTcpSocket           *m_socket;
    Mqtt::MqttBroker     *m_broker;

    QByteArray            m_rxBuffer;     // 未分帧的接收缓冲
    bool                  m_headerParsed = false;
    int                   m_expectRemaining = 0;  // 当前报文剩余长度（已解析出时 >=0）
    quint8                m_pendingType  = 0;
    quint8                m_pendingFlags = 0;

    QString               m_clientId;
    QString               m_username;
    bool                  m_authenticated = false;
    bool                  m_connectReceived = false;
    bool                  m_cleanDisconnect = false;
    quint16               m_keepAlive = 60;
    bool                  m_cleanSession = true;
    Mqtt::WillMessage     m_will;
    QDateTime             m_connectedAt;
    QDateTime             m_lastSeen;

    quint16               m_packetIdCounter = 0;
    // 已发出、等待 PUBACK/PUBCOMP 的在途消息
    QHash<quint16, Mqtt::PublishMessage> m_inflightOut;
    QHash<quint16, int>                  m_inflightAttempts;
    // QoS2 发送侧：已收到 PUBREC、已发 PUBREL、等待 PUBCOMP
    QSet<quint16>                        m_awaitingPubComp;
    // QoS2 接收侧：已收到 PUBLISH、已发 PUBREC、等待 PUBREL
    QHash<quint16, Mqtt::PublishMessage> m_qos2Inbound;

    QTimer               *m_keepAliveTimer = nullptr;
    QTimer               *m_retransmitTimer = nullptr;
    Mqtt::ConnectionStats m_stats;
};

#endif // MQTTCONNECTION_H
