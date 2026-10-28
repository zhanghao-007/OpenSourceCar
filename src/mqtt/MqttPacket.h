#ifndef MQTTPACKET_H
#define MQTTPACKET_H

#include "MqttTypes.h"

#include <QStringList>

namespace Mqtt {

// ---------------------------------------------------------------------------
// MqttPacket —— MQTT 3.1.1 报文编解码器（纯静态工具类）
//
// 约定：
//   * encode*() 返回**完整报文字节流**（固定头 + 可变头 + 有效载荷），
//     调用方可直接 write 到 socket。
//   * parse*() 接收的 body 是**去掉固定头之后**的剩余部分
//     （可变头 + 有效载荷）。固定头的拆解由 MqttConnection 负责。
//   * 所有 parse*() 在报文非法时返回 false 并通过 err 输出人类可读原因，
//     不抛异常，便于服务端记录日志后断开连接而不崩溃。
// ---------------------------------------------------------------------------
class MqttPacket
{
public:
    // ---- 剩余长度变长整数编解码 (MQTT-2.2.3) ----
    // 编码：低 7 位存数据，最高位为 continuation bit，最多 4 字节
    static QByteArray encodeRemainingLength(int length);

    // 解码：成功返回长度值(>=0)，bytesUsed 输出消耗字节数；
    //       数据不完整返回 -1，格式非法返回 -2
    static int decodeRemainingLength(const char *data, int available, int &bytesUsed);

    // ---- 服务端 -> 客户端 报文编码 ----
    static QByteArray encodeConnectAck(bool sessionPresent, ConnectReturnCode rc);
    static QByteArray encodePublish(const PublishMessage &msg, quint16 packetId, bool dup = false);
    static QByteArray encodePubAck(quint16 packetId);
    static QByteArray encodePubRec(quint16 packetId);
    static QByteArray encodePubRel(quint16 packetId);   // QoS2 第二阶段的 REL（flags=0x02）
    static QByteArray encodePubComp(quint16 packetId);
    static QByteArray encodeSubAck(quint16 packetId, const QList<quint8> &returnCodes);
    static QByteArray encodeUnsubAck(quint16 packetId);
    static QByteArray encodePingResp();

    // ---- 客户端 -> 服务端 报文解析 ----
    static bool parseConnect(const QByteArray &body, ConnectInfo &out, QString *err = nullptr);
    static bool parsePublish(quint8 flags, const QByteArray &body, PublishMessage &out, QString *err = nullptr);
    static bool parseSubscribe(const QByteArray &body, quint16 &packetId,
                               QList<Subscription> &out, QString *err = nullptr);
    static bool parseUnsubscribe(const QByteArray &body, quint16 &packetId,
                                 QStringList &out, QString *err = nullptr);
    // PUBACK / PUBREC / PUBREL / PUBCOMP / UNSUBACK 共用的报文标识符解析
    static bool parsePacketId(const QByteArray &body, quint16 &packetId, QString *err = nullptr);

    // ---- 工具 ----
    // 从固定头首字节拆出报文类型与标志位
    static PacketType typeFromHeader(quint8 header) { return static_cast<PacketType>((header >> 4) & 0x0F); }
    static quint8     flagsFromHeader(quint8 header) { return static_cast<quint8>(header & 0x0F); }

    // 生成 PUBLISH 固定头首字节
    static quint8 publishHeaderByte(QoS qos, bool dup, bool retain);

    // UTF-8 编码字符串读写（MQTT-1.5.3：2 字节长度 + UTF-8 字节）
    static bool    writeString(QByteArray &out, const QString &s);
    static bool    readString(const QByteArray &data, int &pos, QString &out, QString *err = nullptr);
    // 二进制数据读写（2 字节长度前缀，用于遗嘱消息 / 密码）
    static bool    writeByteArray(QByteArray &out, const QByteArray &data);
    static bool    readByteArray(const QByteArray &data, int &pos, QByteArray &out, QString *err = nullptr);
    static bool    writeUInt16(QByteArray &out, quint16 v);
    static bool    readUInt16(const QByteArray &data, int &pos, quint16 &out, QString *err = nullptr);
};

} // namespace Mqtt

#endif // MQTTPACKET_H
