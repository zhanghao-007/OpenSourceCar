#include "MqttConnection.h"
#include "MqttPacket.h"
#include "MqttBroker.h"
#include <QTcpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QDebug>

using namespace Mqtt;

MqttConnection::MqttConnection(QTcpSocket *socket, Mqtt::MqttBroker *broker, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
    , m_broker(broker)
    , m_connectedAt(QDateTime::currentDateTime())
    , m_lastSeen(QDateTime::currentDateTime())
{
    m_socket->setParent(this);
    m_keepAliveTimer = new QTimer(this);
    m_retransmitTimer = new QTimer(this);

    connect(m_socket, &QTcpSocket::readyRead, this, &MqttConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &MqttConnection::onSocketDisconnected);
    connect(m_socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onSocketError()));
}

MqttConnection::~MqttConnection()
{
    m_keepAliveTimer->stop();
    m_retransmitTimer->stop();
}

QString MqttConnection::peerAddress() const
{
    QString address = m_socket->peerAddress().toString();
    if (address.startsWith("::ffff:")) {
        address = address.mid(7);
    }
    return address;
}

quint16 MqttConnection::peerPort() const
{
    return m_socket->peerPort();
}

ClientViewInfo MqttConnection::viewInfo() const
{
    ClientViewInfo info;
    info.clientId = m_clientId;
    info.username = m_username;
    info.peerAddress = peerAddress() + ":" + QString::number(peerPort());
    info.keepAlive = m_keepAlive;
    info.cleanSession = m_cleanSession;
    info.connected = true;
    info.connectedAt = m_connectedAt;
    info.lastSeen = m_lastSeen;
    info.stats = m_stats;
    return info;
}

bool MqttConnection::tryTakePacket(quint8 &type, quint8 &flags, QByteArray &body,
                                   QByteArray *raw)
{
    if (m_rxBuffer.size() < 1) {
        return false;
    }

    type = MqttPacket::typeFromHeader(m_rxBuffer[0]);
    flags = MqttPacket::flagsFromHeader(m_rxBuffer[0]);

    int bytesUsed = 0;
    int remaining = MqttPacket::decodeRemainingLength(m_rxBuffer.constData() + 1, m_rxBuffer.size() - 1, bytesUsed);
    if (remaining == -1) {
        return false;
    }
    if (remaining == -2) {
        closeWithError("剩余长度编码非法");
        return false;
    }

    if (m_rxBuffer.size() - 1 - bytesUsed < remaining) {
        return false;
    }

    body = m_rxBuffer.mid(1 + bytesUsed, remaining);
    if (raw)
        *raw = m_rxBuffer.left(1 + bytesUsed + remaining);   // 含固定头的完整报文
    m_rxBuffer.remove(0, 1 + bytesUsed + remaining);
    return true;
}

void MqttConnection::onReadyRead()
{
    // BUGFIX: 必须把读到的数据追加进接收缓冲，否则报文永远拼不完整
    const QByteArray chunk = m_socket->readAll();
    m_stats.bytesReceived += chunk.size();
    m_rxBuffer.append(chunk);

    bool errorOccurred = false;

    // BUGFIX: body 必须是独立的局部变量。原实现把 m_rxBuffer 自身当作出参传入，
    // 导致切包后 body 与缓冲区变成同一个对象，解析出的报文体是错的
    quint8 type = 0;
    quint8 flags = 0;
    QByteArray body;
    QByteArray raw;

    while (!errorOccurred && tryTakePacket(type, flags, body, &raw)) {
        // 嗅探原始报文（含固定头），供运行日志做十六进制转储
        emit packetSniffed(m_clientId.isEmpty() ? QStringLiteral("<未认证>") : m_clientId,
                           QStringLiteral("IN"), raw);

        switch (type) {
        case PACKET_CONNECT:
            if (m_connectReceived) {
                closeWithError(QStringLiteral("重复收到 CONNECT"));
                errorOccurred = true;
            } else {
                handleConnect(flags, body);
            }
            break;
        case PACKET_PUBLISH:
            handlePublish(flags, body);
            break;
        case PACKET_PUBACK:
            handlePubAck(body);
            break;
        case PACKET_PUBREC:
            handlePubRec(body);
            break;
        case PACKET_PUBREL:
            handlePubRel(body);
            break;
        case PACKET_PUBCOMP:
            handlePubComp(body);
            break;
        case PACKET_SUBSCRIBE:
            handleSubscribe(flags, body);
            break;
        case PACKET_UNSUBSCRIBE:
            handleUnsubscribe(flags, body);
            break;
        case PACKET_PINGREQ:
            writeRaw(MqttPacket::encodePingResp());
            break;
        case PACKET_DISCONNECT:
            m_cleanDisconnect = true;
            m_socket->disconnectFromHost();
            break;
        default:
            closeWithError(QStringLiteral("未知报文类型 %1").arg(type));
            errorOccurred = true;
            break;
        }

        if (!errorOccurred) {
            m_lastSeen = QDateTime::currentDateTime();
            m_stats.msgReceived++;
            // 仅在 CONNECT 之后（定时器已设好 interval）才重置心跳计时，
            // 否则会以 interval=0 启动，导致 timeout 被疯狂触发
            if (m_keepAliveTimer->isActive())
                m_keepAliveTimer->start();
        }
    }
}

void MqttConnection::handleConnect(quint8 flags, const QByteArray &body)
{
    Q_UNUSED(flags)   // CONNECT 固定头标志位必须为 0，无需解析
    ConnectInfo info;
    QString err;
    if (!MqttPacket::parseConnect(body, info, &err)) {
        closeWithError(err);
        return;
    }

    if (info.protocolName != "MQTT" || info.protocolLevel != 4) {
        closeWithError("协议名或级别不匹配");
        return;
    }

    if (info.clientId.isEmpty()) {
        info.clientId = "qtcar-" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz") + "-" + QString::number(peerPort());
    }

    if (info.clientId.size() > MAX_CLIENT_ID_LEN) {
        closeWithError("ClientID 长度超过限制");
        return;
    }

    const BrokerConfig &cfg = m_broker->config();
    if (cfg.requireAuth) {
        if (!info.usernameFlag || info.username != cfg.authUsername || info.password != cfg.authPassword.toUtf8()) {
            sendConnectAck(false, CONN_REFUSED_BAD_USERNAME_PASS);
            closeWithError("认证失败");
            return;
        }
    } else if (!cfg.allowAnonymous) {
        sendConnectAck(false, CONN_REFUSED_NOT_AUTHORIZED);
        closeWithError("匿名连接被拒绝");
        return;
    }

    m_clientId = info.clientId;
    m_username = info.username;
    m_cleanSession = info.cleanSession;
    m_keepAlive = info.keepAlive;
    m_will = info.will;
    m_connectReceived = true;
    m_authenticated = true;

    int ms = (m_keepAlive == 0 ? 60 : m_keepAlive) * 1500;
    m_keepAliveTimer->setInterval(ms);
    m_keepAliveTimer->start();

    m_retransmitTimer->setInterval(cfg.retransmitIntervalMs);
    m_retransmitTimer->start();

    sendConnectAck(false, CONN_ACCEPTED);
    emit connectionReady(this);
}

void MqttConnection::handlePublish(quint8 flags, const QByteArray &body)
{
    if (!m_authenticated) {
        closeWithError("未认证");
        return;
    }

    PublishMessage msg;
    QString err;
    if (!MqttPacket::parsePublish(flags, body, msg, &err)) {
        closeWithError(err);
        return;
    }

    msg.sourceClientId = m_clientId;

    // 注意：retain + 空载荷表示"删除保留消息"，仍要走正常的 QoS 确认流程，
    // 否则对端在 QoS1/2 下收不到 PUBACK/PUBREC 会一直重传
    switch (msg.qos) {
    case QoS0:
        emit publishReceived(this, msg);
        break;
    case QoS1:
        writeRaw(MqttPacket::encodePubAck(msg.packetId));
        emit publishReceived(this, msg);
        break;
    case QoS2:
        m_qos2Inbound.insert(msg.packetId, msg);
        writeRaw(MqttPacket::encodePubRec(msg.packetId));
        break;
    default:
        closeWithError("未知 QoS");
        break;
    }
}

void MqttConnection::handlePubRel(const QByteArray &body)
{
    if (!m_authenticated) {
        closeWithError("未认证");
        return;
    }

    quint16 pid;
    if (!MqttPacket::parsePacketId(body, pid)) {
        closeWithError("PUBREL 报文标识符解析失败");
        return;
    }

    QHash<quint16, PublishMessage>::iterator it = m_qos2Inbound.find(pid);
    if (it == m_qos2Inbound.end()) {
        // 重复的 PUBREL：按规范仍回 PUBCOMP，不算错误
        writeRaw(MqttPacket::encodePubComp(pid));
        return;
    }

    // BUGFIX: 先拷出消息再 erase，否则引用失效；原实现根本没有 erase，会泄漏
    const PublishMessage msg = it.value();
    m_qos2Inbound.erase(it);

    writeRaw(MqttPacket::encodePubComp(pid));
    emit publishReceived(this, msg);
}

void MqttConnection::handlePubAck(const QByteArray &body)
{
    quint16 pid;
    if (!MqttPacket::parsePacketId(body, pid)) {
        closeWithError("PUBACK 报文标识符解析失败");
        return;
    }

    emit pubAckReceived(this, pid);
    m_inflightOut.remove(pid);
}

void MqttConnection::handlePubComp(const QByteArray &body)
{
    quint16 pid;
    if (!MqttPacket::parsePacketId(body, pid)) {
        closeWithError("PUBCOMP 报文标识符解析失败");
        return;
    }

    emit pubCompReceived(this, pid);
    m_awaitingPubComp.remove(pid);
}

void MqttConnection::handlePubRec(const QByteArray &body)
{
    quint16 pid;
    if (!MqttPacket::parsePacketId(body, pid)) {
        closeWithError("PUBREC 报文标识符解析失败");
        return;
    }

    if (m_inflightOut.contains(pid)) {
        writeRaw(MqttPacket::encodePubRel(pid));
        m_inflightOut.remove(pid);
        m_awaitingPubComp.insert(pid);
    }

    emit pubRecReceived(this, pid);
}

void MqttConnection::handleSubscribe(quint8 flags, const QByteArray &body)
{
    if (!m_authenticated) {
        closeWithError("未认证");
        return;
    }

    if (flags != 0x02) {
        closeWithError("SUBSCRIBE 标志位非法");
        return;
    }

    quint16 pid;
    QList<Subscription> subs;
    QString err;
    if (!MqttPacket::parseSubscribe(body, pid, subs, &err)) {
        closeWithError(err);
        return;
    }

    QList<quint8> returnCodes;
    QList<Subscription> accepted;
    for (int i = 0; i < subs.size(); ++i) {
        const Subscription &sub = subs.at(i);
        if (!isValidTopicName(sub.filter, true)) {
            returnCodes.append(static_cast<quint8>(QoS_INVALID));   // 0x80 订阅失败
            continue;
        }
        // 服务端最高支持 QoS2
        const QoS granted = static_cast<QoS>(qMin(static_cast<int>(sub.qos),
                                                  static_cast<int>(QoS2)));
        returnCodes.append(static_cast<quint8>(granted));
        // BUGFIX: 只把通过校验的订阅交给 Broker，否则非法过滤器会被写进订阅表
        accepted.append(Subscription(sub.filter, granted));
    }

    // 先回 SUBACK 再投递命中的保留消息：与 mosquitto 等主流 Broker 的顺序一致，
    // 避免某些客户端在收到 SUBACK 之前收到 PUBLISH 时处理异常
    sendSubAck(pid, returnCodes);

    if (!accepted.isEmpty())
        emit subscribeRequested(this, pid, accepted);
}

void MqttConnection::handleUnsubscribe(quint8 flags, const QByteArray &body)
{
    if (!m_authenticated) {
        closeWithError(QStringLiteral("未认证即发送 UNSUBSCRIBE"));
        return;
    }
    if (flags != 0x02) {
        closeWithError("UNSUBSCRIBE 标志位非法");
        return;
    }

    quint16 pid;
    QStringList filters;
    QString err;
    if (!MqttPacket::parseUnsubscribe(body, pid, filters, &err)) {
        closeWithError(err);
        return;
    }

    emit unsubscribeRequested(this, pid, filters);
    sendUnsubAck(pid);
}

void MqttConnection::deliverPublish(const Mqtt::PublishMessage &msg, Mqtt::QoS effectiveQos)
{
    // BUGFIX: 无论哪个等级都必须把副本的 qos 改成实际投递等级。
    // 原实现 QoS0 分支直接用了 msg，而 msg.qos 可能是发布方的 1/2，
    // 会导致 PUBLISH 首字节里的 QoS 位与 packetId 的存在性不一致
    PublishMessage copy = msg;
    copy.qos = effectiveQos;

    if (effectiveQos == QoS0) {
        writeRaw(MqttPacket::encodePublish(copy, 0, false));
    } else if (effectiveQos == QoS1 || effectiveQos == QoS2) {
        const quint16 pid = nextPacketId();
        copy.packetId = pid;
        m_inflightOut.insert(pid, copy);
        m_inflightAttempts.insert(pid, 0);
        writeRaw(MqttPacket::encodePublish(copy, pid, false));
    } else {
        closeWithError(QStringLiteral("无效的投递 QoS"));
        return;
    }

    m_stats.msgSent++;
}

quint16 MqttConnection::nextPacketId()
{
    ++m_packetIdCounter;
    if (m_packetIdCounter == 0) {
        ++m_packetIdCounter;
    }
    return m_packetIdCounter;
}

void MqttConnection::writeRaw(const QByteArray &data)
{
    if (!m_socket->isWritable()) {
        return;
    }

    // 嗅探服务端发出的原始报文
    emit packetSniffed(m_clientId.isEmpty() ? peerAddress() : m_clientId,
                       QStringLiteral("OUT"), data);

    m_socket->write(data);
    m_stats.bytesSent += data.size();
}

void MqttConnection::onRetransmitTimeout()
{
    QList<quint16> ids = m_inflightOut.keys();
    for (quint16 pid : ids) {
        if (!m_awaitingPubComp.contains(pid)) {
            if (++m_inflightAttempts[pid] > 5) {
                m_inflightOut.remove(pid);
            } else {
                const PublishMessage &msg = m_inflightOut.value(pid);
                writeRaw(MqttPacket::encodePublish(msg, pid, true));
            }
        }
    }
}

void MqttConnection::onKeepAliveTimeout()
{
    closeWithError(QString("KeepAlive 超时 (%1 秒无报文)").arg(m_keepAlive));
}

void MqttConnection::onSocketDisconnected()
{
    m_keepAliveTimer->stop();
    m_retransmitTimer->stop();
    emit connectionClosed(this);
}

void MqttConnection::onSocketError()
{
    if (m_socket->error() != QAbstractSocket::RemoteHostClosedError) {
        qDebug() << "Socket error:" << m_socket->errorString();
    }
}

void MqttConnection::kick(const QString &reason)
{
    Q_UNUSED(reason)
    if (!m_socket)
        return;
    m_socket->disconnectFromHost();
    // 捕获裸指针而非 this：对象可能在 3 秒内已被回收
    QTcpSocket *sock = m_socket;
    QTimer::singleShot(3000, this, [sock]() {
        if (sock && sock->state() != QAbstractSocket::UnconnectedState)
            sock->abort();
    });
}

void MqttConnection::closeWithError(const QString &reason)
{
    emit protocolError(this, reason);
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->disconnectFromHost();
}

void MqttConnection::sendConnectAck(bool sessionPresent, Mqtt::ConnectReturnCode rc)
{
    writeRaw(MqttPacket::encodeConnectAck(sessionPresent, rc));
}

void MqttConnection::sendSubAck(quint16 packetId, const QList<quint8> &returnCodes)
{
    writeRaw(MqttPacket::encodeSubAck(packetId, returnCodes));
}

void MqttConnection::sendUnsubAck(quint16 packetId)
{
    writeRaw(MqttPacket::encodeUnsubAck(packetId));
}
