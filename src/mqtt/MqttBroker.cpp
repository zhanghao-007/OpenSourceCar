#include "MqttBroker.h"
#include "MqttConnection.h"
#include "MqttPacket.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QDateTime>

namespace Mqtt {

MqttBroker::MqttBroker(QObject *parent)
    : QObject(parent)
{
}

MqttBroker::~MqttBroker()
{
    stop();
}

void MqttBroker::setConfig(const BrokerConfig &cfg)
{
    m_config = cfg;
}

// ---------------------------------------------------------------------------
// 启动 / 停止
// ---------------------------------------------------------------------------
bool MqttBroker::start(quint16 port, const QString &bindAddress)
{
    if (m_running) {
        emit brokerError(QStringLiteral("服务端已在运行"));
        return false;
    }
    if (!bindAddress.isEmpty())
        m_config.bindAddress = bindAddress;
    if (port != 0)
        m_config.port = port;

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &MqttBroker::onNewConnection);

    const QHostAddress addr = (m_config.bindAddress == QLatin1String("0.0.0.0"))
                                  ? QHostAddress::Any
                                  : QHostAddress(m_config.bindAddress);
    if (!m_server->listen(addr, m_config.port)) {
        const QString err = QStringLiteral("监听 %1:%2 失败：%3")
                                .arg(m_config.bindAddress)
                                .arg(m_config.port)
                                .arg(m_server->errorString());
        emit brokerError(err);
        delete m_server;
        m_server = nullptr;
        return false;
    }

    m_running = true;
    m_stats = BrokerStats();
    m_stats.startedAt = QDateTime::currentDateTime();

    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &MqttBroker::onStatsTimer);
    m_statsTimer->start(1000);

    emit brokerStarted(m_config.port, m_config.bindAddress);
    emit logLine(QStringLiteral("MQTT 服务端已启动，监听 %1:%2")
                     .arg(m_config.bindAddress).arg(m_config.port), 0);
    return true;
}

void MqttBroker::stop()
{
    if (!m_running)
        return;

    if (m_statsTimer) {
        m_statsTimer->stop();
        m_statsTimer->deleteLater();
        m_statsTimer = nullptr;
    }
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    // 断开全部连接（这里的 deleteLater 会在本线程事件循环里执行）
    foreach (MqttConnection *conn, m_unidentified) {
        conn->kick(QStringLiteral("服务端停止"));
        conn->deleteLater();
    }
    m_unidentified.clear();

    const QList<MqttConnection *> conns = m_connections.values();
    foreach (MqttConnection *conn, conns) {
        conn->kick(QStringLiteral("服务端停止"));
        conn->deleteLater();
    }
    m_connections.clear();

    m_running = false;
    emit brokerStopped();
    emit logLine(QStringLiteral("MQTT 服务端已停止"), 0);
}

// ---------------------------------------------------------------------------
// 连接管理
// ---------------------------------------------------------------------------
void MqttBroker::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            break;

        if (m_unidentified.size() + m_connections.size() >= m_config.maxClients) {
            emit logLine(QStringLiteral("连接数已达上限 %1，拒绝 %2")
                             .arg(m_config.maxClients)
                             .arg(socket->peerAddress().toString()), 1);
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }

        MqttConnection *conn = new MqttConnection(socket, this, this);
        connect(conn, &MqttConnection::connectionReady,
                this, &MqttBroker::onConnReady);
        connect(conn, &MqttConnection::connectionClosed,
                this, &MqttBroker::onConnClosed);
        connect(conn, &MqttConnection::publishReceived,
                this, &MqttBroker::onPublishReceived);
        connect(conn, &MqttConnection::subscribeRequested,
                this, &MqttBroker::onSubscribeRequested);
        connect(conn, &MqttConnection::unsubscribeRequested,
                this, &MqttBroker::onUnsubscribeRequested);
        connect(conn, &MqttConnection::pubAckReceived,
                this, &MqttBroker::onPubAck);
        connect(conn, &MqttConnection::pubRecReceived,
                this, &MqttBroker::onPubRec);
        connect(conn, &MqttConnection::pubCompReceived,
                this, &MqttBroker::onPubComp);
        connect(conn, &MqttConnection::protocolError,
                this, &MqttBroker::onProtocolError);
        connect(conn, &MqttConnection::packetSniffed,
                this, &MqttBroker::packetSniffed);

        m_unidentified.append(conn);
    }
}

void MqttBroker::onConnReady(MqttConnection *conn)
{
    m_unidentified.removeAll(conn);

    const QString clientId = conn->clientId();

    // MQTT-3.1.4：同一 ClientID 的旧连接必须被服务端断开
    MqttConnection *old = m_connections.value(clientId, nullptr);
    if (old && old != conn) {
        emit logLine(QStringLiteral("ClientID 重复，踢掉旧连接：%1").arg(clientId), 1);
        old->kick(QStringLiteral("ClientID 重复"));
        m_connections.remove(clientId);
        old->deleteLater();
    }

    bool sessionPresent = false;
    QHash<QString, SessionState>::iterator sit = m_sessions.find(clientId);
    if (conn->cleanSession()) {
        // 清理会话：丢弃此前的持久状态
        m_topicTree.removeClient(clientId);
        if (sit != m_sessions.end())
            m_sessions.erase(sit);
    } else if (sit != m_sessions.end()) {
        sessionPresent = true;
        // 恢复订阅
        SessionState &st = sit.value();
        for (int i = 0; i < st.subscriptions.size(); ++i)
            m_topicTree.subscribe(clientId, st.subscriptions.at(i).filter,
                                  st.subscriptions.at(i).qos);
    } else {
        SessionState st;
        st.clientId     = clientId;
        st.cleanSession = false;
        st.lastSeen     = QDateTime::currentDateTime();
        m_sessions.insert(clientId, st);
    }

    m_connections.insert(clientId, conn);

    ClientViewInfo info = conn->viewInfo();
    info.subscriptions = m_topicTree.subscriptionsOf(clientId);

    if (sessionPresent) {
        // 重放离线期间的排队消息
        SessionState &st = m_sessions[clientId];
        for (int i = 0; i < st.queuedMessages.size(); ++i) {
            const PublishMessage &qm = st.queuedMessages.at(i);
            conn->deliverPublish(qm, qm.qos);
            m_stats.messagesSent++;
        }
        if (!st.queuedMessages.isEmpty()) {
            emit logLine(QStringLiteral("会话恢复：%1，重放 %2 条离线消息")
                             .arg(clientId).arg(st.queuedMessages.size()), 0);
        }
        st.queuedMessages.clear();
    }

    emit clientConnected(info);
    emit logLine(QStringLiteral("客户端已连接：%1 (%2)")
                     .arg(clientId).arg(info.peerAddress), 0);
    refreshClients();
}

void MqttBroker::onConnClosed(MqttConnection *conn)
{
    m_unidentified.removeAll(conn);

    const QString clientId = conn->clientId();
    if (!clientId.isEmpty())
        m_connections.remove(clientId);

    // 非正常断开 → 触发遗嘱
    if (!conn->cleanDisconnect() && conn->will().valid)
        publishWill(conn->will());

    if (clientId.isEmpty()) {
        conn->deleteLater();
        return;
    }

    if (conn->cleanSession()) {
        m_topicTree.removeClient(clientId);
        m_sessions.remove(clientId);
    } else {
        // 保留持久会话：记住订阅，后续离线消息入队
        SessionState &st = m_sessions[clientId];
        st.clientId     = clientId;
        st.cleanSession = false;
        st.subscriptions = m_topicTree.subscriptionsOf(clientId);
        st.lastSeen     = QDateTime::currentDateTime();
    }

    emit clientDisconnected(clientId,
                            conn->cleanDisconnect()
                                ? QStringLiteral("客户端主动断开")
                                : QStringLiteral("连接中断"));
    emit logLine(QStringLiteral("客户端断开：%1").arg(clientId), 0);

    conn->deleteLater();
    refreshClients();
}

void MqttBroker::removeConnection(MqttConnection *conn)
{
    if (!conn)
        return;
    m_unidentified.removeAll(conn);
    m_connections.remove(conn->clientId());
}

MqttConnection *MqttBroker::findConnection(const QString &clientId) const
{
    return m_connections.value(clientId, nullptr);
}

void MqttBroker::kickClient(const QString &clientId)
{
    MqttConnection *conn = m_connections.value(clientId, nullptr);
    if (!conn) {
        emit logLine(QStringLiteral("踢线失败：客户端 %1 不在线").arg(clientId), 1);
        return;
    }
    conn->kick(QStringLiteral("后台强制踢线"));
    emit logLine(QStringLiteral("已踢出客户端：%1").arg(clientId), 0);
}

// ---------------------------------------------------------------------------
// 消息路由
// ---------------------------------------------------------------------------
void MqttBroker::onPublishReceived(MqttConnection *conn, const PublishMessage &msg)
{
    if (!conn)
        return;

    // ---- 保留消息处理 ----
    if (msg.retain && m_config.retainAvailable) {
        if (msg.payload.isEmpty()) {
            if (m_retained.remove(msg.topic) > 0) {
                emit logLine(QStringLiteral("删除保留消息：%1").arg(msg.topic), 0);
                emit retainedListChanged(m_retained.keys());
            }
        } else {
            if (msg.qos == QoS0) {
                // MQTT-2.1.2.1：QoS0 的保留消息应被服务端丢弃
                m_retained.remove(msg.topic);
            } else {
                m_retained.insert(msg.topic, msg);
                emit retainedListChanged(m_retained.keys());
            }
        }
    }

    // ---- 记录消息流水 ----
    MessageRecord rec;
    rec.seq       = ++m_messageSeq;
    rec.timestamp = msg.timestamp;
    rec.topic     = msg.topic;
    rec.payload   = msg.payload;
    rec.qos       = msg.qos;
    rec.retain    = msg.retain;
    rec.direction = QStringLiteral("IN");
    rec.clientId  = msg.sourceClientId;
    emit messageRecord(rec);

    m_stats.messagesReceived++;
    m_stats.bytesReceived += msg.payload.size();

    routeMessage(msg);
}

void MqttBroker::routeMessage(const PublishMessage &msg)
{
    const QList<MatchResult> targets = m_topicTree.match(msg.topic);
    for (int i = 0; i < targets.size(); ++i) {
        const MatchResult &r = targets.at(i);
        // MQTT-3.8.4：投递 QoS = min(发布 QoS, 订阅 QoS)
        const QoS effective = static_cast<QoS>(qMin(static_cast<int>(msg.qos),
                                                    static_cast<int>(r.qos)));
        deliverToClient(r.clientId, msg, effective);
    }
}

void MqttBroker::deliverToClient(const QString &clientId, const PublishMessage &msg,
                                 QoS effectiveQos)
{
    MqttConnection *conn = m_connections.value(clientId, nullptr);
    if (conn) {
        conn->deliverPublish(msg, effectiveQos);
        m_stats.messagesSent++;
        m_stats.bytesSent += msg.payload.size();

        MessageRecord rec;
        rec.seq       = ++m_messageSeq;
        rec.timestamp = QDateTime::currentDateTime();
        rec.topic     = msg.topic;
        rec.payload   = msg.payload;
        rec.qos       = effectiveQos;
        rec.retain    = msg.retain;
        rec.direction = QStringLiteral("OUT");
        rec.clientId  = clientId;
        emit messageRecord(rec);
        return;
    }

    // 离线：仅在持久会话下入队，且 QoS 必须 > 0
    QHash<QString, SessionState>::iterator it = m_sessions.find(clientId);
    if (it == m_sessions.end() || effectiveQos == QoS0)
        return;

    SessionState &st = it.value();
    if (st.queuedMessages.size() >= m_config.maxQueuedMessages) {
        st.queuedMessages.removeFirst();   // 丢弃最旧的一条
    }
    PublishMessage copy = msg;
    copy.qos = effectiveQos;
    st.queuedMessages.append(copy);
}

void MqttBroker::publishWill(const WillMessage &will)
{
    if (!will.valid || will.topic.isEmpty())
        return;

    PublishMessage msg;
    msg.topic          = will.topic;
    msg.payload        = will.payload;
    msg.qos            = will.qos;
    msg.retain         = will.retain;
    msg.sourceClientId = QStringLiteral("<will>");

    emit logLine(QStringLiteral("触发遗嘱消息：%1").arg(will.topic), 1);

    if (msg.retain && m_config.retainAvailable)
        m_retained.insert(msg.topic, msg);

    MessageRecord rec;
    rec.seq       = ++m_messageSeq;
    rec.timestamp = QDateTime::currentDateTime();
    rec.topic     = msg.topic;
    rec.payload   = msg.payload;
    rec.qos       = msg.qos;
    rec.retain    = msg.retain;
    rec.direction = QStringLiteral("IN");
    rec.clientId  = msg.sourceClientId;
    emit messageRecord(rec);

    routeMessage(msg);
}

void MqttBroker::publishLocal(const QString &topic, const QByteArray &payload,
                              QoS qos, bool retain)
{
    if (topic.isEmpty()) {
        emit logLine(QStringLiteral("下发失败：主题为空"), 2);
        return;
    }
    if (!isValidTopicName(topic, false)) {
        emit logLine(QStringLiteral("下发失败：主题名不合法（不能含通配符）%1").arg(topic), 2);
        return;
    }

    PublishMessage msg;
    msg.topic          = topic;
    msg.payload        = payload;
    msg.qos            = qos;
    msg.retain         = retain;
    msg.packetId       = nextLocalPacketId();
    msg.sourceClientId = QStringLiteral("<server>");

    if (retain && m_config.retainAvailable) {
        if (payload.isEmpty())
            m_retained.remove(topic);
        else
            m_retained.insert(topic, msg);
        emit retainedListChanged(m_retained.keys());
    }

    MessageRecord rec;
    rec.seq       = ++m_messageSeq;
    rec.timestamp = QDateTime::currentDateTime();
    rec.topic     = topic;
    rec.payload   = payload;
    rec.qos       = qos;
    rec.retain    = retain;
    rec.direction = QStringLiteral("OUT");
    rec.clientId  = msg.sourceClientId;
    emit messageRecord(rec);

    m_stats.messagesSent++;
    m_stats.bytesSent += payload.size();

    routeMessage(msg);
}

// ---------------------------------------------------------------------------
// 订阅
// ---------------------------------------------------------------------------
void MqttBroker::onSubscribeRequested(MqttConnection *conn, quint16 packetId,
                                      const QList<Subscription> &subs)
{
    Q_UNUSED(packetId)   // SUBACK 由 MqttConnection 自行回复，这里不需要报文标识符
    if (!conn)
        return;
    const QString clientId = conn->clientId();

    for (int i = 0; i < subs.size(); ++i) {
        const Subscription &s = subs.at(i);
        m_topicTree.subscribe(clientId, s.filter, s.qos);

        // 新订阅立即投递命中的保留消息（MQTT-3.8.4）
        QHashIterator<QString, PublishMessage> it(m_retained);
        while (it.hasNext()) {
            it.next();
            if (!topicFilterMatch(s.filter, it.key()))
                continue;
            const PublishMessage &rm = it.value();
            const QoS effective = static_cast<QoS>(qMin(static_cast<int>(rm.qos),
                                                         static_cast<int>(s.qos)));
            conn->deliverPublish(rm, effective);
            m_stats.messagesSent++;
        }
    }

    // 更新持久会话里的订阅快照
    QHash<QString, SessionState>::iterator sit = m_sessions.find(clientId);
    if (sit != m_sessions.end())
        sit.value().subscriptions = m_topicTree.subscriptionsOf(clientId);

    emit logLine(QStringLiteral("%1 订阅 %2").arg(clientId).arg(subs.size()), 0);
    refreshClients();
}

void MqttBroker::onUnsubscribeRequested(MqttConnection *conn, quint16 packetId,
                                        const QStringList &filters)
{
    Q_UNUSED(packetId)
    if (!conn)
        return;
    const QString clientId = conn->clientId();
    for (int i = 0; i < filters.size(); ++i)
        m_topicTree.unsubscribe(clientId, filters.at(i));

    QHash<QString, SessionState>::iterator sit = m_sessions.find(clientId);
    if (sit != m_sessions.end())
        sit.value().subscriptions = m_topicTree.subscriptionsOf(clientId);

    emit logLine(QStringLiteral("%1 退订 %2 个主题").arg(clientId).arg(filters.size()), 0);
    refreshClients();
}

// ---------------------------------------------------------------------------
// QoS 确认（目前仅做清理与计数，重传由 MqttConnection 自行完成）
// ---------------------------------------------------------------------------
void MqttBroker::onPubAck(MqttConnection *conn, quint16 packetId)
{
    Q_UNUSED(conn)
    Q_UNUSED(packetId)
}

void MqttBroker::onPubRec(MqttConnection *conn, quint16 packetId)
{
    Q_UNUSED(conn)
    Q_UNUSED(packetId)
}

void MqttBroker::onPubComp(MqttConnection *conn, quint16 packetId)
{
    Q_UNUSED(conn)
    Q_UNUSED(packetId)
}

void MqttBroker::onProtocolError(MqttConnection *conn, const QString &reason)
{
    const QString id = conn ? conn->clientId() : QStringLiteral("<unknown>");
    emit logLine(QStringLiteral("协议错误 [%1]：%2").arg(id).arg(reason), 2);
}

// ---------------------------------------------------------------------------
// 其它
// ---------------------------------------------------------------------------
void MqttBroker::refreshClients()
{
    QList<ClientViewInfo> list;
    QHashIterator<QString, MqttConnection *> it(m_connections);
    while (it.hasNext()) {
        it.next();
        ClientViewInfo info = it.value()->viewInfo();
        info.subscriptions  = m_topicTree.subscriptionsOf(it.key());
        list.append(info);
    }
    emit clientsSnapshot(list);
}

void MqttBroker::clearRetained(const QString &topic)
{
    if (topic.isEmpty())
        m_retained.clear();
    else
        m_retained.remove(topic);
    emit retainedListChanged(m_retained.keys());
}

quint16 MqttBroker::nextLocalPacketId()
{
    ++m_localPacketId;
    if (m_localPacketId == 0)
        ++m_localPacketId;
    return m_localPacketId;
}

void MqttBroker::onStatsTimer()
{
    emitStats();
}

void MqttBroker::emitStats()
{
    m_stats.activeConnections = m_connections.size();
    m_stats.subscriptions     = m_topicTree.subscriptionCount();
    m_stats.retainedCount     = m_retained.size();
    m_stats.sessions          = m_sessions.size();
    emit statsUpdated(m_stats);
}

} // namespace Mqtt
