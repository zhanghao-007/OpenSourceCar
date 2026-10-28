#ifndef MQTTBROKER_H
#define MQTTBROKER_H

#include "MqttTypes.h"
#include "TopicTree.h"

#include <QObject>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QDateTime>

QT_BEGIN_NAMESPACE
class QTcpServer;
QT_END_NAMESPACE

class MqttConnection;

namespace Mqtt {

// ---------------------------------------------------------------------------
// 持久会话（cleanSession=false 的客户端断开后保留）
// ---------------------------------------------------------------------------
struct SessionState {
    QString               clientId;
    bool                  cleanSession = true;
    QList<Subscription>   subscriptions;
    QList<PublishMessage> queuedMessages;   // 离线期间应投递的消息
    QDateTime             lastSeen;

    SessionState() = default;
};

// ---------------------------------------------------------------------------
// 服务端配置
// ---------------------------------------------------------------------------
struct BrokerConfig {
    quint16  port              = 1883;
    QString  bindAddress       = QStringLiteral("0.0.0.0");
    int      maxClients        = 500;
    bool     allowAnonymous    = true;
    bool     requireAuth       = false;
    QString  authUsername;
    QString  authPassword;
    int      maxQueuedMessages = 1000;     // 单个离线会话的队列上限
    bool     retainAvailable   = true;
    int      retransmitIntervalMs = 5000;  // QoS1/2 在途消息重传间隔
};

// ---------------------------------------------------------------------------
// 运行统计
// ---------------------------------------------------------------------------
struct BrokerStats {
    int       activeConnections = 0;
    int       subscriptions     = 0;
    int       retainedCount     = 0;
    int       sessions          = 0;
    qint64    messagesReceived  = 0;
    qint64    messagesSent      = 0;
    qint64    bytesReceived     = 0;
    qint64    bytesSent         = 0;
    QDateTime startedAt;

    BrokerStats() = default;
};

// ---------------------------------------------------------------------------
// MqttBroker —— 纯 Qt 实现的 MQTT 3.1.1 Broker
//
// 线程模型：本对象与它创建的全部 MqttConnection 均运行在**同一个工作线程**，
// 所有内部状态无锁；UI 线程只能通过信号接收数据拷贝、或通过（自动转为队列
// 调用的）public slot 下达命令。
// ---------------------------------------------------------------------------
class MqttBroker : public QObject
{
    Q_OBJECT
public:
    explicit MqttBroker(QObject *parent = nullptr);
    ~MqttBroker() override;

    void setConfig(const BrokerConfig &cfg);
    BrokerConfig config() const { return m_config; }
    bool isRunning() const { return m_running; }

signals:
    // ---- 面向 UI 的广播（跨线程自动排队）----
    void brokerStarted(quint16 port, const QString &address);
    void brokerStopped();
    void brokerError(const QString &text);
    void logLine(const QString &text, int level);      // level: 0信息 1警告 2错误
    void clientConnected(const Mqtt::ClientViewInfo &info);
    void clientDisconnected(const QString &clientId, const QString &reason);
    void clientsSnapshot(const QList<Mqtt::ClientViewInfo> &list);
    void messageRecord(const Mqtt::MessageRecord &rec);
    void statsUpdated(const Mqtt::BrokerStats &stats);
    void retainedListChanged(const QStringList &topics);
    // 原始报文嗅探：转发自 MqttConnection，用于运行日志的十六进制转储
    void packetSniffed(const QString &clientId, const QString &direction,
                       const QByteArray &raw);

public slots:
    bool start(quint16 port, const QString &bindAddress = QString());
    void stop();

    // 后台界面下发指令：服务端自身作为发布者
    void publishLocal(const QString &topic, const QByteArray &payload,
                      Mqtt::QoS qos = Mqtt::QoS0, bool retain = false);

    void kickClient(const QString &clientId);
    void refreshClients();          // 主动推送一次 clientsSnapshot
    void clearRetained(const QString &topic = QString());

private slots:
    void onNewConnection();
    void onConnReady(MqttConnection *conn);
    void onConnClosed(MqttConnection *conn);
    void onPublishReceived(MqttConnection *conn, const Mqtt::PublishMessage &msg);
    void onSubscribeRequested(MqttConnection *conn, quint16 packetId,
                              const QList<Mqtt::Subscription> &subs);
    void onUnsubscribeRequested(MqttConnection *conn, quint16 packetId,
                                const QStringList &filters);
    void onPubAck(MqttConnection *conn, quint16 packetId);
    void onPubRec(MqttConnection *conn, quint16 packetId);
    void onPubComp(MqttConnection *conn, quint16 packetId);
    void onProtocolError(MqttConnection *conn, const QString &reason);
    void onStatsTimer();

private:
    // ---- 核心路由 ----
    void routeMessage(const Mqtt::PublishMessage &msg);
    void deliverToClient(const QString &clientId, const Mqtt::PublishMessage &msg,
                         Mqtt::QoS effectiveQos);
    void publishWill(const Mqtt::WillMessage &will);

    MqttConnection *findConnection(const QString &clientId) const;
    void removeConnection(MqttConnection *conn);
    void emitStats();

    // 服务端自身发出的消息序号与报文标识符
    quint16 nextLocalPacketId();

private:
    QTcpServer *m_server = nullptr;
    BrokerConfig m_config;
    bool m_running = false;

    // 已建立 TCP、但尚未收到 CONNECT 的连接（还不知道 clientId）
    QList<MqttConnection *>       m_unidentified;
    // clientId -> 连接（仅在线）
    QHash<QString, MqttConnection *> m_connections;
    // clientId -> 持久会话（cleanSession=false 断开后保留）
    QHash<QString, SessionState>     m_sessions;
    // 保留消息：topic -> message
    QHash<QString, Mqtt::PublishMessage> m_retained;

    TopicTree m_topicTree;

    BrokerStats m_stats;
    quint16     m_localPacketId = 0;
    qint64      m_messageSeq = 0;
    class QTimer *m_statsTimer = nullptr;
};

} // namespace Mqtt

// 跨线程信号需要注册为元类型
Q_DECLARE_METATYPE(Mqtt::BrokerStats)
Q_DECLARE_METATYPE(Mqtt::BrokerConfig)
Q_DECLARE_METATYPE(QList<Mqtt::ClientViewInfo>)

#endif // MQTTBROKER_H
