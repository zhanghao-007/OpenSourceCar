#include "MqttPacket.h"

#include <QtEndian>

namespace Mqtt {

// ---------------------------------------------------------------------------
// 剩余长度变长整数编解码 (MQTT-2.2.3)
// ---------------------------------------------------------------------------
QByteArray MqttPacket::encodeRemainingLength(int length) {
    if (length < 0 || length > MAX_PACKET_SIZE) {
        return QByteArray();
    }
    QByteArray out;
    do {
        quint8 digit = length % 128;
        length /= 128;
        if (length > 0) {
            digit |= 0x80;
        }
        out.append(char(digit));
    } while (length > 0);
    return out;
}

int MqttPacket::decodeRemainingLength(const char *data, int available, int &bytesUsed) {
    int value = 0;
    int multiplier = 1;
    bytesUsed = 0;
    for (int i = 0; i < 4; ++i) {
        if (available <= i) {
            bytesUsed = 0;
            return -1; // 需要更多字节
        }
        quint8 byte = static_cast<quint8>(data[i]);
        value += (byte & 127) * multiplier;
        if ((byte & 128) == 0) {
            bytesUsed = i + 1;
            return value;
        }
        multiplier *= 128;
    }
    return -2; // 格式非法
}

// ---------------------------------------------------------------------------
// 服务端 -> 客户端 报文编码
// ---------------------------------------------------------------------------
QByteArray MqttPacket::encodeConnectAck(bool sessionPresent, ConnectReturnCode rc) {
    // BUGFIX: CONNACK 固定头标志位必须为 0（MQTT-3.2.1）。
    // "会话存在"标志属于**可变头**的第 1 个字节，不能塞进固定头标志位，
    // 否则 paho / mosquitto 等标准客户端会判定报文非法并断开。
    quint8 header = (PACKET_CONNACK << 4);
    QByteArray body;
    body.append(sessionPresent ? char(0x01) : char(0x00));   // 连接确认标志
    body.append(static_cast<char>(rc));                      // 连接返回码
    return QByteArray() + char(header) + encodeRemainingLength(body.size()) + body;
}

QByteArray MqttPacket::encodePublish(const PublishMessage &msg, quint16 packetId, bool dup) {
    quint8 header = publishHeaderByte(msg.qos, dup, msg.retain);
    QByteArray body;
    if (!writeString(body, msg.topic)) {
        return QByteArray();
    }
    if (msg.qos > 0) {
        if (!writeUInt16(body, packetId)) {
            return QByteArray();
        }
    }
    body.append(msg.payload);
    return QByteArray() + char(header) + encodeRemainingLength(body.size()) + body;
}

QByteArray MqttPacket::encodePubAck(quint16 packetId) {
    quint8 header = (PACKET_PUBACK << 4);
    QByteArray body;
    if (!writeUInt16(body, packetId)) {
        return QByteArray();
    }
    return QByteArray() + char(header) + encodeRemainingLength(body.size()) + body;
}

QByteArray MqttPacket::encodePubRec(quint16 packetId) {
    quint8 header = (PACKET_PUBREC << 4);
    QByteArray body;
    if (!writeUInt16(body, packetId)) {
        return QByteArray();
    }
    return QByteArray() + char(header) + encodeRemainingLength(body.size()) + body;
}

QByteArray MqttPacket::encodePubRel(quint16 packetId) {
    quint8 header = (PACKET_PUBREL << 4) | 0x02;
    QByteArray body;
    if (!writeUInt16(body, packetId)) {
        return QByteArray();
    }
    return QByteArray() + char(header) + encodeRemainingLength(body.size()) + body;
}

QByteArray MqttPacket::encodePubComp(quint16 packetId) {
    quint8 header = (PACKET_PUBCOMP << 4);
    QByteArray body;
    if (!writeUInt16(body, packetId)) {
        return QByteArray();
    }
    return QByteArray() + char(header) + encodeRemainingLength(body.size()) + body;
}

QByteArray MqttPacket::encodeSubAck(quint16 packetId, const QList<quint8> &returnCodes) {
    quint8 header = (PACKET_SUBACK << 4);
    QByteArray body;
    if (!writeUInt16(body, packetId)) {
        return QByteArray();
    }
    for (int i = 0; i < returnCodes.size(); ++i)
        body.append(static_cast<char>(returnCodes.at(i)));
    return QByteArray() + char(header) + encodeRemainingLength(body.size()) + body;
}

QByteArray MqttPacket::encodeUnsubAck(quint16 packetId) {
    quint8 header = (PACKET_UNSUBACK << 4);
    QByteArray body;
    if (!writeUInt16(body, packetId)) {
        return QByteArray();
    }
    return QByteArray() + char(header) + encodeRemainingLength(body.size()) + body;
}

QByteArray MqttPacket::encodePingResp() {
    quint8 header = (PACKET_PINGRESP << 4);
    return QByteArray() + char(header) + char(0);
}

// ---------------------------------------------------------------------------
// 客户端 -> 服务端 报文解析
// ---------------------------------------------------------------------------
bool MqttPacket::parseConnect(const QByteArray &body, ConnectInfo &out, QString *err) {
    int pos = 0;
    if (!readString(body, pos, out.protocolName, err)) {
        return false;
    }
    if (pos + 1 > body.size()) {
        if (err) *err = QStringLiteral("协议级别字段缺失");
        return false;
    }
    out.protocolLevel = static_cast<quint8>(body[pos++]);
    if (pos + 1 > body.size()) {
        if (err) *err = QStringLiteral("连接标志字段缺失");
        return false;
    }
    // 连接标志位：bit1 cleanSession, bit2 willFlag, bit3-4 willQoS,
    //             bit5 willRetain, bit6 passwordFlag, bit7 usernameFlag
    const quint8 connectFlags = static_cast<quint8>(body[pos++]);
    out.cleanSession  = (connectFlags & 0x02) != 0;
    out.will.valid    = (connectFlags & 0x04) != 0;
    out.will.qos      = static_cast<QoS>((connectFlags >> 3) & 0x03);
    out.will.retain   = (connectFlags & 0x20) != 0;
    out.passwordFlag  = (connectFlags & 0x40) != 0;
    out.usernameFlag  = (connectFlags & 0x80) != 0;
    if (out.will.valid && out.will.qos > QoS2) {
        if (err) *err = QStringLiteral("遗嘱 QoS=3 非法");
        return false;
    }
    if (!readUInt16(body, pos, out.keepAlive, err)) {
        return false;
    }
    if (!readString(body, pos, out.clientId, err)) {
        return false;
    }
    if (out.will.valid) {
        if (!readString(body, pos, out.will.topic, err))
            return false;
        // 遗嘱载荷是二进制数据（2 字节长度前缀），不是 UTF-8 字符串
        if (!readByteArray(body, pos, out.will.payload, err))
            return false;
        if (out.will.topic.isEmpty() || !isValidTopicName(out.will.topic, false)) {
            if (err) *err = QStringLiteral("遗嘱主题名无效");
            return false;
        }
    }
    if (out.usernameFlag) {
        if (!readString(body, pos, out.username, err))
            return false;
    }
    if (out.passwordFlag) {
        if (!readByteArray(body, pos, out.password, err))
            return false;
    }
    return true;
}

bool MqttPacket::parsePublish(quint8 flags, const QByteArray &body, PublishMessage &out, QString *err) {
    int pos = 0;
    if (!readString(body, pos, out.topic, err)) {
        return false;
    }
    out.qos = static_cast<QoS>((flags >> 1) & 0x03);
    out.retain = (flags & 0x01) != 0;
    out.dup = (flags >> 3) & 0x01;
    if (out.qos > QoS2) {          // 报文头 QoS 位为 11b 时非法
        if (err) *err = QStringLiteral("PUBLISH QoS=3 非法");
        return false;
    }
    if (out.qos > 0) {
        if (pos + 2 > body.size()) {
            if (err) *err = QStringLiteral("packetId 缺失");
            return false;
        }
        out.packetId = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(body.constData() + pos));
        pos += 2;
        if (out.packetId == 0) {
            if (err) *err = QStringLiteral("packetId 不能为 0");
            return false;
        }
    }
    out.payload = body.mid(pos);
    if (out.topic.isEmpty() || !isValidTopicName(out.topic, false)) {
        if (err) *err = QStringLiteral("主题名无效");
        return false;
    }
    return true;
}

bool MqttPacket::parseSubscribe(const QByteArray &body, quint16 &packetId,
                               QList<Subscription> &out, QString *err) {
    int pos = 0;
    if (!readUInt16(body, pos, packetId, err)) {
        return false;
    }
    if (packetId == 0) {
        if (err) *err = QStringLiteral("packetId 不能为 0");
        return false;
    }
    while (pos < body.size()) {
        QString filter;
        quint8 qos;
        if (!readString(body, pos, filter, err)) {
            return false;
        }
        if (pos + 1 > body.size()) {
            if (err) *err = QStringLiteral("QoS 字段缺失");
            return false;
        }
        qos = static_cast<quint8>(body[pos++]);
        if ((qos & 0xFC) != 0) {
            if (err) *err = QStringLiteral("QoS 保留位非零");
            return false;
        }
        out.append(Subscription(filter, static_cast<QoS>(qos)));
    }
    if (out.isEmpty()) {
        if (err) *err = QStringLiteral("订阅列表为空");
        return false;
    }
    return true;
}

bool MqttPacket::parseUnsubscribe(const QByteArray &body, quint16 &packetId,
                                 QStringList &out, QString *err) {
    int pos = 0;
    if (!readUInt16(body, pos, packetId, err)) {
        return false;
    }
    if (packetId == 0) {
        if (err) *err = QStringLiteral("packetId 不能为 0");
        return false;
    }
    while (pos < body.size()) {
        QString topic;
        if (!readString(body, pos, topic, err)) {
            return false;
        }
        out.append(topic);
    }
    if (out.isEmpty()) {
        if (err) *err = QStringLiteral("取消订阅列表为空");
        return false;
    }
    return true;
}

bool MqttPacket::parsePacketId(const QByteArray &body, quint16 &packetId, QString *err) {
    if (body.size() != 2) {
        if (err) *err = QStringLiteral("packetId 长度不正确");
        return false;
    }
    packetId = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(body.constData()));
    if (packetId == 0) {
        if (err) *err = QStringLiteral("packetId 不能为 0");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------
quint8 MqttPacket::publishHeaderByte(QoS qos, bool dup, bool retain) {
    return 0x30 | (dup ? 0x08 : 0x00) | (qos << 1) | (retain ? 0x01 : 0x00);
}

// UTF-8 编码字符串合法性：禁止 U+0000 与 UTF-16 代理项（MQTT-1.5.3）
// 注：控制字符 U+0001~U+001F 规范建议拒绝，但部分小车固件会携带，这里放宽放行。
static bool containsIllegalUtf16(const QString &s)
{
    for (int i = 0; i < s.size(); ++i) {
        const ushort u = s.at(i).unicode();
        if (u == 0x0000)
            return true;
        if (u >= 0xD800 && u <= 0xDFFF)   // 代理项，不是合法 Unicode 标量值
            return true;
    }
    return false;
}

bool MqttPacket::writeString(QByteArray &out, const QString &s) {
    const QByteArray utf8 = s.toUtf8();
    if (utf8.size() > 65535)
        return false;
    if (containsIllegalUtf16(s))
        return false;
    if (!writeUInt16(out, static_cast<quint16>(utf8.size())))
        return false;
    out.append(utf8);
    return true;
}

bool MqttPacket::readString(const QByteArray &data, int &pos, QString &out, QString *err) {
    QByteArray raw;
    if (!readByteArray(data, pos, raw, err))
        return false;
    const QString s = QString::fromUtf8(raw);
    if (containsIllegalUtf16(s)) {
        if (err) *err = QStringLiteral("字符串包含非法字符");
        return false;
    }
    out = s;
    return true;
}

bool MqttPacket::writeByteArray(QByteArray &out, const QByteArray &data) {
    if (data.size() > 65535)
        return false;
    if (!writeUInt16(out, static_cast<quint16>(data.size())))
        return false;
    out.append(data);
    return true;
}

bool MqttPacket::readByteArray(const QByteArray &data, int &pos, QByteArray &out, QString *err) {
    if (pos + 2 > data.size()) {
        if (err) *err = QStringLiteral("长度字段缺失");
        return false;
    }
    const quint16 length =
        qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + pos));
    pos += 2;
    if (pos + length > data.size()) {
        if (err) *err = QStringLiteral("数据不足，声明长度 %1，可用 %2")
                            .arg(length).arg(data.size() - pos + 2);
        return false;
    }
    out = data.mid(pos, length);
    pos += length;
    return true;
}

bool MqttPacket::writeUInt16(QByteArray &out, quint16 v) {
    const quint16 be = qToBigEndian<quint16>(v);
    out.append(reinterpret_cast<const char *>(&be), 2);
    return true;
}

bool MqttPacket::readUInt16(const QByteArray &data, int &pos, quint16 &out, QString *err) {
    if (pos + 2 > data.size()) {
        if (err) *err = QStringLiteral("16位整数字段缺失");
        return false;
    }
    out = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(data.constData() + pos));
    pos += 2;
    return true;
}

} // namespace Mqtt
